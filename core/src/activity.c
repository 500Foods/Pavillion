#define _POSIX_C_SOURCE 200809L

#include "pav_activity.h"

#include "dispatch.h"
#include "pav_body.h"
#include "pav_db.h"
#include "pav_platform.h"
#include "pavillion.h"

#include <sqlite3.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LIVE_MAX 256
#define HIST_KEEP 500
#define TITLE_MAX 200
#define DETAIL_MAX 500
#define UNIT_MAX 32
#define ERROR_MAX 500
#define ACCOUNT_MAX 128
#define KIND_MAX 32
#define DEMO_MAX 8

#define ST_QUEUED 0
#define ST_RUNNING 1
#define ST_DONE 2
#define ST_FAILED 3
#define ST_CANCELLED 4

static const char *const k_kinds[] = {
    "account.discover",
    "account.test",
    "attachment.download",
    "body.fetch",
    "cache.evict",
    "export",
    "files.download",
    "files.upload",
    "import",
    "index.rebuild",
    "retention",
    "rules.run",
    "send",
    "sync.calendar",
    "sync.contacts",
    "sync.files",
    "sync.mail",
};

/* Drops finished rows past the cap. Undismissed failures stay. */
static const char k_trim_sql[] =
    "DELETE FROM activity "
    "WHERE NOT (state = 'failed' AND dismissed = 0) "
    "AND id NOT IN ("
    "  SELECT id FROM activity "
    "  WHERE NOT (state = 'failed' AND dismissed = 0) "
    "  ORDER BY ended_ms DESC, id DESC "
    "  LIMIT 500"
    ")";

static const char k_insert_sql[] =
    "INSERT INTO activity ("
    "id, kind, account_id, title, detail, state, started_ms, ended_ms, "
    "prog_done, prog_total, prog_unit, eta_ms, cancellable, error, parent_id, dismissed"
    ") VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)";

typedef struct Act {
    int64_t id;
    char kind[KIND_MAX];
    char account_id[ACCOUNT_MAX];
    int has_account;
    char title[TITLE_MAX];
    char detail[DETAIL_MAX];
    int has_detail;
    int state;
    int64_t started_ms;
    int64_t ended_ms;
    int has_progress;
    int64_t prog_done;
    int64_t prog_total;
    char unit[UNIT_MAX];
    int has_unit;
    int64_t eta_ms;
    int cancellable;
    int cancel_req;
    char error[ERROR_MAX];
    int has_error;
    int64_t parent_id;
    int dismissed;
    int samples;
    int64_t sample_ms;
    int64_t sample_done;
    double rate;
    int64_t last_emit_ms;
} Act;

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static Act *g_items;
static size_t g_n;
static size_t g_cap;
static int64_t g_next_id = 1;
static int g_attached;
static void *g_demo_threads[DEMO_MAX];
static int g_demo_n;

static void copy_trunc(char *dst, size_t dst_len, const char *src) {
    size_t n;

    if (dst_len == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    n = strlen(src);
    if (n >= dst_len) {
        n = dst_len - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void format_id(char *dst, size_t dst_len, int64_t id) {
    snprintf(dst, dst_len, "act-%lld", (long long)id);
}

static const char *state_name(int state) {
    switch (state) {
    case ST_QUEUED:
        return "queued";
    case ST_RUNNING:
        return "running";
    case ST_DONE:
        return "done";
    case ST_FAILED:
        return "failed";
    case ST_CANCELLED:
        return "cancelled";
    default:
        return NULL;
    }
}

static int state_from_name(const char *name) {
    if (name == NULL) {
        return -1;
    }
    if (strcmp(name, "queued") == 0) {
        return ST_QUEUED;
    }
    if (strcmp(name, "running") == 0) {
        return ST_RUNNING;
    }
    if (strcmp(name, "done") == 0) {
        return ST_DONE;
    }
    if (strcmp(name, "failed") == 0) {
        return ST_FAILED;
    }
    if (strcmp(name, "cancelled") == 0) {
        return ST_CANCELLED;
    }
    return -1;
}

static int kind_cmp(const void *key, const void *elem) {
    const char *name = key;
    const char *const *row = elem;

    return strcmp(name, *row);
}

static int kind_ok(const char *kind) {
    return bsearch(kind, k_kinds, sizeof(k_kinds) / sizeof(k_kinds[0]), sizeof(k_kinds[0]), kind_cmp) != NULL;
}

static Act *find_id(int64_t id) {
    size_t i;

    for (i = 0; i < g_n; i++) {
        if (g_items[i].id == id) {
            return &g_items[i];
        }
    }
    return NULL;
}

static int live_count(void) {
    size_t i;
    int n = 0;

    for (i = 0; i < g_n; i++) {
        if (g_items[i].state == ST_RUNNING || g_items[i].state == ST_QUEUED) {
            n++;
        }
    }
    return n;
}

static int finished(const Act *a) {
    return a->state == ST_DONE || a->state == ST_FAILED || a->state == ST_CANCELLED;
}

static int protected_fail(const Act *a) {
    return a->state == ST_FAILED && !a->dismissed;
}

static Act *add_slot(void) {
    Act *next;
    size_t cap;

    if (g_n == g_cap) {
        cap = g_cap == 0 ? 16 : g_cap * 2;
        next = realloc(g_items, cap * sizeof(*g_items));
        if (next == NULL) {
            return NULL;
        }
        g_items = next;
        g_cap = cap;
    }
    memset(&g_items[g_n], 0, sizeof(g_items[g_n]));
    g_items[g_n].eta_ms = -1;
    g_items[g_n].prog_total = -1;
    return &g_items[g_n++];
}

static void remove_at(size_t index) {
    if (index + 1 < g_n) {
        memmove(&g_items[index], &g_items[index + 1], (g_n - index - 1) * sizeof(g_items[0]));
    }
    g_n--;
}

static int is_older(const Act *a, const Act *b) {
    if (a->ended_ms != b->ended_ms) {
        return a->ended_ms < b->ended_ms;
    }
    return a->id < b->id;
}

static void trim_history(void) {
    for (;;) {
        size_t i;
        size_t victim = 0;
        int have = 0;
        int n = 0;

        for (i = 0; i < g_n; i++) {
            if (!finished(&g_items[i]) || protected_fail(&g_items[i])) {
                continue;
            }
            n++;
            if (!have || is_older(&g_items[i], &g_items[victim])) {
                victim = i;
                have = 1;
            }
        }
        if (n <= HIST_KEEP || !have) {
            return;
        }
        remove_at(victim);
    }
}

static int bind_opt_text(sqlite3_stmt *stmt, int index, const char *text, int present) {
    if (!present || text == NULL || text[0] == '\0') {
        return sqlite3_bind_null(stmt, index);
    }
    return sqlite3_bind_text(stmt, index, text, -1, SQLITE_TRANSIENT);
}

typedef struct ActRow {
    int64_t id;
    char kind[KIND_MAX];
    char account_id[ACCOUNT_MAX];
    int has_account;
    char title[TITLE_MAX];
    char detail[DETAIL_MAX];
    int has_detail;
    char state[16];
    int64_t started_ms;
    int64_t ended_ms;
    int has_progress;
    int64_t prog_done;
    int64_t prog_total;
    char unit[UNIT_MAX];
    int has_unit;
    int64_t eta_ms;
    int cancellable;
    char error[ERROR_MAX];
    int has_error;
    int64_t parent_id;
    int dismissed;
} ActRow;

static void log_store_fail(int64_t id);

static void row_done(void *ctx, int rc) {
    ActRow *row = ctx;

    if (rc != 0 && row != NULL) {
        log_store_fail(row->id);
    }
    free(row);
}

static int insert_job(void *ctx, sqlite3 *db) {
    ActRow *row = ctx;
    sqlite3_stmt *stmt = NULL;
    int rc;

    if (pav_db_prepare(db, k_insert_sql, k_insert_sql, &stmt) != 0) {
        return -1;
    }
    if (pav_db_bind_int64(stmt, 1, row->id) != 0 || pav_db_bind_text(stmt, 2, row->kind) != 0 ||
        bind_opt_text(stmt, 3, row->account_id, row->has_account) != SQLITE_OK ||
        pav_db_bind_text(stmt, 4, row->title) != 0 ||
        bind_opt_text(stmt, 5, row->detail, row->has_detail) != SQLITE_OK ||
        pav_db_bind_text(stmt, 6, row->state) != 0 || pav_db_bind_int64(stmt, 7, row->started_ms) != 0 ||
        pav_db_bind_int64(stmt, 8, row->ended_ms) != 0 ||
        (row->has_progress ? pav_db_bind_int64(stmt, 9, row->prog_done) : pav_db_bind_null(stmt, 9)) != 0 ||
        (row->has_progress && row->prog_total >= 0 ? pav_db_bind_int64(stmt, 10, row->prog_total)
                                                   : pav_db_bind_null(stmt, 10)) != 0 ||
        bind_opt_text(stmt, 11, row->unit, row->has_unit) != SQLITE_OK ||
        (row->eta_ms >= 0 ? pav_db_bind_int64(stmt, 12, row->eta_ms) : pav_db_bind_null(stmt, 12)) != 0 ||
        pav_db_bind_int(stmt, 13, row->cancellable ? 1 : 0) != 0 ||
        bind_opt_text(stmt, 14, row->error, row->has_error) != SQLITE_OK ||
        (row->parent_id > 0 ? pav_db_bind_int64(stmt, 15, row->parent_id) : pav_db_bind_null(stmt, 15)) != 0 ||
        pav_db_bind_int(stmt, 16, row->dismissed ? 1 : 0) != 0) {
        return -1;
    }
    rc = pav_db_step(stmt);
    if (rc != SQLITE_DONE) {
        return rc == SQLITE_BUSY ? SQLITE_BUSY : -1;
    }
    return sqlite3_exec(db, k_trim_sql, NULL, NULL, NULL) == SQLITE_OK ? 0 : -1;
}

static int persist_finished(const Act *a) {
    ActRow *row;
    const char *state;

    if (!g_attached) {
        return 0;
    }
    state = state_name(a->state);
    if (state == NULL) {
        return -1;
    }
    row = calloc(1, sizeof(*row));
    if (row == NULL) {
        return -1;
    }
    row->id = a->id;
    copy_trunc(row->kind, sizeof(row->kind), a->kind);
    copy_trunc(row->account_id, sizeof(row->account_id), a->account_id);
    row->has_account = a->has_account;
    copy_trunc(row->title, sizeof(row->title), a->title);
    copy_trunc(row->detail, sizeof(row->detail), a->detail);
    row->has_detail = a->has_detail;
    copy_trunc(row->state, sizeof(row->state), state);
    row->started_ms = a->started_ms;
    row->ended_ms = a->ended_ms;
    row->has_progress = a->has_progress;
    row->prog_done = a->prog_done;
    row->prog_total = a->prog_total;
    copy_trunc(row->unit, sizeof(row->unit), a->unit);
    row->has_unit = a->has_unit;
    row->eta_ms = a->eta_ms;
    row->cancellable = a->cancellable;
    copy_trunc(row->error, sizeof(row->error), a->error);
    row->has_error = a->has_error;
    row->parent_id = a->parent_id;
    row->dismissed = a->dismissed;
    if (pav_db_write(insert_job, row, row_done, row) != 0) {
        free(row);
        return -1;
    }
    return 0;
}

static void dismiss_done(void *ctx, int rc) {
    int64_t *id = ctx;

    if (rc != 0 && id != NULL) {
        log_store_fail(*id);
    }
    free(id);
}

static int dismiss_job(void *ctx, sqlite3 *db) {
    int64_t *id = ctx;
    sqlite3_stmt *stmt = NULL;
    int rc;

    if (pav_db_prepare(db, "activity.dismiss", "UPDATE activity SET dismissed = 1 WHERE id = ?", &stmt) != 0) {
        return -1;
    }
    if (pav_db_bind_int64(stmt, 1, *id) != 0) {
        return -1;
    }
    rc = pav_db_step(stmt);
    if (rc != SQLITE_DONE) {
        return rc == SQLITE_BUSY ? SQLITE_BUSY : -1;
    }
    return sqlite3_exec(db, k_trim_sql, NULL, NULL, NULL) == SQLITE_OK ? 0 : -1;
}

static int persist_dismiss(int64_t id) {
    int64_t *copy;

    if (!g_attached) {
        return 0;
    }
    copy = malloc(sizeof(*copy));
    if (copy == NULL) {
        return -1;
    }
    *copy = id;
    if (pav_db_write(dismiss_job, copy, dismiss_done, copy) != 0) {
        free(copy);
        return -1;
    }
    return 0;
}

static void log_store_fail(int64_t id) {
    char text[32];

    format_id(text, sizeof(text), id);
    pav_log("activity", PAV_ERROR, 1, "cannot store %s", text);
}

static json_t *activity_json(const Act *a) {
    json_t *obj;
    char id[32];
    const char *state;

    obj = json_object();
    state = state_name(a->state);
    if (obj == NULL || state == NULL) {
        json_decref(obj);
        return NULL;
    }
    format_id(id, sizeof(id), a->id);
    if (json_object_set_new(obj, "id", json_string(id)) != 0 ||
        json_object_set_new(obj, "kind", json_string(a->kind)) != 0 ||
        json_object_set_new(obj, "accountId",
                            a->has_account ? json_string(a->account_id) : json_null()) != 0 ||
        json_object_set_new(obj, "title", json_string(a->title)) != 0 ||
        json_object_set_new(obj, "detail", a->has_detail ? json_string(a->detail) : json_null()) != 0 ||
        json_object_set_new(obj, "state", json_string(state)) != 0 ||
        json_object_set_new(obj, "startedMs", json_integer((json_int_t)a->started_ms)) != 0 ||
        json_object_set_new(obj, "endedMs",
                            finished(a) ? json_integer((json_int_t)a->ended_ms) : json_null()) != 0) {
        json_decref(obj);
        return NULL;
    }
    if (a->has_progress) {
        json_t *prog = json_object();

        if (prog == NULL) {
            json_decref(obj);
            return NULL;
        }
        if (json_object_set_new(prog, "done", json_integer((json_int_t)a->prog_done)) != 0 ||
            json_object_set_new(prog, "total",
                                a->prog_total >= 0 ? json_integer((json_int_t)a->prog_total) : json_null()) != 0 ||
            json_object_set_new(prog, "unit", a->has_unit ? json_string(a->unit) : json_null()) != 0) {
            json_decref(prog);
            json_decref(obj);
            return NULL;
        }
        if (json_object_set_new(obj, "progress", prog) != 0) {
            json_decref(obj);
            return NULL;
        }
    } else if (json_object_set_new(obj, "progress", json_null()) != 0) {
        json_decref(obj);
        return NULL;
    }
    if (json_object_set_new(obj, "etaMs", a->eta_ms >= 0 ? json_integer((json_int_t)a->eta_ms) : json_null()) !=
            0 ||
        json_object_set_new(obj, "cancellable", a->cancellable ? json_true() : json_false()) != 0 ||
        json_object_set_new(obj, "error", a->has_error ? json_string(a->error) : json_null()) != 0) {
        json_decref(obj);
        return NULL;
    }
    if (a->parent_id > 0) {
        char parent[32];

        format_id(parent, sizeof(parent), a->parent_id);
        if (json_object_set_new(obj, "parentId", json_string(parent)) != 0) {
            json_decref(obj);
            return NULL;
        }
    } else if (json_object_set_new(obj, "parentId", json_null()) != 0) {
        json_decref(obj);
        return NULL;
    }
    if (json_object_set_new(obj, "dismissed", a->dismissed ? json_true() : json_false()) != 0) {
        json_decref(obj);
        return NULL;
    }
    return obj;
}

static char *event_text(const Act *a) {
    json_t *root;
    json_t *obj;
    char *text;

    obj = activity_json(a);
    root = json_object();
    if (obj == NULL || root == NULL) {
        json_decref(obj);
        json_decref(root);
        return NULL;
    }
    if (json_object_set_new(root, "activity", obj) != 0) {
        json_decref(root);
        return NULL;
    }
    text = json_dumps(root, JSON_COMPACT);
    json_decref(root);
    return text;
}

static void emit_text(char *text) {
    if (text == NULL) {
        return;
    }
    pav_event_post("activity.changed", text);
    free(text);
}

static void log_begin(const Act *a) {
    char id[32];

    format_id(id, sizeof(id), a->id);
    pav_log("activity", PAV_INFO, 2, "begin %s %s", id, a->kind);
}

static void log_end(const Act *a) {
    char id[32];
    const char *state = state_name(a->state);

    format_id(id, sizeof(id), a->id);
    if (a->has_error && a->error[0] != '\0') {
        pav_log("activity", PAV_INFO, 4, "end %s %s %s: %s", id, a->kind, state != NULL ? state : "done", a->error);
    } else {
        pav_log("activity", PAV_INFO, 3, "end %s %s %s", id, a->kind, state != NULL ? state : "done");
    }
}

static void note_rate(Act *a, int64_t done, int64_t total, int64_t now) {
    int64_t dt;
    int64_t delta;
    double instant;
    double alpha;
    double eta;

    if (a->samples == 0) {
        a->sample_ms = now;
        a->sample_done = done;
        a->samples = 1;
        a->rate = 0;
        a->eta_ms = -1;
        return;
    }
    dt = now - a->sample_ms;
    delta = done - a->sample_done;
    if (dt <= 0 || delta <= 0) {
        return;
    }
    instant = (double)delta / (double)dt;
    if (a->samples == 1) {
        a->rate = instant;
        a->samples = 2;
    } else {
        /* Discrete EMA with a 10 second time constant. */
        alpha = (double)dt / ((double)dt + 10000.0);
        a->rate = alpha * instant + (1.0 - alpha) * a->rate;
    }
    a->sample_ms = now;
    a->sample_done = done;
    if (total < 0 || a->rate <= 0.0) {
        a->eta_ms = -1;
        return;
    }
    if (done >= total) {
        a->eta_ms = 0;
        return;
    }
    eta = (double)(total - done) / a->rate;
    if (eta < 0.0 || eta > 9223372036854775000.0) {
        a->eta_ms = -1;
        return;
    }
    a->eta_ms = (int64_t)(eta + 0.5);
}

static void set_text_field(char *dst, size_t dst_len, int *flag, const char *src) {
    if (src == NULL) {
        return;
    }
    if (src[0] == '\0') {
        dst[0] = '\0';
        *flag = 0;
        return;
    }
    copy_trunc(dst, dst_len, src);
    *flag = 1;
}

static void finish_at(size_t index, int state, const char *error) {
    Act snap;
    char *text;

    if (index >= g_n) {
        return;
    }
    if (g_items[index].state != ST_RUNNING && g_items[index].state != ST_QUEUED) {
        return;
    }
    g_items[index].state = state;
    g_items[index].ended_ms = pav_now_ms();
    if (g_items[index].ended_ms < 0) {
        g_items[index].ended_ms = 0;
    }
    set_text_field(g_items[index].error, sizeof(g_items[index].error), &g_items[index].has_error, error);
    if (error == NULL) {
        g_items[index].error[0] = '\0';
        g_items[index].has_error = 0;
    }
    snap = g_items[index];
    if (persist_finished(&snap) != 0) {
        log_store_fail(snap.id);
    }
    text = event_text(&snap);
    log_end(&snap);
    trim_history();
    emit_text(text);
}

static int end_state(int state) {
    if (state == PAV_ACT_DONE) {
        return ST_DONE;
    }
    if (state == PAV_ACT_FAILED) {
        return ST_FAILED;
    }
    if (state == PAV_ACT_CANCELLED) {
        return ST_CANCELLED;
    }
    return -1;
}

static size_t index_of(int64_t id) {
    size_t i;

    for (i = 0; i < g_n; i++) {
        if (g_items[i].id == id) {
            return i;
        }
    }
    return (size_t)-1;
}

int64_t pav_activity_begin(const char *kind, const char *account_id, const char *title, int cancellable,
                           int64_t parent) {
    Act *a;
    char *text;
    int64_t id;
    int64_t now;

    if (kind == NULL || title == NULL || title[0] == '\0' || !kind_ok(kind)) {
        return 0;
    }
    if (strlen(kind) >= KIND_MAX || strlen(title) >= TITLE_MAX) {
        return 0;
    }
    if (account_id != NULL && account_id[0] != '\0' && strlen(account_id) >= ACCOUNT_MAX) {
        return 0;
    }
    pthread_mutex_lock(&g_mu);
    if (live_count() >= LIVE_MAX) {
        pthread_mutex_unlock(&g_mu);
        return 0;
    }
    if (parent != 0 && find_id(parent) == NULL) {
        pthread_mutex_unlock(&g_mu);
        return 0;
    }
    now = pav_now_ms();
    if (now < 0) {
        now = 0;
    }
    a = add_slot();
    if (a == NULL) {
        pthread_mutex_unlock(&g_mu);
        return 0;
    }
    a->id = g_next_id++;
    memcpy(a->kind, kind, strlen(kind) + 1);
    memcpy(a->title, title, strlen(title) + 1);
    if (account_id != NULL && account_id[0] != '\0') {
        memcpy(a->account_id, account_id, strlen(account_id) + 1);
        a->has_account = 1;
    }
    a->state = ST_RUNNING;
    a->started_ms = now;
    a->ended_ms = 0;
    a->eta_ms = -1;
    a->prog_total = -1;
    a->cancellable = cancellable ? 1 : 0;
    a->parent_id = parent > 0 ? parent : 0;
    a->last_emit_ms = now;
    id = a->id;
    text = event_text(a);
    log_begin(a);
    pthread_mutex_unlock(&g_mu);
    emit_text(text);
    return id;
}

void pav_activity_progress(int64_t id, int64_t done, int64_t total, const char *unit, const char *detail) {
    Act *a;
    int64_t now;
    char *text = NULL;

    pthread_mutex_lock(&g_mu);
    a = find_id(id);
    if (a == NULL || (a->state != ST_RUNNING && a->state != ST_QUEUED)) {
        pthread_mutex_unlock(&g_mu);
        return;
    }
    a->has_progress = 1;
    a->prog_done = done;
    a->prog_total = total;
    set_text_field(a->unit, sizeof(a->unit), &a->has_unit, unit);
    set_text_field(a->detail, sizeof(a->detail), &a->has_detail, detail);
    now = pav_now_ms();
    if (now < 0) {
        now = 0;
    }
    note_rate(a, done, total, now);
    if (now - a->last_emit_ms >= 250) {
        text = event_text(a);
        a->last_emit_ms = now;
    }
    pthread_mutex_unlock(&g_mu);
    emit_text(text);
}

void pav_activity_end(int64_t id, int state, const char *error) {
    size_t index;
    int st;

    st = end_state(state);
    if (st < 0) {
        return;
    }
    pthread_mutex_lock(&g_mu);
    index = index_of(id);
    if (index != (size_t)-1) {
        finish_at(index, st, error);
    }
    pthread_mutex_unlock(&g_mu);
}

int pav_activity_cancelled(int64_t id) {
    Act *a;
    int cancelled = 0;

    pthread_mutex_lock(&g_mu);
    a = find_id(id);
    if (a != NULL && a->cancel_req && (a->state == ST_RUNNING || a->state == ST_QUEUED)) {
        cancelled = 1;
    }
    pthread_mutex_unlock(&g_mu);
    return cancelled;
}

static int listed(const Act *a, int history) {
    if (a->state == ST_RUNNING || a->state == ST_QUEUED) {
        return 1;
    }
    if (a->state == ST_FAILED && !a->dismissed) {
        return 1;
    }
    return history;
}

static int cmp_ptr(const void *left, const void *right) {
    const Act *a = *(const Act *const *)left;
    const Act *b = *(const Act *const *)right;

    if (a->id < b->id) {
        return -1;
    }
    if (a->id > b->id) {
        return 1;
    }
    return 0;
}

static int list_data(int history, json_t **data, PavError *err) {
    Act **order = NULL;
    json_t *arr;
    json_t *root;
    size_t i;
    size_t n = 0;

    pthread_mutex_lock(&g_mu);
    if (g_n > 0) {
        order = malloc(g_n * sizeof(*order));
        if (order == NULL) {
            pthread_mutex_unlock(&g_mu);
            pav_error_set(err, "internal", "out of memory", 0);
            return -1;
        }
        for (i = 0; i < g_n; i++) {
            order[i] = &g_items[i];
        }
        n = g_n;
        qsort(order, n, sizeof(*order), cmp_ptr);
    }
    arr = json_array();
    root = json_object();
    if (arr == NULL || root == NULL) {
        free(order);
        json_decref(arr);
        json_decref(root);
        pthread_mutex_unlock(&g_mu);
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    for (i = 0; i < n; i++) {
        json_t *obj;

        if (!listed(order[i], history)) {
            continue;
        }
        obj = activity_json(order[i]);
        if (obj == NULL) {
            free(order);
            json_decref(arr);
            json_decref(root);
            pthread_mutex_unlock(&g_mu);
            pav_error_set(err, "internal", "out of memory", 0);
            return -1;
        }
        if (json_array_append_new(arr, obj) != 0) {
            free(order);
            json_decref(arr);
            json_decref(root);
            pthread_mutex_unlock(&g_mu);
            pav_error_set(err, "internal", "out of memory", 0);
            return -1;
        }
    }
    free(order);
    pthread_mutex_unlock(&g_mu);
    if (json_object_set_new(root, "activities", arr) != 0) {
        json_decref(root);
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    *data = root;
    return 0;
}

static int parse_act_id(const char *text, int64_t *out) {
    const char *p;
    int64_t n = 0;

    if (text == NULL || strncmp(text, "act-", 4) != 0) {
        return -1;
    }
    p = text + 4;
    if (*p < '1' || *p > '9') {
        return -1;
    }
    while (*p >= '0' && *p <= '9') {
        if (n > (INT64_MAX - (*p - '0')) / 10) {
            return -1;
        }
        n = n * 10 + (*p - '0');
        p++;
    }
    if (*p != '\0') {
        return -1;
    }
    *out = n;
    return 0;
}

static int require_id(json_t *args, int64_t *id, PavError *err) {
    const char *key;
    json_t *value;
    int saw = 0;

    json_object_foreach(args, key, value) {
        if (strcmp(key, "id") == 0) {
            if (!json_is_string(value) || parse_act_id(json_string_value(value), id) != 0) {
                pav_error_set(err, "bad_args", "id must be act-<n>", 0);
                return -1;
            }
            saw = 1;
        } else {
            char buf[128];

            snprintf(buf, sizeof(buf), "unknown argument %s", key);
            pav_error_set(err, "bad_args", buf, 0);
            return -1;
        }
    }
    if (!saw) {
        pav_error_set(err, "bad_args", "id is required", 0);
        return -1;
    }
    return 0;
}

int pav_activity_cmd_list(json_t *args, json_t **data, PavError *err) {
    const char *key;
    json_t *value;
    int history = 0;

    json_object_foreach(args, key, value) {
        if (strcmp(key, "includeHistory") == 0) {
            if (!json_is_boolean(value)) {
                pav_error_set(err, "bad_args", "includeHistory must be a boolean", 0);
                return -1;
            }
            history = json_is_true(value);
        } else {
            char buf[128];

            snprintf(buf, sizeof(buf), "unknown argument %s", key);
            pav_error_set(err, "bad_args", buf, 0);
            return -1;
        }
    }
    return list_data(history, data, err);
}

int pav_activity_cmd_cancel(json_t *args, json_t **data, PavError *err) {
    int64_t id = 0;
    Act *a;
    json_t *obj;

    if (require_id(args, &id, err) != 0) {
        return -1;
    }
    pthread_mutex_lock(&g_mu);
    a = find_id(id);
    if (a == NULL) {
        pthread_mutex_unlock(&g_mu);
        pav_error_set(err, "not_found", "unknown activity", 0);
        return -1;
    }
    if (a->state != ST_RUNNING && a->state != ST_QUEUED) {
        pthread_mutex_unlock(&g_mu);
        pav_error_set(err, "bad_args", "activity is not running", 0);
        return -1;
    }
    if (!a->cancellable) {
        pthread_mutex_unlock(&g_mu);
        pav_error_set(err, "bad_args", "activity cannot be cancelled", 0);
        return -1;
    }
    a->cancel_req = 1;
    obj = activity_json(a);
    pav_log("activity", PAV_INFO, 1, "cancel requested for %s", a->kind);
    pthread_mutex_unlock(&g_mu);
    pav_body_cancel(id);
    if (obj == NULL) {
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    *data = obj;
    return 0;
}

int pav_activity_cmd_dismiss(json_t *args, json_t **data, PavError *err) {
    int64_t id = 0;
    size_t index;
    Act snap;
    char *text;
    json_t *obj;

    if (require_id(args, &id, err) != 0) {
        return -1;
    }
    pthread_mutex_lock(&g_mu);
    index = index_of(id);
    if (index == (size_t)-1) {
        pthread_mutex_unlock(&g_mu);
        pav_error_set(err, "not_found", "unknown activity", 0);
        return -1;
    }
    if (g_items[index].state != ST_FAILED) {
        pthread_mutex_unlock(&g_mu);
        pav_error_set(err, "bad_args", "only a failed activity can be dismissed", 0);
        return -1;
    }
    g_items[index].dismissed = 1;
    snap = g_items[index];
    if (persist_dismiss(id) != 0) {
        log_store_fail(id);
    }
    text = event_text(&snap);
    obj = activity_json(&snap);
    trim_history();
    pthread_mutex_unlock(&g_mu);
    emit_text(text);
    pav_log("activity", PAV_INFO, 1, "dismissed failed %s", snap.kind);
    if (obj == NULL) {
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    *data = obj;
    return 0;
}

#ifndef NDEBUG
typedef struct DemoJob {
    int64_t id;
    int steps;
    int ms;
} DemoJob;

static void sleep_slice(int ms) {
    struct timespec ts;

    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static void *demo_main(void *arg) {
    DemoJob *job = arg;
    int step;

    for (step = 1; step <= job->steps; step++) {
        int waited = 0;

        if (pav_activity_cancelled(job->id)) {
            pav_activity_end(job->id, PAV_ACT_CANCELLED, NULL);
            free(job);
            return NULL;
        }
        while (waited < job->ms) {
            int slice = job->ms - waited;

            if (slice > 50) {
                slice = 50;
            }
            sleep_slice(slice);
            waited += slice;
            if (pav_activity_cancelled(job->id)) {
                pav_activity_end(job->id, PAV_ACT_CANCELLED, NULL);
                free(job);
                return NULL;
            }
        }
        pav_activity_progress(job->id, step, job->steps, "steps", NULL);
    }
    if (pav_activity_cancelled(job->id)) {
        pav_activity_end(job->id, PAV_ACT_CANCELLED, NULL);
    } else {
        pav_activity_end(job->id, PAV_ACT_DONE, NULL);
    }
    free(job);
    return NULL;
}

static int start_demo(int64_t id, int steps, int ms) {
    DemoJob *job;
    void *thread = NULL;

    job = malloc(sizeof(*job));
    if (job == NULL) {
        return -1;
    }
    job->id = id;
    job->steps = steps;
    job->ms = ms;
    pthread_mutex_lock(&g_mu);
    if (g_demo_n >= DEMO_MAX) {
        pthread_mutex_unlock(&g_mu);
        free(job);
        return -1;
    }
    if (pav_thread_create(&thread, demo_main, job) != 0) {
        pthread_mutex_unlock(&g_mu);
        free(job);
        return -1;
    }
    g_demo_threads[g_demo_n++] = thread;
    pthread_mutex_unlock(&g_mu);
    return 0;
}

int pav_activity_cmd_demo(json_t *args, json_t **data, PavError *err) {
    const char *key;
    json_t *value;
    json_int_t steps = 0;
    json_int_t ms = 0;
    int saw_steps = 0;
    int saw_ms = 0;
    int64_t id;
    json_t *root;
    char id_text[32];

    if (pav_on_core_thread()) {
        pav_error_set(err, "internal", "demo ran on the core thread", 0);
        return -1;
    }
    json_object_foreach(args, key, value) {
        if (strcmp(key, "steps") == 0) {
            if (!json_is_integer(value)) {
                pav_error_set(err, "bad_args", "steps must be an integer from 1 to 10000", 0);
                return -1;
            }
            steps = json_integer_value(value);
            saw_steps = 1;
        } else if (strcmp(key, "ms") == 0) {
            if (!json_is_integer(value)) {
                pav_error_set(err, "bad_args", "ms must be an integer from 0 to 60000", 0);
                return -1;
            }
            ms = json_integer_value(value);
            saw_ms = 1;
        } else {
            char buf[128];

            snprintf(buf, sizeof(buf), "unknown argument %s", key);
            pav_error_set(err, "bad_args", buf, 0);
            return -1;
        }
    }
    if (!saw_steps || steps < 1 || steps > 10000) {
        pav_error_set(err, "bad_args", "steps must be an integer from 1 to 10000", 0);
        return -1;
    }
    if (!saw_ms || ms < 0 || ms > 60000) {
        pav_error_set(err, "bad_args", "ms must be an integer from 0 to 60000", 0);
        return -1;
    }
    id = pav_activity_begin("sync.mail", NULL, "Demo activity", 1, 0);
    if (id == 0) {
        pav_error_set(err, "internal", "cannot begin demo", 0);
        return -1;
    }
    if (start_demo(id, (int)steps, (int)ms) != 0) {
        pav_activity_end(id, PAV_ACT_FAILED, "cannot start demo");
        pav_error_set(err, "internal", "cannot start demo", 0);
        return -1;
    }
    format_id(id_text, sizeof(id_text), id);
    root = json_object();
    if (root == NULL || json_object_set_new(root, "id", json_string(id_text)) != 0) {
        json_decref(root);
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    *data = root;
    return 0;
}
#endif

static void clear_items(void) {
    free(g_items);
    g_items = NULL;
    g_n = 0;
    g_cap = 0;
}

static int clear_running_job(void *ctx, sqlite3 *db) {
    (void)ctx;
    if (sqlite3_exec(db, "DELETE FROM activity WHERE state = 'queued' OR state = 'running'", NULL, NULL, NULL) !=
        SQLITE_OK) {
        return -1;
    }
    return sqlite3_exec(db, k_trim_sql, NULL, NULL, NULL) == SQLITE_OK ? 0 : -1;
}

static int load_rows(sqlite3 *db) {
    sqlite3_stmt *stmt = NULL;
    int rc;

    rc = sqlite3_prepare_v2(
        db,
        "SELECT id, kind, account_id, title, detail, state, started_ms, ended_ms, "
        "prog_done, prog_total, prog_unit, eta_ms, cancellable, error, parent_id, dismissed "
        "FROM activity ORDER BY id",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return -1;
    }
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        Act *a;
        const char *kind;
        const char *title;
        int state;
        int64_t id;

        kind = (const char *)sqlite3_column_text(stmt, 1);
        title = (const char *)sqlite3_column_text(stmt, 3);
        state = state_from_name((const char *)sqlite3_column_text(stmt, 5));
        id = sqlite3_column_int64(stmt, 0);
        if (kind == NULL || title == NULL || title[0] == '\0' || state < 0 || id <= 0) {
            continue;
        }
        if (state == ST_RUNNING || state == ST_QUEUED) {
            continue;
        }
        a = add_slot();
        if (a == NULL) {
            sqlite3_finalize(stmt);
            return -1;
        }
        a->id = id;
        copy_trunc(a->kind, sizeof(a->kind), kind);
        copy_trunc(a->title, sizeof(a->title), title);
        if (sqlite3_column_type(stmt, 2) != SQLITE_NULL) {
            copy_trunc(a->account_id, sizeof(a->account_id), (const char *)sqlite3_column_text(stmt, 2));
            a->has_account = a->account_id[0] != '\0';
        }
        if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) {
            copy_trunc(a->detail, sizeof(a->detail), (const char *)sqlite3_column_text(stmt, 4));
            a->has_detail = a->detail[0] != '\0';
        }
        a->state = state;
        a->started_ms = sqlite3_column_int64(stmt, 6);
        a->ended_ms = sqlite3_column_type(stmt, 7) == SQLITE_NULL ? 0 : sqlite3_column_int64(stmt, 7);
        if (sqlite3_column_type(stmt, 8) != SQLITE_NULL) {
            a->has_progress = 1;
            a->prog_done = sqlite3_column_int64(stmt, 8);
            if (sqlite3_column_type(stmt, 9) == SQLITE_NULL) {
                a->prog_total = -1;
            } else {
                a->prog_total = sqlite3_column_int64(stmt, 9);
            }
        }
        if (sqlite3_column_type(stmt, 10) != SQLITE_NULL) {
            copy_trunc(a->unit, sizeof(a->unit), (const char *)sqlite3_column_text(stmt, 10));
            a->has_unit = a->unit[0] != '\0';
        }
        a->eta_ms = sqlite3_column_type(stmt, 11) == SQLITE_NULL ? -1 : sqlite3_column_int64(stmt, 11);
        a->cancellable = sqlite3_column_int(stmt, 12) ? 1 : 0;
        if (sqlite3_column_type(stmt, 13) != SQLITE_NULL) {
            copy_trunc(a->error, sizeof(a->error), (const char *)sqlite3_column_text(stmt, 13));
            a->has_error = a->error[0] != '\0';
        }
        if (sqlite3_column_type(stmt, 14) != SQLITE_NULL) {
            a->parent_id = sqlite3_column_int64(stmt, 14);
        }
        a->dismissed = sqlite3_column_int(stmt, 15) ? 1 : 0;
        if (id >= g_next_id) {
            g_next_id = id + 1;
        }
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

static int read_next_id(sqlite3 *db) {
    sqlite3_stmt *stmt = NULL;
    int64_t max_id = 0;

    if (sqlite3_prepare_v2(db, "SELECT COALESCE(MAX(id), 0) FROM activity", -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        max_id = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    if (max_id + 1 > g_next_id) {
        g_next_id = max_id + 1;
    }
    return 0;
}

int pav_activity_attach(void) {
    PavDbRead *read = NULL;
    sqlite3 *db;
    int rc;

    if (!pav_db_writer_running() || pav_db_call(clear_running_job, NULL) != 0) {
        pav_log("activity", PAV_ERROR, 0, "cannot clear running activities");
        return -1;
    }
    if (pav_db_read_begin(&read) != 0) {
        return -1;
    }
    db = pav_db_read_handle(read);
    pthread_mutex_lock(&g_mu);
    clear_items();
    g_next_id = 1;
    g_attached = 0;
    rc = read_next_id(db);
    if (rc == 0) {
        rc = load_rows(db);
    }
    if (rc == 0) {
        g_attached = 1;
        trim_history();
    } else {
        pav_log("activity", PAV_ERROR, 0, "cannot load activity history");
        clear_items();
        g_next_id = 1;
    }
    pthread_mutex_unlock(&g_mu);
    pav_db_read_end(read);
    return rc;
}

static void join_demos(void) {
    void *threads[DEMO_MAX];
    size_t row;
    int n;
    int i;

    pthread_mutex_lock(&g_mu);
    n = g_demo_n;
    for (i = 0; i < n; i++) {
        threads[i] = g_demo_threads[i];
        g_demo_threads[i] = NULL;
    }
    g_demo_n = 0;
    for (row = 0; row < g_n; row++) {
        if (g_items[row].state == ST_RUNNING || g_items[row].state == ST_QUEUED) {
            g_items[row].cancel_req = 1;
        }
    }
    pthread_mutex_unlock(&g_mu);
    for (i = 0; i < n; i++) {
        if (threads[i] != NULL) {
            pav_thread_join(threads[i]);
        }
    }
}

static void end_still_running(void) {
    size_t i;

    pthread_mutex_lock(&g_mu);
    for (i = 0; i < g_n;) {
        if (g_items[i].state == ST_RUNNING || g_items[i].state == ST_QUEUED) {
            finish_at(i, ST_CANCELLED, NULL);
            i = 0;
            continue;
        }
        i++;
    }
    pthread_mutex_unlock(&g_mu);
}

void pav_activity_shutdown(void) {
    if (g_attached) {
        pav_log("activity", PAV_DEBUG, 0, "shutting down, cancelling running work");
    }
    join_demos();
    end_still_running();
    pthread_mutex_lock(&g_mu);
    g_attached = 0;
    clear_items();
    g_next_id = 1;
    pthread_mutex_unlock(&g_mu);
}

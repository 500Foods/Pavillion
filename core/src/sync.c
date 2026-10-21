#define _POSIX_C_SOURCE 200809L

/* M-3a supervisor. PavSup stays on the core thread. The net thread only
   reads a SyncReq. It parses the Session there and posts the rows. The
   request stays allocated until the callback and the supervisor have each
   released it, so a cancel cannot free it while the callback still runs.
   The net callback never takes the core lock while it holds g_sync_mu. */

#include "pav_sync.h"

#include "dispatch.h"
#include "pav_activity.h"
#include "pav_db.h"
#include "pav_jmap.h"
#include "pav_net.h"
#include "pav_platform.h"
#include "pav_secret.h"
#include "pavillion.h"

#include <jansson.h>

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define PAV_SYNC_POLL_MS (120 * 1000)
#define PAV_SYNC_POLL_MIN_MS 1000
#define PAV_SYNC_SESSION_MAX (256u * 1024u)
#define PAV_SYNC_REDIRECTS 3
#define PAV_MAIL_URN "urn:ietf:params:jmap:mail"

typedef struct SyncReq SyncReq;

typedef struct PavSup {
    struct PavSup *next;
    char account_id[64];
    char name[200];
    char session_url[2048];
    char current_url[2048];
    char username[256];
    char auth[32];
    char ca_file[1024];
    char keyring_user[192];
    char state[32];
    int enabled;
    int auto_retry;
    int redirects;
    int attempts;
    uint64_t generation;
    int64_t activity_id;
    SyncReq *inflight;
} PavSup;

typedef struct SyncRemote {
    char ra_id[256];
    char jmap_id[128];
    char name[256];
    int is_personal;
    char *caps;
} SyncRemote;

typedef struct SyncResult {
    char account_id[64];
    char url[2048];
    char location[2048];
    char detail[160];
    char session_state[128];
    uint64_t generation;
    int status;
    int auth_rc;
    int parsed;
    char *session_json;
    SyncRemote *remotes;
    int remote_count;
} SyncResult;

struct SyncReq {
    atomic_int refs;
    uint64_t generation;
    char account_id[64];
    char url[2048];
    char ca_file[1024];
    char keyring_user[192];
    char username[256];
    char auth_method[32];
    PavReq req;
};

typedef struct StateWrite {
    char account_id[64];
    char state[32];
    char detail[160];
    char session_state[128];
    char *session_json;
    SyncRemote *remotes;
    int remote_count;
    int64_t now_ms;
} StateWrite;

static pthread_mutex_t g_sync_mu = PTHREAD_MUTEX_INITIALIZER;
static PavSup *g_sup;
static char g_profile_id[64];
static int g_booted;
static int g_sync_stop;
static int g_net_held;
static int64_t g_poll_ms = PAV_SYNC_POLL_MS;
static int64_t g_next_poll;

static int64_t wall_ms(void) {
    struct timespec ts;

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0 || ts.tv_sec < 0) {
        return 0;
    }
    return (int64_t)ts.tv_sec * 1000 + (int64_t)(ts.tv_nsec / 1000000L);
}

static int copy_fit(char *dst, size_t n, const char *src) {
    int wrote;

    if (n == 0) {
        return 0;
    }
    if (src == NULL) {
        src = "";
    }
    wrote = snprintf(dst, n, "%s", src);
    return wrote >= 0 && (size_t)wrote < n;
}

static int sync_stopped(void) {
    int stop;

    pthread_mutex_lock(&g_sync_mu);
    stop = g_sync_stop;
    pthread_mutex_unlock(&g_sync_mu);
    return stop;
}

static PavSup *find_sup(const char *id) {
    PavSup *s;

    if (id == NULL) {
        return NULL;
    }
    for (s = g_sup; s != NULL; s = s->next) {
        if (strcmp(s->account_id, id) == 0) {
            return s;
        }
    }
    return NULL;
}

static void sync_req_release(SyncReq *req) {
    if (req == NULL) {
        return;
    }
    if (atomic_fetch_sub(&req->refs, 1) == 1) {
        free(req);
    }
}

static void drop_inflight(PavSup *s) {
    SyncReq *req = s->inflight;

    s->inflight = NULL;
    if (req == NULL) {
        return;
    }
    req->req.cancelled = 1;
    sync_req_release(req);
}

static void emit_state(const char *id, const char *state, const char *detail) {
    json_t *obj;
    char *text;

    obj = json_pack("{s:s,s:s,s:s}", "accountId", id, "state", state, "detail",
                    detail != NULL ? detail : "");
    if (obj == NULL) {
        return;
    }
    text = json_dumps(obj, JSON_COMPACT);
    json_decref(obj);
    if (text == NULL) {
        return;
    }
    pav_event_post("sync.state", text);
    free(text);
    pav_log("sync", PAV_INFO, 2, "%s %s", id, state);
}

static int step_done(sqlite3_stmt *stmt) {
    int rc = pav_db_step(stmt);

    return rc == SQLITE_DONE ? 0 : -1;
}

/* The writer already wraps this job in one transaction. A nested BEGIN
   fails, the batch rolls the update back, and the row stays at the idle
   inserted by account.add. A zero-row update means the account is gone:
   leave the remotes alone and let the writer commit. */
static int write_state_job(void *ctx, sqlite3 *db) {
    StateWrite *w = ctx;
    sqlite3_stmt *stmt = NULL;
    int i;

    if (w->session_json != NULL) {
        if (pav_db_prepare(db, "sync.session",
                           "UPDATE account_state SET session_json = ?, session_state = ?, "
                           "session_fetched = ?, state = ?, last_error = NULL, last_sync = ? "
                           "WHERE account_id = ?",
                           &stmt) != 0 ||
            pav_db_bind_text(stmt, 1, w->session_json) != 0 ||
            pav_db_bind_text(stmt, 2, w->session_state) != 0 ||
            pav_db_bind_int64(stmt, 3, w->now_ms) != 0 ||
            pav_db_bind_text(stmt, 4, w->state) != 0 ||
            pav_db_bind_int64(stmt, 5, w->now_ms) != 0 ||
            pav_db_bind_text(stmt, 6, w->account_id) != 0 || step_done(stmt) != 0) {
            return -1;
        }
    } else if (pav_db_prepare(db, "sync.state",
                              "UPDATE account_state SET state = ?, last_error = ? "
                              "WHERE account_id = ?",
                              &stmt) != 0 ||
               pav_db_bind_text(stmt, 1, w->state) != 0 ||
               pav_db_bind_text(stmt, 2, w->detail[0] != '\0' ? w->detail : NULL) != 0 ||
               pav_db_bind_text(stmt, 3, w->account_id) != 0 || step_done(stmt) != 0) {
        return -1;
    }
    if (sqlite3_changes(db) == 0) {
        return 0;
    }
    if (w->session_json == NULL) {
        return 0;
    }
    if (pav_db_prepare(db, "sync.remote_clear",
                       "DELETE FROM remote_account WHERE account_id = ?", &stmt) != 0 ||
        pav_db_bind_text(stmt, 1, w->account_id) != 0 || step_done(stmt) != 0) {
        return -1;
    }
    for (i = 0; i < w->remote_count; i++) {
        SyncRemote *row = &w->remotes[i];

        if (pav_db_prepare(db, "sync.remote_insert",
                           "INSERT INTO remote_account "
                           "(id, account_id, jmap_account_id, name, is_personal, caps) "
                           "VALUES (?, ?, ?, ?, ?, ?)",
                           &stmt) != 0 ||
            pav_db_bind_text(stmt, 1, row->ra_id) != 0 ||
            pav_db_bind_text(stmt, 2, w->account_id) != 0 ||
            pav_db_bind_text(stmt, 3, row->jmap_id) != 0 ||
            pav_db_bind_text(stmt, 4, row->name) != 0 ||
            pav_db_bind_int(stmt, 5, row->is_personal) != 0 ||
            pav_db_bind_text(stmt, 6, row->caps != NULL ? row->caps : "[]") != 0 ||
            step_done(stmt) != 0) {
            return -1;
        }
    }
    return 0;
}

static void end_activity(PavSup *s, int how, const char *error) {
    int64_t id = s->activity_id;

    s->activity_id = 0;
    if (id > 0) {
        pav_activity_end(id, how, error);
    }
}

static void publish_state(PavSup *s, const char *state, const char *detail, int retry, int how) {
    StateWrite w;
    int64_t now;

    pthread_mutex_lock(&g_sync_mu);
    copy_fit(s->state, sizeof(s->state), state);
    s->auto_retry = retry;
    pthread_mutex_unlock(&g_sync_mu);
    if (how != 0) {
        end_activity(s, how, how == PAV_ACT_DONE ? NULL : detail);
    }
    memset(&w, 0, sizeof(w));
    copy_fit(w.account_id, sizeof(w.account_id), s->account_id);
    copy_fit(w.state, sizeof(w.state), state);
    copy_fit(w.detail, sizeof(w.detail), detail != NULL ? detail : "");
    now = pav_now_ms();
    w.now_ms = now > 0 ? now : 0;
    if (pav_db_call(write_state_job, &w) != 0) {
        pav_log("sync", PAV_ERROR, 1, "cannot store state for %s", s->account_id);
    }
    emit_state(s->account_id, state, w.detail);
}

static void publish_session(PavSup *s, SyncResult *r) {
    StateWrite w;
    int64_t now;

    pthread_mutex_lock(&g_sync_mu);
    copy_fit(s->state, sizeof(s->state), "idle");
    s->auto_retry = 0;
    pthread_mutex_unlock(&g_sync_mu);
    end_activity(s, PAV_ACT_DONE, NULL);
    memset(&w, 0, sizeof(w));
    copy_fit(w.account_id, sizeof(w.account_id), s->account_id);
    copy_fit(w.state, sizeof(w.state), "idle");
    copy_fit(w.session_state, sizeof(w.session_state), r->session_state);
    w.session_json = r->session_json;
    w.remotes = r->remotes;
    w.remote_count = r->remote_count;
    now = pav_now_ms();
    w.now_ms = now > 0 ? now : 0;
    if (pav_db_call(write_state_job, &w) != 0) {
        pav_log("sync", PAV_ERROR, 1, "cannot store session for %s", s->account_id);
    }
    emit_state(s->account_id, "idle", "");
}

static void sync_on_done(PavReq *req, void *user);

static int ensure_net(void) {
    if (g_net_held) {
        return 0;
    }
    if (pav_net_start() != 0) {
        return -1;
    }
    g_net_held = 1;
    return 0;
}

static SyncReq *make_req(PavSup *s) {
    SyncReq *req = calloc(1, sizeof(*req));

    if (req == NULL) {
        return NULL;
    }
    atomic_init(&req->refs, 2);
    req->generation = s->generation;
    copy_fit(req->account_id, sizeof(req->account_id), s->account_id);
    copy_fit(req->url, sizeof(req->url), s->current_url);
    copy_fit(req->ca_file, sizeof(req->ca_file), s->ca_file);
    copy_fit(req->keyring_user, sizeof(req->keyring_user), s->keyring_user);
    copy_fit(req->username, sizeof(req->username), s->username);
    copy_fit(req->auth_method, sizeof(req->auth_method), s->auth);
    req->req.method = "GET";
    req->req.url = req->url;
    req->req.account_id = req->account_id;
    req->req.keyring_user = req->keyring_user;
    req->req.username = req->username;
    req->req.auth_method = req->auth_method;
    req->req.ca_file = req->ca_file[0] != '\0' ? req->ca_file : NULL;
    req->req.on_done = sync_on_done;
    req->req.user = req;
    return req;
}

static void begin_activity(PavSup *s) {
    char title[200];
    int64_t id;

    if (s->activity_id > 0) {
        return;
    }
    if (!copy_fit(title, sizeof(title), s->name[0] != '\0' ? s->name : "Mail sync") || title[0] == '\0') {
        copy_fit(title, sizeof(title), "Mail sync");
    }
    id = pav_activity_begin("sync.mail", s->account_id, title, 1, 0);
    if (id > 0) {
        pav_activity_progress(id, 0, -1, NULL, "Session");
    }
    s->activity_id = id;
}

static void fail_submit(PavSup *s, SyncReq *req) {
    pthread_mutex_lock(&g_sync_mu);
    if (s->inflight == req) {
        s->inflight = NULL;
    }
    pthread_mutex_unlock(&g_sync_mu);
    sync_req_release(req);
    sync_req_release(req);
    publish_state(s, "offline", "offline", 1, PAV_ACT_FAILED);
}

static void start_connect(PavSup *s, int follow) {
    SyncReq *req;
    int64_t replaced = 0;

    if (s == NULL) {
        return;
    }
    pthread_mutex_lock(&g_sync_mu);
    if (g_sync_stop || !s->enabled) {
        pthread_mutex_unlock(&g_sync_mu);
        return;
    }
    if (!follow) {
        s->generation++;
        s->redirects = 0;
        copy_fit(s->current_url, sizeof(s->current_url), s->session_url);
        drop_inflight(s);
        replaced = s->activity_id;
        s->activity_id = 0;
    }
    if (s->current_url[0] == '\0' || strncmp(s->current_url, "https://", 8) != 0) {
        pthread_mutex_unlock(&g_sync_mu);
        if (replaced > 0) {
            pav_activity_end(replaced, PAV_ACT_CANCELLED, NULL);
        }
        publish_state(s, "error:server", "sessionUrl must be https", 0, PAV_ACT_FAILED);
        return;
    }
    s->attempts++;
    req = make_req(s);
    if (req == NULL) {
        pthread_mutex_unlock(&g_sync_mu);
        if (replaced > 0) {
            pav_activity_end(replaced, PAV_ACT_CANCELLED, NULL);
        }
        publish_state(s, "offline", "offline", 1, PAV_ACT_FAILED);
        return;
    }
    s->inflight = req;
    pthread_mutex_unlock(&g_sync_mu);

    if (replaced > 0) {
        pav_activity_end(replaced, PAV_ACT_CANCELLED, NULL);
    }
    if (ensure_net() != 0) {
        fail_submit(s, req);
        return;
    }
    begin_activity(s);
    if (!follow) {
        publish_state(s, "syncing", "Session", 0, 0);
    }
    if (pav_net_request(&req->req) != 0) {
        fail_submit(s, req);
    }
}

static size_t url_origin_len(const char *url) {
    const char *p = strstr(url, "://");

    if (p == NULL) {
        return 0;
    }
    p += 3;
    while (*p != '\0' && *p != '/' && *p != '?' && *p != '#') {
        p++;
    }
    return (size_t)(p - url);
}

static int resolve_location(const char *base, const char *loc, char *out, size_t len) {
    size_t origin = url_origin_len(base);
    int n;

    if (loc == NULL || loc[0] == '\0') {
        return -1;
    }
    if (strstr(loc, "://") != NULL) {
        n = snprintf(out, len, "%s", loc);
    } else if (origin == 0) {
        return -1;
    } else if (loc[0] == '/' && loc[1] == '/') {
        const char *colon = strchr(base, ':');

        if (colon == NULL) {
            return -1;
        }
        n = snprintf(out, len, "%.*s:%s", (int)(colon - base), base, loc);
    } else if (loc[0] == '/') {
        n = snprintf(out, len, "%.*s%s", (int)origin, base, loc);
    } else {
        const char *path = base + origin;
        const char *end = path;
        const char *slash = NULL;

        while (*end != '\0' && *end != '?' && *end != '#') {
            if (*end == '/') {
                slash = end;
            }
            end++;
        }
        if (slash == NULL) {
            n = snprintf(out, len, "%.*s/%s", (int)origin, base, loc);
        } else {
            n = snprintf(out, len, "%.*s%s", (int)(slash + 1 - base), base, loc);
        }
    }
    return (n < 0 || (size_t)n >= len) ? -1 : 0;
}

static int header_location(const json_t *headers, char *out, size_t len) {
    const char *key;
    json_t *val;

    out[0] = '\0';
    if (!json_is_object(headers)) {
        return -1;
    }
    json_object_foreach((json_t *)headers, key, val) {
        if (json_is_string(val) && strcasecmp(key, "Location") == 0) {
            return copy_fit(out, len, json_string_value(val)) ? 0 : -1;
        }
    }
    return -1;
}

static int looks_like_html(const char *body, size_t len) {
    size_t i = 0;

    if (body == NULL) {
        return 0;
    }
    while (i < len && (body[i] == ' ' || body[i] == '\t' || body[i] == '\r' || body[i] == '\n')) {
        i++;
    }
    return i < len && body[i] == '<';
}

static int account_has_mail(const json_t *entry) {
    const json_t *caps = json_object_get((json_t *)entry, "capabilities");

    return json_is_object(caps) && json_object_get((json_t *)caps, PAV_MAIL_URN) != NULL;
}

static char *caps_json(const json_t *entry) {
    const json_t *caps = json_object_get((json_t *)entry, "capabilities");
    json_t *arr;
    const char *key;
    json_t *val;
    char *text;

    arr = json_array();
    if (arr == NULL || !json_is_object(caps)) {
        json_decref(arr);
        return NULL;
    }
    json_object_foreach((json_t *)caps, key, val) {
        (void)val;
        if (json_array_append_new(arr, json_string(key)) != 0) {
            json_decref(arr);
            return NULL;
        }
    }
    text = json_dumps(arr, JSON_COMPACT);
    json_decref(arr);
    return text;
}

static int collect_remotes(const char *account_id, const PavJmapSession *sess, SyncRemote **out, int *out_n) {
    size_t n;
    size_t i;
    int count = 0;
    SyncRemote *rows;

    *out = NULL;
    *out_n = 0;
    if (sess->accounts == NULL || !json_is_array(sess->accounts)) {
        return 0;
    }
    n = json_array_size(sess->accounts);
    if (n == 0) {
        return 0;
    }
    rows = calloc(n, sizeof(*rows));
    if (rows == NULL) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        json_t *entry = json_array_get(sess->accounts, i);
        const char *id = json_string_value(json_object_get(entry, "id"));
        const char *name = json_string_value(json_object_get(entry, "name"));
        SyncRemote *row;
        int wrote;

        if (!json_is_object(entry) || id == NULL || id[0] == '\0' || !account_has_mail(entry)) {
            continue;
        }
        row = &rows[count];
        if (!copy_fit(row->jmap_id, sizeof(row->jmap_id), id)) {
            continue;
        }
        wrote = snprintf(row->ra_id, sizeof(row->ra_id), "%s/%s", account_id, row->jmap_id);
        if (wrote < 0 || (size_t)wrote >= sizeof(row->ra_id)) {
            continue;
        }
        copy_fit(row->name, sizeof(row->name), name != NULL ? name : "");
        row->is_personal = json_is_true(json_object_get(entry, "isPersonal")) ? 1 : 0;
        row->caps = caps_json(entry);
        count++;
    }
    if (count == 0) {
        free(rows);
        return 0;
    }
    *out = rows;
    *out_n = count;
    return 0;
}

static void free_remotes(SyncRemote *rows, int n) {
    int i;

    if (rows == NULL) {
        return;
    }
    for (i = 0; i < n; i++) {
        free(rows[i].caps);
    }
    free(rows);
}

void pav_sync_result_free(void *result) {
    SyncResult *r = result;

    if (r == NULL) {
        return;
    }
    free(r->session_json);
    free_remotes(r->remotes, r->remote_count);
    free(r);
}

static void release_outputs(PavReq *req) {
    if (req->resp_headers != NULL) {
        json_decref(req->resp_headers);
        req->resp_headers = NULL;
    }
    pav_net_free(req->body_out);
    req->body_out = NULL;
}

static void sync_on_done(PavReq *req, void *user) {
    SyncReq *owned = user;
    SyncResult *result;
    int stopped;

    (void)req;
    stopped = sync_stopped();
    if (stopped) {
        release_outputs(&owned->req);
        sync_req_release(owned);
        return;
    }
    result = calloc(1, sizeof(*result));
    if (result == NULL) {
        release_outputs(&owned->req);
        sync_req_release(owned);
        return;
    }
    copy_fit(result->account_id, sizeof(result->account_id), owned->account_id);
    copy_fit(result->url, sizeof(result->url), owned->url);
    result->generation = owned->generation;
    result->status = owned->req.status_code;
    result->auth_rc = owned->req.auth_rc;
    if (result->status >= 300 && result->status < 400) {
        (void)header_location(owned->req.resp_headers, result->location, sizeof(result->location));
    }
    if (result->status == 200 && owned->req.body_out != NULL &&
        owned->req.body_out_len > 0 && owned->req.body_out_len <= PAV_SYNC_SESSION_MAX &&
        !looks_like_html(owned->req.body_out, owned->req.body_out_len)) {
        PavJmapSession sess;
        char err[160];

        memset(&sess, 0, sizeof(sess));
        err[0] = '\0';
        if (pav_jmap_parse_session(owned->req.body_out, owned->req.body_out_len, &sess, err, sizeof(err)) == 0) {
            result->session_json = malloc(owned->req.body_out_len + 1);
            if (result->session_json != NULL) {
                memcpy(result->session_json, owned->req.body_out, owned->req.body_out_len);
                result->session_json[owned->req.body_out_len] = '\0';
                copy_fit(result->session_state, sizeof(result->session_state), sess.session_state);
                if (collect_remotes(owned->account_id, &sess, &result->remotes, &result->remote_count) == 0) {
                    result->parsed = 1;
                } else {
                    free(result->session_json);
                    result->session_json = NULL;
                    copy_fit(result->detail, sizeof(result->detail), "invalid session");
                }
            } else {
                copy_fit(result->detail, sizeof(result->detail), "invalid session");
            }
            pav_jmap_session_free(&sess);
        } else {
            copy_fit(result->detail, sizeof(result->detail), "invalid session");
        }
    } else if (result->status == 200) {
        copy_fit(result->detail, sizeof(result->detail),
                 owned->req.body_out_len > PAV_SYNC_SESSION_MAX ? "session too large" : "not a session");
    }
    if (sync_stopped() || pav_core_post_sync(result) != 0) {
        pav_sync_result_free(result);
    }
    release_outputs(&owned->req);
    sync_req_release(owned);
}

static int is_redirect(int status) {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

static void apply_result(PavSup *s, SyncResult *r) {
    char next[2048];

    if (is_redirect(r->status) && r->auth_rc == 0) {
        if (s->redirects >= PAV_SYNC_REDIRECTS || r->location[0] == '\0' ||
            resolve_location(r->url, r->location, next, sizeof(next)) != 0 ||
            strncmp(next, "https://", 8) != 0) {
            const char *detail = "redirect is not https";

            if (s->redirects >= PAV_SYNC_REDIRECTS) {
                detail = "too many redirects";
            } else if (r->location[0] == '\0') {
                detail = "redirect without location";
            }
            publish_state(s, "error:server", detail, 0, PAV_ACT_FAILED);
            return;
        }
        pthread_mutex_lock(&g_sync_mu);
        s->redirects++;
        copy_fit(s->current_url, sizeof(s->current_url), next);
        pthread_mutex_unlock(&g_sync_mu);
        start_connect(s, 1);
        return;
    }
    if (r->auth_rc == PAV_SECRET_LOCKED) {
        publish_state(s, "locked", "keyring is locked", 1, PAV_ACT_FAILED);
        return;
    }
    if (r->auth_rc == PAV_SECRET_MISSING || r->auth_rc == PAV_SECRET_FAILED || r->status == 401) {
        publish_state(s, "error:auth", "authentication failed", 0, PAV_ACT_FAILED);
        return;
    }
    if (r->status == 0) {
        publish_state(s, "offline", "offline", 1, PAV_ACT_FAILED);
        return;
    }
    if (r->status >= 500) {
        publish_state(s, "error:server", "server error", 1, PAV_ACT_FAILED);
        return;
    }
    if (r->status != 200 || !r->parsed) {
        publish_state(s, "error:server", r->detail[0] != '\0' ? r->detail : "request rejected", 0, PAV_ACT_FAILED);
        return;
    }
    publish_session(s, r);
}

void pav_sync_on_result(void *result) {
    SyncResult *r = result;
    PavSup *s;

    if (r == NULL) {
        return;
    }
    pthread_mutex_lock(&g_sync_mu);
    s = find_sup(r->account_id);
    if (g_sync_stop || s == NULL || s->generation != r->generation) {
        pthread_mutex_unlock(&g_sync_mu);
        pav_sync_result_free(r);
        return;
    }
    if (s->inflight != NULL) {
        SyncReq *req = s->inflight;

        s->inflight = NULL;
        sync_req_release(req);
    }
    pthread_mutex_unlock(&g_sync_mu);
    apply_result(s, r);
    pav_sync_result_free(r);
}

static void read_account(json_t *account, int *is_jmap, int *enabled, char *id, size_t id_n, char *name, size_t name_n,
                         char *url, size_t url_n, char *user, size_t user_n, char *auth, size_t auth_n, char *ca,
                         size_t ca_n) {
    const char *kind = json_string_value(json_object_get(account, "kind"));
    const char *auth_in = json_string_value(json_object_get(account, "auth"));
    json_t *en = json_object_get(account, "enabled");

    *is_jmap = kind != NULL && strcmp(kind, "jmap") == 0;
    *enabled = json_is_false(en) ? 0 : 1;
    copy_fit(id, id_n, json_string_value(json_object_get(account, "id")));
    copy_fit(name, name_n, json_string_value(json_object_get(account, "name")));
    copy_fit(url, url_n, json_string_value(json_object_get(account, "sessionUrl")));
    copy_fit(user, user_n, json_string_value(json_object_get(account, "username")));
    copy_fit(auth, auth_n, auth_in != NULL && auth_in[0] != '\0' ? auth_in : "basic");
    copy_fit(ca, ca_n, json_string_value(json_object_get(account, "caFile")));
}

static void fill_keyring(PavSup *s) {
    snprintf(s->keyring_user, sizeof(s->keyring_user), "%s/%s", g_profile_id, s->account_id);
}

static int same_text(const char *a, const char *b) {
    if (a == NULL) {
        a = "";
    }
    if (b == NULL) {
        b = "";
    }
    return strcmp(a, b) == 0;
}

void pav_sync_note_account(json_t *account) {
    char id[64];
    char name[200];
    char url[2048];
    char user[256];
    char auth[32];
    char ca[1024];
    int is_jmap = 0;
    int enabled = 1;
    int changed;
    PavSup *s;

    if (!json_is_object(account) || g_profile_id[0] == '\0') {
        return;
    }
    read_account(account, &is_jmap, &enabled, id, sizeof(id), name, sizeof(name), url, sizeof(url), user, sizeof(user),
                 auth, sizeof(auth), ca, sizeof(ca));
    if (id[0] == '\0') {
        return;
    }
    pthread_mutex_lock(&g_sync_mu);
    s = find_sup(id);
    if (!is_jmap) {
        pthread_mutex_unlock(&g_sync_mu);
        if (s != NULL) {
            pav_sync_note_removed(id);
        }
        return;
    }
    if (s == NULL) {
        s = calloc(1, sizeof(*s));
        if (s == NULL) {
            pthread_mutex_unlock(&g_sync_mu);
            return;
        }
        copy_fit(s->account_id, sizeof(s->account_id), id);
        s->next = g_sup;
        g_sup = s;
        changed = 1;
    } else {
        changed = !same_text(s->session_url, url) || !same_text(s->username, user) || !same_text(s->auth, auth) ||
                  !same_text(s->ca_file, ca) || s->enabled != enabled;
    }
    copy_fit(s->name, sizeof(s->name), name);
    copy_fit(s->session_url, sizeof(s->session_url), url);
    copy_fit(s->username, sizeof(s->username), user);
    copy_fit(s->auth, sizeof(s->auth), auth);
    copy_fit(s->ca_file, sizeof(s->ca_file), ca);
    s->enabled = enabled;
    fill_keyring(s);
    pthread_mutex_unlock(&g_sync_mu);

    if (!enabled) {
        pthread_mutex_lock(&g_sync_mu);
        s->generation++;
        drop_inflight(s);
        pthread_mutex_unlock(&g_sync_mu);
        publish_state(s, "disabled", "", 0, PAV_ACT_CANCELLED);
        return;
    }
    if (changed) {
        start_connect(s, 0);
    }
}

void pav_sync_note_removed(const char *account_id) {
    PavSup *prev = NULL;
    PavSup *s;
    int64_t act = 0;

    if (account_id == NULL) {
        return;
    }
    pthread_mutex_lock(&g_sync_mu);
    for (s = g_sup; s != NULL; prev = s, s = s->next) {
        if (strcmp(s->account_id, account_id) == 0) {
            break;
        }
    }
    if (s == NULL) {
        pthread_mutex_unlock(&g_sync_mu);
        return;
    }
    if (prev == NULL) {
        g_sup = s->next;
    } else {
        prev->next = s->next;
    }
    s->generation++;
    drop_inflight(s);
    act = s->activity_id;
    s->activity_id = 0;
    pthread_mutex_unlock(&g_sync_mu);
    if (act > 0) {
        pav_activity_end(act, PAV_ACT_CANCELLED, NULL);
    }
    free(s);
}

void pav_sync_note_secret(const char *account_id) {
    PavSup *s;

    pthread_mutex_lock(&g_sync_mu);
    s = find_sup(account_id);
    pthread_mutex_unlock(&g_sync_mu);
    if (s != NULL && s->enabled) {
        start_connect(s, 0);
    }
}

int pav_sync_now(const char *account_id) {
    PavSup *s;

    pthread_mutex_lock(&g_sync_mu);
    if (!g_booted || g_sync_stop) {
        pthread_mutex_unlock(&g_sync_mu);
        return 0;
    }
    if (account_id != NULL) {
        s = find_sup(account_id);
        pthread_mutex_unlock(&g_sync_mu);
        if (s != NULL && s->enabled) {
            start_connect(s, 0);
        }
        return 0;
    }
    s = g_sup;
    pthread_mutex_unlock(&g_sync_mu);
    for (; s != NULL; s = s->next) {
        if (s->enabled) {
            start_connect(s, 0);
        }
    }
    return 0;
}

static void read_poll(json_t *doc) {
    json_t *prefs = json_object_get(doc, "preferences");
    json_t *sync = json_object_get(prefs, "sync");
    json_t *poll = json_object_get(sync, "pollSeconds");
    json_int_t v;

    g_poll_ms = PAV_SYNC_POLL_MS;
    if (!json_is_integer(poll)) {
        return;
    }
    v = json_integer_value(poll);
    if (v >= 15 && v <= 86400) {
        g_poll_ms = (int64_t)v * 1000;
    }
}

int pav_sync_boot(json_t *profile_doc, const char *profile_id) {
    json_t *accounts;
    size_t i;

    if (profile_id == NULL || profile_id[0] == '\0') {
        return -1;
    }
    pthread_mutex_lock(&g_sync_mu);
    if (g_booted) {
        pthread_mutex_unlock(&g_sync_mu);
        return 0;
    }
    copy_fit(g_profile_id, sizeof(g_profile_id), profile_id);
    read_poll(profile_doc);
    g_sync_stop = 0;
    g_booted = 1;
    g_next_poll = wall_ms() + g_poll_ms;
    pthread_mutex_unlock(&g_sync_mu);
    if (ensure_net() != 0) {
        pav_log("sync", PAV_ERROR, 0, "cannot start net");
    }
    accounts = json_object_get(profile_doc, "accounts");
    if (!json_is_array(accounts)) {
        return 0;
    }
    for (i = 0; i < json_array_size(accounts); i++) {
        pav_sync_note_account(json_array_get(accounts, i));
    }
    return 0;
}

void pav_sync_shutdown(void) {
    PavSup *s;
    PavSup *next;

    pthread_mutex_lock(&g_sync_mu);
    g_sync_stop = 1;
    g_booted = 0;
    g_net_held = 0;
    s = g_sup;
    g_sup = NULL;
    pthread_mutex_unlock(&g_sync_mu);
    while (s != NULL) {
        int64_t act;

        next = s->next;
        drop_inflight(s);
        act = s->activity_id;
        s->activity_id = 0;
        if (act > 0) {
            pav_activity_end(act, PAV_ACT_CANCELLED, NULL);
        }
        free(s);
        s = next;
    }
}

int pav_sync_deadline(struct timespec *out) {
    int64_t ms;
    int on;

    pthread_mutex_lock(&g_sync_mu);
    on = g_booted && !g_sync_stop;
    ms = g_next_poll;
    pthread_mutex_unlock(&g_sync_mu);
    if (!on || out == NULL) {
        return 0;
    }
    out->tv_sec = (time_t)(ms / 1000);
    out->tv_nsec = (long)((ms % 1000) * 1000000L);
    return 1;
}

void pav_sync_on_wake(void) {
    PavSup *s;
    int64_t now = wall_ms();

    pthread_mutex_lock(&g_sync_mu);
    if (!g_booted || g_sync_stop || now < g_next_poll) {
        pthread_mutex_unlock(&g_sync_mu);
        return;
    }
    g_next_poll = now + g_poll_ms;
    s = g_sup;
    pthread_mutex_unlock(&g_sync_mu);
    for (; s != NULL; s = s->next) {
        int go = 0;

        pthread_mutex_lock(&g_sync_mu);
        go = s->enabled && s->auto_retry && s->inflight == NULL && !g_sync_stop;
        pthread_mutex_unlock(&g_sync_mu);
        if (go) {
            start_connect(s, 0);
        }
    }
}

void pav_sync_set_poll_ms(int64_t ms) {
    if (ms < PAV_SYNC_POLL_MIN_MS) {
        ms = PAV_SYNC_POLL_MIN_MS;
    }
    pthread_mutex_lock(&g_sync_mu);
    g_poll_ms = ms;
    g_next_poll = wall_ms() + ms;
    pthread_mutex_unlock(&g_sync_mu);
}

int pav_sync_attempt_count(const char *account_id) {
    int n = 0;
    PavSup *s;

    pthread_mutex_lock(&g_sync_mu);
    s = find_sup(account_id);
    if (s != NULL) {
        n = s->attempts;
    }
    pthread_mutex_unlock(&g_sync_mu);
    return n;
}

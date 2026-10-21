#define _POSIX_C_SOURCE 200809L

/* M-3 supervisor. PavSup stays on the core thread. The net thread only
   reads a SyncReq. It parses the Session or a type response there and posts
   the rows. The request stays allocated until the callback and the supervisor
   have each released it, so a cancel cannot free it while the callback still
   runs. The net callback never takes the core lock while it holds g_sync_mu. */

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
#define PAV_SYNC_TYPE_MAX (1024u * 1024u)
#define PAV_SYNC_REDIRECTS 3
#define PAV_SYNC_PAGE_MAX 8
/* One Mailbox/get page. A foods-sized mailbox object is a few hundred bytes,
   and a type response is capped at 1 MB. The Session's maxObjectsInGet can
   be lower. The walk repeats until every mailbox is stored. */
#define PAV_SYNC_MBX_PAGE 500
#define PAV_SYNC_LIST_MAX 256
#define PAV_MAIL_URN "urn:ietf:params:jmap:mail"
#define PAV_SUBMISSION_URN "urn:ietf:params:jmap:submission"
#define SYNC_KIND_SESSION 0
#define SYNC_KIND_TYPES 1
#define TYPE_SKIP 0
#define TYPE_GET 1
#define TYPE_CHANGES 2
#define TYPE_GET_IDS 3
#define TYPE_QUERY 4

typedef struct SyncReq SyncReq;

typedef struct SupRemote {
    struct SupRemote *next;
    char ra_id[256];
    char jmap_id[128];
    int mbx_pages;
    int idn_pages;
    int mbx_resyncs;
    int idn_resyncs;
    int force_mbx_get;
    int force_idn_get;
    int pending_mbx_more;
    int pending_idn_more;
    char pending_mbx_state[128];
    char pending_idn_state[128];
    int has_submission;
    int mbx_limit;
    int mbx_pos;
    int mbx_total;
    int mbx_last_n;
    int mbx_paging;
    int mbx_list_pages;
    int mbx_restarts;
    char mbx_anchor[128];
    int mbx_probe;
    int mbx_stored;
} SupRemote;

typedef struct PavSup {
    struct PavSup *next;
    char account_id[64];
    char name[200];
    char session_url[2048];
    char current_url[2048];
    char api_url[2048];
    char username[256];
    char auth[32];
    char ca_file[1024];
    char keyring_user[192];
    char state[32];
    int enabled;
    int auto_retry;
    int redirects;
    int attempts;
    int saw_mailbox;
    uint64_t generation;
    int64_t activity_id;
    SyncReq *inflight;
    SupRemote *remotes;
} PavSup;

typedef struct SyncRemote {
    char ra_id[256];
    char jmap_id[128];
    char name[256];
    int is_personal;
    char *caps;
} SyncRemote;

typedef struct SyncResult {
    int kind;
    char account_id[64];
    uint64_t generation;
    char url[2048];
    char api_url[2048];
    char location[2048];
    char detail[160];
    char session_state[128];
    int status;
    int auth_rc;
    int parsed;
    char *session_json;
    SyncRemote *remotes;
    int remote_count;
} SyncResult;

typedef struct SyncId {
    char id[128];
} SyncId;

typedef struct MbxRow {
    char id[128];
    char parent_id[128];
    char name[256];
    char role[64];
    int sort_order;
    int total;
    int unread;
    int total_threads;
    int unread_threads;
    char *my_rights;
    int is_subscribed;
} MbxRow;

typedef struct IdnRow {
    char id[128];
    char name[256];
    char email[320];
    char *reply_to;
    char *bcc;
    char *sig_html;
    char *sig_text;
    int may_delete;
} IdnRow;

typedef struct TypeResult {
    int kind;
    char account_id[64];
    uint64_t generation;
    char ra_id[256];
    char detail[160];
    int status;
    int auth_rc;
    int mbx_mode;
    int idn_mode;
    int mbx_resync;
    int idn_resync;
    int mbx_more;
    int idn_more;
    int method_error;
    int mbx_total;
    char mbx_state[128];
    char idn_state[128];
    MbxRow *mailboxes;
    int mailbox_n;
    IdnRow *identities;
    int identity_n;
    SyncId *mbx_destroyed;
    int mbx_destroyed_n;
    SyncId *idn_destroyed;
    int idn_destroyed_n;
    SyncId *mbx_fetch;
    int mbx_fetch_n;
    SyncId *idn_fetch;
    int idn_fetch_n;
} TypeResult;

struct SyncReq {
    atomic_int refs;
    uint64_t generation;
    int kind;
    char account_id[64];
    char url[2048];
    char ca_file[1024];
    char keyring_user[192];
    char username[256];
    char auth_method[32];
    char ra_id[256];
    char jmap_id[128];
    int mbx_mode;
    int idn_mode;
    char *body;
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
        free(req->body);
        if (req->req.headers != NULL) {
            json_decref(req->req.headers);
        }
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
    /* Keep a remote row whose id is still in the Session. Deleting it would
       cascade mailbox, identity, and sync_state, and the next poll would have
       no sinceState. Drop only remotes the Session no longer lists. */
    {
        typedef struct HaveId {
            char id[256];
        } HaveId;
        HaveId *have = NULL;
        int have_n = 0;
        int have_cap = 0;
        int rc;
        int h;

        if (pav_db_prepare(db, "sync.remote_ids",
                           "SELECT id FROM remote_account WHERE account_id = ?", &stmt) != 0 ||
            pav_db_bind_text(stmt, 1, w->account_id) != 0) {
            return -1;
        }
        while ((rc = pav_db_step(stmt)) == SQLITE_ROW) {
            const char *id = (const char *)sqlite3_column_text(stmt, 0);
            HaveId *grown;

            if (id == NULL) {
                continue;
            }
            if (have_n == have_cap) {
                have_cap = have_cap == 0 ? 8 : have_cap * 2;
                grown = realloc(have, (size_t)have_cap * sizeof(*have));
                if (grown == NULL) {
                    free(have);
                    return -1;
                }
                have = grown;
            }
            if (!copy_fit(have[have_n].id, sizeof(have[have_n].id), id)) {
                free(have);
                return -1;
            }
            have_n++;
        }
        if (rc != SQLITE_DONE) {
            free(have);
            return -1;
        }
        for (h = 0; h < have_n; h++) {
            int found = 0;

            for (i = 0; i < w->remote_count; i++) {
                if (strcmp(have[h].id, w->remotes[i].ra_id) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                if (pav_db_prepare(db, "sync.remote_delete",
                                   "DELETE FROM remote_account WHERE id = ?", &stmt) != 0 ||
                    pav_db_bind_text(stmt, 1, have[h].id) != 0 || step_done(stmt) != 0) {
                    free(have);
                    return -1;
                }
            }
        }
        for (i = 0; i < w->remote_count; i++) {
            SyncRemote *row = &w->remotes[i];
            int exists = 0;

            for (h = 0; h < have_n; h++) {
                if (strcmp(have[h].id, row->ra_id) == 0) {
                    exists = 1;
                    break;
                }
            }
            if (exists) {
                if (pav_db_prepare(db, "sync.remote_update",
                                   "UPDATE remote_account SET name = ?, is_personal = ?, caps = ? "
                                   "WHERE id = ?",
                                   &stmt) != 0 ||
                    pav_db_bind_text(stmt, 1, row->name) != 0 ||
                    pav_db_bind_int(stmt, 2, row->is_personal) != 0 ||
                    pav_db_bind_text(stmt, 3, row->caps != NULL ? row->caps : "[]") != 0 ||
                    pav_db_bind_text(stmt, 4, row->ra_id) != 0 || step_done(stmt) != 0) {
                    free(have);
                    return -1;
                }
            } else if (pav_db_prepare(db, "sync.remote_insert",
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
                free(have);
                return -1;
            }
        }
        free(have);
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
        s->saw_mailbox = 0;
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

static char *dup_capped(const char *text, size_t max) {
    char *copy;
    size_t n;

    if (text == NULL || text[0] == '\0') {
        return NULL;
    }
    n = strlen(text);
    if (n > max) {
        return NULL;
    }
    copy = malloc(n + 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, text, n + 1);
    return copy;
}

static char *dump_capped(json_t *value, size_t max) {
    char *text;

    if (value == NULL || json_is_null(value)) {
        return NULL;
    }
    text = json_dumps(value, JSON_COMPACT);
    if (text == NULL) {
        return NULL;
    }
    if (strlen(text) > max) {
        free(text);
        return NULL;
    }
    return text;
}

static int json_int_field(const json_t *obj, const char *key) {
    json_t *value = json_object_get(obj, key);

    if (!json_is_integer(value)) {
        return 0;
    }
    return (int)json_integer_value(value);
}

static void copy_json_string(char *dst, size_t n, const json_t *obj, const char *key) {
    const char *text = json_string_value(json_object_get(obj, key));

    if (n == 0) {
        return;
    }
    snprintf(dst, n, "%s", text != NULL ? text : "");
}

static int append_id(SyncId **list, int *n, const char *id) {
    SyncId *grown;

    if (id == NULL || id[0] == '\0' || strlen(id) >= sizeof((*list)->id)) {
        return 0;
    }
    grown = realloc(*list, (size_t)(*n + 1) * sizeof(**list));
    if (grown == NULL) {
        return -1;
    }
    *list = grown;
    memset(&(*list)[*n], 0, sizeof((*list)[*n]));
    copy_fit((*list)[*n].id, sizeof((*list)[*n].id), id);
    (*n)++;
    return 0;
}

static int append_id_array(SyncId **list, int *n, const json_t *arr) {
    size_t i;

    if (!json_is_array(arr)) {
        return 0;
    }
    for (i = 0; i < json_array_size(arr); i++) {
        if (append_id(list, n, json_string_value(json_array_get(arr, i))) != 0) {
            return -1;
        }
    }
    return 0;
}

static int append_mailbox(TypeResult *out, const json_t *obj) {
    MbxRow *grown;
    MbxRow *row;
    const char *id = json_string_value(json_object_get(obj, "id"));

    if (!json_is_object(obj) || id == NULL || id[0] == '\0' || strlen(id) >= sizeof(row->id)) {
        return 0;
    }
    grown = realloc(out->mailboxes, (size_t)(out->mailbox_n + 1) * sizeof(*grown));
    if (grown == NULL) {
        return -1;
    }
    out->mailboxes = grown;
    row = &out->mailboxes[out->mailbox_n];
    memset(row, 0, sizeof(*row));
    copy_fit(row->id, sizeof(row->id), id);
    copy_json_string(row->parent_id, sizeof(row->parent_id), obj, "parentId");
    copy_json_string(row->name, sizeof(row->name), obj, "name");
    copy_json_string(row->role, sizeof(row->role), obj, "role");
    row->sort_order = json_int_field(obj, "sortOrder");
    row->total = json_int_field(obj, "totalEmails");
    row->unread = json_int_field(obj, "unreadEmails");
    row->total_threads = json_int_field(obj, "totalThreads");
    row->unread_threads = json_int_field(obj, "unreadThreads");
    row->my_rights = dump_capped(json_object_get(obj, "myRights"), 4096);
    row->is_subscribed = json_is_true(json_object_get(obj, "isSubscribed")) ? 1 : 0;
    out->mailbox_n++;
    return 0;
}

static int append_identity(TypeResult *out, const json_t *obj) {
    IdnRow *grown;
    IdnRow *row;
    const char *id = json_string_value(json_object_get(obj, "id"));

    if (!json_is_object(obj) || id == NULL || id[0] == '\0' || strlen(id) >= sizeof(row->id)) {
        return 0;
    }
    grown = realloc(out->identities, (size_t)(out->identity_n + 1) * sizeof(*grown));
    if (grown == NULL) {
        return -1;
    }
    out->identities = grown;
    row = &out->identities[out->identity_n];
    memset(row, 0, sizeof(*row));
    copy_fit(row->id, sizeof(row->id), id);
    copy_json_string(row->name, sizeof(row->name), obj, "name");
    copy_json_string(row->email, sizeof(row->email), obj, "email");
    row->reply_to = dump_capped(json_object_get(obj, "replyTo"), 8192);
    row->bcc = dump_capped(json_object_get(obj, "bcc"), 8192);
    row->sig_html = dup_capped(json_string_value(json_object_get(obj, "htmlSignature")), 65536);
    row->sig_text = dup_capped(json_string_value(json_object_get(obj, "textSignature")), 65536);
    row->may_delete = json_is_true(json_object_get(obj, "mayDelete")) ? 1 : 0;
    out->identity_n++;
    return 0;
}

/* 1 mailbox, 2 identity, 0 unknown. Call ids from this file start with m or i. */
static int call_side(const json_t *entry) {
    const char *id = json_string_value(json_array_get(entry, 2));

    if (id == NULL || id[0] == '\0') {
        return 0;
    }
    if (id[0] == 'm') {
        return 1;
    }
    if (id[0] == 'i') {
        return 2;
    }
    return 0;
}

/* RFC 8620 names the method-error field type. The fixture also sends code,
   and one path nests that object under error. */
static const char *error_type_field(const json_t *obj) {
    const char *type;
    const char *code;

    if (!json_is_object(obj)) {
        return NULL;
    }
    type = json_string_value(json_object_get((json_t *)obj, "type"));
    if (type != NULL && type[0] != '\0') {
        return type;
    }
    code = json_string_value(json_object_get((json_t *)obj, "code"));
    if (code != NULL && code[0] != '\0') {
        return code;
    }
    return NULL;
}

static const char *method_error_code(const json_t *entry) {
    const char *name = json_string_value(json_array_get(entry, 0));
    const json_t *args;
    const char *code;
    const json_t *inner;

    if (name == NULL || strcmp(name, "error") != 0) {
        return NULL;
    }
    args = json_array_get(entry, 1);
    if (!json_is_object(args)) {
        return "";
    }
    code = error_type_field(args);
    if (code != NULL) {
        return code;
    }
    inner = json_object_get(args, "error");
    code = error_type_field(inner);
    return code != NULL ? code : "";
}

static int caps_has_urn(const char *caps, const char *urn) {
    const char *p;
    size_t n;

    if (caps == NULL || urn == NULL || urn[0] == '\0') {
        return 0;
    }
    n = strlen(urn);
    for (p = strstr(caps, urn); p != NULL; p = strstr(p + n, urn)) {
        if (p > caps && p[-1] == '"' && p[n] == '"') {
            return 1;
        }
    }
    return 0;
}

static int reduce_types(const char *body, TypeResult *out) {
    json_t *root;
    json_t *responses;
    size_t i;
    size_t n;

    root = json_loads(body, 0, NULL);
    if (!json_is_object(root)) {
        json_decref(root);
        copy_fit(out->detail, sizeof(out->detail), "not a jmap response");
        out->method_error = 1;
        return -1;
    }
    responses = json_object_get(root, "methodResponses");
    if (!json_is_array(responses)) {
        json_decref(root);
        copy_fit(out->detail, sizeof(out->detail), "not a jmap response");
        out->method_error = 1;
        return -1;
    }
    n = json_array_size(responses);
    for (i = 0; i < n; i++) {
        json_t *entry = json_array_get(responses, i);
        const char *name;
        const char *code;
        json_t *args;
        int side;

        if (!json_is_array(entry) || json_array_size(entry) < 2) {
            continue;
        }
        name = json_string_value(json_array_get(entry, 0));
        args = json_array_get(entry, 1);
        side = call_side(entry);
        code = method_error_code(entry);
        if (code != NULL) {
            if (strcmp(code, "cannotCalculateChanges") == 0 && side == 1) {
                out->mbx_resync = 1;
            } else if (strcmp(code, "cannotCalculateChanges") == 0 && side == 2) {
                out->idn_resync = 1;
            } else {
                out->method_error = 1;
                copy_fit(out->detail, sizeof(out->detail), code[0] != '\0' ? code : "request rejected");
            }
            continue;
        }
        if (name == NULL || !json_is_object(args)) {
            continue;
        }
        if (strcmp(name, "Mailbox/query") == 0) {
            json_t *total = json_object_get(args, "total");

            if (json_is_integer(total)) {
                json_int_t ntotal = json_integer_value(total);

                out->mbx_total = ntotal > 0 ? (int)ntotal : 0;
            }
            if (append_id_array(&out->mbx_fetch, &out->mbx_fetch_n, json_object_get(args, "ids")) != 0) {
                json_decref(root);
                return -1;
            }
        } else if (strcmp(name, "Mailbox/get") == 0) {
            size_t k;
            json_t *list = json_object_get(args, "list");

            copy_json_string(out->mbx_state, sizeof(out->mbx_state), args, "state");
            if (json_is_array(list)) {
                for (k = 0; k < json_array_size(list); k++) {
                    if (append_mailbox(out, json_array_get(list, k)) != 0) {
                        json_decref(root);
                        return -1;
                    }
                }
            }
        } else if (strcmp(name, "Identity/get") == 0) {
            size_t k;
            json_t *list = json_object_get(args, "list");

            copy_json_string(out->idn_state, sizeof(out->idn_state), args, "state");
            if (json_is_array(list)) {
                for (k = 0; k < json_array_size(list); k++) {
                    if (append_identity(out, json_array_get(list, k)) != 0) {
                        json_decref(root);
                        return -1;
                    }
                }
            }
        } else if (strcmp(name, "Mailbox/changes") == 0) {
            copy_json_string(out->mbx_state, sizeof(out->mbx_state), args, "newState");
            out->mbx_more = json_is_true(json_object_get(args, "hasMoreChanges")) ? 1 : 0;
            if (append_id_array(&out->mbx_destroyed, &out->mbx_destroyed_n, json_object_get(args, "destroyed")) != 0 ||
                append_id_array(&out->mbx_fetch, &out->mbx_fetch_n, json_object_get(args, "created")) != 0 ||
                append_id_array(&out->mbx_fetch, &out->mbx_fetch_n, json_object_get(args, "updated")) != 0) {
                json_decref(root);
                return -1;
            }
        } else if (strcmp(name, "Identity/changes") == 0) {
            copy_json_string(out->idn_state, sizeof(out->idn_state), args, "newState");
            out->idn_more = json_is_true(json_object_get(args, "hasMoreChanges")) ? 1 : 0;
            if (append_id_array(&out->idn_destroyed, &out->idn_destroyed_n, json_object_get(args, "destroyed")) != 0 ||
                append_id_array(&out->idn_fetch, &out->idn_fetch_n, json_object_get(args, "created")) != 0 ||
                append_id_array(&out->idn_fetch, &out->idn_fetch_n, json_object_get(args, "updated")) != 0) {
                json_decref(root);
                return -1;
            }
        }
    }
    json_decref(root);
    return 0;
}

static void free_type_result(TypeResult *t) {
    int i;

    if (t == NULL) {
        return;
    }
    for (i = 0; i < t->mailbox_n; i++) {
        free(t->mailboxes[i].my_rights);
    }
    for (i = 0; i < t->identity_n; i++) {
        free(t->identities[i].reply_to);
        free(t->identities[i].bcc);
        free(t->identities[i].sig_html);
        free(t->identities[i].sig_text);
    }
    free(t->mailboxes);
    free(t->identities);
    free(t->mbx_destroyed);
    free(t->idn_destroyed);
    free(t->mbx_fetch);
    free(t->idn_fetch);
    free(t);
}

void pav_sync_result_free(void *result) {
    SyncResult *r = result;

    if (r == NULL) {
        return;
    }
    if (r->kind == SYNC_KIND_TYPES) {
        free_type_result(result);
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

static void type_on_done(SyncReq *owned, int stopped) {
    TypeResult *result;

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
    result->kind = SYNC_KIND_TYPES;
    copy_fit(result->account_id, sizeof(result->account_id), owned->account_id);
    copy_fit(result->ra_id, sizeof(result->ra_id), owned->ra_id);
    result->generation = owned->generation;
    result->status = owned->req.status_code;
    result->auth_rc = owned->req.auth_rc;
    result->mbx_mode = owned->mbx_mode;
    result->idn_mode = owned->idn_mode;
    result->mbx_total = -1;
    if (result->status == 200 && owned->req.body_out != NULL && owned->req.body_out_len > 0 &&
        owned->req.body_out_len <= PAV_SYNC_TYPE_MAX) {
        if (reduce_types(owned->req.body_out, result) != 0 && !result->method_error) {
            result->method_error = 1;
            copy_fit(result->detail, sizeof(result->detail), "not a jmap response");
        }
    } else if (result->status == 200) {
        result->method_error = 1;
        copy_fit(result->detail, sizeof(result->detail),
                 owned->req.body_out_len > PAV_SYNC_TYPE_MAX ? "response too large" : "not a jmap response");
    }
    if (sync_stopped() || pav_core_post_sync(result) != 0) {
        pav_sync_result_free(result);
    }
    release_outputs(&owned->req);
    sync_req_release(owned);
}

static void sync_on_done(PavReq *req, void *user) {
    SyncReq *owned = user;
    SyncResult *result;
    int stopped;

    (void)req;
    stopped = sync_stopped();
    if (owned->kind == SYNC_KIND_TYPES) {
        type_on_done(owned, stopped);
        return;
    }
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
                copy_fit(result->api_url, sizeof(result->api_url), sess.api_url);
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

static void apply_session_ok(PavSup *s, SyncResult *r);
static void apply_types(PavSup *s, TypeResult *t);

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
    apply_session_ok(s, r);
}

void pav_sync_on_result(void *result) {
    int kind;
    const char *account_id;
    uint64_t generation;
    PavSup *s;

    if (result == NULL) {
        return;
    }
    kind = *(int *)result;
    if (kind == SYNC_KIND_TYPES) {
        TypeResult *t = result;

        account_id = t->account_id;
        generation = t->generation;
    } else {
        SyncResult *r = result;

        account_id = r->account_id;
        generation = r->generation;
    }
    pthread_mutex_lock(&g_sync_mu);
    s = find_sup(account_id);
    if (g_sync_stop || s == NULL || s->generation != generation) {
        pthread_mutex_unlock(&g_sync_mu);
        pav_sync_result_free(result);
        return;
    }
    if (s->inflight != NULL) {
        SyncReq *req = s->inflight;

        s->inflight = NULL;
        sync_req_release(req);
    }
    pthread_mutex_unlock(&g_sync_mu);
    if (kind == SYNC_KIND_TYPES) {
        apply_types(s, result);
    } else {
        apply_result(s, result);
    }
    pav_sync_result_free(result);
}

static void finish_sync_ok(PavSup *s);
static void begin_remote(PavSup *s, SupRemote *remote);
static void start_type_request(PavSup *s, SupRemote *remote, int mbx_mode, int idn_mode, const SyncId *mbx_ids,
                               int mbx_n, const SyncId *idn_ids, int idn_n);

static void free_sup_remote_list(SupRemote *list) {
    while (list != NULL) {
        SupRemote *next = list->next;

        free(list);
        list = next;
    }
}

static void reset_sup_remote(SupRemote *remote) {
    remote->mbx_pages = 0;
    remote->idn_pages = 0;
    remote->mbx_resyncs = 0;
    remote->idn_resyncs = 0;
    remote->force_mbx_get = 0;
    remote->force_idn_get = 0;
    remote->pending_mbx_more = 0;
    remote->pending_idn_more = 0;
    remote->pending_mbx_state[0] = '\0';
    remote->pending_idn_state[0] = '\0';
    remote->mbx_limit = 0;
    remote->mbx_pos = 0;
    remote->mbx_total = -1;
    remote->mbx_last_n = 0;
    remote->mbx_paging = 0;
    remote->mbx_list_pages = 0;
    remote->mbx_restarts = 0;
    remote->mbx_anchor[0] = '\0';
    remote->mbx_probe = 0;
    remote->mbx_stored = 0;
}

/* A new mailbox walk. The page cap and the one restart belong to this walk. */
static void begin_mbx_walk(SupRemote *remote) {
    remote->mbx_pos = 0;
    remote->mbx_total = -1;
    remote->mbx_last_n = 0;
    remote->mbx_paging = 1;
    remote->mbx_list_pages = 0;
    remote->mbx_restarts = 0;
    remote->mbx_anchor[0] = '\0';
    remote->mbx_probe = 0;
}

/* Reuse a supervisor node when the JMAP account id is unchanged, and zero its
   page counters. A new Session must not drop the node out from under a cycle,
   and a new cycle must not inherit the previous page cap. */
static void merge_sup_remotes(PavSup *s, const SyncResult *r) {
    SupRemote *old;
    SupRemote *keep = NULL;
    SupRemote *tail = NULL;
    int i;

    pthread_mutex_lock(&g_sync_mu);
    old = s->remotes;
    s->remotes = NULL;
    pthread_mutex_unlock(&g_sync_mu);
    for (i = 0; i < r->remote_count; i++) {
        SupRemote *prev = NULL;
        SupRemote *cur;
        SupRemote *node = NULL;

        for (cur = old; cur != NULL; prev = cur, cur = cur->next) {
            if (strcmp(cur->jmap_id, r->remotes[i].jmap_id) == 0) {
                node = cur;
                if (prev == NULL) {
                    old = cur->next;
                } else {
                    prev->next = cur->next;
                }
                node->next = NULL;
                break;
            }
        }
        if (node == NULL) {
            node = calloc(1, sizeof(*node));
            if (node == NULL) {
                continue;
            }
            copy_fit(node->jmap_id, sizeof(node->jmap_id), r->remotes[i].jmap_id);
        }
        copy_fit(node->ra_id, sizeof(node->ra_id), r->remotes[i].ra_id);
        reset_sup_remote(node);
        node->has_submission = caps_has_urn(r->remotes[i].caps, PAV_SUBMISSION_URN);
        if (keep == NULL) {
            keep = node;
        } else {
            tail->next = node;
        }
        tail = node;
    }
    free_sup_remote_list(old);
    pthread_mutex_lock(&g_sync_mu);
    s->remotes = keep;
    pthread_mutex_unlock(&g_sync_mu);
}

static void finish_sync_ok(PavSup *s) {
    if (s->saw_mailbox && s->activity_id > 0) {
        pav_activity_progress(s->activity_id, 0, -1, NULL, "Mailboxes");
    }
    publish_state(s, "idle", "", 0, PAV_ACT_DONE);
}

static void emit_mailboxes(const char *id) {
    json_t *obj = json_pack("{s:s}", "accountId", id);
    char *text;

    if (obj == NULL) {
        return;
    }
    text = json_dumps(obj, JSON_COMPACT);
    json_decref(obj);
    if (text == NULL) {
        return;
    }
    pav_event_post("mail.mailboxes_changed", text);
    free(text);
    pav_log("sync", PAV_INFO, 1, "mailboxes changed %s", id);
}

static int store_session(PavSup *s, SyncResult *r, const char *state) {
    StateWrite w;
    int64_t now;

    memset(&w, 0, sizeof(w));
    copy_fit(w.account_id, sizeof(w.account_id), s->account_id);
    copy_fit(w.state, sizeof(w.state), state);
    copy_fit(w.session_state, sizeof(w.session_state), r->session_state);
    w.session_json = r->session_json;
    w.remotes = r->remotes;
    w.remote_count = r->remote_count;
    now = pav_now_ms();
    w.now_ms = now > 0 ? now : 0;
    return pav_db_call(write_state_job, &w);
}

static void load_states(const char *ra_id, char *mbx, size_t mbx_n, char *idn, size_t idn_n) {
    PavDbRead *read = NULL;
    sqlite3_stmt *stmt = NULL;
    int rc;

    mbx[0] = '\0';
    idn[0] = '\0';
    if (pav_db_read_begin(&read) != 0) {
        return;
    }
    if (sqlite3_prepare_v2(pav_db_read_handle(read), "SELECT type, state FROM sync_state WHERE ra_id = ?", -1, &stmt,
                           NULL) != SQLITE_OK) {
        pav_db_read_end(read);
        return;
    }
    sqlite3_bind_text(stmt, 1, ra_id, -1, SQLITE_TRANSIENT);
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *type = (const char *)sqlite3_column_text(stmt, 0);
        const char *state = (const char *)sqlite3_column_text(stmt, 1);

        if (type == NULL || state == NULL) {
            continue;
        }
        if (strcmp(type, "Mailbox") == 0) {
            copy_fit(mbx, mbx_n, state);
        } else if (strcmp(type, "Identity") == 0) {
            copy_fit(idn, idn_n, state);
        }
    }
    sqlite3_finalize(stmt);
    pav_db_read_end(read);
    (void)rc;
}

/* Page size for this account. The core capability is the RFC field. A missing
   or larger value stays at PAV_SYNC_MBX_PAGE. */
static int session_get_limit(const char *account_id) {
    PavDbRead *read = NULL;
    sqlite3_stmt *stmt = NULL;
    int limit = PAV_SYNC_MBX_PAGE;
    int rc;

    if (account_id == NULL || account_id[0] == '\0' || pav_db_read_begin(&read) != 0) {
        return limit;
    }
    if (sqlite3_prepare_v2(pav_db_read_handle(read), "SELECT session_json FROM account_state WHERE account_id = ?", -1,
                           &stmt, NULL) != SQLITE_OK) {
        pav_db_read_end(read);
        return limit;
    }
    sqlite3_bind_text(stmt, 1, account_id, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const char *text = (const char *)sqlite3_column_text(stmt, 0);
        json_t *root = text != NULL ? json_loads(text, 0, NULL) : NULL;
        json_t *caps = json_object_get(root, "capabilities");
        json_t *core = json_is_object(caps) ? json_object_get(caps, "urn:ietf:params:jmap:core") : NULL;
        json_t *n = json_is_object(core) ? json_object_get(core, "maxObjectsInGet") : NULL;

        if (json_is_integer(n)) {
            json_int_t v = json_integer_value(n);

            if (v > 0 && v < PAV_SYNC_MBX_PAGE) {
                limit = (int)v;
            }
        }
        json_decref(root);
    }
    sqlite3_finalize(stmt);
    pav_db_read_end(read);
    return limit;
}

static int mailbox_count(const char *ra_id) {
    PavDbRead *read = NULL;
    sqlite3_stmt *stmt = NULL;
    int n = 0;

    if (ra_id == NULL || ra_id[0] == '\0' || pav_db_read_begin(&read) != 0) {
        return 0;
    }
    if (sqlite3_prepare_v2(pav_db_read_handle(read), "SELECT count(*) FROM mailbox WHERE ra_id = ?", -1, &stmt, NULL) !=
        SQLITE_OK) {
        pav_db_read_end(read);
        return 0;
    }
    sqlite3_bind_text(stmt, 1, ra_id, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        n = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    pav_db_read_end(read);
    return n;
}

static SupRemote *find_remote(PavSup *s, const char *ra_id) {
    SupRemote *cur;

    for (cur = s->remotes; cur != NULL; cur = cur->next) {
        if (strcmp(cur->ra_id, ra_id) == 0) {
            return cur;
        }
    }
    return NULL;
}

typedef struct TypeWrite {
    char ra_id[256];
    int clear_mbx;
    int clear_idn;
    int replace_mbx;
    int replace_idn;
    const MbxRow *mailboxes;
    int mailbox_n;
    const IdnRow *identities;
    int identity_n;
    const SyncId *mbx_del;
    int mbx_del_n;
    const SyncId *idn_del;
    int idn_del_n;
    const char *mbx_state;
    const char *idn_state;
    int64_t now_ms;
    int missing;
} TypeWrite;

static int bind_empty_null(sqlite3_stmt *stmt, int index, const char *text) {
    if (text == NULL || text[0] == '\0') {
        return pav_db_bind_null(stmt, index);
    }
    return pav_db_bind_text(stmt, index, text);
}

/* The writer already wraps this job. A nested BEGIN would roll the rows back. */
static int write_type_job(void *ctx, sqlite3 *db) {
    TypeWrite *w = ctx;
    sqlite3_stmt *stmt = NULL;
    int i;

    if (pav_db_prepare(db, "sync.remote_one", "SELECT 1 FROM remote_account WHERE id = ?", &stmt) != 0 ||
        pav_db_bind_text(stmt, 1, w->ra_id) != 0) {
        return -1;
    }
    i = pav_db_step(stmt);
    if (i == SQLITE_DONE) {
        w->missing = 1;
        return 0;
    }
    if (i != SQLITE_ROW) {
        return -1;
    }
    if (w->clear_mbx || w->replace_mbx) {
        if (pav_db_prepare(db, "sync.mbx_clear", "DELETE FROM mailbox WHERE ra_id = ?", &stmt) != 0 ||
            pav_db_bind_text(stmt, 1, w->ra_id) != 0 || step_done(stmt) != 0) {
            return -1;
        }
    }
    if (w->clear_mbx) {
        if (pav_db_prepare(db, "sync.state_del", "DELETE FROM sync_state WHERE ra_id = ? AND type = ?", &stmt) != 0 ||
            pav_db_bind_text(stmt, 1, w->ra_id) != 0 || pav_db_bind_text(stmt, 2, "Mailbox") != 0 ||
            step_done(stmt) != 0) {
            return -1;
        }
    }
    if (w->clear_idn || w->replace_idn) {
        if (pav_db_prepare(db, "sync.idn_clear", "DELETE FROM identity WHERE ra_id = ?", &stmt) != 0 ||
            pav_db_bind_text(stmt, 1, w->ra_id) != 0 || step_done(stmt) != 0) {
            return -1;
        }
    }
    if (w->clear_idn) {
        if (pav_db_prepare(db, "sync.state_del", "DELETE FROM sync_state WHERE ra_id = ? AND type = ?", &stmt) != 0 ||
            pav_db_bind_text(stmt, 1, w->ra_id) != 0 || pav_db_bind_text(stmt, 2, "Identity") != 0 ||
            step_done(stmt) != 0) {
            return -1;
        }
    }
    for (i = 0; i < w->mbx_del_n; i++) {
        if (pav_db_prepare(db, "sync.mbx_del", "DELETE FROM mailbox WHERE ra_id = ? AND id = ?", &stmt) != 0 ||
            pav_db_bind_text(stmt, 1, w->ra_id) != 0 || pav_db_bind_text(stmt, 2, w->mbx_del[i].id) != 0 ||
            step_done(stmt) != 0) {
            return -1;
        }
    }
    for (i = 0; i < w->idn_del_n; i++) {
        if (pav_db_prepare(db, "sync.idn_del", "DELETE FROM identity WHERE ra_id = ? AND id = ?", &stmt) != 0 ||
            pav_db_bind_text(stmt, 1, w->ra_id) != 0 || pav_db_bind_text(stmt, 2, w->idn_del[i].id) != 0 ||
            step_done(stmt) != 0) {
            return -1;
        }
    }
    if (!w->clear_mbx) {
        for (i = 0; i < w->mailbox_n; i++) {
            const MbxRow *row = &w->mailboxes[i];

            if (pav_db_prepare(db, "sync.mbx_upsert",
                               "INSERT INTO mailbox (ra_id, id, parent_id, name, role, sort_order, total, unread, "
                               "total_threads, unread_threads, my_rights, is_subscribed) "
                               "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
                               "ON CONFLICT(ra_id, id) DO UPDATE SET parent_id = excluded.parent_id, "
                               "name = excluded.name, role = excluded.role, sort_order = excluded.sort_order, "
                               "total = excluded.total, unread = excluded.unread, "
                               "total_threads = excluded.total_threads, unread_threads = excluded.unread_threads, "
                               "my_rights = excluded.my_rights, is_subscribed = excluded.is_subscribed",
                               &stmt) != 0 ||
                pav_db_bind_text(stmt, 1, w->ra_id) != 0 || pav_db_bind_text(stmt, 2, row->id) != 0 ||
                bind_empty_null(stmt, 3, row->parent_id) != 0 || pav_db_bind_text(stmt, 4, row->name) != 0 ||
                bind_empty_null(stmt, 5, row->role) != 0 || pav_db_bind_int(stmt, 6, row->sort_order) != 0 ||
                pav_db_bind_int(stmt, 7, row->total) != 0 || pav_db_bind_int(stmt, 8, row->unread) != 0 ||
                pav_db_bind_int(stmt, 9, row->total_threads) != 0 ||
                pav_db_bind_int(stmt, 10, row->unread_threads) != 0 ||
                pav_db_bind_text(stmt, 11, row->my_rights) != 0 || pav_db_bind_int(stmt, 12, row->is_subscribed) != 0 ||
                step_done(stmt) != 0) {
                return -1;
            }
        }
    }
    if (!w->clear_idn) {
        for (i = 0; i < w->identity_n; i++) {
            const IdnRow *row = &w->identities[i];

            if (pav_db_prepare(db, "sync.idn_upsert",
                               "INSERT INTO identity (ra_id, id, name, email, reply_to, bcc, sig_html, sig_text, "
                               "may_delete) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) "
                               "ON CONFLICT(ra_id, id) DO UPDATE SET name = excluded.name, email = excluded.email, "
                               "reply_to = excluded.reply_to, bcc = excluded.bcc, sig_html = excluded.sig_html, "
                               "sig_text = excluded.sig_text, may_delete = excluded.may_delete",
                               &stmt) != 0 ||
                pav_db_bind_text(stmt, 1, w->ra_id) != 0 || pav_db_bind_text(stmt, 2, row->id) != 0 ||
                pav_db_bind_text(stmt, 3, row->name) != 0 || pav_db_bind_text(stmt, 4, row->email) != 0 ||
                pav_db_bind_text(stmt, 5, row->reply_to) != 0 || pav_db_bind_text(stmt, 6, row->bcc) != 0 ||
                pav_db_bind_text(stmt, 7, row->sig_html) != 0 || pav_db_bind_text(stmt, 8, row->sig_text) != 0 ||
                pav_db_bind_int(stmt, 9, row->may_delete) != 0 || step_done(stmt) != 0) {
                return -1;
            }
        }
    }
    if (w->mbx_state != NULL && w->mbx_state[0] != '\0' && !w->clear_mbx) {
        if (pav_db_prepare(db, "sync.state_upsert",
                           "INSERT INTO sync_state (ra_id, type, state, updated) VALUES (?, ?, ?, ?) "
                           "ON CONFLICT(ra_id, type) DO UPDATE SET state = excluded.state, updated = excluded.updated",
                           &stmt) != 0 ||
            pav_db_bind_text(stmt, 1, w->ra_id) != 0 || pav_db_bind_text(stmt, 2, "Mailbox") != 0 ||
            pav_db_bind_text(stmt, 3, w->mbx_state) != 0 || pav_db_bind_int64(stmt, 4, w->now_ms) != 0 ||
            step_done(stmt) != 0) {
            return -1;
        }
    }
    if (w->idn_state != NULL && w->idn_state[0] != '\0' && !w->clear_idn) {
        if (pav_db_prepare(db, "sync.state_upsert",
                           "INSERT INTO sync_state (ra_id, type, state, updated) VALUES (?, ?, ?, ?) "
                           "ON CONFLICT(ra_id, type) DO UPDATE SET state = excluded.state, updated = excluded.updated",
                           &stmt) != 0 ||
            pav_db_bind_text(stmt, 1, w->ra_id) != 0 || pav_db_bind_text(stmt, 2, "Identity") != 0 ||
            pav_db_bind_text(stmt, 3, w->idn_state) != 0 || pav_db_bind_int64(stmt, 4, w->now_ms) != 0 ||
            step_done(stmt) != 0) {
            return -1;
        }
    }
    return 0;
}

static int put_json(json_t *obj, const char *key, json_t *value) {
    if (value == NULL) {
        return -1;
    }
    /* set_new frees value on failure. */
    return json_object_set_new(obj, key, value);
}

static int append_method(json_t *calls, const char *name, json_t *args, const char *cid) {
    json_t *call = json_array();
    json_t *method = json_string(name);
    json_t *id = json_string(cid);

    if (call == NULL || method == NULL || id == NULL || args == NULL) {
        json_decref(call);
        json_decref(method);
        json_decref(id);
        json_decref(args);
        return -1;
    }
    if (json_array_append_new(call, method) != 0) {
        json_decref(id);
        json_decref(args);
        json_decref(call);
        return -1;
    }
    if (json_array_append_new(call, args) != 0) {
        json_decref(id);
        json_decref(call);
        return -1;
    }
    if (json_array_append_new(call, id) != 0) {
        json_decref(call);
        return -1;
    }
    if (json_array_append_new(calls, call) != 0) {
        return -1;
    }
    return 0;
}

static json_t *ids_array(const SyncId *ids, int n) {
    json_t *arr = json_array();
    int i;

    if (arr == NULL) {
        return NULL;
    }
    for (i = 0; i < n; i++) {
        json_t *id = json_string(ids[i].id);

        if (id == NULL || json_array_append_new(arr, id) != 0) {
            json_decref(arr);
            return NULL;
        }
    }
    return arr;
}

static json_t *method_args(const char *account, int mode, const char *since, const SyncId *ids, int n, int pos,
                           int limit) {
    json_t *args = json_object();
    json_t *account_id = json_string(account);

    if (args == NULL || account_id == NULL || put_json(args, "accountId", account_id) != 0) {
        json_decref(args);
        if (account_id != NULL && args == NULL) {
            json_decref(account_id);
        }
        return NULL;
    }
    if (mode == TYPE_QUERY) {
        json_t *position = json_integer(pos > 0 ? pos : 0);
        json_t *lim = json_integer(limit > 0 ? limit : PAV_SYNC_MBX_PAGE);
        json_t *calc = json_true();

        if (position == NULL || lim == NULL || calc == NULL) {
            json_decref(position);
            json_decref(lim);
            json_decref(args);
            return NULL;
        }
        if (put_json(args, "position", position) != 0) {
            json_decref(lim);
            json_decref(args);
            return NULL;
        }
        if (put_json(args, "limit", lim) != 0) {
            json_decref(args);
            return NULL;
        }
        if (put_json(args, "calculateTotal", calc) != 0) {
            json_decref(args);
            return NULL;
        }
    } else if (mode == TYPE_GET) {
        if (put_json(args, "ids", json_null()) != 0) {
            json_decref(args);
            return NULL;
        }
    } else if (mode == TYPE_GET_IDS) {
        json_t *arr = ids_array(ids, n);

        if (arr == NULL || put_json(args, "ids", arr) != 0) {
            json_decref(args);
            return NULL;
        }
    } else if (mode == TYPE_CHANGES) {
        json_t *state = json_string(since != NULL ? since : "");
        json_t *max = json_integer(limit > 0 ? limit : PAV_SYNC_MBX_PAGE);

        if (state == NULL || max == NULL) {
            json_decref(state);
            json_decref(max);
            json_decref(args);
            return NULL;
        }
        if (put_json(args, "sinceState", state) != 0) {
            json_decref(max);
            json_decref(args);
            return NULL;
        }
        if (put_json(args, "maxChanges", max) != 0) {
            json_decref(args);
            return NULL;
        }
    }
    return args;
}

static char *build_type_body(SupRemote *remote, int mbx_mode, int idn_mode, const char *mbx_since,
                             const char *idn_since, const SyncId *mbx_ids, int mbx_n, const SyncId *idn_ids,
                             int idn_n) {
    json_t *root = json_object();
    json_t *using = json_array();
    json_t *calls = json_array();
    json_t *core = json_string("urn:ietf:params:jmap:core");
    json_t *mail = json_string("urn:ietf:params:jmap:mail");
    char *text = NULL;

    if (root == NULL || using == NULL || calls == NULL || core == NULL || mail == NULL) {
        json_decref(root);
        json_decref(using);
        json_decref(calls);
        json_decref(core);
        json_decref(mail);
        return NULL;
    }
    if (json_array_append_new(using, core) != 0) {
        json_decref(mail);
        json_decref(using);
        json_decref(calls);
        json_decref(root);
        return NULL;
    }
    if (json_array_append_new(using, mail) != 0) {
        json_decref(using);
        json_decref(calls);
        json_decref(root);
        return NULL;
    }
    /* Stalwart answers Identity/get with unknownMethod unless submission is
       in using. Mailbox/get does not need it. */
    if (idn_mode != TYPE_SKIP) {
        json_t *submission = json_string(PAV_SUBMISSION_URN);

        if (submission == NULL || json_array_append_new(using, submission) != 0) {
            json_decref(using);
            json_decref(calls);
            json_decref(root);
            return NULL;
        }
    }
    if (put_json(root, "using", using) != 0) {
        json_decref(calls);
        json_decref(root);
        return NULL;
    }
    if (idn_mode != TYPE_SKIP) {
        const char *cid = idn_mode == TYPE_GET_IDS ? "if" : "i";
        const char *name = idn_mode == TYPE_CHANGES ? "Identity/changes" : "Identity/get";
        json_t *args = method_args(remote->jmap_id, idn_mode, idn_since, idn_ids, idn_n, 0, PAV_SYNC_MBX_PAGE);

        if (append_method(calls, name, args, cid) != 0) {
            json_decref(calls);
            json_decref(root);
            return NULL;
        }
    }
    if (mbx_mode != TYPE_SKIP) {
        const char *cid = mbx_mode == TYPE_QUERY ? "mq" : (mbx_mode == TYPE_GET_IDS ? "mf" : "m");
        const char *name = mbx_mode == TYPE_QUERY ? "Mailbox/query"
                           : (mbx_mode == TYPE_CHANGES ? "Mailbox/changes" : "Mailbox/get");
        int lim = remote->mbx_probe ? 1 : (remote->mbx_limit > 0 ? remote->mbx_limit : PAV_SYNC_MBX_PAGE);
        json_t *args = method_args(remote->jmap_id, mbx_mode, mbx_since, mbx_ids, mbx_n, remote->mbx_pos, lim);

        if (append_method(calls, name, args, cid) != 0) {
            json_decref(calls);
            json_decref(root);
            return NULL;
        }
    }
    if (put_json(root, "methodCalls", calls) != 0 || put_json(root, "RequestId", json_string("pav-m3")) != 0) {
        json_decref(root);
        return NULL;
    }
    text = json_dumps(root, JSON_COMPACT);
    json_decref(root);
    return text;
}

static SyncReq *make_type_req(PavSup *s, SupRemote *remote, int mbx_mode, int idn_mode, char *body) {
    SyncReq *req = calloc(1, sizeof(*req));

    if (req == NULL) {
        free(body);
        return NULL;
    }
    atomic_init(&req->refs, 2);
    req->kind = SYNC_KIND_TYPES;
    req->generation = s->generation;
    req->mbx_mode = mbx_mode;
    req->idn_mode = idn_mode;
    req->body = body;
    copy_fit(req->account_id, sizeof(req->account_id), s->account_id);
    copy_fit(req->url, sizeof(req->url), s->api_url);
    copy_fit(req->ca_file, sizeof(req->ca_file), s->ca_file);
    copy_fit(req->keyring_user, sizeof(req->keyring_user), s->keyring_user);
    copy_fit(req->username, sizeof(req->username), s->username);
    copy_fit(req->auth_method, sizeof(req->auth_method), s->auth);
    copy_fit(req->ra_id, sizeof(req->ra_id), remote->ra_id);
    copy_fit(req->jmap_id, sizeof(req->jmap_id), remote->jmap_id);
    req->req.method = "POST";
    req->req.url = req->url;
    req->req.account_id = req->account_id;
    req->req.keyring_user = req->keyring_user;
    req->req.username = req->username;
    req->req.auth_method = req->auth_method;
    req->req.ca_file = req->ca_file[0] != '\0' ? req->ca_file : NULL;
    req->req.body = req->body;
    req->req.body_len = body != NULL ? strlen(body) : 0;
    req->req.headers = json_pack("{s:s}", "Content-Type", "application/json");
    req->req.on_done = sync_on_done;
    req->req.user = req;
    if (req->req.headers == NULL) {
        free(req->body);
        free(req);
        return NULL;
    }
    return req;
}

/* 1 ok, 0 too many changes, -1 too many mailbox pages. */
static int pages_ok(SupRemote *remote, int mbx_mode, int idn_mode) {
    int code = 1;

    pthread_mutex_lock(&g_sync_mu);
    if (mbx_mode == TYPE_QUERY) {
        remote->mbx_list_pages++;
        if (remote->mbx_list_pages > PAV_SYNC_LIST_MAX) {
            code = -1;
        }
    }
    if (code == 1 && mbx_mode == TYPE_CHANGES) {
        remote->mbx_pages++;
        if (remote->mbx_pages > PAV_SYNC_PAGE_MAX) {
            code = 0;
        }
    }
    if (code == 1 && idn_mode == TYPE_CHANGES) {
        remote->idn_pages++;
        if (remote->idn_pages > PAV_SYNC_PAGE_MAX) {
            code = 0;
        }
    }
    pthread_mutex_unlock(&g_sync_mu);
    return code;
}

/* One POST for this remote. Does not increment the session attempt counter. */
static void start_type_request(PavSup *s, SupRemote *remote, int mbx_mode, int idn_mode, const SyncId *mbx_ids,
                               int mbx_n, const SyncId *idn_ids, int idn_n) {
    char mbx_since[128];
    char idn_since[128];
    char *body;
    SyncReq *req;

    if (remote == NULL) {
        finish_sync_ok(s);
        return;
    }
    if (mbx_mode == TYPE_GET_IDS && mbx_n <= 0 && !remote->mbx_paging) {
        mbx_mode = TYPE_SKIP;
    }
    if (idn_mode == TYPE_GET_IDS && idn_n <= 0) {
        idn_mode = TYPE_SKIP;
    }
    if (idn_mode != TYPE_SKIP && !remote->has_submission) {
        idn_mode = TYPE_SKIP;
    }
    mbx_since[0] = '\0';
    idn_since[0] = '\0';
    if (mbx_mode == TYPE_CHANGES || idn_mode == TYPE_CHANGES) {
        load_states(remote->ra_id, mbx_since, sizeof(mbx_since), idn_since, sizeof(idn_since));
        if (mbx_mode == TYPE_CHANGES && mbx_since[0] == '\0') {
            mbx_mode = TYPE_QUERY;
            begin_mbx_walk(remote);
        }
        if (idn_mode == TYPE_CHANGES && idn_since[0] == '\0') {
            idn_mode = TYPE_GET;
        }
    }
    if (mbx_mode == TYPE_SKIP && idn_mode == TYPE_SKIP) {
        begin_remote(s, remote->next);
        return;
    }
    if (mbx_mode == TYPE_QUERY && remote->mbx_limit <= 0) {
        remote->mbx_limit = session_get_limit(s->account_id);
    }
    if (mbx_mode == TYPE_CHANGES && remote->mbx_limit <= 0) {
        remote->mbx_limit = session_get_limit(s->account_id);
    }
    {
        int pages = pages_ok(remote, mbx_mode, idn_mode);

        if (pages != 1) {
            publish_state(s, "error:server", pages < 0 ? "too many mailboxes" : "too many changes", 0, PAV_ACT_FAILED);
            return;
        }
    }
    body = build_type_body(remote, mbx_mode, idn_mode, mbx_since, idn_since, mbx_ids, mbx_n, idn_ids, idn_n);
    if (body == NULL) {
        publish_state(s, "offline", "offline", 1, PAV_ACT_FAILED);
        return;
    }
    req = make_type_req(s, remote, mbx_mode, idn_mode, body);
    if (req == NULL) {
        publish_state(s, "offline", "offline", 1, PAV_ACT_FAILED);
        return;
    }
    pthread_mutex_lock(&g_sync_mu);
    if (g_sync_stop || !s->enabled) {
        pthread_mutex_unlock(&g_sync_mu);
        sync_req_release(req);
        sync_req_release(req);
        return;
    }
    drop_inflight(s);
    req->generation = s->generation;
    s->inflight = req;
    pthread_mutex_unlock(&g_sync_mu);
    if (idn_mode != TYPE_SKIP && s->activity_id > 0) {
        pav_activity_progress(s->activity_id, 0, -1, NULL, "Identities");
    }
    if (mbx_mode != TYPE_SKIP) {
        s->saw_mailbox = 1;
        if (idn_mode == TYPE_SKIP && s->activity_id > 0) {
            pav_activity_progress(s->activity_id, 0, -1, NULL, "Mailboxes");
        }
    }
    if (pav_net_request(&req->req) != 0) {
        fail_submit(s, req);
    }
}

static void begin_remote(PavSup *s, SupRemote *remote) {
    char mbx_state[128];
    char idn_state[128];
    int mbx_mode;
    int idn_mode;

    if (remote == NULL) {
        finish_sync_ok(s);
        return;
    }
    load_states(remote->ra_id, mbx_state, sizeof(mbx_state), idn_state, sizeof(idn_state));
    if (remote->force_mbx_get || mbx_state[0] == '\0') {
        mbx_mode = TYPE_QUERY;
        remote->force_mbx_get = 0;
        begin_mbx_walk(remote);
    } else {
        int stored;

        if (remote->mbx_limit <= 0) {
            remote->mbx_limit = session_get_limit(s->account_id);
        }
        stored = mailbox_count(remote->ra_id);
        /* A tree saved by the old ids-null get stops at the cap and still has
           a state, so changes never fetch the mailboxes that were left out. */
        if (remote->mbx_limit > 0 && stored >= remote->mbx_limit) {
            mbx_mode = TYPE_QUERY;
            remote->mbx_probe = 1;
            remote->mbx_stored = stored;
            remote->mbx_pos = 0;
            remote->mbx_paging = 0;
        } else {
            mbx_mode = TYPE_CHANGES;
        }
    }
    if (remote->force_idn_get || idn_state[0] == '\0') {
        idn_mode = TYPE_GET;
        remote->force_idn_get = 0;
    } else {
        idn_mode = TYPE_CHANGES;
    }
    start_type_request(s, remote, mbx_mode, idn_mode, NULL, 0, NULL, 0);
}

static void apply_session_ok(PavSup *s, SyncResult *r) {
    int https;

    if (r->remote_count <= 0) {
        publish_session(s, r);
        return;
    }
    https = strncmp(r->api_url, "https://", 8) == 0;
    pthread_mutex_lock(&g_sync_mu);
    copy_fit(s->state, sizeof(s->state), "syncing");
    s->auto_retry = 0;
    s->saw_mailbox = 0;
    if (https) {
        copy_fit(s->api_url, sizeof(s->api_url), r->api_url);
    }
    pthread_mutex_unlock(&g_sync_mu);
    if (store_session(s, r, "syncing") != 0) {
        pav_log("sync", PAV_ERROR, 1, "cannot store session for %s", s->account_id);
        publish_state(s, "error:server", "cannot store session", 0, PAV_ACT_FAILED);
        return;
    }
    if (!https) {
        publish_state(s, "error:server", "invalid session", 0, PAV_ACT_FAILED);
        return;
    }
    merge_sup_remotes(s, r);
    if (s->remotes == NULL) {
        finish_sync_ok(s);
        return;
    }
    if (s->activity_id > 0) {
        pav_activity_progress(s->activity_id, 0, -1, NULL, "Identities");
    }
    emit_state(s->account_id, "syncing", "Identities");
    begin_remote(s, s->remotes);
}

static int fail_type_transport(PavSup *s, int auth_rc, int status, const char *detail) {
    if (auth_rc == PAV_SECRET_LOCKED) {
        publish_state(s, "locked", "keyring is locked", 1, PAV_ACT_FAILED);
        return 1;
    }
    if (auth_rc == PAV_SECRET_MISSING || auth_rc == PAV_SECRET_FAILED || status == 401) {
        publish_state(s, "error:auth", "authentication failed", 0, PAV_ACT_FAILED);
        return 1;
    }
    if (status == 0) {
        publish_state(s, "offline", "offline", 1, PAV_ACT_FAILED);
        return 1;
    }
    if (status >= 500) {
        publish_state(s, "error:server", "server error", 1, PAV_ACT_FAILED);
        return 1;
    }
    if (status != 200) {
        publish_state(s, "error:server", detail != NULL && detail[0] != '\0' ? detail : "request rejected", 0,
                      PAV_ACT_FAILED);
        return 1;
    }
    return 0;
}

static void apply_one_type(TypeResult *t, SupRemote *remote, int mailbox, TypeWrite *w, int *next_mode, int *emit,
                           const SyncId **fetch, int *fetch_n) {
    int mode = mailbox ? t->mbx_mode : t->idn_mode;
    int resync = mailbox ? t->mbx_resync : t->idn_resync;
    int *pending_more = mailbox ? &remote->pending_mbx_more : &remote->pending_idn_more;
    char *pending_state = mailbox ? remote->pending_mbx_state : remote->pending_idn_state;
    size_t pending_n = sizeof(remote->pending_mbx_state);
    const char *state = mailbox ? t->mbx_state : t->idn_state;
    int more = mailbox ? t->mbx_more : t->idn_more;
    int destroyed_n = mailbox ? t->mbx_destroyed_n : t->idn_destroyed_n;
    int row_n = mailbox ? t->mailbox_n : t->identity_n;
    int fetch_count = mailbox ? t->mbx_fetch_n : t->idn_fetch_n;

    /* A cannotCalculateChanges resync already chose the next mode. Leave it. */
    if (resync) {
        return;
    }
    *next_mode = TYPE_SKIP;
    if (mode == TYPE_SKIP) {
        return;
    }
    if (mode == TYPE_QUERY && mailbox && remote->mbx_probe) {
        int total = t->mbx_total;
        int stored = remote->mbx_stored;

        remote->mbx_probe = 0;
        if (total > stored) {
            begin_mbx_walk(remote);
            *next_mode = TYPE_QUERY;
            return;
        }
        *next_mode = TYPE_CHANGES;
        return;
    }
    if (mode == TYPE_QUERY && mailbox) {
        int n = fetch_count;

        if (remote->mbx_limit > 0 && n > remote->mbx_limit) {
            n = remote->mbx_limit;
        }
        remote->mbx_last_n = n;
        if (t->mbx_total >= 0) {
            remote->mbx_total = t->mbx_total;
        }
        remote->mbx_paging = 1;
        *fetch = t->mbx_fetch;
        *fetch_n = n;
        *next_mode = TYPE_GET_IDS;
        return;
    }
    if (mode == TYPE_CHANGES && fetch_count > 0) {
        if (mailbox) {
            w->mbx_del = t->mbx_destroyed;
            w->mbx_del_n = destroyed_n;
            if (destroyed_n > 0) {
                *emit = 1;
            }
        } else {
            w->idn_del = t->idn_destroyed;
            w->idn_del_n = destroyed_n;
        }
        *fetch = mailbox ? t->mbx_fetch : t->idn_fetch;
        *fetch_n = fetch_count;
        copy_fit(pending_state, pending_n, state);
        *pending_more = more;
        *next_mode = TYPE_GET_IDS;
        return;
    }
    if (mode == TYPE_GET) {
        if (mailbox) {
            w->replace_mbx = 1;
            w->mailboxes = t->mailboxes;
            w->mailbox_n = row_n;
            w->mbx_state = state;
            *emit = 1;
        } else {
            w->replace_idn = 1;
            w->identities = t->identities;
            w->identity_n = row_n;
            w->idn_state = state;
        }
        return;
    }
    if (mode == TYPE_GET_IDS && mailbox && remote->mbx_paging) {
        int keep;

        if (state[0] != '\0' && remote->mbx_anchor[0] != '\0' && strcmp(remote->mbx_anchor, state) != 0) {
            if (remote->mbx_restarts < 1) {
                begin_mbx_walk(remote);
                remote->mbx_restarts = 1;
                w->clear_mbx = 1;
                *next_mode = TYPE_QUERY;
                return;
            }
            w->mailboxes = t->mailboxes;
            w->mailbox_n = row_n;
            w->mbx_state = state;
            remote->mbx_paging = 0;
            *emit = 1;
            return;
        }
        if (state[0] != '\0' && remote->mbx_anchor[0] == '\0') {
            copy_fit(remote->mbx_anchor, sizeof(remote->mbx_anchor), state);
        }
        if (remote->mbx_pos == 0) {
            w->replace_mbx = 1;
        }
        w->mailboxes = t->mailboxes;
        w->mailbox_n = row_n;
        remote->mbx_pos += remote->mbx_last_n;
        keep = remote->mbx_last_n > 0 &&
               ((remote->mbx_total >= 0 && remote->mbx_pos < remote->mbx_total) ||
                (remote->mbx_total < 0 && remote->mbx_limit > 0 && remote->mbx_last_n >= remote->mbx_limit));
        if (keep) {
            *next_mode = TYPE_QUERY;
            return;
        }
        w->mbx_state = state[0] != '\0' ? state : remote->mbx_anchor;
        remote->mbx_paging = 0;
        *emit = 1;
        return;
    }
    if (mode == TYPE_GET_IDS) {
        char saved[128];
        int again = *pending_more;
        const char *use = state;

        copy_fit(saved, sizeof(saved), pending_state);
        *pending_more = 0;
        pending_state[0] = '\0';
        if (saved[0] != '\0') {
            if (mailbox) {
                copy_fit(t->mbx_state, sizeof(t->mbx_state), saved);
                use = t->mbx_state;
            } else {
                copy_fit(t->idn_state, sizeof(t->idn_state), saved);
                use = t->idn_state;
            }
        }
        if (mailbox) {
            w->mailboxes = t->mailboxes;
            w->mailbox_n = row_n;
            w->mbx_state = use;
            if (row_n > 0) {
                *emit = 1;
            }
        } else {
            w->identities = t->identities;
            w->identity_n = row_n;
            w->idn_state = use;
        }
        if (again) {
            *next_mode = TYPE_CHANGES;
        }
        return;
    }
    if (mailbox) {
        w->mbx_del = t->mbx_destroyed;
        w->mbx_del_n = destroyed_n;
        w->mbx_state = state;
        if (destroyed_n > 0) {
            *emit = 1;
        }
    } else {
        w->idn_del = t->idn_destroyed;
        w->idn_del_n = destroyed_n;
        w->idn_state = state;
    }
    if (more) {
        *next_mode = TYPE_CHANGES;
    }
}

static void apply_types(PavSup *s, TypeResult *t) {
    SupRemote *remote;
    TypeWrite w;
    int64_t now;
    int next_mbx = TYPE_SKIP;
    int next_idn = TYPE_SKIP;
    int emit = 0;
    int fatal_mbx;
    int fatal_idn;
    const SyncId *mbx_fetch = NULL;
    const SyncId *idn_fetch = NULL;
    int mbx_fetch_n = 0;
    int idn_fetch_n = 0;

    remote = find_remote(s, t->ra_id);
    if (remote == NULL) {
        publish_state(s, "error:server", "request rejected", 0, PAV_ACT_FAILED);
        return;
    }
    if (fail_type_transport(s, t->auth_rc, t->status, t->detail)) {
        return;
    }
    if (t->method_error) {
        publish_state(s, "error:server", t->detail[0] != '\0' ? t->detail : "request rejected", 0, PAV_ACT_FAILED);
        return;
    }
    fatal_mbx = t->mbx_resync && remote->mbx_resyncs >= 1;
    fatal_idn = t->idn_resync && remote->idn_resyncs >= 1;
    memset(&w, 0, sizeof(w));
    copy_fit(w.ra_id, sizeof(w.ra_id), remote->ra_id);
    now = pav_now_ms();
    w.now_ms = now > 0 ? now : 0;
    if (t->mbx_resync && !fatal_mbx) {
        remote->mbx_resyncs++;
        w.clear_mbx = 1;
        remote->pending_mbx_more = 0;
        remote->pending_mbx_state[0] = '\0';
        remote->force_mbx_get = 1;
        begin_mbx_walk(remote);
        next_mbx = TYPE_QUERY;
    }
    if (t->idn_resync && !fatal_idn) {
        remote->idn_resyncs++;
        w.clear_idn = 1;
        remote->pending_idn_more = 0;
        remote->pending_idn_state[0] = '\0';
        remote->force_idn_get = 1;
        next_idn = TYPE_GET;
    }
    apply_one_type(t, remote, 1, &w, &next_mbx, &emit, &mbx_fetch, &mbx_fetch_n);
    apply_one_type(t, remote, 0, &w, &next_idn, &emit, &idn_fetch, &idn_fetch_n);
    if (pav_db_call(write_type_job, &w) != 0) {
        pav_log("sync", PAV_ERROR, 1, "cannot store mailboxes for %s", s->account_id);
        publish_state(s, "error:server", "cannot store mailboxes", 0, PAV_ACT_FAILED);
        return;
    }
    if (w.missing) {
        publish_state(s, "error:server", "request rejected", 0, PAV_ACT_FAILED);
        return;
    }
    if (emit) {
        emit_mailboxes(s->account_id);
    }
    if (fatal_mbx || fatal_idn) {
        publish_state(s, "error:server", "cannot calculate changes", 0, PAV_ACT_FAILED);
        return;
    }
    if (next_mbx == TYPE_SKIP && next_idn == TYPE_SKIP) {
        begin_remote(s, remote->next);
        return;
    }
    start_type_request(s, remote, next_mbx, next_idn, mbx_fetch, mbx_fetch_n, idn_fetch, idn_fetch_n);
}

static void start_delta(PavSup *s) {
    int64_t replaced = 0;
    SupRemote *remote;
    SupRemote *cur;

    pthread_mutex_lock(&g_sync_mu);
    if (g_sync_stop || !s->enabled || s->inflight != NULL || strcmp(s->state, "idle") != 0 || s->api_url[0] == '\0' ||
        strncmp(s->api_url, "https://", 8) != 0 || s->remotes == NULL) {
        pthread_mutex_unlock(&g_sync_mu);
        return;
    }
    s->generation++;
    drop_inflight(s);
    replaced = s->activity_id;
    s->activity_id = 0;
    s->saw_mailbox = 0;
    s->auto_retry = 0;
    copy_fit(s->state, sizeof(s->state), "syncing");
    for (cur = s->remotes; cur != NULL; cur = cur->next) {
        reset_sup_remote(cur);
    }
    remote = s->remotes;
    pthread_mutex_unlock(&g_sync_mu);
    if (replaced > 0) {
        pav_activity_end(replaced, PAV_ACT_CANCELLED, NULL);
    }
    begin_activity(s);
    if (s->activity_id > 0) {
        pav_activity_progress(s->activity_id, 0, -1, NULL, "Identities");
    }
    publish_state(s, "syncing", "Identities", 0, 0);
    begin_remote(s, remote);
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
    free_sup_remote_list(s->remotes);
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
        free_sup_remote_list(s->remotes);
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
        int retry = 0;
        int delta = 0;

        pthread_mutex_lock(&g_sync_mu);
        retry = s->enabled && s->auto_retry && s->inflight == NULL && !g_sync_stop;
        delta = !retry && s->enabled && s->inflight == NULL && !g_sync_stop && strcmp(s->state, "idle") == 0 &&
                s->api_url[0] != '\0' && s->remotes != NULL;
        pthread_mutex_unlock(&g_sync_mu);
        if (retry) {
            start_connect(s, 0);
        } else if (delta) {
            start_delta(s);
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

static int list_account_arg(json_t *args, const char **account_id, PavError *err) {
    const char *key;
    json_t *value;

    *account_id = NULL;
    if (!json_is_object(args)) {
        pav_error_set(err, "bad_args", "arguments must be an object", 0);
        return -1;
    }
    json_object_foreach(args, key, value) {
        if (strcmp(key, "accountId") == 0) {
            if (!json_is_string(value) || json_string_value(value)[0] == '\0') {
                pav_error_set(err, "bad_args", "accountId must be a non-empty string", 0);
                return -1;
            }
            *account_id = json_string_value(value);
        } else {
            char buf[128];

            snprintf(buf, sizeof(buf), "unknown argument %s", key);
            pav_error_set(err, "bad_args", buf, 0);
            return -1;
        }
    }
    return 0;
}

static int list_ready(PavError *err) {
    PavConfigLoad *load = pav_core_load();

    if (load == NULL || load->profile == NULL || strcmp(load->state, "ready") != 0) {
        pav_error_set(err, "not_ready", "profile is not ready", 0);
        return 0;
    }
    return 1;
}

static int put_text(json_t *obj, const char *key, const char *value) {
    json_t *v = json_string(value != NULL ? value : "");

    if (v == NULL || json_object_set_new(obj, key, v) != 0) {
        return -1;
    }
    return 0;
}

static int put_int_field(json_t *obj, const char *key, int value) {
    json_t *v = json_integer(value);

    if (v == NULL || json_object_set_new(obj, key, v) != 0) {
        return -1;
    }
    return 0;
}

static json_t *parse_or_null(const char *text) {
    json_t *parsed;

    if (text == NULL || text[0] == '\0') {
        return json_null();
    }
    parsed = json_loads(text, 0, NULL);
    return parsed != NULL ? parsed : json_null();
}

int pav_cmd_identity_list(json_t *args, json_t **data, PavError *err) {
    const char *filter = NULL;
    PavDbRead *read = NULL;
    sqlite3_stmt *stmt = NULL;
    json_t *rows;
    json_t *root;
    int rc;

    if (data == NULL) {
        pav_error_set(err, "internal", "missing data", 0);
        return -1;
    }
    *data = NULL;
    if (list_account_arg(args, &filter, err) != 0 || !list_ready(err)) {
        return -1;
    }
    rows = json_array();
    root = json_object();
    if (rows == NULL || root == NULL || put_json(root, "identities", rows) != 0) {
        json_decref(rows);
        json_decref(root);
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    if (pav_db_read_begin(&read) != 0) {
        json_decref(root);
        pav_error_set(err, "internal", "cannot read identities", 0);
        return -1;
    }
    rc = sqlite3_prepare_v2(pav_db_read_handle(read),
                            "SELECT r.account_id, i.ra_id, i.id, i.name, i.email, i.reply_to, i.bcc, i.may_delete "
                            "FROM identity i JOIN remote_account r ON r.id = i.ra_id "
                            "ORDER BY r.account_id, i.email, i.id",
                            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pav_db_read_end(read);
        json_decref(root);
        pav_error_set(err, "internal", "cannot read identities", 0);
        return -1;
    }
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *account_id = (const char *)sqlite3_column_text(stmt, 0);
        const char *ra_id = (const char *)sqlite3_column_text(stmt, 1);
        const char *id = (const char *)sqlite3_column_text(stmt, 2);
        const char *name = (const char *)sqlite3_column_text(stmt, 3);
        const char *email = (const char *)sqlite3_column_text(stmt, 4);
        const char *reply = sqlite3_column_type(stmt, 5) == SQLITE_NULL ? NULL : (const char *)sqlite3_column_text(stmt, 5);
        const char *bcc = sqlite3_column_type(stmt, 6) == SQLITE_NULL ? NULL : (const char *)sqlite3_column_text(stmt, 6);
        json_t *obj;
        json_t *reply_json;
        json_t *bcc_json;

        if (account_id == NULL || id == NULL) {
            continue;
        }
        if (filter != NULL && strcmp(filter, account_id) != 0) {
            continue;
        }
        obj = json_object();
        reply_json = parse_or_null(reply);
        bcc_json = parse_or_null(bcc);
        if (obj == NULL || reply_json == NULL || bcc_json == NULL || put_text(obj, "accountId", account_id) != 0 ||
            put_text(obj, "raId", ra_id != NULL ? ra_id : "") != 0 || put_text(obj, "id", id) != 0 ||
            put_text(obj, "name", name != NULL ? name : "") != 0 ||
            put_text(obj, "email", email != NULL ? email : "") != 0 || put_json(obj, "replyTo", reply_json) != 0 ||
            put_json(obj, "bcc", bcc_json) != 0 ||
            put_json(obj, "mayDelete", json_boolean(sqlite3_column_int(stmt, 7) ? 1 : 0)) != 0 ||
            json_array_append_new(rows, obj) != 0) {
            json_decref(reply_json);
            json_decref(bcc_json);
            json_decref(obj);
            sqlite3_finalize(stmt);
            pav_db_read_end(read);
            json_decref(root);
            pav_error_set(err, "internal", "out of memory", 0);
            return -1;
        }
    }
    sqlite3_finalize(stmt);
    pav_db_read_end(read);
    if (rc != SQLITE_DONE) {
        json_decref(root);
        pav_error_set(err, "internal", "cannot read identities", 0);
        return -1;
    }
    *data = root;
    return 0;
}

typedef struct MbxLoad {
    char account_id[64];
    char ra_id[256];
    char id[128];
    char parent_id[128];
    char name[256];
    char role[64];
    int sort_order;
    int total;
    int unread;
} MbxLoad;

typedef struct TreeNode {
    char id[640];
    char parent_id[640];
    char kind[16];
    char label[256];
    char role[64];
    int role_set;
    int order;
    int unread;
    int total;
    int has_children;
    char swatch[64];
    int swatch_set;
    int sort_order;
    char sort_name[256];
    char sort_id[128];
    char raw_parent[128];
    char ra_id[256];
    char account_id[64];
} TreeNode;

static int add_node(TreeNode **nodes, int *n, const TreeNode *src) {
    TreeNode *grown = realloc(*nodes, (size_t)(*n + 1) * sizeof(*grown));

    if (grown == NULL) {
        return -1;
    }
    *nodes = grown;
    (*nodes)[*n] = *src;
    (*n)++;
    return 0;
}

static int role_rank(const char *role) {
    static const char *names[] = {"inbox", "drafts", "sent", "archive", "junk", "trash"};
    int i;

    if (role == NULL || role[0] == '\0') {
        return 7;
    }
    for (i = 0; i < 6; i++) {
        if (strcmp(role, names[i]) == 0) {
            return i;
        }
    }
    return 6;
}

static int mailbox_before(const TreeNode *a, const TreeNode *b) {
    int ra = role_rank(a->role_set ? a->role : "");
    int rb = role_rank(b->role_set ? b->role : "");
    int cmp;

    if (a->sort_order != b->sort_order) {
        return a->sort_order < b->sort_order;
    }
    if (ra != rb) {
        return ra < rb;
    }
    if (ra == 6) {
        cmp = strcmp(a->role, b->role);
        if (cmp != 0) {
            return cmp < 0;
        }
    }
    cmp = strcmp(a->sort_name, b->sort_name);
    if (cmp != 0) {
        return cmp < 0;
    }
    cmp = strcmp(a->sort_id, b->sort_id);
    if (cmp != 0) {
        return cmp < 0;
    }
    return strcmp(a->id, b->id) < 0;
}

static int order_siblings(TreeNode *nodes, int n) {
    int *idx = malloc((size_t)n * sizeof(*idx));
    int i;
    int j;

    if (n == 0) {
        free(idx);
        return 0;
    }
    if (idx == NULL) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        int seen = 0;
        int count = 0;

        if (strcmp(nodes[i].kind, "mailbox") != 0) {
            continue;
        }
        for (j = 0; j < i; j++) {
            if (strcmp(nodes[j].kind, "mailbox") == 0 && strcmp(nodes[j].parent_id, nodes[i].parent_id) == 0) {
                seen = 1;
                break;
            }
        }
        if (seen) {
            continue;
        }
        for (j = 0; j < n; j++) {
            int at;

            if (strcmp(nodes[j].kind, "mailbox") != 0 || strcmp(nodes[j].parent_id, nodes[i].parent_id) != 0) {
                continue;
            }
            at = count;
            while (at > 0 && mailbox_before(&nodes[j], &nodes[idx[at - 1]])) {
                idx[at] = idx[at - 1];
                at--;
            }
            idx[at] = j;
            count++;
        }
        for (j = 0; j < count; j++) {
            nodes[idx[j]].order = j;
        }
    }
    free(idx);
    return 0;
}

static int account_listed(const TreeNode *nodes, int n, const char *account_id) {
    int i;

    for (i = 0; i < n; i++) {
        if (strcmp(nodes[i].kind, "account") == 0 && strcmp(nodes[i].id, account_id) == 0) {
            return 1;
        }
    }
    return 0;
}

static json_t *node_json(const TreeNode *node) {
    json_t *obj = json_object();

    if (obj == NULL) {
        return NULL;
    }
    if (put_text(obj, "id", node->id) != 0 ||
        (node->parent_id[0] == '\0' ? put_json(obj, "parentId", json_null()) : put_text(obj, "parentId", node->parent_id)) !=
            0 ||
        put_text(obj, "kind", node->kind) != 0 || put_text(obj, "label", node->label) != 0 ||
        (node->role_set ? put_text(obj, "role", node->role) : put_json(obj, "role", json_null())) != 0 ||
        put_int_field(obj, "order", node->order) != 0 || put_int_field(obj, "unread", node->unread) != 0 ||
        put_int_field(obj, "total", node->total) != 0 ||
        put_json(obj, "hasChildren", json_boolean(node->has_children ? 1 : 0)) != 0 ||
        (node->swatch_set ? put_text(obj, "swatch", node->swatch) : put_json(obj, "swatch", json_null())) != 0) {
        json_decref(obj);
        return NULL;
    }
    return obj;
}

int pav_cmd_mailbox_list(json_t *args, json_t **data, PavError *err) {
    const char *filter = NULL;
    PavConfigLoad *load;
    json_t *accounts;
    PavDbRead *read = NULL;
    sqlite3_stmt *stmt = NULL;
    MbxLoad *loaded = NULL;
    TreeNode *nodes = NULL;
    json_t *rows = NULL;
    json_t *root = NULL;
    int loaded_n = 0;
    int node_n = 0;
    int i;
    int j;
    int rc;
    size_t ai;

    if (data == NULL) {
        pav_error_set(err, "internal", "missing data", 0);
        return -1;
    }
    *data = NULL;
    if (list_account_arg(args, &filter, err) != 0 || !list_ready(err)) {
        return -1;
    }
    load = pav_core_load();
    accounts = json_object_get((json_t *)load->profile->doc, "accounts");
    if (json_is_array(accounts)) {
        for (ai = 0; ai < json_array_size(accounts); ai++) {
            json_t *acct = json_array_get(accounts, ai);
            const char *id = json_string_value(json_object_get(acct, "id"));
            const char *name = json_string_value(json_object_get(acct, "name"));
            const char *color = json_string_value(json_object_get(acct, "color"));
            TreeNode node;

            if (id == NULL || id[0] == '\0') {
                continue;
            }
            if (filter != NULL && strcmp(filter, id) != 0) {
                continue;
            }
            memset(&node, 0, sizeof(node));
            if (!copy_fit(node.id, sizeof(node.id), id) || !copy_fit(node.kind, sizeof(node.kind), "account") ||
                !copy_fit(node.label, sizeof(node.label), name != NULL && name[0] != '\0' ? name : id) ||
                !copy_fit(node.account_id, sizeof(node.account_id), id)) {
                continue;
            }
            node.order = (int)ai;
            if (color != NULL && color[0] != '\0' && copy_fit(node.swatch, sizeof(node.swatch), color)) {
                node.swatch_set = 1;
            }
            if (add_node(&nodes, &node_n, &node) != 0) {
                free(nodes);
                pav_error_set(err, "internal", "out of memory", 0);
                return -1;
            }
        }
    }
    if (pav_db_read_begin(&read) != 0) {
        free(nodes);
        pav_error_set(err, "internal", "cannot read mailboxes", 0);
        return -1;
    }
    rc = sqlite3_prepare_v2(pav_db_read_handle(read),
                            "SELECT r.account_id, m.ra_id, m.id, IFNULL(m.parent_id, ''), IFNULL(m.name, ''), "
                            "IFNULL(m.role, ''), IFNULL(m.sort_order, 0), IFNULL(m.total, 0), IFNULL(m.unread, 0) "
                            "FROM mailbox m JOIN remote_account r ON r.id = m.ra_id",
                            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        pav_db_read_end(read);
        free(nodes);
        pav_error_set(err, "internal", "cannot read mailboxes", 0);
        return -1;
    }
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        MbxLoad row;
        MbxLoad *grown;
        const char *account_id = (const char *)sqlite3_column_text(stmt, 0);

        memset(&row, 0, sizeof(row));
        if (account_id == NULL || !account_listed(nodes, node_n, account_id)) {
            continue;
        }
        if (!copy_fit(row.account_id, sizeof(row.account_id), account_id) ||
            !copy_fit(row.ra_id, sizeof(row.ra_id), (const char *)sqlite3_column_text(stmt, 1)) ||
            !copy_fit(row.id, sizeof(row.id), (const char *)sqlite3_column_text(stmt, 2)) || row.id[0] == '\0') {
            continue;
        }
        copy_fit(row.parent_id, sizeof(row.parent_id), (const char *)sqlite3_column_text(stmt, 3));
        copy_fit(row.name, sizeof(row.name), (const char *)sqlite3_column_text(stmt, 4));
        copy_fit(row.role, sizeof(row.role), (const char *)sqlite3_column_text(stmt, 5));
        row.sort_order = sqlite3_column_int(stmt, 6);
        row.total = sqlite3_column_int(stmt, 7);
        row.unread = sqlite3_column_int(stmt, 8);
        grown = realloc(loaded, (size_t)(loaded_n + 1) * sizeof(*loaded));
        if (grown == NULL) {
            free(loaded);
            sqlite3_finalize(stmt);
            pav_db_read_end(read);
            free(nodes);
            pav_error_set(err, "internal", "out of memory", 0);
            return -1;
        }
        loaded = grown;
        loaded[loaded_n++] = row;
    }
    sqlite3_finalize(stmt);
    pav_db_read_end(read);
    if (rc != SQLITE_DONE) {
        free(loaded);
        free(nodes);
        pav_error_set(err, "internal", "cannot read mailboxes", 0);
        return -1;
    }
    for (i = 0; i < loaded_n; i++) {
        TreeNode node;
        int wrote;

        memset(&node, 0, sizeof(node));
        wrote = snprintf(node.id, sizeof(node.id), "mbx:%s:%s", loaded[i].ra_id, loaded[i].id);
        if (wrote < 0 || (size_t)wrote >= sizeof(node.id) || !copy_fit(node.kind, sizeof(node.kind), "mailbox") ||
            !copy_fit(node.label, sizeof(node.label), loaded[i].name[0] != '\0' ? loaded[i].name : loaded[i].id) ||
            !copy_fit(node.sort_name, sizeof(node.sort_name), loaded[i].name) ||
            !copy_fit(node.sort_id, sizeof(node.sort_id), loaded[i].id) ||
            !copy_fit(node.raw_parent, sizeof(node.raw_parent), loaded[i].parent_id) ||
            !copy_fit(node.ra_id, sizeof(node.ra_id), loaded[i].ra_id) ||
            !copy_fit(node.account_id, sizeof(node.account_id), loaded[i].account_id)) {
            continue;
        }
        if (loaded[i].role[0] != '\0' && copy_fit(node.role, sizeof(node.role), loaded[i].role)) {
            node.role_set = 1;
        }
        node.sort_order = loaded[i].sort_order;
        node.total = loaded[i].total;
        node.unread = loaded[i].unread;
        if (add_node(&nodes, &node_n, &node) != 0) {
            free(loaded);
            free(nodes);
            pav_error_set(err, "internal", "out of memory", 0);
            return -1;
        }
    }
    free(loaded);
    loaded = NULL;
    for (i = 0; i < node_n; i++) {
        int found = 0;

        if (strcmp(nodes[i].kind, "mailbox") != 0) {
            continue;
        }
        if (nodes[i].raw_parent[0] != '\0') {
            for (j = 0; j < node_n; j++) {
                if (strcmp(nodes[j].kind, "mailbox") == 0 && strcmp(nodes[j].ra_id, nodes[i].ra_id) == 0 &&
                    strcmp(nodes[j].sort_id, nodes[i].raw_parent) == 0) {
                    copy_fit(nodes[i].parent_id, sizeof(nodes[i].parent_id), nodes[j].id);
                    found = 1;
                    break;
                }
            }
        }
        if (!found) {
            copy_fit(nodes[i].parent_id, sizeof(nodes[i].parent_id), nodes[i].account_id);
        }
        for (j = 0; j < node_n; j++) {
            if (strcmp(nodes[j].kind, "account") == 0 && strcmp(nodes[j].id, nodes[i].account_id) == 0) {
                nodes[j].unread += nodes[i].unread;
                nodes[j].total += nodes[i].total;
                break;
            }
        }
    }
    for (i = 0; i < node_n; i++) {
        if (nodes[i].parent_id[0] == '\0') {
            continue;
        }
        for (j = 0; j < node_n; j++) {
            if (strcmp(nodes[j].id, nodes[i].parent_id) == 0) {
                nodes[j].has_children = 1;
                break;
            }
        }
    }
    if (order_siblings(nodes, node_n) != 0) {
        free(nodes);
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    rows = json_array();
    root = json_object();
    if (rows == NULL || root == NULL || put_json(root, "nodes", rows) != 0) {
        json_decref(rows);
        json_decref(root);
        free(nodes);
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    for (i = 0; i < node_n; i++) {
        json_t *obj = node_json(&nodes[i]);

        if (obj == NULL || json_array_append_new(rows, obj) != 0) {
            json_decref(obj);
            json_decref(root);
            free(nodes);
            pav_error_set(err, "internal", "out of memory", 0);
            return -1;
        }
    }
    free(nodes);
    *data = root;
    return 0;
}

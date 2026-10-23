#define _POSIX_C_SOURCE 200809L

#include "dispatch.h"
#include "account.h"
#include "logring.h"
#include "pav_logdb.h"
#include "pav_search.h"
#include "pav_sync.h"
#include "themes.h"
#include "window_state.h"
#include "pav_secret.h"

#include "pav_platform.h"
#include "pavillion.h"

#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAV_CMD_WORKER 1u
/* Network-bound (H Lock): slow-command WARN only past PAV_SLOW_NET_COMMAND_MS. */
#define PAV_CMD_NET 2u
#define EVENT_QUEUE_CAP 256
#define EVENT_NAME_MAX 64
#define EVENT_JSON_MAX 65536
#define LOG_CLIENT_MAX 2000

typedef int (*PavHandler)(json_t *args, json_t **data, PavError *err);

typedef struct PavCommand {
    const char *name;
    PavHandler handler;
    unsigned flags;
} PavCommand;

typedef struct EventItem {
    char *name;
    char *json;
} EventItem;

static int cmd_log_client(json_t *args, json_t **data, PavError *err);
static int cmd_export_write_text(json_t *args, json_t **data, PavError *err);
static int cmd_log_export(json_t *args, json_t **data, PavError *err);
static int cmd_log_tail(json_t *args, json_t **data, PavError *err);
static int cmd_log_query(json_t *args, json_t **data, PavError *err);
static int cmd_log_stats(json_t *args, json_t **data, PavError *err);
static int cmd_log_trim(json_t *args, json_t **data, PavError *err);
static int cmd_log_trim_preview(json_t *args, json_t **data, PavError *err);
static int cmd_prefs_get(json_t *args, json_t **data, PavError *err);
static int cmd_prefs_set(json_t *args, json_t **data, PavError *err);
static int cmd_prefs_ui_get(json_t *args, json_t **data, PavError *err);
static int cmd_prefs_ui_set(json_t *args, json_t **data, PavError *err);
static int cmd_status(json_t *args, json_t **data, PavError *err);
static int cmd_sync_now(json_t *args, json_t **data, PavError *err);
int pav_cache_usage(json_t *args, json_t **data, PavError *err);
#ifndef NDEBUG
int pav_cmd_secret_smoke(json_t *args, json_t **data, PavError *err);
#endif

/* strcmp order. bsearch requires it. */
static const PavCommand k_commands[] = {
    {"account.add", pav_account_add, 0},
    {"account.discover", pav_account_discover, PAV_CMD_WORKER | PAV_CMD_NET},
    {"account.list", pav_account_list, 0},
    {"account.remove", pav_account_remove, 0},
    {"account.set_secret", pav_account_set_secret, 0},
    {"account.test", pav_account_test, PAV_CMD_WORKER | PAV_CMD_NET},
    {"account.update", pav_account_update, 0},
    {"activity.cancel", pav_activity_cmd_cancel, 0},
    {"activity.dismiss", pav_activity_cmd_dismiss, 0},
    {"activity.list", pav_activity_cmd_list, 0},
    {"cache.usage", pav_cache_usage, PAV_CMD_WORKER | PAV_CMD_NET},
#ifndef NDEBUG
    {"debug.activity_demo", pav_activity_cmd_demo, PAV_CMD_WORKER},
    {"debug.secret_smoke", pav_cmd_secret_smoke, 0},
#endif
    {"export.write_text", cmd_export_write_text, 0},
    {"identity.list", pav_cmd_identity_list, 0},
    {"index.rebuild", pav_cmd_index_rebuild, 0},
    {"log.client", cmd_log_client, 0},
    {"log.export", cmd_log_export, 0},
    {"log.query", cmd_log_query, 0},
    {"log.stats", cmd_log_stats, 0},
    {"log.tail", cmd_log_tail, 0},
    {"log.trim", cmd_log_trim, 0},
    {"log.trim_preview", cmd_log_trim_preview, 0},
    {"mailbox.list", pav_cmd_mailbox_list, 0},
    {"message.get", pav_cmd_message_get, PAV_CMD_WORKER | PAV_CMD_NET},
    {"message.list", pav_cmd_message_list, 0},
    {"prefs.get", cmd_prefs_get, 0},
    {"prefs.set", cmd_prefs_set, 0},
    {"prefs.ui.get", cmd_prefs_ui_get, 0},
    {"prefs.ui.set", cmd_prefs_ui_set, 0},
    {"search.run", pav_cmd_search_run, 0},
    {"sync.now", cmd_sync_now, 0},
    {"system.status", cmd_status, 0},
    {"themes.ensure", pav_themes_ensure, 0},
    {"themes.folder.set", pav_themes_folder_set, 0},
    {"themes.list", pav_themes_list, 0},
    {"window.initial", pav_cmd_window_initial, 0},
    {"window.report", pav_cmd_window_report, 0},
};

static pthread_mutex_t g_ev_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_ev_cv = PTHREAD_COND_INITIALIZER;
static EventItem g_ev_q[EVENT_QUEUE_CAP];
static int g_ev_head;
static int g_ev_tail;
static int g_ev_count;
static unsigned long long g_ev_dropped;
static void *g_ev_thread;
static int g_ev_alive;
static int g_ev_stop;
static pav_event_cb g_ev_cb;
static void *g_ev_user;

void pav_error_set(PavError *err, const char *code, const char *message, int retryable) {
    if (err == NULL) {
        return;
    }
    snprintf(err->code, sizeof(err->code), "%s", code != NULL && code[0] != '\0' ? code : "internal");
    snprintf(err->message, sizeof(err->message), "%s", message != NULL ? message : "");
    if (strcmp(err->code, "network") == 0) {
        err->retryable = 1;
    } else if (strcmp(err->code, "server") == 0) {
        err->retryable = retryable ? 1 : 0;
    } else {
        err->retryable = 0;
    }
}

static char *dumps_root(json_t *root) {
    char *text;

    if (root == NULL) {
        return NULL;
    }
    text = json_dumps(root, JSON_COMPACT);
    json_decref(root);
    return text;
}

char *pav_reply_error(const char *code, const char *message, int retryable) {
    PavError err;
    json_t *error;
    json_t *root;

    pav_error_set(&err, code, message, retryable);
    error = json_object();
    root = json_object();
    if (error == NULL || root == NULL) {
        json_decref(error);
        json_decref(root);
        return NULL;
    }
    if (json_object_set_new(error, "code", json_string(err.code)) != 0 ||
        json_object_set_new(error, "message", json_string(err.message)) != 0 ||
        json_object_set_new(error, "retryable", err.retryable ? json_true() : json_false()) != 0 ||
        json_object_set_new(root, "ok", json_false()) != 0 ||
        json_object_set_new(root, "error", error) != 0) {
        json_decref(error);
        json_decref(root);
        return NULL;
    }
    return dumps_root(root);
}

char *pav_reply_ok(json_t *data) {
    json_t *root = json_object();

    if (root == NULL) {
        return NULL;
    }
    if (json_object_set_new(root, "ok", json_true()) != 0) {
        json_decref(root);
        return NULL;
    }
    if (data == NULL) {
        if (json_object_set_new(root, "data", json_null()) != 0) {
            json_decref(root);
            return NULL;
        }
    } else if (json_object_set(root, "data", data) != 0) {
        json_decref(root);
        return NULL;
    }
    return dumps_root(root);
}

static int command_cmp(const void *key, const void *elem) {
    const char *name = key;
    const PavCommand *cmd = elem;

    return strcmp(name, cmd->name);
}

static const PavCommand *command_find(const char *name) {
    return bsearch(name, k_commands, sizeof(k_commands) / sizeof(k_commands[0]), sizeof(k_commands[0]),
                   command_cmp);
}

int pav_command_is_worker(const char *name) {
    const PavCommand *cmd = command_find(name);

    return cmd != NULL && (cmd->flags & PAV_CMD_WORKER) != 0;
}

int pav_command_count(void) {
    return (int)(sizeof(k_commands) / sizeof(k_commands[0]));
}

const char *pav_command_name(int index) {
    if (index < 0 || index >= pav_command_count()) {
        return NULL;
    }
    return k_commands[index].name;
}

static int reject_unknown_args(json_t *args, PavError *err) {
    const char *key;
    json_t *value;
    char buf[512];

    json_object_foreach(args, key, value) {
        (void)value;
        snprintf(buf, sizeof(buf), "unknown argument %s", key);
        pav_error_set(err, "bad_args", buf, 0);
        return -1;
    }
    return 0;
}

static int cmd_sync_now(json_t *args, json_t **data, PavError *err) {
    const char *key;
    json_t *value;
    const char *account_id = NULL;

    json_object_foreach(args, key, value) {
        if (strcmp(key, "accountId") == 0) {
            if (!json_is_string(value) || json_string_value(value)[0] == '\0') {
                pav_error_set(err, "bad_args", "accountId must be a non-empty string", 0);
                return -1;
            }
            account_id = json_string_value(value);
        } else {
            char buf[128];

            snprintf(buf, sizeof(buf), "unknown argument %s", key);
            pav_error_set(err, "bad_args", buf, 0);
            return -1;
        }
    }
    *data = json_object();
    if (*data == NULL) {
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    if (pav_sync_now(account_id) != 0) {
        json_decref(*data);
        *data = NULL;
        pav_error_set(err, "internal", "cannot sync", 0);
        return -1;
    }
    return 0;
}

static int cmd_status(json_t *args, json_t **data, PavError *err) {
    if (reject_unknown_args(args, err) != 0) {
        return -1;
    }
    return pav_core_status_data(data, err);
}

static int cmd_prefs_get(json_t *args, json_t **data, PavError *err) {
    if (reject_unknown_args(args, err) != 0) {
        return -1;
    }
    return pav_core_prefs_get_data(data, err);
}

static int cmd_prefs_set(json_t *args, json_t **data, PavError *err) {
    return pav_core_prefs_set(args, data, err);
}

static int cmd_prefs_ui_get(json_t *args, json_t **data, PavError *err) {
    if (reject_unknown_args(args, err) != 0) {
        return -1;
    }
    return pav_core_ui_get_data(data, err);
}

static int cmd_prefs_ui_set(json_t *args, json_t **data, PavError *err) {
    return pav_core_ui_set(args, data, err);
}

static int subsystem_ok(const char *text) {
    size_t i;
    size_t n;

    if (text == NULL || text[0] < 'a' || text[0] > 'z') {
        return 0;
    }
    n = strlen(text);
    if (n > 16) {
        return 0;
    }
    for (i = 1; i < n; i++) {
        char c = text[i];

        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
            return 0;
        }
    }
    return 1;
}

static int level_from_name(const char *name, int *out) {
    if (strcmp(name, "error") == 0) {
        *out = PAV_ERROR;
    } else if (strcmp(name, "warn") == 0) {
        *out = PAV_WARN;
    } else if (strcmp(name, "info") == 0) {
        *out = PAV_INFO;
    } else if (strcmp(name, "debug") == 0) {
        *out = PAV_DEBUG;
    } else if (strcmp(name, "trace") == 0) {
        *out = PAV_TRACE;
    } else {
        return -1;
    }
    return 0;
}

/* Replaces CR, LF, and CRLF with U+23CE. The caller frees the result. */
static char *replace_newlines(const char *message) {
    size_t n = strlen(message);
    size_t i;
    size_t w = 0;
    char *out = malloc(n * 3 + 1);

    if (out == NULL) {
        return NULL;
    }
    for (i = 0; i < n; i++) {
        if (message[i] == '\r' || message[i] == '\n') {
            if (message[i] == '\r' && i + 1 < n && message[i + 1] == '\n') {
                i++;
            }
            out[w++] = (char)0xE2;
            out[w++] = (char)0x8E;
            out[w++] = (char)0xAA;
        } else {
            out[w++] = message[i];
        }
    }
    out[w] = '\0';
    return out;
}

/* H-4: log.client accepts 50 lines per second. Lines over the limit are
   dropped, still answer ok, and are counted into one WARN when the next
   second's first line arrives. */
#define LOG_CLIENT_PER_SECOND 50
static pthread_mutex_t g_client_mu = PTHREAD_MUTEX_INITIALIZER;
static int64_t g_client_window;
static int g_client_count;
static int g_client_dropped;

static int client_rate_ok(void) {
    struct timespec ts;
    int64_t second;
    int dropped = 0;
    int ok;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    second = (int64_t)ts.tv_sec;
    pthread_mutex_lock(&g_client_mu);
    if (second != g_client_window) {
        dropped = g_client_dropped;
        g_client_window = second;
        g_client_count = 0;
        g_client_dropped = 0;
    }
    ok = g_client_count < LOG_CLIENT_PER_SECOND;
    if (ok) {
        g_client_count++;
    } else {
        g_client_dropped++;
    }
    pthread_mutex_unlock(&g_client_mu);
    if (dropped > 0) {
        pav_log("ui", PAV_WARN, 2, "log.client dropped %d lines over %d per second", dropped, LOG_CLIENT_PER_SECOND);
    }
    return ok;
}

static int cmd_log_client(json_t *args, json_t **data, PavError *err) {
    const char *key;
    json_t *value;
    const char *subsystem = NULL;
    const char *level_name = NULL;
    const char *message = NULL;
    char *clean;
    int level = PAV_INFO;
    char buf[512];

    json_object_foreach(args, key, value) {
        if (strcmp(key, "subsystem") == 0) {
            if (!json_is_string(value)) {
                pav_error_set(err, "bad_args", "subsystem must be a string", 0);
                return -1;
            }
            subsystem = json_string_value(value);
        } else if (strcmp(key, "level") == 0) {
            if (!json_is_string(value)) {
                pav_error_set(err, "bad_args", "level must be error, warn, info, debug, or trace", 0);
                return -1;
            }
            level_name = json_string_value(value);
        } else if (strcmp(key, "message") == 0) {
            if (!json_is_string(value)) {
                pav_error_set(err, "bad_args", "message must be a string", 0);
                return -1;
            }
            message = json_string_value(value);
        } else {
            snprintf(buf, sizeof(buf), "unknown argument %s", key);
            pav_error_set(err, "bad_args", buf, 0);
            return -1;
        }
    }
    if (subsystem == NULL || level_name == NULL || message == NULL) {
        pav_error_set(err, "bad_args", "log.client requires subsystem, level, and message", 0);
        return -1;
    }
    if (!subsystem_ok(subsystem)) {
        pav_error_set(err, "bad_args", "subsystem must match ^[a-z][a-z0-9_]{0,15}$", 0);
        return -1;
    }
    if (level_from_name(level_name, &level) != 0) {
        pav_error_set(err, "bad_args", "level must be error, warn, info, debug, or trace", 0);
        return -1;
    }
    if (strlen(message) > LOG_CLIENT_MAX) {
        pav_error_set(err, "too_large", "message is too large", 0);
        return -1;
    }
    if (client_rate_ok()) {
        clean = replace_newlines(message);
        if (clean == NULL) {
            pav_error_set(err, "internal", "out of memory", 0);
            return -1;
        }
        pav_ui_log(subsystem, level, clean);
        free(clean);
    }
    *data = json_object();
    if (*data == NULL) {
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    return 0;
}

#define LOG_QUERY_MAX 10000
#define LOG_SESSION_ALL 0
#define LOG_SESSION_CURRENT 1
#define LOG_SESSION_PREVIOUS 2
#define LOG_SESSION_ID 3

/* log.query and log.export share one filter (H-4 Lock, Reading history). */
typedef struct LogFilter {
    const json_t *levels; /* NULL: every level */
    const json_t *subsystems;
    const char *text;
    PavLogQuery q;
    int session_kind;
    int64_t session_id;
} LogFilter;

static json_t *g_all_levels;

static const json_t *all_levels(void) {
    if (g_all_levels == NULL) {
        g_all_levels = json_pack("[sssss]", "ERROR", "WARN", "INFO", "DEBUG", "TRACE");
    }
    return g_all_levels;
}

static int level_bit(const char *name) {
    static const char *const names[] = {"ERROR", "WARN", "INFO", "DEBUG", "TRACE"};
    int i;

    for (i = 0; i < 5; i++) {
        if (strcmp(name, names[i]) == 0) {
            return i;
        }
    }
    return -1;
}

static int bad(PavError *err, const char *message) {
    pav_error_set(err, "bad_args", message, 0);
    return -1;
}

static int ms_arg(json_t *value, int64_t *out) {
    if (!json_is_integer(value) || json_integer_value(value) < 0) {
        return -1;
    }
    *out = (int64_t)json_integer_value(value);
    return 0;
}

/* One key of a filter. Returns 1 when key is a filter key, 0 when it is not,
   -1 on a bad value. */
static int filter_key(LogFilter *f, const char *key, json_t *value, PavError *err) {
    size_t i;
    size_t n;

    if (strcmp(key, "levels") == 0) {
        if (!json_is_array(value)) {
            return bad(err, "levels must be an array of level names");
        }
        n = json_array_size(value);
        for (i = 0; i < n; i++) {
            const char *item = json_string_value(json_array_get(value, i));

            if (item == NULL || level_bit(item) < 0) {
                return bad(err, "levels must be an array of level names");
            }
        }
        f->levels = value;
        return 1;
    }
    if (strcmp(key, "subsystems") == 0) {
        if (json_is_null(value)) {
            f->subsystems = NULL;
            return 1;
        }
        if (!json_is_array(value)) {
            return bad(err, "subsystems must be an array of strings or null");
        }
        n = json_array_size(value);
        for (i = 0; i < n; i++) {
            const char *item = json_string_value(json_array_get(value, i));

            if (item == NULL || item[0] == '\0' || strlen(item) > 128 || strpbrk(item, "\r\n") != NULL) {
                return bad(err, "subsystems must be an array of strings or null");
            }
        }
        f->subsystems = value;
        return 1;
    }
    if (strcmp(key, "text") == 0) {
        if (!json_is_string(value) || strlen(json_string_value(value)) > 2000) {
            return bad(err, "text must be a string of at most 2000 bytes");
        }
        f->text = json_string_value(value);
        return 1;
    }
    if (strcmp(key, "since") == 0) {
        if (json_is_null(value)) {
            return 1;
        }
        if (ms_arg(value, &f->q.since) != 0) {
            return bad(err, "since must be UTC milliseconds");
        }
        f->q.has_since = 1;
        return 1;
    }
    if (strcmp(key, "until") == 0) {
        if (json_is_null(value)) {
            return 1;
        }
        if (ms_arg(value, &f->q.until) != 0) {
            return bad(err, "until must be UTC milliseconds");
        }
        f->q.has_until = 1;
        return 1;
    }
    if (strcmp(key, "session") == 0) {
        const char *s = json_string_value(value);

        if (s != NULL && strcmp(s, "current") == 0) {
            f->session_kind = LOG_SESSION_CURRENT;
        } else if (s != NULL && strcmp(s, "previous") == 0) {
            f->session_kind = LOG_SESSION_PREVIOUS;
        } else if (s != NULL && strcmp(s, "all") == 0) {
            f->session_kind = LOG_SESSION_ALL;
        } else if (json_is_integer(value) && json_integer_value(value) > 0) {
            f->session_kind = LOG_SESSION_ID;
            f->session_id = (int64_t)json_integer_value(value);
        } else {
            return bad(err, "session must be current, previous, all, or a session id");
        }
        return 1;
    }
    return 0;
}

static void filter_finish(LogFilter *f) {
    size_t i;

    if (f->levels == NULL) {
        f->levels = all_levels();
    }
    f->q.level_mask = 0;
    for (i = 0; i < json_array_size(f->levels); i++) {
        int bit = level_bit(json_string_value(json_array_get(f->levels, i)));

        if (bit >= 0) {
            f->q.level_mask |= 1u << bit;
        }
    }
}

/* On the log thread: resolve the session filter. Returns 1 when nothing can
   match (no previous run). */
static int filter_session(LogFilter *f, sqlite3 *db, int64_t session) {
    if (f->session_kind == LOG_SESSION_CURRENT) {
        f->q.session = session;
    } else if (f->session_kind == LOG_SESSION_PREVIOUS) {
        if (pav_logdb_previous_session(db, session, &f->q.session) != 0 || f->q.session == 0) {
            return 1;
        }
    } else if (f->session_kind == LOG_SESSION_ID) {
        f->q.session = f->session_id;
    } else {
        f->q.session = 0;
    }
    return 0;
}

typedef struct QueryCtx {
    LogFilter *f;
    int limit;
    json_t *newest_first;
    json_t *sessions; /* "id" -> started UTC ms, for the separators */
    sqlite3 *db;
    int64_t current;
} QueryCtx;

static int query_line(void *ctx, int64_t seq, int64_t session, const char *line) {
    QueryCtx *q = ctx;
    json_t *obj;
    char key[24];

    if (!pav_logring_line_kept(line, q->f->levels, q->f->subsystems, q->f->text)) {
        return 0;
    }
    obj = pav_logring_line_json((uint64_t)seq, line);
    if (obj != NULL && json_object_set_new(obj, "session", json_integer((json_int_t)session)) != 0) {
        json_decref(obj);
        obj = NULL;
    }
    snprintf(key, sizeof(key), "%lld", (long long)session);
    if (obj != NULL && json_object_get(q->sessions, key) == NULL) {
        int64_t started = 0;

        if (pav_logdb_session_started(q->db, session, &started) == 0) {
            json_object_set_new(q->sessions, key, json_integer((json_int_t)started));
        }
    }
    if (obj == NULL || json_array_append_new(q->newest_first, obj) != 0) {
        return -1;
    }
    return (int)json_array_size(q->newest_first) >= q->limit ? 1 : 0;
}

static int query_on_log_thread(void *ctx, sqlite3 *db, int64_t session) {
    QueryCtx *q = ctx;

    q->db = db;
    q->current = session;

    if (filter_session(q->f, db, session) != 0) {
        return 0;
    }
    return pav_logdb_walk(db, &q->f->q, query_line, q);
}

static int cmd_log_query(json_t *args, json_t **data, PavError *err) {
    const char *key;
    json_t *value;
    LogFilter f;
    QueryCtx q;
    json_t *lines;
    int limit = 0;
    int rc;
    size_t i;
    char buf[512];

    memset(&f, 0, sizeof(f));
    json_object_foreach(args, key, value) {
        if (strcmp(key, "beforeSeq") == 0) {
            if (!json_is_integer(value) || json_integer_value(value) < 1) {
                return bad(err, "beforeSeq must be an integer >= 1");
            }
            f.q.before_seq = (int64_t)json_integer_value(value);
        } else if (strcmp(key, "limit") == 0) {
            if (!json_is_integer(value) || json_integer_value(value) < 1 ||
                json_integer_value(value) > LOG_QUERY_MAX) {
                return bad(err, "limit must be an integer from 1 to 10000");
            }
            limit = (int)json_integer_value(value);
        } else {
            rc = filter_key(&f, key, value, err);
            if (rc < 0) {
                return -1;
            }
            if (rc == 0) {
                snprintf(buf, sizeof(buf), "unknown argument %s", key);
                return bad(err, buf);
            }
        }
    }
    if (limit == 0) {
        return bad(err, "limit must be an integer from 1 to 10000");
    }
    filter_finish(&f);
    q.f = &f;
    q.limit = limit;
    q.newest_first = json_array();
    q.sessions = json_object();
    q.db = NULL;
    q.current = 0;
    if (q.newest_first == NULL || q.sessions == NULL) {
        json_decref(q.sessions);
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    pav_log_flush();
    rc = pav_log_db_call(query_on_log_thread, &q);
    if (rc == -2) {
        json_decref(q.newest_first);
        json_decref(q.sessions);
        pav_error_set(err, "not_ready", "log.db is not open", 0);
        return -1;
    }
    if (rc != 0) {
        json_decref(q.newest_first);
        json_decref(q.sessions);
        pav_error_set(err, "internal", "cannot read the log", 0);
        return -1;
    }
    lines = json_array();
    for (i = json_array_size(q.newest_first); lines != NULL && i > 0; i--) {
        json_array_append(lines, json_array_get(q.newest_first, i - 1));
    }
    json_decref(q.newest_first);
    *data = lines != NULL ? json_pack("{s:o,s:o,s:I}", "lines", lines, "sessions", q.sessions, "currentSession", (json_int_t)q.current) : NULL;
    if (*data == NULL) {
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    return 0;
}

#define LOG_EXPORT_MAX (32u * 1024u * 1024u)

typedef struct ExportCtx {
    LogFilter *f;
    char **lines; /* newest first */
    size_t count;
    size_t cap;
    size_t total;
    int too_large;
} ExportCtx;

static int export_line(void *ctx, int64_t seq, int64_t session, const char *line) {
    ExportCtx *e = ctx;

    (void)session;
    size_t len = strlen(line);

    (void)seq;
    if (!pav_logring_line_kept(line, e->f->levels, e->f->subsystems, e->f->text)) {
        return 0;
    }
    if (e->total > LOG_EXPORT_MAX || len > LOG_EXPORT_MAX - e->total) {
        e->too_large = 1;
        return 1;
    }
    if (e->count == e->cap) {
        size_t next = e->cap == 0 ? 1024 : e->cap * 2;
        char **grown = realloc(e->lines, next * sizeof(*grown));

        if (grown == NULL) {
            return -1;
        }
        e->lines = grown;
        e->cap = next;
    }
    e->lines[e->count] = strdup(line);
    if (e->lines[e->count] == NULL) {
        return -1;
    }
    e->count++;
    e->total += len;
    return 0;
}

static int export_on_log_thread(void *ctx, sqlite3 *db, int64_t session) {
    ExportCtx *e = ctx;

    if (filter_session(e->f, db, session) != 0) {
        return 0;
    }
    return pav_logdb_walk(db, &e->f->q, export_line, e);
}

/* log.db when it is open; the ring otherwise (a test sink, or no profile). */
static int export_bytes(LogFilter *f, char **out, size_t *out_len, int *lines) {
    ExportCtx e;
    size_t i;
    size_t off = 0;
    int rc;

    memset(&e, 0, sizeof(e));
    e.f = f;
    rc = pav_log_db_call(export_on_log_thread, &e);
    if (rc == -2) {
        return pav_logring_export(f->levels, f->subsystems, f->text, out, out_len, lines);
    }
    if (rc == 0 && !e.too_large) {
        *out = malloc(e.total + 1);
        if (*out == NULL) {
            rc = -1;
        } else {
            for (i = e.count; i > 0; i--) {
                size_t len = strlen(e.lines[i - 1]);

                memcpy(*out + off, e.lines[i - 1], len);
                off += len;
            }
            (*out)[off] = '\0';
            *out_len = off;
            *lines = (int)e.count;
        }
    }
    for (i = 0; i < e.count; i++) {
        free(e.lines[i]);
    }
    free(e.lines);
    if (e.too_large) {
        return -2;
    }
    return rc == 0 ? 0 : -1;
}

/* An export target: absolute file path, parent directory exists (log.export,
   export.write_text). */
static int out_path_check(const char *path, PavError *err) {
    char parent[4096];

    if (path[0] == '\0' || strlen(path) >= 4096 || strpbrk(path, "\r\n") != NULL || path[strlen(path) - 1] == '/') {
        return bad(err, "path must be an absolute file path");
    }
    if (!pav_path_is_absolute(path)) {
        return bad(err, "path must be an absolute file path");
    }
    if (pav_is_dir(path)) {
        return bad(err, "path is a directory");
    }
    if (pav_path_parent(parent, sizeof(parent), path) != 0 || !pav_is_dir(parent)) {
        return bad(err, "directory does not exist");
    }
    return 0;
}

#define EXPORT_TEXT_MAX (8u * 1024u * 1024u)

/* D-10: write page-built export text (Accounts CSV) to a dialog-picked path. */
static int cmd_export_write_text(json_t *args, json_t **data, PavError *err) {
    const char *key;
    json_t *value;
    const char *path = NULL;
    const char *text = NULL;
    size_t len = 0;
    const char *name;
    const PavPlatform *platform;
    char buf[512];

    json_object_foreach(args, key, value) {
        if (strcmp(key, "path") == 0) {
            if (!json_is_string(value)) {
                return bad(err, "path must be a string");
            }
            path = json_string_value(value);
        } else if (strcmp(key, "text") == 0) {
            if (!json_is_string(value)) {
                return bad(err, "text must be a string");
            }
            text = json_string_value(value);
            len = json_string_length(value);
        } else {
            snprintf(buf, sizeof(buf), "unknown argument %s", key);
            return bad(err, buf);
        }
    }
    if (path == NULL || text == NULL) {
        return bad(err, "export.write_text requires path and text");
    }
    if (out_path_check(path, err) != 0) {
        return -1;
    }
    if (strlen(text) != len) {
        return bad(err, "text must not contain NUL");
    }
    if (len > EXPORT_TEXT_MAX) {
        pav_error_set(err, "too_large", "export is too large", 0);
        return -1;
    }
    name = strrchr(path, '/');
    name = name != NULL ? name + 1 : path;
    platform = pav_platform();
    if (platform == NULL || platform->write_private == NULL ||
        platform->write_private(path, text, len) != 0) {
        pav_log("export", PAV_WARN, 2, "write failed: %s", name);
        pav_error_set(err, "internal", "cannot write the file", 0);
        return -1;
    }
    pav_log("export", PAV_INFO, 2, "wrote %zu bytes to %s", len, name);
    *data = json_pack("{s:I}", "bytes", (json_int_t)len);
    if (*data == NULL) {
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    return 0;
}

static int cmd_log_export(json_t *args, json_t **data, PavError *err) {
    const char *key;
    json_t *value;
    const char *path = NULL;
    json_t *filter = NULL;
    LogFilter f;
    char *bytes = NULL;
    size_t bytes_len = 0;
    int lines = 0;
    int rc;
    char buf[512];
    const PavPlatform *platform;

    memset(&f, 0, sizeof(f));
    f.text = "";
    json_object_foreach(args, key, value) {
        if (strcmp(key, "path") == 0) {
            if (!json_is_string(value)) {
                return bad(err, "path must be a string");
            }
            path = json_string_value(value);
        } else if (strcmp(key, "filter") == 0) {
            if (!json_is_object(value)) {
                return bad(err, "filter must be an object");
            }
            filter = value;
        } else {
            snprintf(buf, sizeof(buf), "unknown argument %s", key);
            return bad(err, buf);
        }
    }
    if (path == NULL || filter == NULL) {
        return bad(err, "log.export requires path and filter");
    }
    if (out_path_check(path, err) != 0) {
        return -1;
    }
    json_object_foreach(filter, key, value) {
        rc = filter_key(&f, key, value, err);
        if (rc < 0) {
            return -1;
        }
        if (rc == 0) {
            snprintf(buf, sizeof(buf), "unknown argument %s", key);
            return bad(err, buf);
        }
    }
    if (f.levels == NULL) {
        return bad(err, "levels must be an array of level names");
    }
    filter_finish(&f);
    pav_log_flush();
    rc = export_bytes(&f, &bytes, &bytes_len, &lines);
    if (rc == -2) {
        free(bytes);
        pav_error_set(err, "too_large", "log export is too large", 0);
        return -1;
    }
    if (rc != 0 || bytes == NULL) {
        free(bytes);
        pav_error_set(err, "internal", "cannot read the log", 0);
        return -1;
    }
    platform = pav_platform();
    if (platform == NULL || platform->write_private == NULL ||
        platform->write_private(path, bytes, bytes_len) != 0) {
        free(bytes);
        pav_error_set(err, "internal", "cannot write the log", 0);
        return -1;
    }
    free(bytes);
    *data = json_pack("{s:i}", "lines", lines);
    if (*data == NULL) {
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    return 0;
}

static json_t *stats_json(const PavLogStats *st, int preview) {
    json_t *obj = json_pack("{s:I,s:I}", "bytes", (json_int_t)st->bytes, "records", (json_int_t)st->records);

    if (obj == NULL) {
        return NULL;
    }
    if (json_object_set_new(obj, "oldestTs", st->records > 0 ? json_integer(st->oldest_ts) : json_null()) != 0 ||
        (!preview &&
         (json_object_set_new(obj, "oldestSeq", st->records > 0 ? json_integer(st->oldest_seq) : json_null()) != 0 ||
          json_object_set_new(obj, "capMb", json_integer(st->cap_mb)) != 0))) {
        json_decref(obj);
        return NULL;
    }
    return obj;
}

static int stats_reply(int rc, const PavLogStats *st, int preview, json_t **data, PavError *err) {
    if (rc != 0) {
        pav_error_set(err, "not_ready", "log.db is not open", 0);
        return -1;
    }
    *data = stats_json(st, preview);
    if (*data == NULL) {
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    return 0;
}

static int mb_arg(json_t *args, int required, int *mb, PavError *err) {
    const char *key;
    json_t *value;
    char buf[512];

    *mb = 0;
    json_object_foreach(args, key, value) {
        if (strcmp(key, "mb") == 0) {
            if (!json_is_integer(value) || json_integer_value(value) < 1 || json_integer_value(value) > 100) {
                return bad(err, "mb must be an integer from 1 to 100");
            }
            *mb = (int)json_integer_value(value);
        } else {
            snprintf(buf, sizeof(buf), "unknown argument %s", key);
            return bad(err, buf);
        }
    }
    if (required && *mb == 0) {
        return bad(err, "mb must be an integer from 1 to 100");
    }
    return 0;
}

static int cmd_log_stats(json_t *args, json_t **data, PavError *err) {
    PavLogStats st;
    const char *key;
    json_t *value;
    char buf[512];

    json_object_foreach(args, key, value) {
        (void)value;
        snprintf(buf, sizeof(buf), "unknown argument %s", key);
        return bad(err, buf);
    }
    pav_log_flush();
    return stats_reply(pav_log_stats(&st), &st, 0, data, err);
}

static int cmd_log_trim_preview(json_t *args, json_t **data, PavError *err) {
    PavLogStats st;
    int mb;

    if (mb_arg(args, 1, &mb, err) != 0) {
        return -1;
    }
    pav_log_flush();
    return stats_reply(pav_log_trim_preview(mb, &st), &st, 1, data, err);
}

/* With mb: Apply (writes preferences.log_max_mb, then trims). Without: Trim
   Now to the saved cap, writing nothing. */
static int cmd_log_trim(json_t *args, json_t **data, PavError *err) {
    PavLogStats st;
    int mb;

    if (mb_arg(args, 0, &mb, err) != 0) {
        return -1;
    }
    if (mb > 0 && pav_core_set_log_max_mb(mb, err) != 0) {
        return -1;
    }
    pav_log_flush();
    return stats_reply(pav_log_trim(mb, &st), &st, 0, data, err);
}

static int cmd_log_tail(json_t *args, json_t **data, PavError *err) {
    const char *key;
    json_t *value;
    uint64_t since = 0;
    int limit = PAV_LOGRING_CAP;
    int min_level = PAV_DEBUG;
    const json_t *subsystems = NULL;
    char buf[512];

    json_object_foreach(args, key, value) {
        if (strcmp(key, "sinceSeq") == 0) {
            if (!json_is_integer(value) || json_integer_value(value) < 0) {
                pav_error_set(err, "bad_args", "sinceSeq must be an integer >= 0", 0);
                return -1;
            }
            since = (uint64_t)json_integer_value(value);
        } else if (strcmp(key, "limit") == 0) {
            json_int_t n;

            if (!json_is_integer(value)) {
                pav_error_set(err, "bad_args", "limit must be an integer from 1 to 10000", 0);
                return -1;
            }
            n = json_integer_value(value);
            if (n < 1 || n > PAV_LOGRING_CAP) {
                pav_error_set(err, "bad_args", "limit must be an integer from 1 to 10000", 0);
                return -1;
            }
            limit = (int)n;
        } else if (strcmp(key, "minLevel") == 0) {
            if (!json_is_string(value) || level_from_name(json_string_value(value), &min_level) != 0) {
                pav_error_set(err, "bad_args", "minLevel must be error, warn, info, debug, or trace", 0);
                return -1;
            }
        } else if (strcmp(key, "subsystems") == 0) {
            size_t i;
            size_t n;

            if (!json_is_array(value)) {
                pav_error_set(err, "bad_args", "subsystems must be an array of strings", 0);
                return -1;
            }
            n = json_array_size(value);
            for (i = 0; i < n; i++) {
                if (!json_is_string(json_array_get(value, i))) {
                    pav_error_set(err, "bad_args", "subsystems must be an array of strings", 0);
                    return -1;
                }
            }
            subsystems = value;
        } else {
            snprintf(buf, sizeof(buf), "unknown argument %s", key);
            pav_error_set(err, "bad_args", buf, 0);
            return -1;
        }
    }
    pav_log_flush();
    if (pav_logring_read(since, limit, min_level, subsystems, data) != 0 || *data == NULL) {
        pav_error_set(err, "internal", "cannot read the log", 0);
        return -1;
    }
    return 0;
}

/* H-4 commit 4: command logging. TRACE name and duration (log.* is skipped so
   the log view's own reads do not feed the log), WARN when slow or unknown,
   INFO outcome for account commands with the account id and error code only. */
#define PAV_SLOW_COMMAND_MS 250
#define PAV_SLOW_NET_COMMAND_MS 10000

static int64_t dispatch_mono_ms(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static const char *account_id_of(json_t *args, json_t *data, char *host, size_t host_len) {
    const char *keys[2] = {"id", "accountId"};
    int i;
    json_t *url;

    for (i = 0; i < 2; i++) {
        json_t *v = json_is_object(data) ? json_object_get(data, keys[i]) : NULL;

        if (json_is_string(v)) {
            return json_string_value(v);
        }
        v = json_object_get(args, keys[i]);
        if (json_is_string(v)) {
            return json_string_value(v);
        }
    }
    /* No account id yet (account.test, a failed add): name the session host.
       Never the username: an email address is DEBUG-only (H redaction). */
    url = json_object_get(args, "sessionUrl");
    if (json_is_string(url)) {
        pav_account_test_host(json_string_value(url), host, host_len);
        if (host[0] != '\0') {
            return host;
        }
    }
    url = json_object_get(args, "email");
    if (json_is_string(url) && strchr(json_string_value(url), '@') != NULL) {
        snprintf(host, host_len, "%s", strchr(json_string_value(url), '@') + 1);
        return host;
    }
    return "-";
}

static void log_command(const char *name, json_t *args, json_t *data, int rc, const PavError *err, int64_t ms) {
    char host[256];

    if (strncmp(name, "account.", 8) == 0 && strcmp(name, "account.list") != 0) {
        if (rc == 0) {
            pav_log("account", PAV_INFO, 2, "%s ok: %s", name, account_id_of(args, data, host, sizeof(host)));
        } else {
            pav_log("account", PAV_WARN, 3, "%s failed: %s (%s)", name, account_id_of(args, NULL, host, sizeof(host)), err->code);
        }
    }
    if (strncmp(name, "log.", 4) == 0) {
        return;
    }
    const PavCommand *cmd = command_find(name);
    int net = cmd != NULL && (cmd->flags & PAV_CMD_NET) != 0;

    if (ms >= (net ? PAV_SLOW_NET_COMMAND_MS : PAV_SLOW_COMMAND_MS)) {
        pav_log("dispatch", PAV_WARN, 2, "slow command %s: %lld ms", name, (long long)ms);
    } else if (net) {
        pav_log("dispatch", PAV_DEBUG, 3, "%s %s in %lld ms", name, rc == 0 ? "done" : err->code, (long long)ms);
    } else if (rc != 0) {
        pav_log("dispatch", PAV_DEBUG, 2, "%s failed: %s", name, err->code);
    } else {
        pav_log("dispatch", PAV_TRACE, 2, "%s %lld ms", name, (long long)ms);
    }
}

char *pav_dispatch_reply(const char *name, json_t *args) {
    int64_t started;
    int rc;
    const PavCommand *cmd;
    json_t *data = NULL;
    PavError err;
    char buf[512];
    char *text;

    memset(&err, 0, sizeof(err));
    if (name == NULL || name[0] == '\0') {
        return pav_reply_error("bad_args", "missing command", 0);
    }
    if (args == NULL || !json_is_object(args)) {
        return pav_reply_error("bad_args", "arguments are not an object", 0);
    }
    cmd = command_find(name);
    if (cmd == NULL) {
        snprintf(buf, sizeof(buf), "unknown command %s", name);
        pav_log("dispatch", PAV_WARN, 1, "unknown command %s", name);
        return pav_reply_error("bad_args", buf, 0);
    }
    started = dispatch_mono_ms();
    rc = cmd->handler(args, &data, &err);
    if (rc != 0 && err.code[0] == '\0') {
        pav_error_set(&err, "internal", "command failed", 0);
    }
    log_command(name, args, data, rc, &err, dispatch_mono_ms() - started);
    /* K Lock: a typed password crosses from the page once; wipe it now. */
    pav_secret_wipe_json(args, "secret");
    if (rc != 0) {
        json_decref(data);
        if (err.code[0] == '\0') {
            pav_error_set(&err, "internal", "command failed", 0);
        }
        text = pav_reply_error(err.code, err.message, err.retryable);
        return text;
    }
    text = pav_reply_ok(data);
    json_decref(data);
    return text;
}

void pavillion_free(void *p) {
    free(p);
}

static void event_free_item(EventItem *item) {
    if (item == NULL) {
        return;
    }
    free(item->name);
    free(item->json);
    item->name = NULL;
    item->json = NULL;
}

static void event_queue_clear_unlocked(void) {
    while (g_ev_count > 0) {
        event_free_item(&g_ev_q[g_ev_head]);
        g_ev_head = (g_ev_head + 1) % EVENT_QUEUE_CAP;
        g_ev_count--;
    }
    g_ev_head = 0;
    g_ev_tail = 0;
}

static void *event_thread_main(void *arg) {
    (void)arg;
    pthread_mutex_lock(&g_ev_mu);
    for (;;) {
        EventItem item;
        pav_event_cb cb;
        void *user;

        while (g_ev_count == 0 && !g_ev_stop) {
            pthread_cond_wait(&g_ev_cv, &g_ev_mu);
        }
        if (g_ev_count == 0) {
            g_ev_alive = 0;
            pthread_mutex_unlock(&g_ev_mu);
            break;
        }
        item = g_ev_q[g_ev_head];
        g_ev_q[g_ev_head].name = NULL;
        g_ev_q[g_ev_head].json = NULL;
        g_ev_head = (g_ev_head + 1) % EVENT_QUEUE_CAP;
        g_ev_count--;
        cb = g_ev_cb;
        user = g_ev_user;
        if (cb == NULL) {
            g_ev_dropped++;
            pthread_mutex_unlock(&g_ev_mu);
            event_free_item(&item);
            pthread_mutex_lock(&g_ev_mu);
            continue;
        }
        pthread_mutex_unlock(&g_ev_mu);
        cb(item.name, item.json, user);
        event_free_item(&item);
        pthread_mutex_lock(&g_ev_mu);
    }
    return NULL;
}

int pav_event_thread_start(void) {
    pthread_mutex_lock(&g_ev_mu);
    if (g_ev_alive) {
        pthread_mutex_unlock(&g_ev_mu);
        return -1;
    }
    event_queue_clear_unlocked();
    g_ev_dropped = 0;
    g_ev_stop = 0;
    if (pav_thread_create(&g_ev_thread, event_thread_main, NULL) != 0) {
        pthread_mutex_unlock(&g_ev_mu);
        return -1;
    }
    g_ev_alive = 1;
    pthread_mutex_unlock(&g_ev_mu);
    return 0;
}

void pav_event_thread_stop(void) {
    void *thread = NULL;

    pthread_mutex_lock(&g_ev_mu);
    g_ev_cb = NULL;
    g_ev_user = NULL;
    if (!g_ev_alive) {
        event_queue_clear_unlocked();
        pthread_mutex_unlock(&g_ev_mu);
        return;
    }
    g_ev_stop = 1;
    thread = g_ev_thread;
    g_ev_thread = NULL;
    pthread_cond_broadcast(&g_ev_cv);
    pthread_mutex_unlock(&g_ev_mu);
    if (thread != NULL) {
        pav_thread_join(thread);
    }
}

void pav_on_event(pav_event_cb cb, void *user) {
    pthread_mutex_lock(&g_ev_mu);
    g_ev_cb = cb;
    g_ev_user = user;
    pthread_mutex_unlock(&g_ev_mu);
}

static int event_name_ok(const char *name) {
    size_t i;
    size_t n;

    if (name == NULL || name[0] < 'a' || name[0] > 'z') {
        return 0;
    }
    n = strlen(name);
    if (n >= EVENT_NAME_MAX) {
        return 0;
    }
    for (i = 1; i < n; i++) {
        char c = name[i];

        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.')) {
            return 0;
        }
    }
    return 1;
}

int pav_event_post(const char *name, const char *json) {
    char *name_copy;
    char *json_copy;
    size_t json_len;

    if (!event_name_ok(name)) {
        return -1;
    }
    if (json == NULL) {
        json = "{}";
    }
    json_len = strlen(json);
    if (json_len > EVENT_JSON_MAX) {
        return -1;
    }
    pthread_mutex_lock(&g_ev_mu);
    if (g_ev_cb == NULL || !g_ev_alive) {
        g_ev_dropped++;
        pthread_mutex_unlock(&g_ev_mu);
        return 0;
    }
    if (g_ev_count >= EVENT_QUEUE_CAP) {
        g_ev_dropped++;
        pthread_mutex_unlock(&g_ev_mu);
        return -1;
    }
    name_copy = strdup(name);
    json_copy = malloc(json_len + 1);
    if (name_copy == NULL || json_copy == NULL) {
        free(name_copy);
        free(json_copy);
        pthread_mutex_unlock(&g_ev_mu);
        return -1;
    }
    memcpy(json_copy, json, json_len + 1);
    g_ev_q[g_ev_tail].name = name_copy;
    g_ev_q[g_ev_tail].json = json_copy;
    g_ev_tail = (g_ev_tail + 1) % EVENT_QUEUE_CAP;
    g_ev_count++;
    pthread_cond_signal(&g_ev_cv);
    pthread_mutex_unlock(&g_ev_mu);
    return 0;
}

unsigned long long pav_event_dropped(void) {
    unsigned long long n;

    pthread_mutex_lock(&g_ev_mu);
    n = g_ev_dropped;
    pthread_mutex_unlock(&g_ev_mu);
    return n;
}

/* Test hook: forget the log.client rate window. */
void pav_log_client_rate_reset(void) {
    pthread_mutex_lock(&g_client_mu);
    g_client_window = 0;
    g_client_count = 0;
    g_client_dropped = 0;
    pthread_mutex_unlock(&g_client_mu);
}

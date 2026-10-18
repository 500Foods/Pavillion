#define _POSIX_C_SOURCE 200809L

#include "dispatch.h"
#include "logring.h"
#include "themes.h"
#include "window_state.h"
#include "pav_secret.h"

#include "pav_platform.h"
#include "pavillion.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAV_CMD_WORKER 1u
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
static int cmd_log_export(json_t *args, json_t **data, PavError *err);
static int cmd_log_tail(json_t *args, json_t **data, PavError *err);
static int cmd_prefs_get(json_t *args, json_t **data, PavError *err);
static int cmd_prefs_set(json_t *args, json_t **data, PavError *err);
static int cmd_prefs_ui_get(json_t *args, json_t **data, PavError *err);
static int cmd_prefs_ui_set(json_t *args, json_t **data, PavError *err);
static int cmd_status(json_t *args, json_t **data, PavError *err);
static int cmd_sync_now(json_t *args, json_t **data, PavError *err);
#ifndef NDEBUG
int pav_cmd_secret_smoke(json_t *args, json_t **data, PavError *err);
#endif

/* strcmp order. bsearch requires it. */
static const PavCommand k_commands[] = {
    {"activity.cancel", pav_activity_cmd_cancel, 0},
    {"activity.dismiss", pav_activity_cmd_dismiss, 0},
    {"activity.list", pav_activity_cmd_list, 0},
#ifndef NDEBUG
    {"debug.activity_demo", pav_activity_cmd_demo, PAV_CMD_WORKER},
    {"debug.secret_smoke", pav_cmd_secret_smoke, 0},
#endif
    {"log.client", cmd_log_client, 0},
    {"log.export", cmd_log_export, 0},
    {"log.tail", cmd_log_tail, 0},
    {"prefs.get", cmd_prefs_get, 0},
    {"prefs.set", cmd_prefs_set, 0},
    {"prefs.ui.get", cmd_prefs_ui_get, 0},
    {"prefs.ui.set", cmd_prefs_ui_set, 0},
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

    json_object_foreach(args, key, value) {
        if (strcmp(key, "accountId") == 0) {
            if (!json_is_string(value) || json_string_value(value)[0] == '\0') {
                pav_error_set(err, "bad_args", "accountId must be a non-empty string", 0);
                return -1;
            }
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
                pav_error_set(err, "bad_args", "level must be error, warn, info, or debug", 0);
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
        pav_error_set(err, "bad_args", "level must be error, warn, info, or debug", 0);
        return -1;
    }
    if (strlen(message) > LOG_CLIENT_MAX) {
        pav_error_set(err, "too_large", "message is too large", 0);
        return -1;
    }
    clean = replace_newlines(message);
    if (clean == NULL) {
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    pav_ui_log(subsystem, level, clean);
    free(clean);
    *data = json_object();
    if (*data == NULL) {
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    return 0;
}

static int export_level_name(const char *name) {
    return strcmp(name, "ERROR") == 0 || strcmp(name, "WARN") == 0 || strcmp(name, "INFO") == 0 ||
           strcmp(name, "DEBUG") == 0;
}

static int cmd_log_export(json_t *args, json_t **data, PavError *err) {
    const char *key;
    json_t *value;
    const char *path = NULL;
    json_t *filter = NULL;
    const json_t *levels = NULL;
    const json_t *subsystems = NULL;
    const char *text = "";
    char *bytes = NULL;
    size_t bytes_len = 0;
    int lines = 0;
    int rc;
    char parent[4096];
    char buf[512];
    const PavPlatform *platform;
    size_t i;
    size_t n;

    json_object_foreach(args, key, value) {
        if (strcmp(key, "path") == 0) {
            if (!json_is_string(value)) {
                pav_error_set(err, "bad_args", "path must be a string", 0);
                return -1;
            }
            path = json_string_value(value);
        } else if (strcmp(key, "filter") == 0) {
            if (!json_is_object(value)) {
                pav_error_set(err, "bad_args", "filter must be an object", 0);
                return -1;
            }
            filter = value;
        } else {
            snprintf(buf, sizeof(buf), "unknown argument %s", key);
            pav_error_set(err, "bad_args", buf, 0);
            return -1;
        }
    }
    if (path == NULL || filter == NULL) {
        pav_error_set(err, "bad_args", "log.export requires path and filter", 0);
        return -1;
    }
    if (path[0] == '\0' || strlen(path) >= 4096 || strpbrk(path, "\r\n") != NULL || path[strlen(path) - 1] == '/') {
        pav_error_set(err, "bad_args", "path must be an absolute file path", 0);
        return -1;
    }
    if (!pav_path_is_absolute(path)) {
        pav_error_set(err, "bad_args", "path must be an absolute file path", 0);
        return -1;
    }
    if (pav_is_dir(path)) {
        pav_error_set(err, "bad_args", "path is a directory", 0);
        return -1;
    }
    if (pav_path_parent(parent, sizeof(parent), path) != 0 || !pav_is_dir(parent)) {
        pav_error_set(err, "bad_args", "directory does not exist", 0);
        return -1;
    }
    json_object_foreach(filter, key, value) {
        if (strcmp(key, "levels") == 0) {
            if (!json_is_array(value)) {
                pav_error_set(err, "bad_args", "levels must be an array of level names", 0);
                return -1;
            }
            n = json_array_size(value);
            for (i = 0; i < n; i++) {
                const char *item = json_string_value(json_array_get(value, i));

                if (item == NULL || !export_level_name(item)) {
                    pav_error_set(err, "bad_args", "levels must be an array of level names", 0);
                    return -1;
                }
            }
            levels = value;
        } else if (strcmp(key, "subsystems") == 0) {
            if (json_is_null(value)) {
                subsystems = NULL;
            } else if (!json_is_array(value)) {
                pav_error_set(err, "bad_args", "subsystems must be an array of strings or null", 0);
                return -1;
            } else {
                n = json_array_size(value);
                for (i = 0; i < n; i++) {
                    const char *item = json_string_value(json_array_get(value, i));

                    if (item == NULL || item[0] == '\0' || strlen(item) > 128 || strpbrk(item, "\r\n") != NULL) {
                        pav_error_set(err, "bad_args", "subsystems must be an array of strings or null", 0);
                        return -1;
                    }
                }
                subsystems = value;
            }
        } else if (strcmp(key, "text") == 0) {
            if (!json_is_string(value) || strlen(json_string_value(value)) > 2000) {
                pav_error_set(err, "bad_args", "text must be a string of at most 2000 bytes", 0);
                return -1;
            }
            text = json_string_value(value);
        } else {
            snprintf(buf, sizeof(buf), "unknown argument %s", key);
            pav_error_set(err, "bad_args", buf, 0);
            return -1;
        }
    }
    if (levels == NULL) {
        pav_error_set(err, "bad_args", "levels must be an array of level names", 0);
        return -1;
    }
    pav_log_flush();
    rc = pav_logring_export(levels, subsystems, text, &bytes, &bytes_len, &lines);
    if (rc == -2) {
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
    *data = json_object();
    if (*data == NULL || json_object_set_new(*data, "lines", json_integer(lines)) != 0) {
        json_decref(*data);
        *data = NULL;
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    return 0;
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
                pav_error_set(err, "bad_args", "minLevel must be error, warn, info, or debug", 0);
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

char *pav_dispatch_reply(const char *name, json_t *args) {
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
        return pav_reply_error("bad_args", buf, 0);
    }
    if (cmd->handler(args, &data, &err) != 0) {
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

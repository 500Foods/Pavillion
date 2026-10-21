#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
/* _POSIX_C_SOURCE hides flock and LOCK_* in <sys/file.h> on macOS. */
#define _DARWIN_C_SOURCE
#endif

#include "account.h"
#include "dispatch.h"
#include "pav_logdb.h"
#include "logring.h"
#include "pav_activity.h"
#include "pav_cfg.h"
#include "pav_config.h"
#include "pav_net.h"
#include "pav_platform.h"
#include "pav_store.h"
#include "pav_sync.h"
#include "pavillion.h"

#include "window_state.h"

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "pav_compat.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/file.h>
#endif

#define CORE_QUEUE_CAP 256
#define CORE_CMD_STOP 1
#define CORE_CMD_PREFS 2
#define CORE_CMD_ICONS 3
#define CORE_CMD_KIT 4
#define CORE_CMD_UI 5
#define CORE_CMD_CALL 6
#define CORE_CMD_CAPS 7 /* K-Q10: store a Test result; call->args is the payload */
#define CORE_CMD_SYNC 8 /* M-3a: call is a SyncResult; free with pav_sync_result_free */
#define PAV_CALL_JSON_MAX (2u * 1024u * 1024u)

/* No longer in the FFI. The older Unity tests still call pavillion_status. */
typedef struct PavillionStatus {
    char profile_name[256];
    char profile_id[64];
    char state[32];
    char error[512];
} PavillionStatus;

typedef struct CoreQItem {
    int cmd;
    PavCall *call;
} CoreQItem;

static pthread_mutex_t g_core_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_core_cv = PTHREAD_COND_INITIALIZER;
static pthread_t g_core_thread;
static int g_core_alive;
static int g_core_published;
static int g_core_stop;
static CoreQItem g_core_q[CORE_QUEUE_CAP];
static int g_core_q_head;
static int g_core_q_tail;
static int g_core_q_count;
/* Before the first start, status matches the old stub so the shell can read it. */
static PavillionStatus g_status = { .state = "ready" };

/* Captured at start from the environment the shell sets. "1" is true. */
static int g_host_debug;
static int g_host_selftest;

static int env_is_one(const char *name) {
    const char *value = getenv(name);

    return value != NULL && strcmp(value, "1") == 0;
}
static char g_core_workdir[4096];
static PavConfigLoad *g_load;

const char *pav_core_workdir(void) {
    return g_core_workdir;
}
PavConfigLoad *pav_core_load(void) {
    return g_load;
}
static PavStore *g_store;
static PavShellPrefs g_prefs_snap;
static PavShellPrefs g_prefs_req;
static int g_prefs_ready;
static int g_prefs_busy;
static int g_prefs_done;
static int g_prefs_rc;
static unsigned g_prefs_mask;
static char g_prefs_err[512];
static char g_icons_req[65536];
static int g_icons_done;
static int g_icons_busy;
static int g_icons_rc;
static char g_icons_err[512];
static char g_kit_req[256];
static int g_kit_done;
static int g_kit_busy;
static int g_kit_rc;
static char g_kit_err[512];
static char g_ui_req[PAV_UI_JSON_MAX + 1];
static int g_ui_done;
static int g_ui_busy;
static int g_ui_rc;
static char g_ui_err[512];

static void copy_field(char *dst, size_t dst_len, const char *src) {
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

static void core_publish_unlocked(const char *state, const char *name, const char *id, const char *error) {
    memset(&g_status, 0, sizeof(g_status));
    copy_field(g_status.state, sizeof(g_status.state), state != NULL ? state : "error");
    copy_field(g_status.profile_name, sizeof(g_status.profile_name), name);
    copy_field(g_status.profile_id, sizeof(g_status.profile_id), id);
    copy_field(g_status.error, sizeof(g_status.error), error);
    g_core_published = 1;
    pthread_cond_broadcast(&g_core_cv);
}

static void set_start_err(char *err, size_t err_len, const char *msg);
static int log_thread_running(void);
static int core_prepare_prefs(char *err, size_t err_len);
static int core_apply_prefs(char *err, size_t err_len);
static int core_apply_icons(char *err, size_t err_len);
static int core_apply_kit(char *err, size_t err_len);
static int core_apply_ui(char *err, size_t err_len);
static int worker_submit(PavCall *call);
static void worker_drain(void);

/* One process per profile (B Lock, 2026-10-19): an exclusive, non-blocking
   lock on "<profile file>.lock" is held from attach to detach. Linux and
   macOS use flock (per open file, so the kernel drops it if the process
   dies); Windows uses LockFileEx on the same file. */
#define PAV_PROFILE_LOCKED_MSG "This profile is open in another Pavillion window."
static int g_profile_lock_fd = -1;

static int profile_lock_take(const char *profile_path, char *err, size_t err_len) {
    char path[4200];
    int fd;

    if (snprintf(path, sizeof(path), "%s.lock", profile_path) >= (int)sizeof(path)) {
        snprintf(err, err_len, "path is too long");
        return -1;
    }
    fd = open(path, O_RDWR | O_CREAT | O_BINARY, 0600);
    if (fd < 0) {
        snprintf(err, err_len, "cannot open the profile lock");
        return -1;
    }
#ifdef _WIN32
    {
        OVERLAPPED ov;
        HANDLE h = (HANDLE)_get_osfhandle(fd);

        memset(&ov, 0, sizeof(ov));
        if (h == INVALID_HANDLE_VALUE ||
            !LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov)) {
            close(fd);
            snprintf(err, err_len, "%s", PAV_PROFILE_LOCKED_MSG);
            return -1;
        }
    }
#else
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        int busy = errno == EWOULDBLOCK;

        close(fd);
        snprintf(err, err_len, "%s", busy ? PAV_PROFILE_LOCKED_MSG : "cannot lock the profile");
        return -1;
    }
#endif
    g_profile_lock_fd = fd;
    pav_log("core", PAV_DEBUG, 0, "profile lock held");
    return 0;
}

static void profile_lock_release(void) {
    if (g_profile_lock_fd < 0) {
        return;
    }
#ifndef _WIN32
    (void)flock(g_profile_lock_fd, LOCK_UN);
#endif
    close(g_profile_lock_fd); /* closing also releases LockFileEx */
    g_profile_lock_fd = -1;
}

static void core_release_profile(void) {
    pav_activity_shutdown();
    pav_store_close(g_store);
    g_store = NULL;
    pav_config_load_free(g_load);
    g_load = NULL;
    profile_lock_release();
}

/* Flushes queued log lines, then publishes. The caller is the core thread
   and does not hold g_core_mu. */
static void publish_boot(const char *state, const char *name, const char *id, const char *error) {
    if (strcmp(state, "error") == 0) {
        pav_log("core", PAV_ERROR, 1, "profile attach failed: %s", error != NULL ? error : "unknown");
    }
    pav_log_flush();
    pthread_mutex_lock(&g_core_mu);
    core_publish_unlocked(state, name, id, error);
    pthread_mutex_unlock(&g_core_mu);
}

/* Loads the profile and, on success, opens SQLite. Publishes ready or error.
   Does not hold g_core_mu across the file and database calls. */
/* H-4: log files from before log.db are deleted, not imported. A failure
   other than a missing file is one WARN. */
static void remove_old_logs(const char *workdir, const char *id) {
    char logs[4608];
    char path[4700];
    char name[96];
    const char *names[3];
    int failed = 0;
    size_t i;

    if (pav_path_join(logs, sizeof(logs), workdir, "logs") != 0) {
        return;
    }
    snprintf(name, sizeof(name), "%s.log", id);
    names[0] = name;
    names[1] = NULL;
    names[2] = "pavillion.log";
    for (i = 0; i < 3; i++) {
        char gen[100];
        const char *leaf = names[i];

        if (leaf == NULL) {
            snprintf(gen, sizeof(gen), "%s.log.1", id);
            leaf = gen;
        }
        if (pav_path_join(path, sizeof(path), logs, leaf) == 0 && remove(path) != 0 && errno != ENOENT) {
            failed = 1;
        }
    }
    if (rmdir(logs) != 0 && errno != ENOENT && errno != ENOTEMPTY && errno != EEXIST) {
        failed = 1;
    }
    if (failed) {
        pav_log("core", PAV_WARN, 1, "cannot remove old log files in %s", logs);
    }
}

static void core_boot(void) {
    char err[512];
    PavProfile *profile;

    core_release_profile();
    err[0] = '\0';
    if (g_core_workdir[0] == '\0') {
        publish_boot("error", NULL, NULL, "missing workdir");
        return;
    }
    if (pav_config_ensure(g_core_workdir, err, sizeof(err)) != 0) {
        publish_boot("error", NULL, NULL, err[0] != '\0' ? err : "cannot create profile");
        return;
    }
    if (pav_config_load(g_core_workdir, &g_load, err, sizeof(err)) != 0 || g_load == NULL) {
        publish_boot("error", NULL, NULL, err[0] != '\0' ? err : "missing index");
        return;
    }
    if (strcmp(g_load->state, "ready") != 0) {
        publish_boot("error", NULL, NULL, g_load->error);
        return;
    }
    profile = g_load->profile;
    if (profile_lock_take(g_load->profile_path, err, sizeof(err)) != 0) {
        publish_boot("error", profile->name, profile->id, err);
        return;
    }
    pav_log("core", PAV_INFO, 3, "profile %s attaching (engine %s, timezone %s)", profile->id, profile->engine, profile->timezone);
    {
        int filled = pav_account_migrate_email((json_t *)profile->doc);
        if (filled > 0) {
            pav_log("account", PAV_INFO, 1, "%d accounts got missing fields (email, icon, capabilities: K-Q6, K-Q9, K-Q10)", filled);
        }
    }
    if (pav_profile_check_engine(profile, err, sizeof(err)) != 0) {
        publish_boot("error", profile->name, profile->id, err);
        return;
    }
    if (pav_config_prepare(g_core_workdir, profile, err, sizeof(err)) != 0 ||
        pav_store_open(&g_store, profile->engine, profile->resolved_storage, err, sizeof(err)) != 0 ||
        pav_store_migrate(g_store, err, sizeof(err)) != 0) {
        publish_boot("error", profile->name, profile->id, err);
        return;
    }
    if (pav_activity_attach() != 0) {
        publish_boot("error", profile->name, profile->id, "cannot open activity registry");
        return;
    }
    (void)pav_log_set_cap_mb(profile->log_max_mb);
    (void)pav_log_attach_profile(profile->resolved_logs);
    pav_log("core", PAV_DEBUG, 1, "log.db attached, cap %d MB", profile->log_max_mb);
    remove_old_logs(g_core_workdir, profile->id);
    if (core_prepare_prefs(err, sizeof(err)) != 0) {
        publish_boot("error", profile->name, profile->id, err);
        return;
    }
    pav_log("core", PAV_INFO, 2, "core ready: profile %s, %d accounts", profile->id, (int)profile->account_count);
    if (pav_sync_boot((json_t *)profile->doc, profile->id) != 0) {
        pav_log("core", PAV_ERROR, 1, "cannot start sync for %s", profile->id);
    }
    publish_boot("ready", profile->name, profile->id, NULL);
}

/* Moves an old layout into preferences.ui, then publishes the window snapshot.
   The caller is the core thread and does not hold g_core_mu. */
static int core_prepare_prefs(char *err, size_t err_len) {
    PavShellPrefs snap;
    int changed = 0;

    if (g_load == NULL || g_load->profile == NULL || g_load->profile_path[0] == '\0') {
        set_start_err(err, err_len, "missing profile");
        return -1;
    }
    if (pav_profile_migrate_ui(g_load->profile, &changed, err, err_len) != 0) {
        return -1;
    }
    if ((changed || g_load->profile->logs_migrated) &&
        pav_profile_store(g_load->profile_path, g_load->profile, err, err_len) != 0) {
        return -1;
    }
    if (pav_profile_read_shell(g_load->profile, &snap, err, err_len) != 0) {
        return -1;
    }
    pthread_mutex_lock(&g_core_mu);
    g_prefs_snap = snap;
    g_prefs_ready = 1;
    pthread_mutex_unlock(&g_core_mu);
    return 0;
}

static void set_start_err(char *err, size_t err_len, const char *msg) {
    pav_log("core", PAV_ERROR, 1, "start failed: %s", msg);
    if (err == NULL || err_len == 0) {
        return;
    }
    snprintf(err, err_len, "%s", msg);
}

static void core_queue_clear_unlocked(void) {
    memset(g_core_q, 0, sizeof(g_core_q));
    g_core_q_head = 0;
    g_core_q_tail = 0;
    g_core_q_count = 0;
}

/* The caller holds g_core_mu. A full queue returns -1. Stop still wakes the
   thread through g_core_stop, so a full queue does not hang the process. */
static int core_queue_push_unlocked(int cmd, PavCall *call) {
    if (g_core_q_count >= CORE_QUEUE_CAP) {
        return -1;
    }
    g_core_q[g_core_q_tail].cmd = cmd;
    g_core_q[g_core_q_tail].call = call;
    g_core_q_tail = (g_core_q_tail + 1) % CORE_QUEUE_CAP;
    g_core_q_count++;
    pthread_cond_broadcast(&g_core_cv);
    return 0;
}

static int core_queue_pop_unlocked(int *cmd, PavCall **call) {
    if (g_core_q_count == 0) {
        return -1;
    }
    *cmd = g_core_q[g_core_q_head].cmd;
    if (call != NULL) {
        *call = g_core_q[g_core_q_head].call;
    }
    g_core_q[g_core_q_head].call = NULL;
    g_core_q_head = (g_core_q_head + 1) % CORE_QUEUE_CAP;
    g_core_q_count--;
    return 0;
}

/* The caller holds g_core_mu. The waiter frees the PavCall. */
static void fail_call_unlocked(PavCall *call, const char *code, const char *message) {
    if (call == NULL || call->done) {
        return;
    }
    if (call->args != NULL) {
        json_decref(call->args);
        call->args = NULL;
    }
    call->reply = pav_reply_error(code, message, 0);
    call->done = 1;
}

static void core_fail_prefs_unlocked(const char *msg) {
    if (!g_prefs_busy || g_prefs_done) {
        return;
    }
    g_prefs_rc = -1;
    copy_field(g_prefs_err, sizeof(g_prefs_err), msg);
    g_prefs_done = 1;
    pthread_cond_broadcast(&g_core_cv);
}

/* The caller holds g_core_mu. 1 when a window write or a sync poll is waiting.
   *out is the earlier CLOCK_REALTIME deadline. */
static int core_next_deadline(struct timespec *out) {
    struct timespec window_dl;
    struct timespec sync_dl;
    int window_on = pav_window_deadline(&window_dl);
    int sync_on = pav_sync_deadline(&sync_dl);

    if (!window_on && !sync_on) {
        return 0;
    }
    if (window_on && sync_on &&
        (sync_dl.tv_sec < window_dl.tv_sec ||
         (sync_dl.tv_sec == window_dl.tv_sec && sync_dl.tv_nsec < window_dl.tv_nsec))) {
        *out = sync_dl;
    } else {
        *out = window_on ? window_dl : sync_dl;
    }
    return 1;
}

static void *core_thread_main(void *arg) {
    (void)arg;
    core_boot();
    pthread_mutex_lock(&g_core_mu);
    for (;;) {
        int cmd = 0;
        PavCall *call = NULL;

        while (g_core_q_count == 0 && !g_core_stop) {
            struct timespec deadline;

            if (core_next_deadline(&deadline)) {
                if (pthread_cond_timedwait(&g_core_cv, &g_core_mu, &deadline) == ETIMEDOUT) {
                    pthread_mutex_unlock(&g_core_mu);
                    pav_window_fire();
                    pav_sync_on_wake();
                    pthread_mutex_lock(&g_core_mu);
                }
            } else {
                pthread_cond_wait(&g_core_cv, &g_core_mu);
            }
        }
        if (g_core_q_count == 0) {
            break;
        }
        if (core_queue_pop_unlocked(&cmd, &call) != 0) {
            continue;
        }
        if (cmd == CORE_CMD_STOP) {
            break;
        }
        if (cmd == CORE_CMD_CALL) {
            char *reply;

            if (call == NULL) {
                continue;
            }
            if (pav_command_is_worker(call->name)) {
                pthread_mutex_unlock(&g_core_mu);
                if (worker_submit(call) != 0) {
                    pthread_mutex_lock(&g_core_mu);
                    fail_call_unlocked(call, "internal", "cannot start worker");
                    pthread_cond_broadcast(&g_core_cv);
                } else {
                    pthread_mutex_lock(&g_core_mu);
                }
                continue;
            }
            pthread_mutex_unlock(&g_core_mu);
            reply = pav_dispatch_reply(call->name, call->args);
            json_decref(call->args);
            call->args = NULL;
            pthread_mutex_lock(&g_core_mu);
            call->reply = reply;
            call->done = 1;
            pthread_cond_broadcast(&g_core_cv);
            continue;
        }
        if (cmd == CORE_CMD_SYNC) {
            pthread_mutex_unlock(&g_core_mu);
            pav_sync_on_result(call);
            pthread_mutex_lock(&g_core_mu);
            continue;
        }
        if (cmd == CORE_CMD_CAPS) {
            if (call != NULL) {
                json_t *payload = call->args;
                pthread_mutex_unlock(&g_core_mu);
                (void)pav_account_store_capabilities(json_string_value(json_object_get(payload, "id")),
                                                     json_incref(json_object_get(payload, "capabilities")),
                                                     json_string_value(json_object_get(payload, "checked")));
                json_decref(payload);
                free(call);
                pthread_mutex_lock(&g_core_mu);
            }
            continue;
        }
        if (cmd == CORE_CMD_PREFS) {
            pthread_mutex_unlock(&g_core_mu);
            core_apply_prefs(g_prefs_err, sizeof(g_prefs_err));
            pthread_mutex_lock(&g_core_mu);
            g_prefs_done = 1;
            pthread_cond_broadcast(&g_core_cv);
        }
        if (cmd == CORE_CMD_ICONS) {
            pthread_mutex_unlock(&g_core_mu);
            core_apply_icons(g_icons_err, sizeof(g_icons_err));
            pthread_mutex_lock(&g_core_mu);
            g_icons_done = 1;
            pthread_cond_broadcast(&g_core_cv);
        }
        if (cmd == CORE_CMD_KIT) {
            pthread_mutex_unlock(&g_core_mu);
            core_apply_kit(g_kit_err, sizeof(g_kit_err));
            pthread_mutex_lock(&g_core_mu);
            g_kit_done = 1;
            pthread_cond_broadcast(&g_core_cv);
        }
        if (cmd == CORE_CMD_UI) {
            pthread_mutex_unlock(&g_core_mu);
            core_apply_ui(g_ui_err, sizeof(g_ui_err));
            pthread_mutex_lock(&g_core_mu);
            g_ui_done = 1;
            pthread_cond_broadcast(&g_core_cv);
        }
    }
    pthread_mutex_unlock(&g_core_mu);
    worker_drain();
    pthread_mutex_lock(&g_core_mu);
    g_core_alive = 0;
    core_fail_prefs_unlocked("core stopped");
    g_icons_rc = -1;
    g_icons_done = 1;
    g_kit_rc = -1;
    g_kit_done = 1;
    g_ui_rc = -1;
    g_ui_done = 1;
    {
        int left_cmd = 0;
        PavCall *left = NULL;

        while (core_queue_pop_unlocked(&left_cmd, &left) == 0) {
            if (left_cmd == CORE_CMD_SYNC) {
                pav_sync_result_free(left);
            } else if (left != NULL) {
                fail_call_unlocked(left, "not_ready", "core stopped");
            }
        }
    }
    pthread_cond_broadcast(&g_core_cv);
    pthread_mutex_unlock(&g_core_mu);
    pav_window_flush();
    pav_sync_shutdown();
    core_release_profile();
    return NULL;
}

#define WORKER_CAP 8

static pthread_mutex_t g_worker_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_worker_cv = PTHREAD_COND_INITIALIZER;
static void *g_worker_thread;
static int g_worker_alive;
static int g_worker_stop;
static int g_worker_busy;
static PavCall *g_worker_q[WORKER_CAP];
static int g_worker_n;

static void worker_finish_call(PavCall *call, char *reply) {
    pthread_mutex_lock(&g_core_mu);
    call->reply = reply;
    call->done = 1;
    pthread_cond_broadcast(&g_core_cv);
    pthread_mutex_unlock(&g_core_mu);
}

static void *worker_main(void *arg) {
    (void)arg;
    for (;;) {
        PavCall *call;
        char *reply;

        pthread_mutex_lock(&g_worker_mu);
        while (g_worker_n == 0 && !g_worker_stop) {
            pthread_cond_wait(&g_worker_cv, &g_worker_mu);
        }
        if (g_worker_n == 0 && g_worker_stop) {
            g_worker_alive = 0;
            pthread_cond_broadcast(&g_worker_cv);
            pthread_mutex_unlock(&g_worker_mu);
            break;
        }
        call = g_worker_q[0];
        if (g_worker_n > 1) {
            memmove(&g_worker_q[0], &g_worker_q[1], (size_t)(g_worker_n - 1) * sizeof(g_worker_q[0]));
        }
        g_worker_n--;
        g_worker_busy = 1;
        pthread_mutex_unlock(&g_worker_mu);
        reply = pav_dispatch_reply(call->name, call->args);
        json_decref(call->args);
        call->args = NULL;
        worker_finish_call(call, reply);
        pthread_mutex_lock(&g_worker_mu);
        g_worker_busy = 0;
        pthread_cond_broadcast(&g_worker_cv);
        pthread_mutex_unlock(&g_worker_mu);
    }
    return NULL;
}

/* The caller does not hold g_core_mu. The worker publishes call->done. */
static int worker_submit(PavCall *call) {
    pthread_mutex_lock(&g_worker_mu);
    if (!g_worker_alive) {
        g_worker_stop = 0;
        g_worker_n = 0;
        g_worker_busy = 0;
        if (pav_thread_create(&g_worker_thread, worker_main, NULL) != 0) {
            pthread_mutex_unlock(&g_worker_mu);
            return -1;
        }
        g_worker_alive = 1;
    }
    if (g_worker_stop || g_worker_n >= WORKER_CAP) {
        pthread_mutex_unlock(&g_worker_mu);
        return -1;
    }
    g_worker_q[g_worker_n++] = call;
    pthread_cond_signal(&g_worker_cv);
    pthread_mutex_unlock(&g_worker_mu);
    return 0;
}

/* Joins the worker after the calls it already took have published done. */
static void worker_drain(void) {
    void *thread = NULL;

    pthread_mutex_lock(&g_worker_mu);
    if (!g_worker_alive) {
        pthread_mutex_unlock(&g_worker_mu);
        return;
    }
    g_worker_stop = 1;
    pthread_cond_broadcast(&g_worker_cv);
    while (g_worker_alive) {
        pthread_cond_wait(&g_worker_cv, &g_worker_mu);
    }
    thread = g_worker_thread;
    g_worker_thread = NULL;
    pthread_mutex_unlock(&g_worker_mu);
    if (thread != NULL) {
        pav_thread_join(thread);
    }
}

/* The caller is the core thread and does not hold g_core_mu.
   g_prefs_req and g_prefs_mask stay put until this returns: the next apply
   waits on g_prefs_busy. */
static int core_apply_prefs(char *err, size_t err_len) {
    PavShellPrefs stored;
    PavProfile *profile;

    err[0] = '\0';
    if (g_load == NULL || g_load->profile == NULL || g_load->profile_path[0] == '\0') {
        set_start_err(err, err_len, "missing profile");
        g_prefs_rc = -1;
        return -1;
    }
    profile = g_load->profile;
    if (pav_profile_write_shell(profile, &g_prefs_req, g_prefs_mask, err, err_len) != 0 ||
        pav_profile_store(g_load->profile_path, profile, err, err_len) != 0 ||
        pav_profile_read_shell(profile, &stored, err, err_len) != 0) {
        g_prefs_rc = -1;
        if (err[0] == '\0') {
            set_start_err(err, err_len, "cannot write profile");
        }
        return -1;
    }
    pthread_mutex_lock(&g_core_mu);
    g_prefs_snap = stored;
    g_prefs_ready = 1;
    g_prefs_rc = 0;
    err[0] = '\0';
    pthread_mutex_unlock(&g_core_mu);
    return 0;
}

/* The caller is the core thread and does not hold g_core_mu.
   g_icons_req holds the JSON string; the result is written to g_icons_rc. */
static int core_apply_icons(char *err, size_t err_len) {
    PavProfile *profile;

    err[0] = '\0';
    if (g_load == NULL || g_load->profile == NULL || g_load->profile_path[0] == '\0') {
        set_start_err(err, err_len, "missing profile");
        g_icons_rc = -1;
        return -1;
    }
    profile = g_load->profile;
    if (pav_profile_write_icons(profile, g_icons_req, err, err_len) != 0 ||
        pav_profile_store(g_load->profile_path, profile, err, err_len) != 0) {
        g_icons_rc = -1;
        if (err[0] == '\0') {
            set_start_err(err, err_len, "cannot write icons");
        }
        return -1;
    }
    g_icons_rc = 0;
    err[0] = '\0';
    return 0;
}

static int core_apply_kit(char *err, size_t err_len) {
    PavProfile *profile;

    err[0] = '\0';
    if (g_load == NULL || g_load->profile == NULL || g_load->profile_path[0] == '\0') {
        set_start_err(err, err_len, "missing profile");
        g_kit_rc = -1;
        return -1;
    }
    profile = g_load->profile;
    if (pav_profile_write_kit(profile, g_kit_req, err, err_len) != 0 ||
        pav_profile_store(g_load->profile_path, profile, err, err_len) != 0) {
        g_kit_rc = -1;
        if (err[0] == '\0') {
            set_start_err(err, err_len, "cannot write kit");
        }
        return -1;
    }
    g_kit_rc = 0;
    err[0] = '\0';
    return 0;
}

/* The caller is the core thread and does not hold g_core_mu.
   g_ui_req holds the JSON object; the result is written to g_ui_rc. */
static int core_apply_ui(char *err, size_t err_len) {
    PavProfile *profile;

    err[0] = '\0';
    if (g_load == NULL || g_load->profile == NULL || g_load->profile_path[0] == '\0') {
        set_start_err(err, err_len, "missing profile");
        g_ui_rc = -1;
        return -1;
    }
    profile = g_load->profile;
    if (pav_profile_write_ui(profile, g_ui_req, err, err_len) != 0 ||
        pav_profile_store(g_load->profile_path, profile, err, err_len) != 0) {
        g_ui_rc = -1;
        if (err[0] == '\0') {
            set_start_err(err, err_len, "cannot write preferences.ui");
        }
        return -1;
    }
    g_ui_rc = 0;
    err[0] = '\0';
    return 0;
}

int pavillion_api_version(void) {
    return PAVILLION_API_VERSION;
}

int pavillion_start(const char *workdir, char *err, size_t err_len) {
    int rc;
    int started_log = 0;
    const PavPlatform *platform;

    pav_platform_install_host();
    platform = pav_platform();
    if (workdir != NULL && workdir[0] != '\0' &&
        (platform == NULL || platform->mkdir_private == NULL || platform->mkdir_private(workdir) != 0)) {
        set_start_err(err, err_len, "cannot create workdir");
        return -1;
    }
    pav_window_reset();
    pthread_mutex_lock(&g_core_mu);
    if (g_core_alive) {
        pthread_mutex_unlock(&g_core_mu);
        set_start_err(err, err_len, "already running");
        return -1;
    }
    if (workdir == NULL || workdir[0] == '\0') {
        g_core_workdir[0] = '\0';
    } else if (strlen(workdir) >= sizeof(g_core_workdir)) {
        pthread_mutex_unlock(&g_core_mu);
        set_start_err(err, err_len, "path is too long");
        return -1;
    } else {
        memcpy(g_core_workdir, workdir, strlen(workdir) + 1);
    }
    if (g_core_workdir[0] != '\0') {
        (void)pav_log_set_workdir(g_core_workdir);
    }
    g_host_debug = env_is_one("PAVILLION_DEBUG");
    g_host_selftest = env_is_one("PAVILLION_SELFTEST");
    g_core_stop = 0;
    g_core_published = 0;
    g_prefs_ready = 0;
    g_prefs_busy = 0;
    g_prefs_done = 1;
    g_icons_busy = 0;
    g_icons_done = 1;
    g_icons_req[0] = '\0';
    g_kit_busy = 0;
    g_kit_done = 1;
    g_kit_req[0] = '\0';
    g_ui_busy = 0;
    g_ui_done = 1;
    g_ui_req[0] = '\0';
    core_queue_clear_unlocked();
    if (pav_event_thread_start() != 0) {
        pthread_mutex_unlock(&g_core_mu);
        set_start_err(err, err_len, "cannot start events");
        return -1;
    }
    if (!log_thread_running()) {
        if (pav_log_thread_start() != 0) {
            pthread_mutex_unlock(&g_core_mu);
            pav_event_thread_stop();
            set_start_err(err, err_len, "cannot start log");
            return -1;
        }
        started_log = 1;
    }
    rc = pthread_create(&g_core_thread, NULL, core_thread_main, NULL);
    if (rc != 0) {
        pthread_mutex_unlock(&g_core_mu);
        pav_event_thread_stop();
        if (started_log) {
            pav_log_thread_shutdown();
        }
        set_start_err(err, err_len, "cannot start core");
        return -1;
    }
    g_core_alive = 1;
    while (!g_core_published) {
        pthread_cond_wait(&g_core_cv, &g_core_mu);
    }
    pthread_mutex_unlock(&g_core_mu);
    pav_log("core", PAV_INFO, 3, "started: api %d, %s build, workdir %s", PAVILLION_API_VERSION, g_host_debug ? "debug" : "release", g_core_workdir[0] != '\0' ? g_core_workdir : "(none)");
    if (g_host_selftest) {
        pav_log("core", PAV_INFO, 0, "selftest mode");
    }
    return 0;
}

void pavillion_stop(void) {
    pthread_t thread;
    int join = 0;

    pthread_mutex_lock(&g_core_mu);
    if (!g_core_alive) {
        pthread_mutex_unlock(&g_core_mu);
        pav_event_thread_stop();
        pav_log_thread_shutdown();
        return;
    }
    pav_log("core", PAV_INFO, 0, "stopping");
    g_core_stop = 1;
    if (core_queue_push_unlocked(CORE_CMD_STOP, NULL) != 0) {
        pthread_cond_broadcast(&g_core_cv);
    }
    thread = g_core_thread;
    join = 1;
    pthread_mutex_unlock(&g_core_mu);
    if (join) {
        pthread_join(thread, NULL);
    }
    pav_log("core", PAV_DEBUG, 0, "core thread joined");
    /* After the core thread has finished, including its sync shutdown.
       A second stop joins nothing and does not drop the net ref again. */
    pav_net_stop();
    pav_event_thread_stop();
    pav_log("core", PAV_INFO, 0, "stopped");
    pav_log_thread_shutdown();
}

int pavillion_status(PavillionStatus *out) {
    if (out == NULL) {
        return -1;
    }
    pthread_mutex_lock(&g_core_mu);
    *out = g_status;
    pthread_mutex_unlock(&g_core_mu);
    return 0;
}

int pavillion_prefs_get(PavShellPrefs *out) {
    if (out == NULL) {
        return -1;
    }
    pthread_mutex_lock(&g_core_mu);
    if (!g_core_alive || !g_prefs_ready) {
        pthread_mutex_unlock(&g_core_mu);
        return -1;
    }
    *out = g_prefs_snap;
    pthread_mutex_unlock(&g_core_mu);
    return 0;
}

int pavillion_prefs_apply(const PavShellPrefs *in, unsigned mask, char *err, size_t err_len) {
    int rc;

    if (in == NULL) {
        set_start_err(err, err_len, "missing prefs");
        return -1;
    }
    pthread_mutex_lock(&g_core_mu);
    while (g_prefs_busy && g_core_alive && !g_core_stop) {
        pthread_cond_wait(&g_core_cv, &g_core_mu);
    }
    if (!g_core_alive || g_core_stop || !g_prefs_ready) {
        pthread_mutex_unlock(&g_core_mu);
        set_start_err(err, err_len, "core is not running");
        return -1;
    }
    g_prefs_req = *in;
    g_prefs_mask = mask;
    g_prefs_done = 0;
    g_prefs_rc = -1;
    g_prefs_err[0] = '\0';
    g_prefs_busy = 1;
    if (core_queue_push_unlocked(CORE_CMD_PREFS, NULL) != 0) {
        g_prefs_busy = 0;
        g_prefs_done = 1;
        pthread_mutex_unlock(&g_core_mu);
        set_start_err(err, err_len, "core is busy");
        return -1;
    }
    while (!g_prefs_done && g_core_alive) {
        pthread_cond_wait(&g_core_cv, &g_core_mu);
    }
    rc = g_prefs_done ? g_prefs_rc : -1;
    if (rc != 0) {
        set_start_err(err, err_len, g_prefs_err[0] != '\0' ? g_prefs_err : "core stopped");
    }
    g_prefs_busy = 0;
    pthread_cond_broadcast(&g_core_cv);
    pthread_mutex_unlock(&g_core_mu);
    return rc;
}

int pav_core_thread_alive(void) {
    int alive;

    pthread_mutex_lock(&g_core_mu);
    alive = g_core_alive;
    pthread_mutex_unlock(&g_core_mu);
    return alive;
}

int pavillion_icons_get(char **out, char *err, size_t err_len) {
    int rc = 0;

    if (out == NULL) {
        set_start_err(err, err_len, "missing out");
        return -1;
    }
    *out = NULL;
    pthread_mutex_lock(&g_core_mu);
    if (!g_core_alive || g_load == NULL || g_load->profile == NULL) {
        pthread_mutex_unlock(&g_core_mu);
        set_start_err(err, err_len, "core is not running");
        return -1;
    }
    rc = pav_profile_read_icons(g_load->profile, out, err, err_len);
    pthread_mutex_unlock(&g_core_mu);
    return rc;
}

int pavillion_icons_apply(const char *json, char *err, size_t err_len) {
    int rc;

    if (json == NULL) {
        set_start_err(err, err_len, "missing icons");
        return -1;
    }
    pthread_mutex_lock(&g_core_mu);
    if (!g_core_alive || g_core_stop || !g_prefs_ready) {
        pthread_mutex_unlock(&g_core_mu);
        set_start_err(err, err_len, "core is not running");
        return -1;
    }
    while (g_icons_busy && g_core_alive && !g_core_stop) {
        pthread_cond_wait(&g_core_cv, &g_core_mu);
    }
    if (!g_core_alive || g_core_stop || !g_prefs_ready) {
        pthread_mutex_unlock(&g_core_mu);
        set_start_err(err, err_len, "core is not running");
        return -1;
    }
    if (strlen(json) >= sizeof(g_icons_req)) {
        pthread_mutex_unlock(&g_core_mu);
        set_start_err(err, err_len, "icons payload too large");
        return -1;
    }
    memcpy(g_icons_req, json, strlen(json) + 1);
    g_icons_done = 0;
    g_icons_rc = -1;
    g_icons_err[0] = '\0';
    g_icons_busy = 1;
    if (core_queue_push_unlocked(CORE_CMD_ICONS, NULL) != 0) {
        g_icons_busy = 0;
        g_icons_done = 1;
        pthread_mutex_unlock(&g_core_mu);
        set_start_err(err, err_len, "core is busy");
        return -1;
    }
    while (!g_icons_done && g_core_alive) {
        pthread_cond_wait(&g_core_cv, &g_core_mu);
    }
    rc = g_icons_done ? g_icons_rc : -1;
    if (rc != 0) {
        set_start_err(err, err_len, g_icons_err[0] != '\0' ? g_icons_err : "core stopped");
    }
    g_icons_busy = 0;
    pthread_cond_broadcast(&g_core_cv);
    pthread_mutex_unlock(&g_core_mu);
    return rc;
}

int pavillion_kit_get(char *out, size_t out_len, char *err, size_t err_len) {
    int rc = 0;

    if (out == NULL || out_len == 0) {
        set_start_err(err, err_len, "missing out");
        return -1;
    }
    out[0] = '\0';
    pthread_mutex_lock(&g_core_mu);
    if (!g_core_alive || g_load == NULL || g_load->profile == NULL) {
        pthread_mutex_unlock(&g_core_mu);
        set_start_err(err, err_len, "core is not running");
        return -1;
    }
    rc = pav_profile_read_kit(g_load->profile, out, out_len, err, err_len);
    pthread_mutex_unlock(&g_core_mu);
    return rc;
}

int pavillion_kit_apply(const char *kit, char *err, size_t err_len) {
    int rc;

    if (kit == NULL) {
        set_start_err(err, err_len, "missing kit");
        return -1;
    }
    pthread_mutex_lock(&g_core_mu);
    if (!g_core_alive || g_core_stop || !g_prefs_ready) {
        pthread_mutex_unlock(&g_core_mu);
        set_start_err(err, err_len, "core is not running");
        return -1;
    }
    while (g_kit_busy && g_core_alive && !g_core_stop) {
        pthread_cond_wait(&g_core_cv, &g_core_mu);
    }
    if (!g_core_alive || g_core_stop || !g_prefs_ready) {
        pthread_mutex_unlock(&g_core_mu);
        set_start_err(err, err_len, "core is not running");
        return -1;
    }
    if (strlen(kit) >= sizeof(g_kit_req)) {
        pthread_mutex_unlock(&g_core_mu);
        set_start_err(err, err_len, "kit id is too long");
        return -1;
    }
    memcpy(g_kit_req, kit, strlen(kit) + 1);
    g_kit_done = 0;
    g_kit_rc = -1;
    g_kit_err[0] = '\0';
    g_kit_busy = 1;
    if (core_queue_push_unlocked(CORE_CMD_KIT, NULL) != 0) {
        g_kit_busy = 0;
        g_kit_done = 1;
        pthread_mutex_unlock(&g_core_mu);
        set_start_err(err, err_len, "core is busy");
        return -1;
    }
    while (!g_kit_done && g_core_alive) {
        pthread_cond_wait(&g_core_cv, &g_core_mu);
    }
    rc = g_kit_done ? g_kit_rc : -1;
    if (rc != 0) {
        set_start_err(err, err_len, g_kit_err[0] != '\0' ? g_kit_err : "core stopped");
    }
    g_kit_busy = 0;
    pthread_cond_broadcast(&g_core_cv);
    pthread_mutex_unlock(&g_core_mu);
    return rc;
}

int pavillion_ui_get(char **json_out, char *err, size_t err_len) {
    int rc;

    if (json_out == NULL) {
        set_start_err(err, err_len, "missing out");
        return -1;
    }
    *json_out = NULL;
    pthread_mutex_lock(&g_core_mu);
    if (!g_core_alive || g_load == NULL || g_load->profile == NULL) {
        pthread_mutex_unlock(&g_core_mu);
        set_start_err(err, err_len, "core is not running");
        return -1;
    }
    rc = pav_profile_read_ui(g_load->profile, json_out, err, err_len);
    pthread_mutex_unlock(&g_core_mu);
    return rc;
}

int pavillion_ui_set(const char *json, char *err, size_t err_len) {
    int rc;
    size_t len;

    if (json == NULL) {
        set_start_err(err, err_len, "missing arguments");
        return -1;
    }
    len = strlen(json);
    if (len > PAV_UI_JSON_MAX) {
        set_start_err(err, err_len, "preferences.ui is too large");
        return -1;
    }
    pthread_mutex_lock(&g_core_mu);
    if (!g_core_alive || g_core_stop || !g_prefs_ready) {
        pthread_mutex_unlock(&g_core_mu);
        set_start_err(err, err_len, "core is not running");
        return -1;
    }
    while (g_ui_busy && g_core_alive && !g_core_stop) {
        pthread_cond_wait(&g_core_cv, &g_core_mu);
    }
    if (!g_core_alive || g_core_stop || !g_prefs_ready) {
        pthread_mutex_unlock(&g_core_mu);
        set_start_err(err, err_len, "core is not running");
        return -1;
    }
    memcpy(g_ui_req, json, len + 1);
    g_ui_done = 0;
    g_ui_rc = -1;
    g_ui_err[0] = '\0';
    g_ui_busy = 1;
    if (core_queue_push_unlocked(CORE_CMD_UI, NULL) != 0) {
        g_ui_busy = 0;
        g_ui_done = 1;
        pthread_mutex_unlock(&g_core_mu);
        set_start_err(err, err_len, "core is busy");
        return -1;
    }
    while (!g_ui_done && g_core_alive) {
        pthread_cond_wait(&g_core_cv, &g_core_mu);
    }
    rc = g_ui_done ? g_ui_rc : -1;
    if (rc != 0) {
        set_start_err(err, err_len, g_ui_err[0] != '\0' ? g_ui_err : "core stopped");
    }
    g_ui_busy = 0;
    pthread_cond_broadcast(&g_core_cv);
    pthread_mutex_unlock(&g_core_mu);
    return rc;
}

int pav_on_core_thread(void) {
    int same = 0;

    pthread_mutex_lock(&g_core_mu);
    if (g_core_alive) {
        same = pthread_equal(pthread_self(), g_core_thread);
    }
    pthread_mutex_unlock(&g_core_mu);
    return same ? 1 : 0;
}

static int command_name_ok(const char *name) {
    size_t i;
    size_t n;

    if (name == NULL || name[0] < 'a' || name[0] > 'z') {
        return 0;
    }
    n = strlen(name);
    if (n >= 64) {
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

static char *reply_or_fail(char **result_json, const char *code, const char *message) {
    char *text = pav_reply_error(code, message, 0);

    if (text == NULL) {
        return NULL;
    }
    *result_json = text;
    return text;
}

int pavillion_call(const char *cmd, const char *args_json, char **result_json) {
    json_error_t jerr;
    json_t *args;
    PavCall *call;

    if (result_json == NULL) {
        return -1;
    }
    *result_json = NULL;
    if (!command_name_ok(cmd)) {
        return reply_or_fail(result_json, "bad_args", "missing or invalid command") == NULL ? -1 : 0;
    }
    if (args_json == NULL || strlen(args_json) > PAV_CALL_JSON_MAX) {
        const char *code = args_json == NULL ? "bad_args" : "too_large";
        const char *message = args_json == NULL ? "arguments are not an object" : "arguments are too large";

        return reply_or_fail(result_json, code, message) == NULL ? -1 : 0;
    }
    args = json_loads(args_json, 0, &jerr);
    if (args == NULL || !json_is_object(args)) {
        json_decref(args);
        return reply_or_fail(result_json, "bad_args", "arguments are not an object") == NULL ? -1 : 0;
    }
    call = calloc(1, sizeof(*call));
    if (call == NULL) {
        json_decref(args);
        return -1;
    }
    memcpy(call->name, cmd, strlen(cmd) + 1);
    call->args = args;
    pthread_mutex_lock(&g_core_mu);
    if (!g_core_alive || g_core_stop) {
        pthread_mutex_unlock(&g_core_mu);
        json_decref(args);
        free(call);
        return reply_or_fail(result_json, "not_ready", "core is not running") == NULL ? -1 : 0;
    }
    if (core_queue_push_unlocked(CORE_CMD_CALL, call) != 0) {
        pthread_mutex_unlock(&g_core_mu);
        json_decref(args);
        free(call);
        return reply_or_fail(result_json, "internal", "command queue is full") == NULL ? -1 : 0;
    }
    while (!call->done && g_core_alive) {
        pthread_cond_wait(&g_core_cv, &g_core_mu);
    }
    if (!call->done) {
        pthread_mutex_unlock(&g_core_mu);
        json_decref(call->args);
        free(call);
        return reply_or_fail(result_json, "not_ready", "core stopped") == NULL ? -1 : 0;
    }
    *result_json = call->reply;
    pthread_mutex_unlock(&g_core_mu);
    free(call);
    return *result_json != NULL ? 0 : -1;
}

static int profile_is_ready(PavError *err) {
    if (g_load == NULL || g_load->profile == NULL || strcmp(g_load->state, "ready") != 0) {
        pav_error_set(err, "not_ready", "profile is not ready", 0);
        return 0;
    }
    return 1;
}

static int fail_profile(PavError *err, const char *message) {
    const char *text = message != NULL && message[0] != '\0' ? message : "cannot update preferences";

    if (strstr(text, "too large") != NULL) {
        pav_error_set(err, "too_large", text, 0);
    } else if (strstr(text, "forbidden") != NULL) {
        pav_error_set(err, "forbidden", text, 0);
    } else if (strstr(text, "not running") != NULL || strstr(text, "missing profile") != NULL) {
        pav_error_set(err, "not_ready", text, 0);
    } else if (strstr(text, "out of memory") != NULL) {
        pav_error_set(err, "internal", text, 0);
    } else {
        pav_error_set(err, "bad_args", text, 0);
    }
    return -1;
}

static int json_set_or_oom(json_t *obj, const char *key, json_t *value, PavError *err) {
    if (obj == NULL || value == NULL || json_object_set_new(obj, key, value) != 0) {
        json_decref(value);
        json_decref(obj);
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    return 0;
}

int pav_core_status_data(json_t **data, PavError *err) {
    PavillionStatus status;
    json_t *obj;
    int host_debug;
    int host_selftest;

    if (data == NULL) {
        pav_error_set(err, "internal", "missing data", 0);
        return -1;
    }
    *data = NULL;
    pthread_mutex_lock(&g_core_mu);
    status = g_status;
    host_debug = g_host_debug;
    host_selftest = g_host_selftest;
    pthread_mutex_unlock(&g_core_mu);
    obj = json_object();
    if (json_set_or_oom(obj, "profileName", json_string(status.profile_name), err) != 0 ||
        json_set_or_oom(obj, "profileId", json_string(status.profile_id), err) != 0 ||
        json_set_or_oom(obj, "state", json_string(status.state), err) != 0 ||
        json_set_or_oom(obj, "error", json_string(status.error), err) != 0 ||
        json_set_or_oom(obj, "debug", host_debug ? json_true() : json_false(), err) != 0 ||
        json_set_or_oom(obj, "selftest", host_selftest ? json_true() : json_false(), err) != 0) {
        return -1;
    }
    *data = obj;
    return 0;
}

static char *read_capped(const char *path, size_t cap) {
    FILE *file = fopen(path, "rb");
    long n;
    char *buf;
    size_t got;

    if (file == NULL) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    n = ftell(file);
    if (n < 0 || (size_t)n > cap) {
        fclose(file);
        return NULL;
    }
    rewind(file);
    buf = malloc((size_t)n + 1);
    if (buf == NULL) {
        fclose(file);
        return NULL;
    }
    got = fread(buf, 1, (size_t)n, file);
    fclose(file);
    if (got != (size_t)n) {
        free(buf);
        return NULL;
    }
    buf[n] = '\0';
    return buf;
}

static int log_level_name_ok(const char *name) {
    return name != NULL && (strcmp(name, "error") == 0 || strcmp(name, "warn") == 0 || strcmp(name, "info") == 0 ||
                            strcmp(name, "debug") == 0 || strcmp(name, "trace") == 0);
}

/* Writes the index log_level and applies it. Does not touch the profile. */
static int store_index_log_level(const char *level, char *err, size_t err_len) {
    char path[4096];
    char *text;
    char *rewritten = NULL;
    PavIndex *index = NULL;
    const PavPlatform *platform;

    if (g_load == NULL || g_core_workdir[0] == '\0') {
        cfg_err(err, err_len, "missing workdir");
        return -1;
    }
    if (!log_level_name_ok(level)) {
        cfg_err(err, err_len, "logLevel must be error, warn, info, debug, or trace");
        return -1;
    }
    if (pav_path_join(path, sizeof(path), g_core_workdir, "Pavillion.json") != 0) {
        cfg_err(err, err_len, "path is too long");
        return -1;
    }
    text = read_capped(path, 1024u * 1024u);
    if (text == NULL) {
        cfg_err(err, err_len, "cannot read index");
        return -1;
    }
    if (pav_index_parse(text, &index, err, err_len) != 0) {
        free(text);
        return -1;
    }
    free(text);
    if (cfg_set_str(index->doc, "log_level", level, err, err_len) != 0) {
        pav_index_free(index);
        return -1;
    }
    if (pav_index_rewrite(index, &rewritten) != 0) {
        pav_index_free(index);
        cfg_err(err, err_len, "out of memory");
        return -1;
    }
    pav_index_free(index);
    platform = pav_platform();
    if (platform == NULL || platform->write_private == NULL ||
        platform->write_private(path, rewritten, strlen(rewritten)) != 0) {
        free(rewritten);
        cfg_err(err, err_len, "cannot write index");
        return -1;
    }
    free(rewritten);
    memset(g_load->log_level, 0, sizeof(g_load->log_level));
    memcpy(g_load->log_level, level, strlen(level));
    return 0;
}

int pav_core_prefs_get_data(json_t **data, PavError *err) {
    PavProfile *profile;
    json_t *obj;

    if (data == NULL) {
        pav_error_set(err, "internal", "missing data", 0);
        return -1;
    }
    *data = NULL;
    if (!profile_is_ready(err)) {
        return -1;
    }
    profile = g_load->profile;
    obj = json_object();
    if (json_set_or_oom(obj, "timezone", json_string(profile->timezone), err) != 0 ||
        json_set_or_oom(obj, "weekStart", json_string(profile->week_start), err) != 0 ||
        json_set_or_oom(obj, "timeFormat", json_string(profile->time_format), err) != 0 ||
        json_set_or_oom(obj, "theme", json_string(profile->theme), err) != 0 ||
        json_set_or_oom(obj, "density", json_string(profile->density), err) != 0 ||
        json_set_or_oom(obj, "windowMaximized", profile->window_maximized ? json_true() : json_false(), err) !=
            0 ||
        json_set_or_oom(obj, "windowWidth", json_integer(profile->window_width), err) != 0 ||
        json_set_or_oom(obj, "windowHeight", json_integer(profile->window_height), err) != 0 ||
        json_set_or_oom(obj, "windowX",
                        profile->window_x_set ? json_integer(profile->window_x) : json_null(), err) != 0 ||
        json_set_or_oom(obj, "windowY",
                        profile->window_y_set ? json_integer(profile->window_y) : json_null(), err) != 0 ||
        json_set_or_oom(obj, "chrome", json_string(profile->chrome), err) != 0 ||
        json_set_or_oom(obj, "logLevel",
                        json_string(g_load->log_level[0] != '\0' ? g_load->log_level : "info"), err) != 0) {
        return -1;
    }
    *data = obj;
    return 0;
}

static int take_pref_string(json_t *value, size_t cap, const char **out, const char *wire, PavError *err) {
    const char *text;
    char buf[128];

    if (!json_is_string(value)) {
        snprintf(buf, sizeof(buf), "%s must be a string", wire);
        pav_error_set(err, "bad_args", buf, 0);
        return -1;
    }
    text = json_string_value(value);
    if (text == NULL || text[0] == '\0' || strlen(text) >= cap) {
        snprintf(buf, sizeof(buf), "%s is empty or too long", wire);
        pav_error_set(err, "bad_args", buf, 0);
        return -1;
    }
    *out = text;
    return 0;
}

static int take_bool(json_t *value, int *out, const char *wire, PavError *err) {
    char buf[128];

    if (!json_is_boolean(value)) {
        snprintf(buf, sizeof(buf), "%s must be a boolean", wire);
        pav_error_set(err, "bad_args", buf, 0);
        return -1;
    }
    *out = json_is_true(value) ? 1 : 0;
    return 0;
}

static int take_int(json_t *value, int *out, const char *wire, PavError *err) {
    json_int_t number;
    char buf[128];

    if (!json_is_integer(value)) {
        snprintf(buf, sizeof(buf), "%s must be an integer", wire);
        pav_error_set(err, "bad_args", buf, 0);
        return -1;
    }
    number = json_integer_value(value);
    if (number < -32000 || number > 32000) {
        snprintf(buf, sizeof(buf), "%s is out of range", wire);
        pav_error_set(err, "bad_args", buf, 0);
        return -1;
    }
    *out = (int)number;
    return 0;
}

static int take_int_or_null(json_t *value, int *set, int *coord, const char *wire, PavError *err) {
    if (json_is_null(value)) {
        *set = 0;
        *coord = 0;
        return 0;
    }
    if (take_int(value, coord, wire, err) != 0) {
        return -1;
    }
    *set = 1;
    return 0;
}

static int apply_pref_string(PavProfile *profile, const char *file_key, const char *value, char *dst,
                             size_t cap, char *errbuf, size_t err_len) {
    json_t *prefs = cfg_obj(profile->doc, "preferences", 0);

    if (prefs == NULL) {
        snprintf(errbuf, err_len, "%s", "missing profile fields");
        return -1;
    }
    if (cfg_set_str(prefs, file_key, value, errbuf, err_len) != 0) {
        return -1;
    }
    memset(dst, 0, cap);
    memcpy(dst, value, strlen(value));
    return 0;
}

/* H-4: names the changed preferences.ui keys as dotted paths. Values are
   never logged. Stops at depth 4 and at the buffer end ("..."). */
static void ui_diff_add(char *buf, size_t len, const char *path) {
    size_t used = strlen(buf);

    if (used + 4 >= len || strstr(buf, "...") != NULL) {
        return;
    }
    if (used + strlen(path) + 2 >= len - 4) {
        snprintf(buf + used, len - used, "%s...", used > 0 ? "," : "");
        return;
    }
    snprintf(buf + used, len - used, "%s%s", used > 0 ? "," : "", path);
}

static void ui_diff_keys(json_t *old, json_t *cur, const char *prefix, int depth, char *buf, size_t len) {
    const char *key;
    json_t *value;
    char path[256];

    if (json_is_object(old) && json_is_object(cur) && depth < 4) {
        json_object_foreach(cur, key, value) {
            snprintf(path, sizeof(path), "%s%s%s", prefix, prefix[0] != '\0' ? "." : "", key);
            ui_diff_keys(json_object_get(old, key), value, path, depth + 1, buf, len);
        }
        json_object_foreach(old, key, value) {
            if (json_object_get(cur, key) == NULL) {
                snprintf(path, sizeof(path), "%s%s%s", prefix, prefix[0] != '\0' ? "." : "", key);
                ui_diff_add(buf, len, path);
            }
        }
        return;
    }
    if (old == NULL || cur == NULL || !json_equal(old, cur)) {
        ui_diff_add(buf, len, prefix[0] != '\0' ? prefix : "ui");
    }
}

int pav_core_prefs_set(json_t *args, json_t **data, PavError *err) {
    PavProfile *profile;
    PavShellPrefs shell;
    const char *timezone = NULL;
    const char *week_start = NULL;
    const char *time_format = NULL;
    const char *theme = NULL;
    const char *density = NULL;
    const char *log_level = NULL;
    const char *key;
    json_t *value;
    char errbuf[512];
    char buf[128];
    int window_touched = 0;
    int profile_touched = 0;
    int any = 0;

    if (data == NULL) {
        pav_error_set(err, "internal", "missing data", 0);
        return -1;
    }
    *data = NULL;
    if (!profile_is_ready(err)) {
        return -1;
    }
    profile = g_load->profile;
    if (json_object_size(args) == 0) {
        pav_error_set(err, "bad_args", "nothing to save", 0);
        return -1;
    }
    errbuf[0] = '\0';
    if (pav_profile_read_shell(profile, &shell, errbuf, sizeof(errbuf)) != 0) {
        return fail_profile(err, errbuf);
    }
    json_object_foreach(args, key, value) {
        any = 1;
        if (strcmp(key, "timezone") == 0) {
            if (take_pref_string(value, sizeof(profile->timezone), &timezone, key, err) != 0) {
                return -1;
            }
        } else if (strcmp(key, "weekStart") == 0) {
            if (take_pref_string(value, sizeof(profile->week_start), &week_start, key, err) != 0) {
                return -1;
            }
        } else if (strcmp(key, "timeFormat") == 0) {
            if (take_pref_string(value, sizeof(profile->time_format), &time_format, key, err) != 0) {
                return -1;
            }
        } else if (strcmp(key, "theme") == 0) {
            if (take_pref_string(value, sizeof(profile->theme), &theme, key, err) != 0) {
                return -1;
            }
        } else if (strcmp(key, "density") == 0) {
            if (take_pref_string(value, sizeof(profile->density), &density, key, err) != 0) {
                return -1;
            }
        } else if (strcmp(key, "windowMaximized") == 0) {
            if (take_bool(value, &shell.window_maximized, key, err) != 0) {
                return -1;
            }
            window_touched = 1;
        } else if (strcmp(key, "windowWidth") == 0) {
            if (take_int(value, &shell.window_width, key, err) != 0) {
                return -1;
            }
            window_touched = 1;
        } else if (strcmp(key, "windowHeight") == 0) {
            if (take_int(value, &shell.window_height, key, err) != 0) {
                return -1;
            }
            window_touched = 1;
        } else if (strcmp(key, "windowX") == 0) {
            if (take_int_or_null(value, &shell.window_x_set, &shell.window_x, key, err) != 0) {
                return -1;
            }
            window_touched = 1;
        } else if (strcmp(key, "windowY") == 0) {
            if (take_int_or_null(value, &shell.window_y_set, &shell.window_y, key, err) != 0) {
                return -1;
            }
            window_touched = 1;
        } else if (strcmp(key, "chrome") == 0) {
            const char *chrome = NULL;

            if (take_pref_string(value, sizeof(shell.chrome), &chrome, key, err) != 0) {
                return -1;
            }
            memset(shell.chrome, 0, sizeof(shell.chrome));
            memcpy(shell.chrome, chrome, strlen(chrome));
            window_touched = 1;
        } else if (strcmp(key, "logLevel") == 0) {
            if (!json_is_string(value) || !log_level_name_ok(json_string_value(value))) {
                pav_error_set(err, "bad_args", "logLevel must be error, warn, info, debug, or trace", 0);
                return -1;
            }
            log_level = json_string_value(value);
        } else {
            snprintf(buf, sizeof(buf), "unknown preference %s", key);
            pav_error_set(err, "bad_args", buf, 0);
            return -1;
        }
    }
    if (!any) {
        pav_error_set(err, "bad_args", "nothing to save", 0);
        return -1;
    }
    errbuf[0] = '\0';
    if (log_level != NULL && store_index_log_level(log_level, errbuf, sizeof(errbuf)) != 0) {
        pav_error_set(err, "internal", errbuf[0] != '\0' ? errbuf : "cannot write index", 0);
        return -1;
    }
    profile_touched = window_touched || timezone != NULL || week_start != NULL || time_format != NULL ||
                      theme != NULL || density != NULL;
    if (profile_touched && window_touched &&
        pav_profile_write_shell(profile, &shell, PAV_PREFS_WINDOW, errbuf, sizeof(errbuf)) != 0) {
        return fail_profile(err, errbuf);
    }
    if (profile_touched &&
        ((timezone != NULL &&
          apply_pref_string(profile, "timezone", timezone, profile->timezone, sizeof(profile->timezone), errbuf,
                            sizeof(errbuf)) != 0) ||
         (week_start != NULL && apply_pref_string(profile, "week_start", week_start, profile->week_start,
                                                  sizeof(profile->week_start), errbuf, sizeof(errbuf)) != 0) ||
         (time_format != NULL && apply_pref_string(profile, "time_format", time_format, profile->time_format,
                                                   sizeof(profile->time_format), errbuf, sizeof(errbuf)) != 0) ||
         (theme != NULL &&
          apply_pref_string(profile, "theme", theme, profile->theme, sizeof(profile->theme), errbuf,
                            sizeof(errbuf)) != 0) ||
         (density != NULL && apply_pref_string(profile, "density", density, profile->density,
                                               sizeof(profile->density), errbuf, sizeof(errbuf)) != 0))) {
        return fail_profile(err, errbuf);
    }
    if (profile_touched && pav_profile_store(g_load->profile_path, profile, errbuf, sizeof(errbuf)) != 0) {
        return fail_profile(err, errbuf[0] != '\0' ? errbuf : "cannot write profile");
    }
    {
        char keys[256];
        const char *k;
        json_t *v;

        keys[0] = '\0';
        json_object_foreach(args, k, v) {
            (void)v;
            ui_diff_add(keys, sizeof(keys), k);
        }
        pav_log("config", PAV_DEBUG, 1, "prefs set: %s", keys[0] != '\0' ? keys : "nothing");
    }
    if (pav_profile_read_shell(profile, &shell, errbuf, sizeof(errbuf)) != 0) {
        return fail_profile(err, errbuf);
    }
    pthread_mutex_lock(&g_core_mu);
    g_prefs_snap = shell;
    g_prefs_ready = 1;
    pthread_mutex_unlock(&g_core_mu);
    return pav_core_prefs_get_data(data, err);
}

int pav_core_ui_get_data(json_t **data, PavError *err) {
    char *text = NULL;
    char errbuf[512];
    json_error_t jerr;
    json_t *obj;

    if (data == NULL) {
        pav_error_set(err, "internal", "missing data", 0);
        return -1;
    }
    *data = NULL;
    if (!profile_is_ready(err)) {
        return -1;
    }
    errbuf[0] = '\0';
    if (pav_profile_read_ui(g_load->profile, &text, errbuf, sizeof(errbuf)) != 0) {
        return fail_profile(err, errbuf);
    }
    obj = json_loads(text, JSON_PRESERVE_ORDER, &jerr);
    free(text);
    if (obj == NULL || !json_is_object(obj)) {
        json_decref(obj);
        pav_error_set(err, "internal", "preferences.ui is not an object", 0);
        return -1;
    }
    *data = obj;
    return 0;
}

/* log.trim {mb}: writes preferences.log_max_mb (H-4 Lock, Retention). */
int pav_core_set_log_max_mb(int mb, PavError *err) {
    json_t *prefs;
    char errbuf[512];

    if (mb < 1 || mb > 100) {
        pav_error_set(err, "bad_args", "mb must be an integer from 1 to 100", 0);
        return -1;
    }
    if (!profile_is_ready(err)) {
        return -1;
    }
    errbuf[0] = '\0';
    prefs = json_object_get((json_t *)g_load->profile->doc, "preferences");
    if (!json_is_object(prefs) || json_object_set_new(prefs, "log_max_mb", json_integer(mb)) != 0) {
        return fail_profile(err, "cannot write preferences.log_max_mb");
    }
    g_load->profile->log_max_mb = mb;
    if (pav_profile_store(g_load->profile_path, g_load->profile, errbuf, sizeof(errbuf)) != 0) {
        return fail_profile(err, errbuf);
    }
    pav_log("config", PAV_INFO, 1, "log size cap set to %d MB", mb);
    return 0;
}

int pav_core_ui_set(json_t *args, json_t **data, PavError *err) {
    char *compact;
    char changed[480];
    char errbuf[512];

    if (data == NULL) {
        pav_error_set(err, "internal", "missing data", 0);
        return -1;
    }
    *data = NULL;
    if (!profile_is_ready(err)) {
        return -1;
    }
    compact = json_dumps(args, JSON_COMPACT | JSON_PRESERVE_ORDER);
    if (compact == NULL) {
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    if (strlen(compact) > PAV_UI_JSON_MAX) {
        free(compact);
        pav_error_set(err, "too_large", "preferences.ui is too large", 0);
        return -1;
    }
    {
        json_t *prefs_obj = json_object_get((json_t *)g_load->profile->doc, "preferences");
        json_t *old_ui = json_is_object(prefs_obj) ? json_object_get(prefs_obj, "ui") : NULL;

        changed[0] = '\0';
        ui_diff_keys(old_ui, args, "", 0, changed, sizeof(changed));
    }
    errbuf[0] = '\0';
    if (pav_profile_write_ui(g_load->profile, compact, errbuf, sizeof(errbuf)) != 0 ||
        pav_profile_store(g_load->profile_path, g_load->profile, errbuf, sizeof(errbuf)) != 0) {
        free(compact);
        return fail_profile(err, errbuf[0] != '\0' ? errbuf : "cannot write preferences.ui");
    }
    free(compact);
    pav_log("config", PAV_DEBUG, 1, "prefs saved: %s", changed[0] != '\0' ? changed : "no changes");
    return pav_core_ui_get_data(data, err);
}

#define PAV_LOG_MSG_MAX 4096
#define LOG_DEST_NONE 0
#define LOG_DEST_WORKDIR 1
#define LOG_DEST_PROFILE 2
#define LOG_DEST_EXPLICIT 3
/* log.db writer (H-4 Lock): about every 100 ms or 500 rows. */
#define LOG_DB_FLUSH_MS 100
#define LOG_DB_FLUSH_ROWS 500
#define LOG_ERROR_WAIT_MS 250
/* Lines kept between workdir and attach. The oldest go first past this. */
#define LOG_PENDING_MAX 10000

#define LOG_REQ_NONE 0
#define LOG_REQ_STATS 1
#define LOG_REQ_PREVIEW 2
#define LOG_REQ_TRIM 3
#define LOG_REQ_CALL 4

typedef struct LogNode {
    struct LogNode *next;
    char *line;
    char *subsystem;
    char *msg;
    int64_t ts;
    uint64_t ticket;
    int level;
} LogNode;

typedef struct BatchNode {
    struct BatchNode *next;
    uint64_t seq;
    char *line;
} BatchNode;

typedef struct DbRow {
    struct DbRow *next;
    uint64_t seq;
    uint64_t ticket;
    int64_t ts;
    int level;
    char *subsystem;
    char *text;
} DbRow;

static pthread_mutex_t g_log_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_log_cv = PTHREAD_COND_INITIALIZER;
static void *g_log_thread;
static pthread_t g_log_tid;
static FILE *g_log_out;
static LogNode *g_q_head;
static LogNode *g_q_tail;
static BatchNode *g_batch_head;
static BatchNode *g_batch_tail;
static int64_t g_batch_started_ms;
static DbRow *g_db_head;
static DbRow *g_db_tail;
static size_t g_db_count;
static int64_t g_db_started_ms;
static int g_db_urgent;
static uint64_t g_ticket_next;
static uint64_t g_ticket_done;
static sqlite3 *g_logdb;
static int64_t g_log_session;
static int g_log_cap_mb = 10;
static char g_log_switch_path[4096];
static int g_log_dest;

static int g_log_alive;
static int g_log_shutdown;
static int g_log_flush_req;
static int g_log_switch_req;
static int g_log_req;
static int g_log_req_mb;
static int g_log_req_rc;
static PavLogStats g_log_req_out;
static PavLogDbFn g_log_req_fn;
static void *g_log_req_ctx;
static uint64_t g_log_seq;

/* Same specifier count as Hydrogen's count_format_specifiers. */
static size_t count_format_specifiers(const char *format) {
    size_t count = 0;
    const char *ptr = format;

    if (format == NULL) {
        return 0;
    }

    while ((ptr = strchr(ptr, '%')) != NULL) {
        ptr++;
        if (*ptr == '%') {
            ptr++;
            continue;
        }
        while (*ptr == '-' || *ptr == '+' || *ptr == ' ' || *ptr == '#' || *ptr == '0' || *ptr == '\'') {
            ptr++;
        }
        if (*ptr == '*') {
            ptr++;
        } else {
            while (isdigit((unsigned char)*ptr)) {
                ptr++;
            }
        }
        if (*ptr == '.') {
            ptr++;
            if (*ptr == '*') {
                ptr++;
            } else {
                while (isdigit((unsigned char)*ptr)) {
                    ptr++;
                }
            }
        }
        if (*ptr == 'h' || *ptr == 'l' || *ptr == 'L' || *ptr == 'z' || *ptr == 'j' || *ptr == 't') {
            if ((*ptr == 'l' && *(ptr + 1) == 'l') || (*ptr == 'h' && *(ptr + 1) == 'h')) {
                ptr += 2;
            } else {
                ptr++;
            }
        }
        if (strchr("diouxXeEfFgGaAcspn", *ptr) != NULL) {
            count++;
            ptr++;
        }
    }
    return count;
}

/* Every level is captured (H-4 Lock, Levels). The index log_level is only
   the Log view's default filter. */
static int level_kept(int level) {
    return level >= PAV_ERROR && level <= PAV_TRACE;
}

static const char *level_name(int level) {
    switch (level) {
    case PAV_ERROR:
        return "ERROR";
    case PAV_WARN:
        return "WARN";
    case PAV_INFO:
        return "INFO";
    case PAV_DEBUG:
        return "DEBUG";
    case PAV_TRACE:
        return "TRACE";
    default:
        return "?????";
    }
}

static int64_t mono_ms(void) {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (int64_t)ts.tv_sec * 1000 + (int64_t)(ts.tv_nsec / 1000000L);
}

static int batch_due_unlocked(void) {
    if (g_batch_head == NULL) {
        return 0;
    }
    return mono_ms() - g_batch_started_ms >= 100;
}

static int batch_wait_ms_unlocked(void) {
    int64_t left = 100 - (mono_ms() - g_batch_started_ms);

    if (left < 1) {
        return 1;
    }
    if (left > 100) {
        return 100;
    }
    return (int)left;
}

static void abs_after_ms(struct timespec *ts, int ms) {
    if (clock_gettime(CLOCK_REALTIME, ts) != 0) {
        ts->tv_sec = 0;
        ts->tv_nsec = 0;
        return;
    }
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec += ts->tv_nsec / 1000000000L;
        ts->tv_nsec %= 1000000000L;
    }
}

/* stderr only when no database can hold the line: before any workdir exists
   or after log.db failed to open, every level (H Lock, 2026-10-19: logs go
   to the database only). The test sink keeps stderr quiet. */
static int stderr_too_unlocked(int level) {
    if (g_log_dest == LOG_DEST_EXPLICIT) {
        return 0;
    }
    if (g_log_dest == LOG_DEST_NONE) {
        return 1;
    }
    (void)level;
    return 0;
}

static void emit_line_unlocked(int level, const char *line) {
    if (g_log_out != NULL) {
        fputs(line, g_log_out);
        fflush(g_log_out);
    }
    if (stderr_too_unlocked(level)) {
        fputs(line, stderr);
        fflush(stderr);
    }
}

static void batch_add_unlocked(uint64_t seq, const char *line) {
    BatchNode *node = malloc(sizeof(*node));
    char *copy;

    if (node == NULL) {
        return;
    }
    copy = strdup(line);
    if (copy == NULL) {
        free(node);
        return;
    }
    node->seq = seq;
    node->line = copy;
    node->next = NULL;
    if (g_batch_tail != NULL) {
        g_batch_tail->next = node;
    } else {
        g_batch_head = node;
        g_batch_started_ms = mono_ms();
    }
    g_batch_tail = node;
}

static void free_rows(DbRow *head) {
    while (head != NULL) {
        DbRow *next = head->next;

        free(head->subsystem);
        free(head->text);
        free(head);
        head = next;
    }
}

static void db_add_unlocked(uint64_t seq, uint64_t ticket, int64_t ts, int level, const char *subsystem,
                            const char *msg) {
    DbRow *row = calloc(1, sizeof(*row));

    if (row == NULL) {
        return;
    }
    row->subsystem = strdup(subsystem);
    row->text = strdup(msg);
    if (row->subsystem == NULL || row->text == NULL) {
        free_rows(row);
        return;
    }
    row->seq = seq;
    row->ticket = ticket;
    row->ts = ts;
    row->level = level;
    if (g_db_tail != NULL) {
        g_db_tail->next = row;
    } else {
        g_db_head = row;
        g_db_started_ms = mono_ms();
    }
    g_db_tail = row;
    g_db_count++;
    if (level == PAV_ERROR) {
        g_db_urgent = 1;
    }
    while (g_log_dest == LOG_DEST_WORKDIR && g_db_count > LOG_PENDING_MAX) {
        DbRow *old = g_db_head;

        g_db_head = old->next;
        old->next = NULL;
        free_rows(old);
        g_db_count--;
    }
}

static void drop_rows_unlocked(void) {
    free_rows(g_db_head);
    g_db_head = NULL;
    g_db_tail = NULL;
    g_db_count = 0;
    g_db_urgent = 0;
    g_ticket_done = g_ticket_next;
}

/* The caller holds g_log_mu. The line is written now and copied into the
   ring, and kept for log.db once a workdir exists. */
static void commit_unlocked(const LogNode *node) {
    uint64_t seq;

    emit_line_unlocked(node->level, node->line);
    seq = ++g_log_seq;
    (void)pav_logring_push(seq, node->line);
    if (g_log_alive) {
        batch_add_unlocked(seq, node->line);
    }
    if (g_log_dest == LOG_DEST_WORKDIR || g_log_dest == LOG_DEST_PROFILE) {
        db_add_unlocked(seq, node->ticket, node->ts, node->level, node->subsystem, node->msg);
    }
}

static void free_node(LogNode *node) {
    if (node != NULL) {
        free(node->line);
        free(node->subsystem);
        free(node->msg);
        free(node);
    }
}

/* The caller holds g_log_mu. A running log thread writes the line later.
   Otherwise the caller writes it now, so a line before start is not lost.
   Returns the line's ticket, or 0. */
/* H-4 repeat collapse (Lock): the previous line, and how many identical
   lines followed it. Guarded by g_log_mu. */
#define LOG_DUP_IDLE_MS 1000
static int g_dup_valid;
static int g_dup_level;
static char g_dup_subsystem[32];
static char g_dup_msg[PAV_LOG_MSG_MAX];
static unsigned long g_dup_n;
static int64_t g_dup_last_ms;

static uint64_t deliver_unlocked(int level, const char *subsystem, const char *msg);
static int64_t mono_ms(void);

static void dup_flush_unlocked(void) {
    char text[96];

    if (g_dup_valid && g_dup_n > 0) {
        snprintf(text, sizeof(text), "previous line repeated %lu times", g_dup_n);
        (void)deliver_unlocked(g_dup_level, g_dup_subsystem, text);
    }
    g_dup_n = 0;
    g_dup_valid = 0;
}

/* Returns 1 when the line repeats the previous one and was only counted. */
static int dup_check_unlocked(int level, const char *subsystem, const char *msg) {
    if (g_dup_valid && level == g_dup_level && strcmp(subsystem, g_dup_subsystem) == 0 &&
        strcmp(msg, g_dup_msg) == 0) {
        g_dup_n++;
        g_dup_last_ms = mono_ms();
        pthread_cond_broadcast(&g_log_cv);
        return 1;
    }
    dup_flush_unlocked();
    g_dup_valid = 1;
    g_dup_level = level;
    snprintf(g_dup_subsystem, sizeof(g_dup_subsystem), "%s", subsystem);
    snprintf(g_dup_msg, sizeof(g_dup_msg), "%s", msg);
    g_dup_n = 0;
    g_dup_last_ms = mono_ms();
    return 0;
}

static int dup_idle_due_unlocked(void) {
    return g_dup_valid && g_dup_n > 0 && mono_ms() - g_dup_last_ms >= LOG_DUP_IDLE_MS;
}

static uint64_t deliver_unlocked(int level, const char *subsystem, const char *msg) {
    char stamp[32];
    LogNode *node;
    int wrote;

    if (!level_kept(level)) {
        return 0;
    }
    node = calloc(1, sizeof(*node));
    if (node == NULL) {
        return 0;
    }
    node->ts = pav_now_ms();
    if (pav_format_utc(stamp, sizeof(stamp), node->ts) != 0) {
        snprintf(stamp, sizeof(stamp), "0000-00-00T00:00:00.000Z");
    }
    wrote = snprintf(NULL, 0, "%s %-5s %s  %s\n", stamp, level_name(level), subsystem, msg);
    if (wrote < 0) {
        free(node);
        return 0;
    }
    node->line = malloc((size_t)wrote + 1);
    node->subsystem = strdup(subsystem);
    node->msg = strdup(msg);
    if (node->line == NULL || node->subsystem == NULL || node->msg == NULL) {
        free_node(node);
        return 0;
    }
    snprintf(node->line, (size_t)wrote + 1, "%s %-5s %s  %s\n", stamp, level_name(level), subsystem, msg);
    node->level = level;
    node->ticket = ++g_ticket_next;
    if (g_log_alive) {
        if (g_q_tail != NULL) {
            g_q_tail->next = node;
        } else {
            g_q_head = node;
        }
        g_q_tail = node;
        pthread_cond_signal(&g_log_cv);
        return node->ticket;
    }
    commit_unlocked(node);
    free_node(node);
    return 0;
}

static void free_batch(BatchNode *head) {
    while (head != NULL) {
        BatchNode *next = head->next;

        free(head->line);
        free(head);
        head = next;
    }
}

static BatchNode *take_batch_unlocked(void) {
    BatchNode *head = g_batch_head;

    g_batch_head = NULL;
    g_batch_tail = NULL;
    return head;
}

static void post_lines(json_t *lines) {
    json_t *root;
    char *text;
    size_t n;
    size_t i;
    size_t mid;
    json_t *left;
    json_t *right;

    if (lines == NULL || json_array_size(lines) == 0) {
        json_decref(lines);
        return;
    }
    root = json_object();
    if (root == NULL || json_object_set(root, "lines", lines) != 0) {
        json_decref(root);
        json_decref(lines);
        return;
    }
    text = json_dumps(root, JSON_COMPACT);
    n = json_array_size(lines);
    if (text != NULL && strlen(text) <= 60000) {
        pav_event_post("log.lines", text);
        free(text);
        json_decref(root);
        json_decref(lines);
        return;
    }
    free(text);
    json_decref(root);
    if (n <= 1) {
        json_decref(lines);
        return;
    }
    mid = n / 2;
    left = json_array();
    right = json_array();
    if (left == NULL || right == NULL) {
        json_decref(left);
        json_decref(right);
        json_decref(lines);
        return;
    }
    for (i = 0; i < n; i++) {
        json_array_append(i < mid ? left : right, json_array_get(lines, i));
    }
    json_decref(lines);
    post_lines(left);
    post_lines(right);
}

static void post_batch(BatchNode *head) {
    json_t *lines = json_array();
    BatchNode *node;

    if (lines == NULL) {
        free_batch(head);
        return;
    }
    for (node = head; node != NULL; node = node->next) {
        json_t *obj = pav_logring_line_json(node->seq, node->line);

        if (obj != NULL && json_array_append_new(lines, obj) != 0) {
            json_decref(obj);
        }
    }
    free_batch(head);
    post_lines(lines);
}

static void emit_batch_outside_lock(void) {
    BatchNode *batch = take_batch_unlocked();

    pthread_mutex_unlock(&g_log_mu);
    post_batch(batch);
    pthread_mutex_lock(&g_log_mu);
}

/* log.lines waits while lines may still be renumbered by attach. */
static int batch_held_unlocked(void) {
    return g_log_dest == LOG_DEST_WORKDIR && !g_log_shutdown;
}

static int db_due_unlocked(void) {
    if (g_db_head == NULL || g_log_dest != LOG_DEST_PROFILE || g_logdb == NULL) {
        return 0;
    }
    return g_log_shutdown || g_log_flush_req || g_db_urgent || g_db_count >= LOG_DB_FLUSH_ROWS ||
           mono_ms() - g_db_started_ms >= LOG_DB_FLUSH_MS;
}

/* The caller holds g_log_mu and is the log thread. One transaction, written
   outside the lock. */
static void flush_db_outside_lock(void) {
    DbRow *head = g_db_head;
    size_t count = g_db_count;
    PavLogRow *rows;
    DbRow *row;
    uint64_t last = 0;
    size_t i = 0;

    g_db_head = NULL;
    g_db_tail = NULL;
    g_db_count = 0;
    g_db_urgent = 0;
    pthread_mutex_unlock(&g_log_mu);
    rows = calloc(count > 0 ? count : 1, sizeof(*rows));
    for (row = head; row != NULL; row = row->next) {
        if (rows != NULL && i < count) {
            rows[i].seq = (int64_t)row->seq;
            rows[i].ts = row->ts;
            rows[i].level = row->level;
            rows[i].subsystem = row->subsystem;
            rows[i].text = row->text;
            i++;
        }
        last = row->ticket;
    }
    if (rows != NULL) {
        (void)pav_logdb_insert(g_logdb, g_log_session, rows, i);
    }
    free(rows);
    free_rows(head);
    pthread_mutex_lock(&g_log_mu);
    if (last > g_ticket_done) {
        g_ticket_done = last;
    }
    pthread_cond_broadcast(&g_log_cv);
}

/* The caller holds g_log_mu and is the log thread. */
static void attach_db_unlocked(void) {
    char path[sizeof(g_log_switch_path)];
    sqlite3 *db = NULL;
    int64_t session = 0;
    int64_t max_seq = 0;
    int rc;

    memcpy(path, g_log_switch_path, sizeof(path));
    pthread_mutex_unlock(&g_log_mu);
    rc = pav_logdb_open(path, &db, &session, &max_seq, NULL, 0);
    pthread_mutex_lock(&g_log_mu);
    if (rc != 0) {
        /* No record this run: stderr and the ring only. */
        drop_rows_unlocked();
        g_log_dest = LOG_DEST_NONE;
        return;
    }
    if (g_logdb != NULL) {
        pav_logdb_close(g_logdb);
    }
    {
        uint64_t first = g_db_head != NULL ? g_db_head->seq : g_log_seq + 1;
        uint64_t base = (uint64_t)max_seq + 1;

        if (base > first) {
            uint64_t offset = base - first;
            DbRow *row;
            BatchNode *node;

            for (row = g_db_head; row != NULL; row = row->next) {
                row->seq += offset;
            }
            for (node = g_batch_head; node != NULL; node = node->next) {
                if (node->seq >= first) {
                    node->seq += offset;
                }
            }
            pav_logring_renumber(first, offset);
            g_log_seq += offset;
        }
    }
    g_logdb = db;
    g_log_session = session;
    g_log_dest = LOG_DEST_PROFILE;
    g_db_started_ms = mono_ms() - LOG_DB_FLUSH_MS;
}

/* The caller holds g_log_mu and is the log thread. */
static void run_req_outside_lock(void) {
    int req = g_log_req;
    int mb = g_log_req_mb;
    PavLogStats out;
    int rc = -1;

    memset(&out, 0, sizeof(out));
    if (mb == 0) {
        mb = g_log_cap_mb;
    }
    pthread_mutex_unlock(&g_log_mu);
    if (g_logdb != NULL) {
        int64_t cap = (int64_t)mb * 1024 * 1024;

        if (req == LOG_REQ_STATS) {
            rc = pav_logdb_stats(g_logdb, &out);
        } else if (req == LOG_REQ_PREVIEW) {
            rc = pav_logdb_trim_preview(g_logdb, cap, &out);
        } else if (req == LOG_REQ_TRIM) {
            rc = pav_logdb_trim(g_logdb, cap, g_log_session, &out);
        } else if (req == LOG_REQ_CALL) {
            rc = g_log_req_fn(g_log_req_ctx, g_logdb, g_log_session);
        }
    }
    pthread_mutex_lock(&g_log_mu);
    if (rc == 0 && req == LOG_REQ_TRIM) {
        g_log_cap_mb = mb;
    }
    out.cap_mb = g_log_cap_mb;
    g_log_req_out = out;
    g_log_req_rc = rc;
    g_log_req = LOG_REQ_NONE;
    pthread_cond_broadcast(&g_log_cv);
}

/* Shutdown: the rows are written, then trimmed to the cap (H-4 Lock). */
static void close_db_outside_lock(void) {
    sqlite3 *db = g_logdb;
    int64_t cap = (int64_t)g_log_cap_mb * 1024 * 1024;
    int64_t session = g_log_session;

    g_logdb = NULL;
    g_log_dest = LOG_DEST_NONE;
    pthread_mutex_unlock(&g_log_mu);
    (void)pav_logdb_trim(db, cap, session, NULL);
    pav_logdb_close(db);
    pthread_mutex_lock(&g_log_mu);
}

static void *log_thread_main(void *arg) {
    (void)arg;
    pthread_mutex_lock(&g_log_mu);
    g_log_tid = pthread_self();
    for (;;) {
        if (g_q_head != NULL && g_db_count < LOG_DB_FLUSH_ROWS) {
            LogNode *node = g_q_head;

            g_q_head = node->next;
            if (g_q_head == NULL) {
                g_q_tail = NULL;
            }
            commit_unlocked(node);
            free_node(node);
            if (batch_due_unlocked() && !batch_held_unlocked()) {
                emit_batch_outside_lock();
            }
            continue;
        }
        if (g_log_switch_req) {
            attach_db_unlocked();
            g_log_switch_req = 0;
            pthread_cond_broadcast(&g_log_cv);
            continue;
        }
        if (db_due_unlocked() || (g_db_head != NULL && g_q_head != NULL && g_log_dest == LOG_DEST_PROFILE)) {
            flush_db_outside_lock();
            continue;
        }
        if (g_q_head != NULL) {
            /* Pending rows at the cap with no log.db yet: keep committing. */
            LogNode *node = g_q_head;

            g_q_head = node->next;
            if (g_q_head == NULL) {
                g_q_tail = NULL;
            }
            commit_unlocked(node);
            free_node(node);
            continue;
        }
        if (g_batch_head != NULL && !batch_held_unlocked() && (g_log_shutdown || batch_due_unlocked())) {
            emit_batch_outside_lock();
            continue;
        }
        if (dup_idle_due_unlocked() || ((g_log_flush_req || g_log_shutdown) && g_dup_valid && g_dup_n > 0)) {
            dup_flush_unlocked();
            continue;
        }
        if (g_log_req != LOG_REQ_NONE) {
            run_req_outside_lock();
            continue;
        }
        if (g_log_flush_req) {
            g_log_flush_req = 0;
            pthread_cond_broadcast(&g_log_cv);
            continue;
        }
        if (g_log_shutdown) {
            if (g_logdb != NULL) {
                close_db_outside_lock();
            }
            g_log_alive = 0;
            g_ticket_done = g_ticket_next;
            pthread_cond_broadcast(&g_log_cv);
            pthread_mutex_unlock(&g_log_mu);
            return NULL;
        }
        {
            int wait = -1;

            if (g_batch_head != NULL && !batch_held_unlocked()) {
                wait = batch_wait_ms_unlocked();
            }
            if (g_dup_valid && g_dup_n > 0) {
                int64_t left = LOG_DUP_IDLE_MS - (mono_ms() - g_dup_last_ms);
                int dup_wait = left < 1 ? 1 : (int)left;

                if (wait < 0 || dup_wait < wait) {
                    wait = dup_wait;
                }
            }
            if (g_db_head != NULL && g_log_dest == LOG_DEST_PROFILE) {
                int64_t left = LOG_DB_FLUSH_MS - (mono_ms() - g_db_started_ms);
                int db_wait = left < 1 ? 1 : (int)left;

                if (wait < 0 || db_wait < wait) {
                    wait = db_wait;
                }
            }
            if (wait >= 0) {
                struct timespec ts;

                abs_after_ms(&ts, wait);
                pthread_cond_timedwait(&g_log_cv, &g_log_mu, &ts);
                continue;
            }
        }
        pthread_cond_wait(&g_log_cv, &g_log_mu);
    }
}

static int log_thread_running(void) {
    int alive;

    pthread_mutex_lock(&g_log_mu);
    alive = g_log_alive;
    pthread_mutex_unlock(&g_log_mu);
    return alive;
}

int pav_log_thread_start(void) {
    pthread_mutex_lock(&g_log_mu);
    if (g_log_alive) {
        pthread_mutex_unlock(&g_log_mu);
        return -1;
    }
    g_dup_valid = 0;
    g_dup_n = 0;
    g_log_shutdown = 0;
    if (pav_thread_create(&g_log_thread, log_thread_main, NULL) != 0) {
        pthread_mutex_unlock(&g_log_mu);
        return -1;
    }
    g_log_alive = 1;
    pthread_mutex_unlock(&g_log_mu);
    return 0;
}

void pav_log_thread_shutdown(void) {
    void *thread;

    pthread_mutex_lock(&g_log_mu);
    if (!g_log_alive) {
        pthread_mutex_unlock(&g_log_mu);
        return;
    }
    g_log_shutdown = 1;
    thread = g_log_thread;
    pthread_cond_broadcast(&g_log_cv);
    pthread_mutex_unlock(&g_log_mu);
    pav_thread_join(thread);
}

void pav_log_flush(void) {
    pthread_mutex_lock(&g_log_mu);
    if (!g_log_alive) {
        pthread_mutex_unlock(&g_log_mu);
        return;
    }
    g_log_flush_req = 1;
    pthread_cond_broadcast(&g_log_cv);
    while (g_log_flush_req && g_log_alive) {
        pthread_cond_wait(&g_log_cv, &g_log_mu);
    }
    g_log_flush_req = 0;
    pthread_mutex_unlock(&g_log_mu);
}

int pav_log_set_workdir(const char *workdir) {
    pthread_mutex_lock(&g_log_mu);
    if (g_log_dest == LOG_DEST_EXPLICIT || g_log_dest == LOG_DEST_PROFILE) {
        pthread_mutex_unlock(&g_log_mu);
        return 0;
    }
    if (workdir == NULL || workdir[0] == '\0') {
        if (g_log_dest == LOG_DEST_WORKDIR) {
            drop_rows_unlocked();
            g_log_dest = LOG_DEST_NONE;
        }
        pthread_mutex_unlock(&g_log_mu);
        return 0;
    }
    g_log_dest = LOG_DEST_WORKDIR;
    pthread_mutex_unlock(&g_log_mu);
    return 0;
}

int pav_log_attach_profile(const char *path) {
    int rc;

    pthread_mutex_lock(&g_log_mu);
    if (path == NULL || path[0] == '\0' || strlen(path) >= sizeof(g_log_switch_path)) {
        pthread_mutex_unlock(&g_log_mu);
        return -1;
    }
    if (g_log_dest == LOG_DEST_EXPLICIT) {
        pthread_mutex_unlock(&g_log_mu);
        return 0;
    }
    if (!g_log_alive) {
        pthread_mutex_unlock(&g_log_mu);
        return -1;
    }
    memcpy(g_log_switch_path, path, strlen(path) + 1);
    g_log_switch_req = 1;
    pthread_cond_broadcast(&g_log_cv);
    while (g_log_switch_req && g_log_alive) {
        pthread_cond_wait(&g_log_cv, &g_log_mu);
    }
    rc = g_log_dest == LOG_DEST_PROFILE ? 0 : -1;
    pthread_mutex_unlock(&g_log_mu);
    return rc;
}

int pav_log_set_output(const char *path) {
    pthread_mutex_lock(&g_log_mu);
    if (g_log_out != NULL) {
        fclose(g_log_out);
    }
    g_log_out = NULL;
    if (g_log_dest != LOG_DEST_PROFILE) {
        drop_rows_unlocked();
        g_log_dest = LOG_DEST_NONE;
    }
    if (path != NULL) {
        g_log_out = fopen(path, "wb");
        if (g_log_out == NULL) {
            pthread_mutex_unlock(&g_log_mu);
            return -1;
        }
        g_log_dest = LOG_DEST_EXPLICIT;
    }
    pthread_mutex_unlock(&g_log_mu);
    return 0;
}

int pav_log_set_cap_mb(int mb) {
    if (mb < 1 || mb > 100) {
        return -1;
    }
    pthread_mutex_lock(&g_log_mu);
    g_log_cap_mb = mb;
    pthread_mutex_unlock(&g_log_mu);
    return 0;
}

static int log_request(int req, int mb, PavLogStats *out) {
    int rc;

    if (out == NULL || (mb != 0 && (mb < 1 || mb > 100)) || (req == LOG_REQ_PREVIEW && mb == 0)) {
        return -1;
    }
    pthread_mutex_lock(&g_log_mu);
    if (!g_log_alive || g_log_dest != LOG_DEST_PROFILE ||
        pthread_equal(pthread_self(), g_log_tid)) {
        pthread_mutex_unlock(&g_log_mu);
        return -1;
    }
    while (g_log_req != LOG_REQ_NONE && g_log_alive) {
        pthread_cond_wait(&g_log_cv, &g_log_mu);
    }
    g_log_req = req;
    g_log_req_mb = mb;
    g_log_req_rc = -1;
    pthread_cond_broadcast(&g_log_cv);
    while (g_log_req == req && g_log_alive) {
        pthread_cond_wait(&g_log_cv, &g_log_mu);
    }
    rc = g_log_req_rc;
    *out = g_log_req_out;
    pthread_mutex_unlock(&g_log_mu);
    return rc;
}

int pav_log_db_call(PavLogDbFn fn, void *ctx) {
    int rc;

    if (fn == NULL) {
        return -1;
    }
    pthread_mutex_lock(&g_log_mu);
    if (!g_log_alive || g_log_dest != LOG_DEST_PROFILE || pthread_equal(pthread_self(), g_log_tid)) {
        pthread_mutex_unlock(&g_log_mu);
        return -2;
    }
    while (g_log_req != LOG_REQ_NONE && g_log_alive) {
        pthread_cond_wait(&g_log_cv, &g_log_mu);
    }
    if (!g_log_alive || g_log_dest != LOG_DEST_PROFILE) {
        pthread_mutex_unlock(&g_log_mu);
        return -2;
    }
    g_log_req = LOG_REQ_CALL;
    g_log_req_fn = fn;
    g_log_req_ctx = ctx;
    g_log_req_rc = -1;
    pthread_cond_broadcast(&g_log_cv);
    while (g_log_req == LOG_REQ_CALL && g_log_alive) {
        pthread_cond_wait(&g_log_cv, &g_log_mu);
    }
    rc = g_log_req_rc;
    pthread_mutex_unlock(&g_log_mu);
    return rc;
}

int pav_log_stats(PavLogStats *out) {
    return log_request(LOG_REQ_STATS, 0, out);
}

int pav_log_trim_preview(int mb, PavLogStats *out) {
    return log_request(LOG_REQ_PREVIEW, mb, out);
}

int pav_log_trim(int mb, PavLogStats *out) {
    return log_request(LOG_REQ_TRIM, mb, out);
}

/* An ERROR waits up to about 250 ms for its log.db commit (H-4 Lock). */
static void wait_ticket_unlocked(uint64_t ticket) {
    struct timespec ts;

    if (ticket == 0 || !g_log_alive || g_log_dest != LOG_DEST_PROFILE ||
        pthread_equal(pthread_self(), g_log_tid)) {
        return;
    }
    abs_after_ms(&ts, LOG_ERROR_WAIT_MS);
    pthread_cond_broadcast(&g_log_cv);
    while (g_ticket_done < ticket && g_log_alive && g_log_dest == LOG_DEST_PROFILE) {
        if (pthread_cond_timedwait(&g_log_cv, &g_log_mu, &ts) != 0) {
            break;
        }
    }
}

/* H-4 redaction, applied to every line in pav_log (core and log.client):
   the rest of an Authorization header and any Basic/Bearer credential, a URL
   query string, the value after a secret-named key (secret, password, token,
   key) followed by = or :, and at INFO and above any email address. Callers
   should still pass ids, not content; this is the safety net. */
static int redact_word_end(char c) {
    return c == '\0' || isspace((unsigned char)c) || c == ',' || c == ';' || c == '"' || c == '\'' ||
           c == ')' || c == ']' || c == '}' || c == '&';
}

static int redact_ci_prefix(const char *s, const char *word) {
    size_t i;

    for (i = 0; word[i] != '\0'; i++) {
        if (tolower((unsigned char)s[i]) != word[i]) {
            return 0;
        }
    }
    return 1;
}

static int redact_email_char(char c) {
    return isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-' || c == '+' || c == '%';
}

static void pav_log_redact(char *msg, size_t cap, int level) {
    static const char *const secret_words[] = {"password", "secret", "token", "key"};
    char out[PAV_LOG_MSG_MAX];
    size_t r = 0;
    size_t w = 0;
    size_t n = strlen(msg);

#define REDACT_PUT(str) do { const char *p_ = (str); while (*p_ != '\0' && w + 1 < sizeof(out)) out[w++] = *p_++; } while (0)
    while (r < n && w + 1 < sizeof(out)) {
        const char *s = msg + r;
        size_t i;
        int done = 0;

        if (redact_ci_prefix(s, "authorization:")) {
            REDACT_PUT("Authorization: [redacted]");
            while (r < n && msg[r] != '\n') {
                r++;
            }
            continue;
        }
        if ((r == 0 || !isalnum((unsigned char)msg[r - 1])) &&
            (redact_ci_prefix(s, "basic ") || redact_ci_prefix(s, "bearer "))) {
            size_t word = redact_ci_prefix(s, "basic ") ? 6 : 7;
            size_t j = r + word;

            size_t k = j;

            while (k < n && !redact_word_end(msg[k])) {
                k++;
            }
            /* A credential is one long token; "basic apply" is not. */
            if (k - j >= 8) {
                while (r < j && w + 1 < sizeof(out)) {
                    out[w++] = msg[r++];
                }
                REDACT_PUT("[redacted]");
                r = j;
                while (r < n && !redact_word_end(msg[r])) {
                    r++;
                }
                continue;
            }
        }
        if ((redact_ci_prefix(s, "https://") || redact_ci_prefix(s, "http://"))) {
            size_t scheme = redact_ci_prefix(s, "https://") ? 8 : 7;
            size_t j = r + scheme;
            size_t at = 0;

            while (j < n && !isspace((unsigned char)msg[j]) && msg[j] != '/' && msg[j] != '?' && msg[j] != '#') {
                if (msg[j] == '@') {
                    at = j;
                }
                j++;
            }
            if (at != 0) {
                /* user:password@host: the credentials never reach the log. */
                while (r < (size_t)(s - msg) + scheme && w + 1 < sizeof(out)) {
                    out[w++] = msg[r++];
                }
                REDACT_PUT("[redacted]@");
                r = at + 1;
            }
            while (r < n && !isspace((unsigned char)msg[r]) && msg[r] != '?' && msg[r] != '#' && w + 1 < sizeof(out)) {
                out[w++] = msg[r++];
            }
            if (r < n && (msg[r] == '?' || msg[r] == '#')) {
                REDACT_PUT(msg[r] == '?' ? "?[redacted]" : "#[redacted]");
                while (r < n && !isspace((unsigned char)msg[r])) {
                    r++;
                }
            }
            continue;
        }
        for (i = 0; i < sizeof(secret_words) / sizeof(secret_words[0]) && !done; i++) {
            size_t len = strlen(secret_words[i]);
            size_t j = r + len;

            if (!redact_ci_prefix(s, secret_words[i])) {
                continue;
            }
            /* A secret word ending a key name: "apiKey=", "password: ", "\"token\":". */
            if (j < n && (msg[j] == '"' || msg[j] == '\'')) {
                j++;
            }
            while (j < n && msg[j] == ' ') {
                j++;
            }
            if (j < n && (msg[j] == '=' || msg[j] == ':')) {
                j++;
                while (j < n && (msg[j] == ' ' || msg[j] == '"' || msg[j] == '\'')) {
                    j++;
                }
                if (j < n && !redact_word_end(msg[j])) {
                    while (r < j && w + 1 < sizeof(out)) {
                        out[w++] = msg[r++];
                    }
                    REDACT_PUT("[redacted]");
                    while (r < n && !redact_word_end(msg[r])) {
                        r++;
                    }
                    done = 1;
                }
            }
        }
        if (done) {
            continue;
        }
        if (level <= PAV_INFO && msg[r] == '@' && r > 0 && redact_email_char(msg[r - 1])) {
            size_t start = w;
            size_t j = r + 1;
            int dot = 0;

            while (j < n && redact_email_char(msg[j])) {
                dot |= msg[j] == '.';
                j++;
            }
            if (dot && j > r + 2) {
                while (start > 0 && redact_email_char(out[start - 1])) {
                    start--;
                }
                w = start;
                REDACT_PUT("[email]");
                r = j;
                continue;
            }
        }
        out[w++] = msg[r++];
    }
#undef REDACT_PUT
    out[w] = '\0';
    snprintf(msg, cap, "%s", out);
}

void pav_log(const char *subsystem, int level, int num_args, const char *fmt, ...) {
    size_t specifiers;
    char msg[PAV_LOG_MSG_MAX];
    va_list ap;
    uint64_t ticket;

    if (subsystem == NULL || subsystem[0] == '\0') {
        subsystem = "log";
    }
    if (fmt == NULL) {
        fmt = "";
    }

    pthread_mutex_lock(&g_log_mu);
    specifiers = count_format_specifiers(fmt);
    if (num_args < 0 || (size_t)num_args != specifiers) {
        snprintf(msg, sizeof(msg),
                 "format specifier count mismatch: format has %zu, num_args is %d",
                 specifiers, num_args);
        level = PAV_ERROR;
        subsystem = "log";
    } else {
        va_start(ap, fmt);
        vsnprintf(msg, sizeof(msg), fmt, ap);
        va_end(ap);
        pav_log_redact(msg, sizeof(msg), level);
    }
    if (dup_check_unlocked(level, subsystem, msg)) {
        pthread_mutex_unlock(&g_log_mu);
        return;
    }
    ticket = deliver_unlocked(level, subsystem, msg);
    if (level == PAV_ERROR) {
        wait_ticket_unlocked(ticket);
    }
    pthread_mutex_unlock(&g_log_mu);
}

/* Called from the frontend shell so JS errors surface in the core log instead
   of disappearing into the browser console. message is passed as a single %s
   argument so percent signs in it cannot break the format string. */
int pav_ui_log(const char *subsystem, int level, const char *message) {
    pav_log(subsystem, level, 1, "%s", message != NULL ? message : "");
    return 0;
}

int pav_core_post_capabilities(const char *id, json_t *caps, const char *checked) {
    PavCall *call;
    json_t *payload;

    if (id == NULL || caps == NULL || checked == NULL) {
        json_decref(caps);
        return -1;
    }
    payload = json_pack("{s:s, s:o, s:s}", "id", id, "capabilities", caps, "checked", checked);
    call = payload != NULL ? calloc(1, sizeof(*call)) : NULL;
    if (call == NULL) {
        json_decref(payload);
        return -1;
    }
    snprintf(call->name, sizeof(call->name), "%s", "account.capabilities");
    call->args = payload;
    pthread_mutex_lock(&g_core_mu);
    if (!g_core_alive || g_core_stop || core_queue_push_unlocked(CORE_CMD_CAPS, call) != 0) {
        pthread_mutex_unlock(&g_core_mu);
        json_decref(payload);
        free(call);
        return -1;
    }
    pthread_mutex_unlock(&g_core_mu);
    return 0;
}

int pav_core_post_sync(void *result) {
    if (result == NULL) {
        return -1;
    }
    pthread_mutex_lock(&g_core_mu);
    if (!g_core_alive || g_core_stop || core_queue_push_unlocked(CORE_CMD_SYNC, result) != 0) {
        pthread_mutex_unlock(&g_core_mu);
        return -1;
    }
    pthread_mutex_unlock(&g_core_mu);
    return 0;
}

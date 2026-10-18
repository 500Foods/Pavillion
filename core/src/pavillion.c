#define _POSIX_C_SOURCE 200809L

#include "dispatch.h"
#include "logring.h"
#include "pav_activity.h"
#include "pav_cfg.h"
#include "pav_config.h"
#include "pav_platform.h"
#include "pav_store.h"
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

#define CORE_QUEUE_CAP 256
#define CORE_CMD_STOP 1
#define CORE_CMD_PREFS 2
#define CORE_CMD_ICONS 3
#define CORE_CMD_KIT 4
#define CORE_CMD_UI 5
#define CORE_CMD_CALL 6
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

const char *pav_core_workdir(void) {
    return g_core_workdir;
}
static PavConfigLoad *g_load;
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
static void apply_index_log_level(const char *name);
static int core_prepare_prefs(char *err, size_t err_len);
static int core_apply_prefs(char *err, size_t err_len);
static int core_apply_icons(char *err, size_t err_len);
static int core_apply_kit(char *err, size_t err_len);
static int core_apply_ui(char *err, size_t err_len);
static int worker_submit(PavCall *call);
static void worker_drain(void);

static void core_release_profile(void) {
    pav_activity_shutdown();
    pav_store_close(g_store);
    g_store = NULL;
    pav_config_load_free(g_load);
    g_load = NULL;
}

/* Flushes queued log lines, then publishes. The caller is the core thread
   and does not hold g_core_mu. */
static void publish_boot(const char *state, const char *name, const char *id, const char *error) {
    pav_log_flush();
    pthread_mutex_lock(&g_core_mu);
    core_publish_unlocked(state, name, id, error);
    pthread_mutex_unlock(&g_core_mu);
}

/* Loads the profile and, on success, opens SQLite. Publishes ready or error.
   Does not hold g_core_mu across the file and database calls. */
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
    apply_index_log_level(g_load->log_level);
    if (strcmp(g_load->state, "ready") != 0) {
        publish_boot("error", NULL, NULL, g_load->error);
        return;
    }
    profile = g_load->profile;
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
    (void)pav_log_attach_profile(profile->resolved_logs);
    if (core_prepare_prefs(err, sizeof(err)) != 0) {
        publish_boot("error", profile->name, profile->id, err);
        return;
    }
    pav_log("core", PAV_INFO, 0, "core ready");
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
    if (changed && pav_profile_store(g_load->profile_path, g_load->profile, err, err_len) != 0) {
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

static void *core_thread_main(void *arg) {
    (void)arg;
    core_boot();
    pthread_mutex_lock(&g_core_mu);
    for (;;) {
        int cmd = 0;
        PavCall *call = NULL;

        while (g_core_q_count == 0 && !g_core_stop) {
            struct timespec deadline;

            if (pav_window_deadline(&deadline)) {
                if (pthread_cond_timedwait(&g_core_cv, &g_core_mu, &deadline) == ETIMEDOUT) {
                    pthread_mutex_unlock(&g_core_mu);
                    pav_window_fire();
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
            if (left != NULL) {
                fail_call_unlocked(left, "not_ready", "core stopped");
            }
        }
    }
    pthread_cond_broadcast(&g_core_cv);
    pthread_mutex_unlock(&g_core_mu);
    pav_window_flush();
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
        apply_index_log_level(NULL);
        return;
    }
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
    /* After the core thread has finished. A second stop joins nothing. */
    pav_event_thread_stop();
    pav_log_thread_shutdown();
    apply_index_log_level(NULL);
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
                            strcmp(name, "debug") == 0);
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
        cfg_err(err, err_len, "logLevel must be error, warn, info, or debug");
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
    apply_index_log_level(level);
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
                pav_error_set(err, "bad_args", "logLevel must be error, warn, info, or debug", 0);
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

int pav_core_ui_set(json_t *args, json_t **data, PavError *err) {
    char *compact;
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
    errbuf[0] = '\0';
    if (pav_profile_write_ui(g_load->profile, compact, errbuf, sizeof(errbuf)) != 0 ||
        pav_profile_store(g_load->profile_path, g_load->profile, errbuf, sizeof(errbuf)) != 0) {
        free(compact);
        return fail_profile(err, errbuf[0] != '\0' ? errbuf : "cannot write preferences.ui");
    }
    free(compact);
    pav_log("config", PAV_DEBUG, 0, "prefs saved");
    return pav_core_ui_get_data(data, err);
}

#define PAV_LOG_MSG_MAX 4096
#define PAV_LOG_ROTATE_BYTES (10L * 1024L * 1024L)
#define LOG_DEST_NONE 0
#define LOG_DEST_WORKDIR 1
#define LOG_DEST_PROFILE 2
#define LOG_DEST_EXPLICIT 3

typedef struct LogNode {
    struct LogNode *next;
    char *line;
    int level;
} LogNode;

typedef struct BatchNode {
    struct BatchNode *next;
    uint64_t seq;
    char *line;
} BatchNode;

static pthread_mutex_t g_log_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_log_cv = PTHREAD_COND_INITIALIZER;
static void *g_log_thread;
static FILE *g_log_out;
static LogNode *g_q_head;
static LogNode *g_q_tail;
static BatchNode *g_batch_head;
static BatchNode *g_batch_tail;
static int64_t g_batch_started_ms;
static char g_log_switch_path[4096];
static int g_log_own;
static int g_log_dest;
static int g_log_level = PAV_INFO;

/* NULL and any name other than error, warn, or debug keep info. */
static void apply_index_log_level(const char *name) {
    int level = PAV_INFO;

    if (name != NULL && strcmp(name, "error") == 0) {
        level = PAV_ERROR;
    } else if (name != NULL && strcmp(name, "warn") == 0) {
        level = PAV_WARN;
    } else if (name != NULL && strcmp(name, "debug") == 0) {
        level = PAV_DEBUG;
    }
    pthread_mutex_lock(&g_log_mu);
    g_log_level = level;
    pthread_mutex_unlock(&g_log_mu);
}
static int g_log_alive;
static int g_log_shutdown;
static int g_log_flush_req;
static int g_log_switch_req;
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

static int level_kept(int level) {
    if (level < PAV_ERROR || level > PAV_DEBUG) {
        return 0;
    }
    return level <= g_log_level;
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
    default:
        return "?????";
    }
}

static void format_stamp(char *out, size_t out_len) {
    if (pav_format_utc(out, out_len, pav_now_ms()) != 0) {
        snprintf(out, out_len, "0000-00-00T00:00:00.000Z");
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

/* Once the profile log is open, a terminal sees warn and error only.
   Info and debug stay in the file and the ring. */
static int stderr_too_unlocked(int level) {
    if (g_log_dest == LOG_DEST_EXPLICIT) {
        return 0;
    }
    if (g_log_dest == LOG_DEST_NONE || g_log_dest == LOG_DEST_WORKDIR) {
        return 1;
    }
    return g_log_dest == LOG_DEST_PROFILE && level <= PAV_WARN && isatty(fileno(stderr));
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

/* The caller holds g_log_mu. The line is written now and copied into the ring. */
static void commit_unlocked(int level, const char *line) {
    uint64_t seq;

    emit_line_unlocked(level, line);
    seq = ++g_log_seq;
    (void)pav_logring_push(seq, line);
    if (g_log_alive) {
        batch_add_unlocked(seq, line);
    }
}

static int enqueue_line_unlocked(char *line, int level) {
    LogNode *node = malloc(sizeof(*node));

    if (node == NULL) {
        return -1;
    }
    node->line = line;
    node->level = level;
    node->next = NULL;
    if (g_q_tail != NULL) {
        g_q_tail->next = node;
    } else {
        g_q_head = node;
    }
    g_q_tail = node;
    pthread_cond_signal(&g_log_cv);
    return 0;
}

/* The caller holds g_log_mu. A running log thread writes the bytes later.
   Otherwise the caller writes them now, so a line before start is not lost. */
static void deliver_unlocked(int level, const char *subsystem, const char *msg) {
    char stamp[32];
    char *line;
    int wrote;

    if (!level_kept(level)) {
        return;
    }
    format_stamp(stamp, sizeof(stamp));
    wrote = snprintf(NULL, 0, "%s %-5s %s  %s\n", stamp, level_name(level), subsystem, msg);
    if (wrote < 0) {
        return;
    }
    line = malloc((size_t)wrote + 1);
    if (line == NULL) {
        return;
    }
    snprintf(line, (size_t)wrote + 1, "%s %-5s %s  %s\n", stamp, level_name(level), subsystem, msg);
    if (g_log_alive && enqueue_line_unlocked(line, level) == 0) {
        return;
    }
    commit_unlocked(level, line);
    free(line);
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

static void rotate_if_over(const char *path) {
    FILE *file;
    long size;
    char bak[4100];
    size_t len;

    file = fopen(path, "rb");
    if (file == NULL) {
        return;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return;
    }
    size = ftell(file);
    fclose(file);
    if (size <= PAV_LOG_ROTATE_BYTES) {
        return;
    }
    len = strlen(path);
    if (len + 3 >= sizeof(bak)) {
        return;
    }
    memcpy(bak, path, len);
    bak[len] = '.';
    bak[len + 1] = '1';
    bak[len + 2] = '\0';
    (void)remove(bak);
    (void)rename(path, bak);
}

/* The caller holds g_log_mu. Closes the workdir file and opens path. */
static int open_profile_unlocked(const char *path) {
    FILE *file;

    rotate_if_over(path);
    file = fopen(path, "a");
    if (file == NULL) {
        return -1;
    }
    (void)pav_file_private(path);
    if (g_log_own && g_log_out != NULL) {
        fclose(g_log_out);
    }
    g_log_out = file;
    g_log_own = 1;
    g_log_dest = LOG_DEST_PROFILE;
    return 0;
}

static void *log_thread_main(void *arg) {
    (void)arg;
    pthread_mutex_lock(&g_log_mu);
    for (;;) {
        if (g_q_head != NULL) {
            LogNode *node = g_q_head;

            g_q_head = node->next;
            if (g_q_head == NULL) {
                g_q_tail = NULL;
            }
            commit_unlocked(node->level, node->line);
            free(node->line);
            free(node);
            if (batch_due_unlocked()) {
                emit_batch_outside_lock();
            }
            continue;
        }
        if (g_log_switch_req) {
            (void)open_profile_unlocked(g_log_switch_path);
            g_log_switch_req = 0;
            pthread_cond_broadcast(&g_log_cv);
            continue;
        }
        if (g_batch_head != NULL && (g_log_shutdown || batch_due_unlocked())) {
            emit_batch_outside_lock();
            continue;
        }
        if (g_log_flush_req) {
            g_log_flush_req = 0;
            pthread_cond_broadcast(&g_log_cv);
            continue;
        }
        if (g_log_shutdown) {
            g_log_alive = 0;
            pthread_cond_broadcast(&g_log_cv);
            pthread_mutex_unlock(&g_log_mu);
            return NULL;
        }
        if (g_batch_head != NULL) {
            struct timespec ts;

            abs_after_ms(&ts, batch_wait_ms_unlocked());
            pthread_cond_timedwait(&g_log_cv, &g_log_mu, &ts);
            continue;
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
    char logs[4608];
    char path[4608];
    FILE *file;
    const PavPlatform *platform;

    pthread_mutex_lock(&g_log_mu);
    if (g_log_dest == LOG_DEST_EXPLICIT || g_log_dest == LOG_DEST_PROFILE) {
        pthread_mutex_unlock(&g_log_mu);
        return 0;
    }
    if (workdir == NULL || workdir[0] == '\0') {
        if (g_log_dest == LOG_DEST_WORKDIR && g_log_own && g_log_out != NULL) {
            fclose(g_log_out);
            g_log_out = NULL;
            g_log_own = 0;
        }
        if (g_log_dest == LOG_DEST_WORKDIR) {
            g_log_dest = LOG_DEST_NONE;
        }
        pthread_mutex_unlock(&g_log_mu);
        return 0;
    }
    pthread_mutex_unlock(&g_log_mu);

    platform = pav_platform();
    if (pav_path_join(logs, sizeof(logs), workdir, "logs") != 0 || platform == NULL ||
        platform->mkdir_private == NULL || platform->mkdir_private(logs) != 0 ||
        pav_path_join(path, sizeof(path), logs, "pavillion.log") != 0) {
        return -1;
    }
    file = fopen(path, "a");
    if (file == NULL) {
        return -1;
    }
    (void)pav_file_private(path);

    pthread_mutex_lock(&g_log_mu);
    if (g_log_dest == LOG_DEST_EXPLICIT || g_log_dest == LOG_DEST_PROFILE) {
        fclose(file);
        pthread_mutex_unlock(&g_log_mu);
        return 0;
    }
    if (g_log_own && g_log_out != NULL) {
        fclose(g_log_out);
    }
    g_log_out = file;
    g_log_own = 1;
    g_log_dest = LOG_DEST_WORKDIR;
    pthread_mutex_unlock(&g_log_mu);
    return 0;
}

int pav_log_attach_profile(const char *path) {
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
        int rc = open_profile_unlocked(path);

        pthread_mutex_unlock(&g_log_mu);
        return rc;
    }
    memcpy(g_log_switch_path, path, strlen(path) + 1);
    g_log_switch_req = 1;
    pthread_cond_broadcast(&g_log_cv);
    while (g_log_switch_req && g_log_alive) {
        pthread_cond_wait(&g_log_cv, &g_log_mu);
    }
    {
        int rc = g_log_dest == LOG_DEST_PROFILE ? 0 : -1;

        pthread_mutex_unlock(&g_log_mu);
        return rc;
    }
}

int pav_log_set_output(const char *path) {
    pthread_mutex_lock(&g_log_mu);
    if (g_log_own && g_log_out != NULL) {
        fclose(g_log_out);
    }
    g_log_own = 0;
    g_log_out = NULL;
    g_log_dest = LOG_DEST_NONE;
    if (path != NULL) {
        g_log_out = fopen(path, "w");
        if (g_log_out == NULL) {
            pthread_mutex_unlock(&g_log_mu);
            return -1;
        }
        g_log_own = 1;
        g_log_dest = LOG_DEST_EXPLICIT;
    }
    pthread_mutex_unlock(&g_log_mu);
    return 0;
}

void pav_log(const char *subsystem, int level, int num_args, const char *fmt, ...) {
    size_t specifiers;
    char msg[PAV_LOG_MSG_MAX];
    va_list ap;

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
        deliver_unlocked(PAV_ERROR, "log", msg);
        pthread_mutex_unlock(&g_log_mu);
        return;
    }
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    deliver_unlocked(level, subsystem, msg);
    pthread_mutex_unlock(&g_log_mu);
}

/* Called from the frontend shell so JS errors surface in the core log instead
   of disappearing into the browser console. message is passed as a single %s
   argument so percent signs in it cannot break the format string. */
int pav_ui_log(const char *subsystem, int level, const char *message) {
    pav_log(subsystem, level, 1, "%s", message != NULL ? message : "");
    return 0;
}

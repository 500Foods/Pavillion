#define _POSIX_C_SOURCE 200809L

#include "pav_config.h"
#include "pav_store.h"
#include "pavillion.h"

#include <ctype.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CORE_QUEUE_CAP 8
#define CORE_CMD_STOP 1
#define CORE_CMD_PREFS 2
#define CORE_CMD_ICONS 3
#define CORE_CMD_KIT 4

static pthread_mutex_t g_core_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_core_cv = PTHREAD_COND_INITIALIZER;
static pthread_t g_core_thread;
static int g_core_alive;
static int g_core_published;
static int g_core_stop;
static int g_core_q[CORE_QUEUE_CAP];
static int g_core_q_head;
static int g_core_q_tail;
static int g_core_q_count;
/* Before the first start, status matches the old stub so the shell can read it. */
static PavillionStatus g_status = { .state = "ready" };
static char g_core_workdir[4096];
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

static void attach_profile_log(const char *path);
static void set_start_err(char *err, size_t err_len, const char *msg);
static int core_prepare_prefs(char *err, size_t err_len);
static int core_apply_prefs(char *err, size_t err_len);
static int core_apply_icons(char *err, size_t err_len);
static int core_apply_kit(char *err, size_t err_len);

static void core_release_profile(void) {
    pav_store_close(g_store);
    g_store = NULL;
    pav_config_load_free(g_load);
    g_load = NULL;
}

/* Loads the profile and, on success, opens SQLite. Publishes ready or error.
   Does not hold g_core_mu across the file and database calls. */
static void core_boot(void) {
    char err[512];
    PavProfile *profile;

    core_release_profile();
    err[0] = '\0';
    if (g_core_workdir[0] == '\0') {
        pthread_mutex_lock(&g_core_mu);
        core_publish_unlocked("error", NULL, NULL, "missing workdir");
        pthread_mutex_unlock(&g_core_mu);
        return;
    }
    if (pav_config_ensure(g_core_workdir, err, sizeof(err)) != 0) {
        pthread_mutex_lock(&g_core_mu);
        core_publish_unlocked("error", NULL, NULL, err[0] != '\0' ? err : "cannot create profile");
        pthread_mutex_unlock(&g_core_mu);
        return;
    }
    if (pav_config_load(g_core_workdir, &g_load, err, sizeof(err)) != 0 || g_load == NULL) {
        pthread_mutex_lock(&g_core_mu);
        core_publish_unlocked("error", NULL, NULL, err[0] != '\0' ? err : "missing index");
        pthread_mutex_unlock(&g_core_mu);
        return;
    }
    if (strcmp(g_load->state, "ready") != 0) {
        pthread_mutex_lock(&g_core_mu);
        core_publish_unlocked("error", NULL, NULL, g_load->error);
        pthread_mutex_unlock(&g_core_mu);
        return;
    }
    profile = g_load->profile;
    if (pav_profile_check_engine(profile, err, sizeof(err)) != 0) {
        pthread_mutex_lock(&g_core_mu);
        core_publish_unlocked("error", profile->name, profile->id, err);
        pthread_mutex_unlock(&g_core_mu);
        return;
    }
    if (pav_config_prepare(g_core_workdir, profile, err, sizeof(err)) != 0 ||
        pav_store_open(&g_store, profile->engine, profile->resolved_storage, err, sizeof(err)) != 0) {
        pthread_mutex_lock(&g_core_mu);
        core_publish_unlocked("error", profile->name, profile->id, err);
        pthread_mutex_unlock(&g_core_mu);
        return;
    }
    attach_profile_log(profile->resolved_logs);
    if (core_prepare_prefs(err, sizeof(err)) != 0) {
        pthread_mutex_lock(&g_core_mu);
        core_publish_unlocked("error", profile->name, profile->id, err);
        pthread_mutex_unlock(&g_core_mu);
        return;
    }
    pav_log("core", PAV_INFO, 0, "core ready");
    pthread_mutex_lock(&g_core_mu);
    core_publish_unlocked("ready", profile->name, profile->id, NULL);
    pthread_mutex_unlock(&g_core_mu);
}

/* Inserts missing preferences.zoom keys and preferences.ui when it is missing,
   then publishes the snapshot. The caller is the core thread and does not hold
   g_core_mu. */
static int core_prepare_prefs(char *err, size_t err_len) {
    PavShellPrefs snap;
    int added_zoom = 0;
    int added_ui = 0;

    if (g_load == NULL || g_load->profile == NULL || g_load->profile_path[0] == '\0') {
        set_start_err(err, err_len, "missing profile");
        return -1;
    }
    if (pav_profile_ensure_zoom(g_load->profile, &added_zoom, err, err_len) != 0) {
        return -1;
    }
    if (pav_profile_ensure_ui(g_load->profile, &added_ui, err, err_len) != 0) {
        return -1;
    }
    if ((added_zoom || added_ui) &&
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
    if (err == NULL || err_len == 0) {
        return;
    }
    snprintf(err, err_len, "%s", msg);
}

static void core_queue_clear_unlocked(void) {
    g_core_q_head = 0;
    g_core_q_tail = 0;
    g_core_q_count = 0;
}

/* The caller holds g_core_mu. A full queue still wakes the thread via g_core_stop. */
static int core_queue_push_unlocked(int cmd) {
    if (g_core_q_count >= CORE_QUEUE_CAP) {
        return -1;
    }
    g_core_q[g_core_q_tail] = cmd;
    g_core_q_tail = (g_core_q_tail + 1) % CORE_QUEUE_CAP;
    g_core_q_count++;
    pthread_cond_broadcast(&g_core_cv);
    return 0;
}

static int core_queue_pop_unlocked(int *cmd) {
    if (g_core_q_count == 0) {
        return -1;
    }
    *cmd = g_core_q[g_core_q_head];
    g_core_q_head = (g_core_q_head + 1) % CORE_QUEUE_CAP;
    g_core_q_count--;
    return 0;
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

        while (g_core_q_count == 0 && !g_core_stop) {
            pthread_cond_wait(&g_core_cv, &g_core_mu);
        }
        if (g_core_q_count == 0) {
            break;
        }
        if (core_queue_pop_unlocked(&cmd) != 0) {
            continue;
        }
        if (cmd == CORE_CMD_STOP) {
            break;
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
    }
    g_core_alive = 0;
    core_fail_prefs_unlocked("core stopped");
    g_icons_rc = -1;
    g_icons_done = 1;
    g_kit_rc = -1;
    g_kit_done = 1;
    pthread_cond_broadcast(&g_core_cv);
    pthread_mutex_unlock(&g_core_mu);
    core_release_profile();
    return NULL;
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

int pavillion_api_version(void) {
    return PAVILLION_API_VERSION;
}

int pavillion_start(const char *workdir, char *err, size_t err_len) {
    int rc;

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
    core_queue_clear_unlocked();
    rc = pthread_create(&g_core_thread, NULL, core_thread_main, NULL);
    if (rc != 0) {
        pthread_mutex_unlock(&g_core_mu);
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
        pav_log_thread_shutdown();
        return;
    }
    g_core_stop = 1;
    if (core_queue_push_unlocked(CORE_CMD_STOP) != 0) {
        pthread_cond_broadcast(&g_core_cv);
    }
    thread = g_core_thread;
    join = 1;
    pthread_mutex_unlock(&g_core_mu);
    if (join) {
        pthread_join(thread, NULL);
    }
    /* After the core thread has finished. A second stop joins nothing. */
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
    if (core_queue_push_unlocked(CORE_CMD_PREFS) != 0) {
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
    if (core_queue_push_unlocked(CORE_CMD_ICONS) != 0) {
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
    if (core_queue_push_unlocked(CORE_CMD_KIT) != 0) {
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

typedef struct LogNode {
    struct LogNode *next;
    char *line;
} LogNode;

static pthread_mutex_t g_log_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_log_cv = PTHREAD_COND_INITIALIZER;
static pthread_t g_log_thread;
static FILE *g_log_out;
static LogNode *g_q_head;
static LogNode *g_q_tail;
static int g_log_own;
static int g_log_level = PAV_INFO;
static int g_log_alive;
static int g_log_shutdown;

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
    struct timespec ts;
    struct tm tm;
    int msec;

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        snprintf(out, out_len, "0000-00-00T00:00:00.000Z");
        return;
    }
    gmtime_r(&ts.tv_sec, &tm);
    msec = (int)(ts.tv_nsec / 1000000L);
    if (msec > 999) {
        msec = 999;
    }
    snprintf(out, out_len, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec, msec);
}

static void emit_bytes_unlocked(const char *line) {
    FILE *out = g_log_out != NULL ? g_log_out : stderr;
    fputs(line, out);
    fflush(out);
}

static int enqueue_line_unlocked(const char *line) {
    LogNode *node = malloc(sizeof(*node));
    char *copy;

    if (node == NULL) {
        return -1;
    }
    copy = strdup(line);
    if (copy == NULL) {
        free(node);
        return -1;
    }
    node->line = copy;
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
    char line[1200];

    if (!level_kept(level)) {
        return;
    }
    format_stamp(stamp, sizeof(stamp));
    snprintf(line, sizeof(line), "%s %-5s %s  %s\n",
             stamp, level_name(level), subsystem, msg);
    if (g_log_alive && enqueue_line_unlocked(line) == 0) {
        return;
    }
    emit_bytes_unlocked(line);
}

static void *log_thread_main(void *arg) {
    (void)arg;
    pthread_mutex_lock(&g_log_mu);
    for (;;) {
        LogNode *node;

        while (g_q_head == NULL && !g_log_shutdown) {
            pthread_cond_wait(&g_log_cv, &g_log_mu);
        }
        if (g_q_head == NULL && g_log_shutdown) {
            g_log_alive = 0;
            pthread_mutex_unlock(&g_log_mu);
            break;
        }
        node = g_q_head;
        g_q_head = node->next;
        if (g_q_head == NULL) {
            g_q_tail = NULL;
        }
        emit_bytes_unlocked(node->line);
        free(node->line);
        free(node);
    }
    return NULL;
}

int pav_log_thread_start(void) {
    int rc;

    pthread_mutex_lock(&g_log_mu);
    if (g_log_alive) {
        pthread_mutex_unlock(&g_log_mu);
        return -1;
    }
    g_log_shutdown = 0;
    rc = pthread_create(&g_log_thread, NULL, log_thread_main, NULL);
    if (rc != 0) {
        pthread_mutex_unlock(&g_log_mu);
        return -1;
    }
    g_log_alive = 1;
    pthread_mutex_unlock(&g_log_mu);
    return 0;
}

void pav_log_thread_shutdown(void) {
    pthread_t thread;

    pthread_mutex_lock(&g_log_mu);
    if (!g_log_alive) {
        pthread_mutex_unlock(&g_log_mu);
        return;
    }
    g_log_shutdown = 1;
    thread = g_log_thread;
    pthread_cond_broadcast(&g_log_cv);
    pthread_mutex_unlock(&g_log_mu);
    pthread_join(thread, NULL);
}

/* Opens the profile log for append when nothing else owns the output.
   A test that already called pav_log_set_output keeps that file. */
static void attach_profile_log(const char *path) {
    FILE *file;

    if (path == NULL || path[0] == '\0') {
        return;
    }
    pthread_mutex_lock(&g_log_mu);
    if (g_log_out != NULL) {
        pthread_mutex_unlock(&g_log_mu);
        return;
    }
    file = fopen(path, "a");
    if (file != NULL) {
        g_log_out = file;
        g_log_own = 1;
    }
    pthread_mutex_unlock(&g_log_mu);
}

int pav_log_set_output(const char *path) {
    pthread_mutex_lock(&g_log_mu);
    if (g_log_own && g_log_out != NULL) {
        fclose(g_log_out);
    }
    g_log_own = 0;
    g_log_out = NULL;
    if (path != NULL) {
        g_log_out = fopen(path, "w");
        if (g_log_out == NULL) {
            pthread_mutex_unlock(&g_log_mu);
            return -1;
        }
        g_log_own = 1;
    }
    pthread_mutex_unlock(&g_log_mu);
    return 0;
}

void pav_log(const char *subsystem, int level, int num_args, const char *fmt, ...) {
    size_t specifiers;
    char msg[1024];
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
    pav_log(subsystem, level, 1, "%s", message);
    return 0;
}

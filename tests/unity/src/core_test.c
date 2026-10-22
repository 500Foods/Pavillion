#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "pav_activity.h"
#include "dispatch.h"
#include "pav_net.h"
#include "pav_jmap.h"
#include "pav_auth.h"
#include "account.h"
#include "pav_platform.h"
#include "pav_secret.h"
#include "pav_store.h"
#include "pav_cfg.h"
#include "pav_config.h"
#include "pav_db.h"
#include "pav_blob.h"
#include "pav_sync.h"
#include "pavillion.h"
#include "logring.h"
#include "unity.h"

#include <jansson.h>

#include <sqlite3.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#ifndef _WIN32
#include <sys/file.h>
#endif
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "pav_compat.h"

static int fixture_email_hold(int hold);
static int g_email_hold;

/* File modes are a no-op on Windows (G Lock), so mode checks are skipped there. */
#ifdef _WIN32
#define ASSERT_MODE(want, st) ((void)(st))
#else
#define ASSERT_MODE(want, st) TEST_ASSERT_EQUAL_INT((want), (int)((st).st_mode & 0777))
#endif

/* Removed from pavillion.h in G-8. The bodies stay so these tests still link. */
typedef struct PavillionStatus {
    char profile_name[256];
    char profile_id[64];
    char state[32];
    char error[512];
} PavillionStatus;

int pavillion_status(PavillionStatus *out);
int pavillion_prefs_get(PavShellPrefs *out);
int pavillion_prefs_apply(const PavShellPrefs *in, unsigned mask, char *err, size_t err_len);
int pavillion_icons_get(char **out, char *err, size_t err_len);
int pavillion_icons_apply(const char *json, char *err, size_t err_len);
int pavillion_kit_get(char *out, size_t out_len, char *err, size_t err_len);
int pavillion_kit_apply(const char *kit, char *err, size_t err_len);
int pavillion_ui_get(char **json_out, char *err, size_t err_len);
int pavillion_ui_set(const char *json, char *err, size_t err_len);
int usleep(unsigned int usec);

#define ALT_LINES 20

static pthread_mutex_t g_turn_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_turn_cv = PTHREAD_COND_INITIALIZER;
static int g_turn;

static char g_log_path[64];
static int g_saved_stderr = -1;
static char g_err_path[64];

/* A profile warn or error still copies to stderr when it is a terminal.
   Park that stream so those lines do not mix into Unity. */
static void restore_stderr(void) {
    int err_fd;

    if (g_saved_stderr < 0) {
        return;
    }
    fflush(stderr);
    err_fd = fileno(stderr);
    if (err_fd >= 0) {
        dup2(g_saved_stderr, err_fd);
    }
    close(g_saved_stderr);
    g_saved_stderr = -1;
    if (g_err_path[0] != '\0') {
        unlink(g_err_path);
        g_err_path[0] = '\0';
    }
}

static void capture_stderr(void) {
    char path[] = "/tmp/pav-err-XXXXXX";
    int fd;
    int err_fd;

    TEST_ASSERT_TRUE(g_saved_stderr < 0);
    fflush(stderr);
    fd = mkstemp(path);
    TEST_ASSERT_TRUE(fd >= 0);
    err_fd = fileno(stderr);
    g_saved_stderr = dup(err_fd);
    /* dup2 returns the new fd on POSIX but 0 on the Windows CRT; only -1 is failure. */
    if (g_saved_stderr < 0 || dup2(fd, err_fd) < 0) {
        char msg[160];
        int e = errno;

        snprintf(msg, sizeof(msg), "cannot capture stderr: %s failed: %s",
                 g_saved_stderr < 0 ? "dup" : "dup2", strerror(e));
        close(fd);
        if (g_saved_stderr >= 0) {
            close(g_saved_stderr);
            g_saved_stderr = -1;
        }
        unlink(path);
        TEST_FAIL_MESSAGE(msg);
    }
    TEST_ASSERT_EQUAL_INT(0, close(fd));
    TEST_ASSERT_TRUE(strlen(path) < sizeof(g_err_path));
    memcpy(g_err_path, path, strlen(path) + 1);
}

static int timestamp_ok(const char *s) {
    static const char shape[] = "dddd-dd-ddTdd:dd:dd.dddZ";
    size_t i;

    if (strlen(s) < sizeof(shape) - 1) {
        return 0;
    }
    for (i = 0; shape[i] != '\0'; i++) {
        if (shape[i] == 'd') {
            if (s[i] < '0' || s[i] > '9') {
                return 0;
            }
        } else if (s[i] != shape[i]) {
            return 0;
        }
    }
    return 1;
}

static void open_log(void) {
    char path[] = "/tmp/pav-log-XXXXXX";
    int fd = mkstemp(path);

    TEST_ASSERT_TRUE(fd >= 0);
    TEST_ASSERT_EQUAL_INT(0, close(fd));
    TEST_ASSERT_TRUE(strlen(path) < sizeof(g_log_path));
    memcpy(g_log_path, path, strlen(path) + 1);
    TEST_ASSERT_EQUAL_INT(0, pav_log_set_output(g_log_path));
    TEST_ASSERT_EQUAL_INT(0, pav_log_thread_start());
}

static char *read_log(void) {
    FILE *f;
    long n;
    char *buf;
    size_t got;

    pav_log_thread_shutdown();
    TEST_ASSERT_EQUAL_INT(0, pav_log_set_output(NULL));
    f = fopen(g_log_path, "rb");
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL_INT(0, fseek(f, 0, SEEK_END));
    n = ftell(f);
    TEST_ASSERT_TRUE(n >= 0);
    rewind(f);
    buf = calloc((size_t)n + 1, 1);
    TEST_ASSERT_NOT_NULL(buf);
    got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    TEST_ASSERT_EQUAL_INT((int)n, (int)got);
    unlink(g_log_path);
    g_log_path[0] = '\0';
    return buf;
}

static void assert_one_line(const char *text, const char *tail) {
    const char *nl = strchr(text, '\n');
    size_t tail_len = strlen(tail);

    TEST_ASSERT_NOT_NULL(nl);
    TEST_ASSERT_EQUAL_CHAR('\0', nl[1]);
    TEST_ASSERT_TRUE(timestamp_ok(text));
    TEST_ASSERT_EQUAL_UINT((unsigned)tail_len, (unsigned)(nl - (text + 24)));
    TEST_ASSERT_EQUAL_INT(0, memcmp(text + 24, tail, tail_len));
}

void setUp(void) {
    const PavPlatform *platform = pav_platform();

    if (platform == NULL || platform->name == NULL || strcmp(platform->name, "fake") != 0) {
        TEST_FAIL_MESSAGE("platform is not the fake");
    }
}

static char g_cfg_dir[80];
static char g_extra_dir[80];
static PavStore *g_test_store;

static void rm_tree(const char *path) {
    DIR *dir;
    struct dirent *ent;

    dir = opendir(path);
    if (dir == NULL) {
        unlink(path);
        return;
    }
    while ((ent = readdir(dir)) != NULL) {
        char child[512];
        struct stat st;

        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
        if (lstat(child, &st) == 0 && S_ISDIR(st.st_mode)) {
            rm_tree(child);
        } else {
            unlink(child);
        }
    }
    closedir(dir);
    rmdir(path);
}

static void remove_cfg_dir(void) {
    if (g_cfg_dir[0] != '\0') {
        rm_tree(g_cfg_dir);
        g_cfg_dir[0] = '\0';
    }
    if (g_extra_dir[0] != '\0') {
        rm_tree(g_extra_dir);
        g_extra_dir[0] = '\0';
    }
}

void tearDown(void) {
    if (g_email_hold) {
        g_email_hold = 0;
        (void)fixture_email_hold(0);
    }
    unsetenv("PAVILLION_DEBUG");
    unsetenv("PAVILLION_SELFTEST");
    pav_platform_fake_reset();
    if (g_test_store != NULL) {
        pav_store_close(g_test_store);
        g_test_store = NULL;
    }
    pavillion_stop();
    pav_log_thread_shutdown();
    pav_log_set_output(NULL);
    pav_log_set_workdir(NULL);
    (void)pav_log_set_cap_mb(10);
    pav_logring_reset();
    pav_log_client_rate_reset();
    restore_stderr();
    if (g_log_path[0] != '\0') {
        unlink(g_log_path);
        g_log_path[0] = '\0';
    }
    remove_cfg_dir();
}

static void make_slot(char *slot, size_t slot_len);

void test_start_reports_ready_and_stop_is_repeatable(void) {
    PavillionStatus status;
    int rc;

    open_log();
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    rc = pavillion_start(g_cfg_dir, NULL, 0);
    TEST_ASSERT_EQUAL_INT(0, rc);

    rc = pavillion_status(&status);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("ready", status.state, status.error);
    TEST_ASSERT_EQUAL_STRING("Default", status.profile_name);
    TEST_ASSERT_EQUAL_STRING("default", status.profile_id);

    pavillion_stop();
    pavillion_stop();
}

static void *read_status(void *arg) {
    PavillionStatus *out = arg;

    if (pavillion_status(out) != 0) {
        out->state[0] = '\0';
    }
    return NULL;
}

void test_core_thread_ready_after_start_and_joined_on_stop(void) {
    PavillionStatus status;
    PavillionStatus other;
    pthread_t reader;

    memset(&other, 0, sizeof(other));
    open_log();
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    TEST_ASSERT_EQUAL_INT(1, pav_core_thread_alive());

    TEST_ASSERT_EQUAL_INT(0, pavillion_status(&status));
    TEST_ASSERT_EQUAL_STRING("ready", status.state);
    TEST_ASSERT_EQUAL_STRING("Default", status.profile_name);
    TEST_ASSERT_EQUAL_STRING("default", status.profile_id);
    TEST_ASSERT_EQUAL_STRING("", status.error);

    TEST_ASSERT_EQUAL_INT(0, pthread_create(&reader, NULL, read_status, &other));
    TEST_ASSERT_EQUAL_INT(0, pthread_join(reader, NULL));
    TEST_ASSERT_EQUAL_STRING("ready", other.state);
    TEST_ASSERT_EQUAL_STRING("Default", other.profile_name);

    pavillion_stop();
    TEST_ASSERT_EQUAL_INT(0, pav_core_thread_alive());
    TEST_ASSERT_EQUAL_INT(0, pavillion_status(&status));
    TEST_ASSERT_EQUAL_STRING("ready", status.state);
}

void test_stop_joins_restarts_and_refuses_double_start(void) {
    PavillionStatus status;
    char err[64];
    char *text;

    open_log();
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    pav_log("core", PAV_INFO, 0, "before-stop");

    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, pavillion_start(g_cfg_dir, err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(err, "already running"));
    TEST_ASSERT_EQUAL_INT(1, pav_core_thread_alive());

    pavillion_stop();
    TEST_ASSERT_EQUAL_INT(0, pav_core_thread_alive());
    pavillion_stop();
    TEST_ASSERT_EQUAL_INT(0, pav_core_thread_alive());
    /* Stop joined the log thread, so a new one can start. */
    TEST_ASSERT_EQUAL_INT(0, pav_log_thread_start());

    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    TEST_ASSERT_EQUAL_INT(1, pav_core_thread_alive());
    TEST_ASSERT_EQUAL_INT(0, pavillion_status(&status));
    TEST_ASSERT_EQUAL_STRING("ready", status.state);
    TEST_ASSERT_EQUAL_STRING("Default", status.profile_name);
    pavillion_stop();

    text = read_log();
    TEST_ASSERT_NOT_NULL(strstr(text, "before-stop"));
    free(text);
}

void test_log_line_format_and_level_filter(void) {
    const char *secret = "pw-fixture-hunter2";
    char *text;

    open_log();
    pav_log("core", PAV_DEBUG, 0, "debug-is-captured");
    pav_log("core", PAV_INFO, 0, "core ready");
    text = read_log();

    /* Every level is captured; the index log_level only filters the view. */
    TEST_ASSERT_NULL(strstr(text, secret));
    TEST_ASSERT_NOT_NULL(strstr(text, " DEBUG core  debug-is-captured\n"));
    assert_one_line(strchr(text, '\n') + 1, " INFO  core  core ready");
    free(text);
}

void test_log_trace_captured_like_other_levels(void) {
    json_t *root = NULL;
    json_t *lines;
    char *text;

    pav_logring_reset();
    pav_log_client_rate_reset();
    open_log();
    pav_log("core", PAV_TRACE, 0, "trace-line");
    pav_log("core", PAV_DEBUG, 0, "debug-line");
    pav_log_flush();
    TEST_ASSERT_EQUAL_INT(0, pav_logring_read(0, PAV_LOGRING_CAP, PAV_TRACE, NULL, &root));
    TEST_ASSERT_NOT_NULL(root);
    lines = json_object_get(root, "lines");
    TEST_ASSERT_TRUE(json_is_array(lines));
    TEST_ASSERT_EQUAL_INT(2, (int)json_array_size(lines));
    TEST_ASSERT_EQUAL_STRING("TRACE", json_string_value(json_object_get(json_array_get(lines, 0), "level")));
    TEST_ASSERT_EQUAL_STRING("trace-line", json_string_value(json_object_get(json_array_get(lines, 0), "text")));
    TEST_ASSERT_EQUAL_STRING("debug-line", json_string_value(json_object_get(json_array_get(lines, 1), "text")));
    json_decref(root);
    root = NULL;
    /* minLevel debug leaves TRACE out, as any level above the minimum. */
    TEST_ASSERT_EQUAL_INT(0, pav_logring_read(0, PAV_LOGRING_CAP, PAV_DEBUG, NULL, &root));
    TEST_ASSERT_EQUAL_INT(1, (int)json_array_size(json_object_get(root, "lines")));
    json_decref(root);
    text = read_log();
    TEST_ASSERT_NOT_NULL(strstr(text, " TRACE core  trace-line\n"));
    free(text);
}

void test_log_formats_args_and_literal_percent(void) {
    char *text;

    open_log();
    pav_log("core", PAV_INFO, 0, "100%% done");
    text = read_log();
    assert_one_line(text, " INFO  core  100% done");
    free(text);

    open_log();
    pav_log("core", PAV_INFO, 1, "hello %s", "there");
    text = read_log();
    assert_one_line(text, " INFO  core  hello there");
    free(text);
}

struct AltArg {
    int id;
};

static void *alt_log_thread(void *arg) {
    int id = ((struct AltArg *)arg)->id;
    int i;

    for (i = 0; i < ALT_LINES; i++) {
        pthread_mutex_lock(&g_turn_mu);
        while (g_turn != id) {
            pthread_cond_wait(&g_turn_cv, &g_turn_mu);
        }
        pav_log("core", PAV_INFO, 2, "T%d-%02d", id, i);
        g_turn = 1 - id;
        pthread_cond_signal(&g_turn_cv);
        pthread_mutex_unlock(&g_turn_mu);
    }
    return NULL;
}

void test_log_two_threads_keep_call_order(void) {
    pthread_t threads[2];
    struct AltArg args[2];
    char *text;
    char *cursor;
    int i;

    g_turn = 0;
    open_log();
    args[0].id = 0;
    args[1].id = 1;
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&threads[0], NULL, alt_log_thread, &args[0]));
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&threads[1], NULL, alt_log_thread, &args[1]));
    TEST_ASSERT_EQUAL_INT(0, pthread_join(threads[0], NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_join(threads[1], NULL));

    text = read_log();
    cursor = text;
    for (i = 0; i < ALT_LINES * 2; i++) {
        char tail[64];
        size_t tail_len;

        snprintf(tail, sizeof(tail), " INFO  core  T%d-%02d", i % 2, i / 2);
        tail_len = strlen(tail);
        TEST_ASSERT_TRUE(timestamp_ok(cursor));
        TEST_ASSERT_EQUAL_INT(0, memcmp(cursor + 24, tail, tail_len));
        cursor += 24 + tail_len;
        TEST_ASSERT_EQUAL_CHAR('\n', *cursor);
        cursor++;
    }
    TEST_ASSERT_EQUAL_CHAR('\0', *cursor);
    free(text);
}

void test_log_num_args_mismatch_logs_error(void) {
    char *text;

    open_log();
    pav_log("core", PAV_INFO, 2, "core ready");
    text = read_log();
    TEST_ASSERT_NULL(strstr(text, "core ready"));
    assert_one_line(text, " ERROR log  format specifier count mismatch: format has 0, num_args is 2");
    free(text);
}

static const char k_index[] =
    "{\n"
    "  \"kind\": \"pavillion-index\",\n"
    "  \"schema\": 1,\n"
    "  \"active\": \"default.json\",\n"
    "  \"profile_dir\": \"profiles\",\n"
    "  \"log_level\": \"info\",\n"
    "  \"editor\": \"andrew\"\n"
    "}";

static const char k_profile[] =
    "{\n"
    "  \"kind\": \"pavillion-profile\",\n"
    "  \"schema\": 1,\n"
    "  \"id\": \"default\",\n"
    "  \"name\": \"Default\",\n"
    "  \"note\": \"keep-me\",\n"
    "  \"storage\": { \"engine\": \"sqlite\", \"path\": \"default.sqlite\" },\n"
    "  \"logs\": \"logs/default.log\",\n"
    "  \"blobs\": \"blobs/default\",\n"
    "  \"cache\": \"cache/default\",\n"
    "  \"accounts\": [],\n"
    "  \"preferences\": {\n"
    "    \"timezone\": \"America/Vancouver\",\n"
    "    \"week_start\": \"monday\",\n"
    "    \"time_format\": \"24h\",\n"
    "    \"theme\": \"system\",\n"
    "    \"density\": \"comfortable\",\n"
    "    \"zoom\": {\n"
    "      \"shell\": 1,\n"
    "      \"navButtons\": 1,\n"
    "      \"navBody\": 1,\n"
    "      \"mailList\": 1,\n"
    "      \"mailDetail\": 1\n"
    "    },\n"
    "    \"window\": {\n"
    "      \"maximized\": false,\n"
    "      \"width\": 1200,\n"
    "      \"height\": 800,\n"
    "      \"x\": null,\n"
    "      \"y\": null,\n"
    "      \"chrome\": \"system\"\n"
    "    }\n"
    "  }\n"
    "}";

void test_parse_index_and_profile(void) {
    PavIndex *index = NULL;
    PavProfile *profile = NULL;
    char err[128];

    TEST_ASSERT_EQUAL_INT(0, pav_index_parse(k_index, &index, err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("pavillion-index", index->kind);
    TEST_ASSERT_EQUAL_INT(1, index->schema);
    TEST_ASSERT_EQUAL_STRING("default.json", index->active);
    TEST_ASSERT_EQUAL_STRING("profiles", index->profile_dir);
    TEST_ASSERT_EQUAL_STRING("info", index->log_level);

    TEST_ASSERT_EQUAL_INT(0, pav_profile_parse(k_profile, &profile, err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("pavillion-profile", profile->kind);
    TEST_ASSERT_EQUAL_INT(1, profile->schema);
    TEST_ASSERT_EQUAL_STRING("default", profile->id);
    TEST_ASSERT_EQUAL_STRING("Default", profile->name);
    TEST_ASSERT_EQUAL_STRING("sqlite", profile->engine);
    TEST_ASSERT_EQUAL_STRING("default.sqlite", profile->storage_path);
    /* H-4: an old .log path is rewritten to <id>.log.db on load. */
    TEST_ASSERT_EQUAL_STRING("default.log.db", profile->logs);
    TEST_ASSERT_EQUAL_INT(1, profile->logs_migrated);
    TEST_ASSERT_EQUAL_INT(10, profile->log_max_mb);
    TEST_ASSERT_EQUAL_STRING("blobs/default", profile->blobs);
    TEST_ASSERT_EQUAL_STRING("cache/default", profile->cache);
    TEST_ASSERT_EQUAL_STRING("America/Vancouver", profile->timezone);
    TEST_ASSERT_EQUAL_STRING("monday", profile->week_start);
    TEST_ASSERT_EQUAL_STRING("24h", profile->time_format);
    TEST_ASSERT_EQUAL_STRING("system", profile->theme);
    TEST_ASSERT_EQUAL_STRING("comfortable", profile->density);
    TEST_ASSERT_EQUAL_INT(0, profile->window_maximized);
    TEST_ASSERT_EQUAL_INT(1200, profile->window_width);
    TEST_ASSERT_EQUAL_INT(800, profile->window_height);
    TEST_ASSERT_EQUAL_INT(0, profile->window_x_set);
    TEST_ASSERT_EQUAL_INT(0, profile->window_y_set);
    TEST_ASSERT_EQUAL_STRING("system", profile->chrome);
    /* k_profile has no preferences.cache. Parse fills the defaults. */
    TEST_ASSERT_EQUAL_INT(2048, profile->bodies_mb);
    TEST_ASSERT_EQUAL_INT(4096, profile->blobs_mb);
    TEST_ASSERT_EQUAL_INT(1, profile->cache_filled);
    TEST_ASSERT_EQUAL_UINT(0, (unsigned)profile->account_count);

    pav_index_free(index);
    pav_profile_free(profile);
}

void test_reject_schema_and_missing_kind(void) {
    PavIndex *index = NULL;
    PavProfile *profile = NULL;
    char err[128];
    const char *bad_schema =
        "{ \"kind\": \"pavillion-index\", \"schema\": 2, \"active\": \"default.json\","
        " \"profile_dir\": \"profiles\", \"log_level\": \"info\" }";
    const char *no_kind =
        "{ \"schema\": 1, \"id\": \"default\", \"name\": \"Default\" }";

    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, pav_index_parse(bad_schema, &index, err, sizeof(err)));
    TEST_ASSERT_NULL(index);
    TEST_ASSERT_NOT_NULL(strstr(err, "schema"));

    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, pav_profile_parse(no_kind, &profile, err, sizeof(err)));
    TEST_ASSERT_NULL(profile);
    TEST_ASSERT_NOT_NULL(strstr(err, "kind"));
}

void test_reject_secret_keys(void) {
    static const char *keys[] = {"password", "token", "secret", "refresh_token"};
    size_t i;

    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        PavProfile *profile = NULL;
        char text[256];
        char err[128];

        snprintf(text, sizeof(text),
                 "{ \"kind\": \"pavillion-profile\", \"schema\": 1,"
                 " \"accounts\": [ { \"id\": \"a\", \"%s\": \"nope\" } ] }",
                 keys[i]);
        err[0] = '\0';
        TEST_ASSERT_EQUAL_INT(-1, pav_profile_parse(text, &profile, err, sizeof(err)));
        TEST_ASSERT_NULL(profile);
        TEST_ASSERT_NOT_NULL(strstr(err, keys[i]));
    }
}

void test_unknown_keys_survive_rewrite(void) {
    PavIndex *index = NULL;
    PavProfile *profile = NULL;
    char *index_text = NULL;
    char *profile_text = NULL;
    char err[128];

    TEST_ASSERT_EQUAL_INT(0, pav_index_parse(k_index, &index, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(0, pav_profile_parse(k_profile, &profile, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(0, pav_index_rewrite(index, &index_text));
    TEST_ASSERT_EQUAL_INT(0, pav_profile_rewrite(profile, &profile_text));
    TEST_ASSERT_NOT_NULL(strstr(index_text, "\"editor\""));
    TEST_ASSERT_NOT_NULL(strstr(index_text, "andrew"));
    TEST_ASSERT_NOT_NULL(strstr(profile_text, "\"note\""));
    TEST_ASSERT_NOT_NULL(strstr(profile_text, "keep-me"));
    TEST_ASSERT_NOT_NULL(strstr(profile_text, "\"accounts\""));

    pav_index_free(index);
    pav_profile_free(profile);
    free(index_text);
    free(profile_text);
}

static const char k_default_index[] =
    "{\n"
    "  \"kind\": \"pavillion-index\",\n"
    "  \"schema\": 1,\n"
    "  \"active\": \"default.json\",\n"
    "  \"profile_dir\": \"profiles\",\n"
    "  \"log_level\": \"info\",\n"
    "  \"next_account_number\": 1\n"
    "}\n";

static const char k_default_profile[] =
    "{\n"
    "  \"kind\": \"pavillion-profile\",\n"
    "  \"schema\": 1,\n"
    "  \"id\": \"default\",\n"
    "  \"name\": \"Default\",\n"
    "  \"storage\": {\n"
    "    \"engine\": \"sqlite\",\n"
    "    \"path\": \"default.sqlite\"\n"
    "  },\n"
    "  \"logs\": \"default.log.db\",\n"
    "  \"blobs\": \"blobs/default\",\n"
    "  \"cache\": \"cache/default\",\n"
    "  \"accounts\": [],\n"
    "  \"preferences\": {\n"
    "    \"timezone\": \"America/Vancouver\",\n"
    "    \"week_start\": \"monday\",\n"
    "    \"time_format\": \"24h\",\n"
    "    \"theme\": \"system\",\n"
    "    \"density\": \"comfortable\",\n"
    "    \"cache\": {\n"
    "      \"bodiesMb\": 2048,\n"
    "      \"blobsMb\": 4096\n"
    "    },\n"
    "    \"window\": {\n"
    "      \"maximized\": false,\n"
    "      \"width\": 1200,\n"
    "      \"height\": 800,\n"
    "      \"x\": null,\n"
    "      \"y\": null,\n"
    "      \"chrome\": \"system\"\n"
    "    },\n"
    "    \"ui\": {\n"
    "      \"zoom\": {\n"
    "        \"shell\": 1,\n"
    "        \"navButtons\": 1,\n"
    "        \"navBody\": 1,\n"
    "        \"mailList\": 1,\n"
    "        \"mailDetail\": 1\n"
    "      },\n"
    "      \"nav\": { \"width\": 320, \"min\": 200, \"max\": 500 },\n"
    "      \"mail\": { \"split\": 0.5, \"min\": 0.25, \"max\": 0.75 },\n"
    "      \"contacts\": { \"split\": 0.5, \"min\": 0.25, \"max\": 0.75 },\n"
    "      \"files\": { \"split\": 0.5, \"min\": 0.25, \"max\": 0.75 },\n"
    "      \"fontawesome_kit\": \"45801522ef\"\n"
    "    }\n"
    "  }\n"
    "}\n";

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    long n;
    char *buf;
    size_t got;

    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL_INT(0, fseek(f, 0, SEEK_END));
    n = ftell(f);
    TEST_ASSERT_TRUE(n >= 0);
    rewind(f);
    buf = calloc((size_t)n + 1, 1);
    TEST_ASSERT_NOT_NULL(buf);
    got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    TEST_ASSERT_EQUAL_INT((int)n, (int)got);
    return buf;
}

static char *take_stderr(void) {
    char *text;

    fflush(stderr);
    TEST_ASSERT_TRUE(g_err_path[0] != '\0');
    text = slurp(g_err_path);
    restore_stderr();
    return text;
}

static int count_text(const char *haystack, const char *needle) {
    int count = 0;
    const char *p;
    size_t n;

    if (haystack == NULL || needle == NULL) {
        return 0;
    }
    n = strlen(needle);
    if (n == 0) {
        return 0;
    }
    p = haystack;
    while ((p = strstr(p, needle)) != NULL) {
        count++;
        p += n;
    }
    return count;
}

static json_t *tail_lines(json_t *root) {
    json_t *lines = json_object_get(json_object_get(root, "data"), "lines");

    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    TEST_ASSERT_TRUE(json_is_array(lines));
    return lines;
}

static const char *line_text(json_t *line) {
    return json_string_value(json_object_get(line, "text"));
}

static int lines_have(json_t *lines, const char *text) {
    size_t i;

    for (i = 0; i < json_array_size(lines); i++) {
        const char *got = line_text(json_array_get(lines, i));

        if (got != NULL && strcmp(got, text) == 0) {
            return 1;
        }
    }
    return 0;
}

static json_int_t seq_of(json_t *lines, const char *text) {
    size_t i;

    for (i = 0; i < json_array_size(lines); i++) {
        json_t *line = json_array_get(lines, i);
        const char *got = line_text(line);

        if (got != NULL && strcmp(got, text) == 0) {
            json_t *seq = json_object_get(line, "seq");

            TEST_ASSERT_TRUE(json_is_integer(seq));
            return json_integer_value(seq);
        }
    }
    TEST_FAIL_MESSAGE("line not found");
    return -1;
}

void test_ensure_writes_defaults_once(void) {
    char dir[] = "/tmp/pav-cfg-XXXXXX";
    char index_path[160];
    char profile_path[160];
    char err[256];
    struct stat st;
    char *index_text;
    char *profile_text;
    FILE *out;

    TEST_ASSERT_NOT_NULL(mkdtemp(dir));
    memcpy(g_cfg_dir, dir, strlen(dir) + 1);
    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(0, pav_config_ensure(dir, err, sizeof(err)));

    snprintf(index_path, sizeof(index_path), "%s/Pavillion.json", dir);
    snprintf(profile_path, sizeof(profile_path), "%s/profiles/default.json", dir);
    TEST_ASSERT_EQUAL_INT(0, stat(index_path, &st));
    ASSERT_MODE(0600, st);
    TEST_ASSERT_EQUAL_INT(0, stat(profile_path, &st));
    ASSERT_MODE(0600, st);

    index_text = slurp(index_path);
    profile_text = slurp(profile_path);
    TEST_ASSERT_EQUAL_STRING(k_default_index, index_text);
    TEST_ASSERT_EQUAL_STRING(k_default_profile, profile_text);
    free(index_text);
    free(profile_text);

    out = fopen(profile_path, "wb");
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(fputs("changed\n", out) >= 0);
    TEST_ASSERT_EQUAL_INT(0, fclose(out));
    TEST_ASSERT_EQUAL_INT(0, pav_config_ensure(dir, err, sizeof(err)));
    profile_text = slurp(profile_path);
    index_text = slurp(index_path);
    TEST_ASSERT_EQUAL_STRING("changed\n", profile_text);
    TEST_ASSERT_EQUAL_STRING(k_default_index, index_text);
    free(profile_text);
    free(index_text);
}

static void make_slot(char *slot, size_t slot_len) {
    char templ[] = "/tmp/pav-cfg-XXXXXX";

    TEST_ASSERT_NOT_NULL(mkdtemp(templ));
    TEST_ASSERT_TRUE(strlen(templ) < slot_len);
    memcpy(slot, templ, strlen(templ) + 1);
}

static void write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");

    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_TRUE(fputs(text, f) >= 0);
    TEST_ASSERT_EQUAL_INT(0, fclose(f));
}

static void write_profile_logs(const char *path, const char *id, const char *name, const char *logs) {
    FILE *f = fopen(path, "wb");

    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_TRUE(fprintf(f,
        "{\"kind\":\"pavillion-profile\",\"schema\":1,\"id\":\"%s\",\"name\":\"%s\","
        "\"storage\":{\"engine\":\"sqlite\",\"path\":\"default.sqlite\"},"
        "\"logs\":\"%s\",\"blobs\":\"blobs/default\",\"cache\":\"cache/default\","
        "\"accounts\":[],"
        "\"preferences\":{\"timezone\":\"America/Vancouver\",\"week_start\":\"monday\","
        "\"time_format\":\"24h\",\"theme\":\"system\",\"density\":\"comfortable\","
        "\"zoom\":{\"shell\":1},"
        "\"window\":{\"maximized\":false,\"width\":1200,\"height\":800,"
        "\"x\":null,\"y\":null,\"chrome\":\"system\"}}}",
        id, name, logs) > 0);
    TEST_ASSERT_EQUAL_INT(0, fclose(f));
}

static void write_profile(const char *path, const char *id, const char *name) {
    write_profile_logs(path, id, name, "logs/default.log");
}

static int tree_has_sqlite(const char *path) {
    DIR *dir;
    struct dirent *ent;
    size_t n = strlen(path);

    if (n >= 7 && strcmp(path + n - 7, ".sqlite") == 0) {
        return 1;
    }
    dir = opendir(path);
    if (dir == NULL) {
        return 0;
    }
    while ((ent = readdir(dir)) != NULL) {
        char child[512];
        int found;

        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
        found = tree_has_sqlite(child);
        if (found) {
            closedir(dir);
            return 1;
        }
    }
    closedir(dir);
    return 0;
}

static void write_index(const char *path, const char *active, const char *profile_dir) {
    FILE *f = fopen(path, "wb");

    TEST_ASSERT_NOT_NULL(f);
    if (profile_dir == NULL) {
        TEST_ASSERT_TRUE(fprintf(f,
            "{\"kind\":\"pavillion-index\",\"schema\":1,\"active\":\"%s\",\"log_level\":\"info\"}",
            active) > 0);
    } else {
        TEST_ASSERT_TRUE(fprintf(f,
            "{\"kind\":\"pavillion-index\",\"schema\":1,\"active\":\"%s\","
            "\"profile_dir\":\"%s\",\"log_level\":\"info\"}",
            active, profile_dir) > 0);
    }
    TEST_ASSERT_EQUAL_INT(0, fclose(f));
}

void test_scan_ignores_stray_json_and_the_index(void) {
    char path[160];
    PavConfigLoad *load = NULL;
    char err[128];
    char *log;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    snprintf(path, sizeof(path), "%s/profiles", g_cfg_dir);
    TEST_ASSERT_EQUAL_INT(0, mkdir(path, 0700));
    snprintf(path, sizeof(path), "%s/Pavillion.json", g_cfg_dir);
    write_index(path, "default.json", "profiles");
    snprintf(path, sizeof(path), "%s/profiles/default.json", g_cfg_dir);
    write_profile(path, "default", "Default");
    snprintf(path, sizeof(path), "%s/profiles/notes.json", g_cfg_dir);
    write_text(path, "{\"kind\":\"notes\",\"schema\":1}");
    snprintf(path, sizeof(path), "%s/profiles/Pavillion.json", g_cfg_dir);
    write_text(path, "{\"kind\":\"pavillion-index\",\"schema\":1}");

    open_log();
    TEST_ASSERT_EQUAL_INT(0, pav_config_load(g_cfg_dir, &load, err, sizeof(err)));
    log = read_log();

    TEST_ASSERT_EQUAL_STRING("ready", load->state);
    TEST_ASSERT_EQUAL_STRING("Default", load->profile->name);
    TEST_ASSERT_NOT_NULL(strstr(log, "notes.json"));
    TEST_ASSERT_NULL(strstr(log, "Pavillion.json"));
    TEST_ASSERT_EQUAL_INT(0, access(path, F_OK));

    pav_config_load_free(load);
    free(log);
}

void test_scan_warns_on_id_mismatch(void) {
    char path[160];
    PavConfigLoad *load = NULL;
    char err[128];
    char *log;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    snprintf(path, sizeof(path), "%s/profiles", g_cfg_dir);
    TEST_ASSERT_EQUAL_INT(0, mkdir(path, 0700));
    snprintf(path, sizeof(path), "%s/Pavillion.json", g_cfg_dir);
    write_index(path, "default.json", "profiles");
    snprintf(path, sizeof(path), "%s/profiles/default.json", g_cfg_dir);
    write_profile(path, "default", "Default");
    snprintf(path, sizeof(path), "%s/profiles/other.json", g_cfg_dir);
    write_profile(path, "nope", "Other");

    open_log();
    TEST_ASSERT_EQUAL_INT(0, pav_config_load(g_cfg_dir, &load, err, sizeof(err)));
    log = read_log();

    TEST_ASSERT_EQUAL_STRING("ready", load->state);
    TEST_ASSERT_EQUAL_STRING("default", load->profile->id);
    TEST_ASSERT_NOT_NULL(strstr(log, "other.json"));
    TEST_ASSERT_NOT_NULL(strstr(log, "id does not match filename"));
    TEST_ASSERT_EQUAL_INT(0, access(path, F_OK));

    pav_config_load_free(load);
    free(log);
}

void test_broken_active_creates_nothing(void) {
    char path[160];
    PavConfigLoad *load = NULL;
    char err[128];
    char *text;
    char *log;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    snprintf(path, sizeof(path), "%s/profiles", g_cfg_dir);
    TEST_ASSERT_EQUAL_INT(0, mkdir(path, 0700));
    snprintf(path, sizeof(path), "%s/Pavillion.json", g_cfg_dir);
    write_index(path, "missing.json", "profiles");
    open_log();
    TEST_ASSERT_EQUAL_INT(0, pav_config_load(g_cfg_dir, &load, err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("error", load->state);
    TEST_ASSERT_NOT_NULL(strstr(load->error, "missing.json"));
    TEST_ASSERT_NULL(load->profile);
    snprintf(path, sizeof(path), "%s/profiles/missing.json", g_cfg_dir);
    TEST_ASSERT_TRUE(access(path, F_OK) != 0);
    pav_config_load_free(load);
    load = NULL;

    snprintf(path, sizeof(path), "%s/profiles/default.json", g_cfg_dir);
    write_text(path, "{");
    snprintf(path, sizeof(path), "%s/Pavillion.json", g_cfg_dir);
    write_index(path, "default.json", "profiles");
    TEST_ASSERT_EQUAL_INT(0, pav_config_load(g_cfg_dir, &load, err, sizeof(err)));
    log = read_log();
    TEST_ASSERT_EQUAL_STRING("error", load->state);
    TEST_ASSERT_NOT_NULL(strstr(load->error, "default.json"));
    TEST_ASSERT_NOT_NULL(strstr(log, "default.json"));
    snprintf(path, sizeof(path), "%s/profiles/default.json", g_cfg_dir);
    text = slurp(path);
    TEST_ASSERT_EQUAL_STRING("{", text);
    free(text);
    free(log);
    pav_config_load_free(load);
}

void test_profile_dir_default_and_absolute(void) {
    char path[160];
    PavConfigLoad *load = NULL;
    char err[128];

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    snprintf(path, sizeof(path), "%s/profiles", g_cfg_dir);
    TEST_ASSERT_EQUAL_INT(0, mkdir(path, 0700));
    snprintf(path, sizeof(path), "%s/Pavillion.json", g_cfg_dir);
    write_index(path, "default.json", NULL);
    snprintf(path, sizeof(path), "%s/profiles/default.json", g_cfg_dir);
    write_profile(path, "default", "Default");
    TEST_ASSERT_EQUAL_INT(0, pav_config_load(g_cfg_dir, &load, err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("ready", load->state);
    TEST_ASSERT_EQUAL_STRING("Default", load->profile->name);
    pav_config_load_free(load);
    load = NULL;
    remove_cfg_dir();

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    make_slot(g_extra_dir, sizeof(g_extra_dir));
    snprintf(path, sizeof(path), "%s/Pavillion.json", g_cfg_dir);
    write_index(path, "default.json", g_extra_dir);
    snprintf(path, sizeof(path), "%s/default.json", g_extra_dir);
    write_profile(path, "default", "Default");
    TEST_ASSERT_EQUAL_INT(0, pav_config_load(g_cfg_dir, &load, err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("ready", load->state);
    TEST_ASSERT_EQUAL_STRING("default", load->profile->id);
    snprintf(path, sizeof(path), "%s/profiles", g_cfg_dir);
    TEST_ASSERT_TRUE(access(path, F_OK) != 0);
    pav_config_load_free(load);
}

void test_prepare_resolves_paths_and_skips_sqlite(void) {
    char path[160];
    char expect[160];
    char absolute_log[160];
    PavConfigLoad *load = NULL;
    char err[256];
    struct stat st;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    snprintf(path, sizeof(path), "%s/profiles", g_cfg_dir);
    TEST_ASSERT_EQUAL_INT(0, mkdir(path, 0700));
    snprintf(path, sizeof(path), "%s/Pavillion.json", g_cfg_dir);
    write_index(path, "default.json", "profiles");
    snprintf(path, sizeof(path), "%s/profiles/default.json", g_cfg_dir);
    write_profile(path, "default", "Default");
    TEST_ASSERT_EQUAL_INT(0, pav_config_load(g_cfg_dir, &load, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(0, pav_config_prepare(g_cfg_dir, load->profile, err, sizeof(err)));

    snprintf(expect, sizeof(expect), "%s/default.log.db", g_cfg_dir);
    TEST_ASSERT_EQUAL_STRING(expect, load->profile->resolved_logs);
    TEST_ASSERT_TRUE(access(expect, F_OK) != 0);
    snprintf(expect, sizeof(expect), "%s/logs", g_cfg_dir);
    TEST_ASSERT_TRUE(access(expect, F_OK) != 0);

    snprintf(expect, sizeof(expect), "%s/blobs/default", g_cfg_dir);
    TEST_ASSERT_EQUAL_STRING(expect, load->profile->resolved_blobs);
    TEST_ASSERT_EQUAL_INT(0, stat(expect, &st));
    ASSERT_MODE(0700, st);

    snprintf(expect, sizeof(expect), "%s/cache/default", g_cfg_dir);
    TEST_ASSERT_EQUAL_STRING(expect, load->profile->resolved_cache);
    TEST_ASSERT_EQUAL_INT(0, stat(expect, &st));
    TEST_ASSERT_TRUE(S_ISDIR(st.st_mode));

    snprintf(expect, sizeof(expect), "%s/default.sqlite", g_cfg_dir);
    TEST_ASSERT_EQUAL_STRING(expect, load->profile->resolved_storage);
    TEST_ASSERT_TRUE(access(expect, F_OK) != 0);
    TEST_ASSERT_EQUAL_INT(0, tree_has_sqlite(g_cfg_dir));
    pav_config_load_free(load);
    load = NULL;
    remove_cfg_dir();

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    make_slot(g_extra_dir, sizeof(g_extra_dir));
    snprintf(absolute_log, sizeof(absolute_log), "%s/app.log.db", g_extra_dir);
    snprintf(path, sizeof(path), "%s/profiles", g_cfg_dir);
    TEST_ASSERT_EQUAL_INT(0, mkdir(path, 0700));
    snprintf(path, sizeof(path), "%s/Pavillion.json", g_cfg_dir);
    write_index(path, "default.json", "profiles");
    snprintf(path, sizeof(path), "%s/profiles/default.json", g_cfg_dir);
    write_profile_logs(path, "default", "Default", absolute_log);
    TEST_ASSERT_EQUAL_INT(0, pav_config_load(g_cfg_dir, &load, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(0, pav_config_prepare(g_cfg_dir, load->profile, err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING(absolute_log, load->profile->resolved_logs);
    TEST_ASSERT_TRUE(access(absolute_log, F_OK) != 0);
    snprintf(path, sizeof(path), "%s/logs/default.log", g_cfg_dir);
    TEST_ASSERT_TRUE(access(path, F_OK) != 0);
    pav_config_load_free(load);
    load = NULL;
    remove_cfg_dir();

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    snprintf(path, sizeof(path), "%s/blocked", g_cfg_dir);
    write_text(path, "not a directory");
    snprintf(path, sizeof(path), "%s/profiles", g_cfg_dir);
    TEST_ASSERT_EQUAL_INT(0, mkdir(path, 0700));
    snprintf(path, sizeof(path), "%s/Pavillion.json", g_cfg_dir);
    write_index(path, "default.json", "profiles");
    snprintf(path, sizeof(path), "%s/profiles/default.json", g_cfg_dir);
    write_profile_logs(path, "default", "Default", "blocked/app.log.db");
    TEST_ASSERT_EQUAL_INT(0, pav_config_load(g_cfg_dir, &load, err, sizeof(err)));
    /* prepare no longer touches the log path (H-4). */
    TEST_ASSERT_EQUAL_INT(0, pav_config_prepare(g_cfg_dir, load->profile, err, sizeof(err)));
    snprintf(expect, sizeof(expect), "%s/blocked/app.log.db", g_cfg_dir);
    TEST_ASSERT_TRUE(access(expect, F_OK) != 0);
    TEST_ASSERT_EQUAL_INT(0, tree_has_sqlite(g_cfg_dir));
    pav_config_load_free(load);
}

static void profile_text(char *dst, size_t dst_len, const char *storage) {
    int wrote = snprintf(dst, dst_len,
        "{\"kind\":\"pavillion-profile\",\"schema\":1,\"id\":\"default\",\"name\":\"Default\","
        "\"storage\":{%s},"
        "\"logs\":\"logs/default.log\",\"blobs\":\"blobs/default\",\"cache\":\"cache/default\","
        "\"accounts\":[],"
        "\"preferences\":{\"timezone\":\"America/Vancouver\",\"week_start\":\"monday\","
        "\"time_format\":\"24h\",\"theme\":\"system\",\"density\":\"comfortable\","
        "\"zoom\":{\"shell\":1},"
        "\"window\":{\"maximized\":false,\"width\":1200,\"height\":800,"
        "\"x\":null,\"y\":null,\"chrome\":\"system\"}}}",
        storage);

    TEST_ASSERT_TRUE(wrote > 0 && (size_t)wrote < dst_len);
}

/* 001's seven tables, 002's mail tables, and the contentless email_fts shadows. */
enum { PAV_STORE_TABLES = 23 };

static int user_table_count(const char *path) {
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    int count = -1;

    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        if (db != NULL) {
            sqlite3_close(db);
        }
        return -1;
    }
    if (sqlite3_prepare_v2(db,
            "SELECT count(*) FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'",
            -1, &stmt, NULL) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return count;
}

void test_sqlite_open_exec_and_reopen(void) {
    char path[160];
    char err[256];
    PavStore *store = NULL;
    struct stat st;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    snprintf(path, sizeof(path), "%s/mail.sqlite", g_cfg_dir);

    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, pav_store_open(&store, "db2", path, err, sizeof(err)));
    TEST_ASSERT_NULL(store);
    TEST_ASSERT_NOT_NULL(strstr(err, "db2"));
    TEST_ASSERT_TRUE(access(path, F_OK) != 0);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, pav_store_open(&store, "sqlite", path, err, sizeof(err)), err);
    TEST_ASSERT_NOT_NULL(store);
    TEST_ASSERT_EQUAL_INT(0, stat(path, &st));
    TEST_ASSERT_TRUE(S_ISREG(st.st_mode));
    ASSERT_MODE(0600, st);
    TEST_ASSERT_EQUAL_INT(0, pav_store_exec(store, "SELECT 1", err, sizeof(err)));
    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, pav_store_exec(store, "SELECT", err, sizeof(err)));
    TEST_ASSERT_TRUE(err[0] != '\0');
    pav_store_close(store);
    store = NULL;

    TEST_ASSERT_EQUAL_INT(0, stat(path, &st));
    ASSERT_MODE(0600, st);
    TEST_ASSERT_EQUAL_INT(0, user_table_count(path));

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, pav_store_open(&store, "sqlite", path, err, sizeof(err)), err);
    TEST_ASSERT_EQUAL_INT(0, pav_store_exec(store, "SELECT 1", err, sizeof(err)));
    pav_store_close(store);
}

void test_engine_sqlite_or_named_error(void) {
    char text[1024];
    char err[128];
    PavProfile *profile = NULL;

    profile_text(text, sizeof(text), "\"path\":\"default.sqlite\"");
    TEST_ASSERT_EQUAL_INT(0, pav_profile_parse(text, &profile, err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("sqlite", profile->engine);
    TEST_ASSERT_EQUAL_INT(0, pav_profile_check_engine(profile, err, sizeof(err)));
    pav_profile_free(profile);
    profile = NULL;

    profile_text(text, sizeof(text), "\"engine\":\"sqlite\",\"path\":\"default.sqlite\"");
    TEST_ASSERT_EQUAL_INT(0, pav_profile_parse(text, &profile, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(0, pav_profile_check_engine(profile, err, sizeof(err)));
    pav_profile_free(profile);
    profile = NULL;

    profile_text(text, sizeof(text), "\"engine\":\"db2\",\"path\":\"default.sqlite\"");
    TEST_ASSERT_EQUAL_INT(0, pav_profile_parse(text, &profile, err, sizeof(err)));
    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, pav_profile_check_engine(profile, err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(err, "db2"));
    pav_profile_free(profile);
}

void test_start_loads_default_profile(void) {
    char path[160];
    PavillionStatus status;
    struct stat st;
    char *text;

    open_log();
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, pavillion_status(&status));
    TEST_ASSERT_EQUAL_STRING("ready", status.state);
    TEST_ASSERT_EQUAL_STRING("Default", status.profile_name);
    TEST_ASSERT_EQUAL_STRING("default", status.profile_id);
    TEST_ASSERT_EQUAL_STRING("", status.error);

    snprintf(path, sizeof(path), "%s/default.sqlite", g_cfg_dir);
    TEST_ASSERT_EQUAL_INT(0, stat(path, &st));
    TEST_ASSERT_TRUE(S_ISREG(st.st_mode));
    ASSERT_MODE(0600, st);

    pavillion_stop();
    TEST_ASSERT_EQUAL_INT(0, pav_core_thread_alive());
    text = read_log();
    TEST_ASSERT_NOT_NULL(strstr(text, " INFO  core  core ready"));
    free(text);
}

void test_start_errors_leave_the_thread_up(void) {
    char path[160];
    char text[1024];
    PavillionStatus status;
    char *body;
    char err[128];

    open_log();
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(NULL, NULL, 0));
    TEST_ASSERT_EQUAL_INT(1, pav_core_thread_alive());
    TEST_ASSERT_EQUAL_INT(0, pavillion_status(&status));
    TEST_ASSERT_EQUAL_STRING("error", status.state);
    TEST_ASSERT_NOT_NULL(strstr(status.error, "missing workdir"));
    pavillion_stop();

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    snprintf(path, sizeof(path), "%s/not-a-dir", g_cfg_dir);
    write_text(path, "x");
    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, pavillion_start(path, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(0, pav_core_thread_alive());
    TEST_ASSERT_NOT_NULL(strstr(err, "cannot create workdir"));
    pavillion_stop();
    remove_cfg_dir();

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    snprintf(path, sizeof(path), "%s/profiles", g_cfg_dir);
    TEST_ASSERT_EQUAL_INT(0, mkdir(path, 0700));
    snprintf(path, sizeof(path), "%s/Pavillion.json", g_cfg_dir);
    write_index(path, "default.json", "profiles");
    snprintf(path, sizeof(path), "%s/profiles/default.json", g_cfg_dir);
    write_text(path, "{");
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    TEST_ASSERT_EQUAL_INT(1, pav_core_thread_alive());
    TEST_ASSERT_EQUAL_INT(0, pavillion_status(&status));
    TEST_ASSERT_EQUAL_STRING("error", status.state);
    TEST_ASSERT_NOT_NULL(strstr(status.error, "default.json"));
    body = slurp(path);
    TEST_ASSERT_EQUAL_STRING("{", body);
    free(body);
    TEST_ASSERT_EQUAL_INT(0, tree_has_sqlite(g_cfg_dir));
    pavillion_stop();
    remove_cfg_dir();

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    snprintf(path, sizeof(path), "%s/profiles", g_cfg_dir);
    TEST_ASSERT_EQUAL_INT(0, mkdir(path, 0700));
    snprintf(path, sizeof(path), "%s/Pavillion.json", g_cfg_dir);
    write_index(path, "default.json", "profiles");
    snprintf(path, sizeof(path), "%s/profiles/default.json", g_cfg_dir);
    profile_text(text, sizeof(text), "\"engine\":\"db2\",\"path\":\"default.sqlite\"");
    write_text(path, text);
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    TEST_ASSERT_EQUAL_INT(1, pav_core_thread_alive());
    TEST_ASSERT_EQUAL_INT(0, pavillion_status(&status));
    TEST_ASSERT_EQUAL_STRING("error", status.state);
    TEST_ASSERT_NOT_NULL(strstr(status.error, "db2"));
    TEST_ASSERT_EQUAL_STRING("Default", status.profile_name);
    TEST_ASSERT_EQUAL_INT(0, tree_has_sqlite(g_cfg_dir));
    pavillion_stop();
}

void test_prefs_round_trip_clamp_and_mask(void) {
    PavShellPrefs prefs;
    char err[256];
    char path[160];
    struct stat st;

    memset(&prefs, 0, sizeof(prefs));
    TEST_ASSERT_EQUAL_INT(-1, pavillion_prefs_get(&prefs));
    open_log();
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, pavillion_prefs_get(&prefs));
    TEST_ASSERT_EQUAL_INT(0, prefs.window_maximized);
    TEST_ASSERT_EQUAL_INT(1200, prefs.window_width);
    TEST_ASSERT_EQUAL_INT(800, prefs.window_height);
    TEST_ASSERT_EQUAL_INT(0, prefs.window_x_set);
    TEST_ASSERT_EQUAL_INT(0, prefs.window_y_set);
    TEST_ASSERT_EQUAL_STRING("system", prefs.chrome);

    prefs.window_width = 1500;
    prefs.window_height = 900;
    prefs.window_x_set = 1;
    prefs.window_y_set = 1;
    prefs.window_x = 12;
    prefs.window_y = 34;
    prefs.window_maximized = 0;
    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(0, pavillion_prefs_apply(&prefs, PAV_PREFS_WINDOW, err, sizeof(err)));
    memset(&prefs, 0, sizeof(prefs));
    TEST_ASSERT_EQUAL_INT(0, pavillion_prefs_get(&prefs));
    TEST_ASSERT_EQUAL_INT(1500, prefs.window_width);
    TEST_ASSERT_EQUAL_INT(900, prefs.window_height);
    TEST_ASSERT_EQUAL_INT(1, prefs.window_x_set);
    TEST_ASSERT_EQUAL_INT(1, prefs.window_y_set);
    TEST_ASSERT_EQUAL_INT(12, prefs.window_x);
    TEST_ASSERT_EQUAL_INT(34, prefs.window_y);
    TEST_ASSERT_EQUAL_STRING("system", prefs.chrome);

    prefs.window_width = 111;
    TEST_ASSERT_EQUAL_INT(-1, pavillion_prefs_apply(&prefs, 0, err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(err, "nothing to save"));
    TEST_ASSERT_EQUAL_INT(0, pavillion_prefs_get(&prefs));
    TEST_ASSERT_EQUAL_INT(1500, prefs.window_width);

    prefs.window_width = 1600;
    prefs.window_height = 1000;
    prefs.window_maximized = 1;
    TEST_ASSERT_EQUAL_INT(0, pavillion_prefs_apply(&prefs, PAV_PREFS_WINDOW, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(0, pavillion_prefs_get(&prefs));
    TEST_ASSERT_EQUAL_INT(1600, prefs.window_width);
    TEST_ASSERT_EQUAL_INT(1000, prefs.window_height);
    TEST_ASSERT_EQUAL_INT(1, prefs.window_maximized);
    TEST_ASSERT_EQUAL_STRING("system", prefs.chrome);

    memcpy(prefs.chrome, "nope", sizeof("nope"));
    TEST_ASSERT_EQUAL_INT(-1, pavillion_prefs_apply(&prefs, PAV_PREFS_WINDOW, err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(err, "chrome"));
    memcpy(prefs.chrome, "system", sizeof("system"));
    prefs.window_width = 10;
    TEST_ASSERT_EQUAL_INT(-1, pavillion_prefs_apply(&prefs, PAV_PREFS_WINDOW, err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(err, "window size"));
    TEST_ASSERT_EQUAL_INT(0, pavillion_prefs_get(&prefs));
    TEST_ASSERT_EQUAL_INT(1600, prefs.window_width);
    TEST_ASSERT_EQUAL_STRING("system", prefs.chrome);

    prefs.window_x_set = 0;
    prefs.window_y_set = 0;
    prefs.window_maximized = 0;
    TEST_ASSERT_EQUAL_INT(0, pavillion_prefs_apply(&prefs, PAV_PREFS_WINDOW, err, sizeof(err)));
    snprintf(path, sizeof(path), "%s/profiles/default.json", g_cfg_dir);
    TEST_ASSERT_EQUAL_INT(0, stat(path, &st));
    ASSERT_MODE(0600, st);

    pavillion_stop();
    TEST_ASSERT_EQUAL_INT(-1, pavillion_prefs_apply(&prefs, PAV_PREFS_WINDOW, err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(err, "not running"));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, pavillion_prefs_get(&prefs));
    TEST_ASSERT_EQUAL_INT(1600, prefs.window_width);
    TEST_ASSERT_EQUAL_INT(1000, prefs.window_height);
    TEST_ASSERT_EQUAL_INT(0, prefs.window_maximized);
    TEST_ASSERT_EQUAL_INT(0, prefs.window_x_set);
    TEST_ASSERT_EQUAL_INT(0, prefs.window_y_set);
    TEST_ASSERT_EQUAL_STRING("system", prefs.chrome);
    pavillion_stop();
}

void test_missing_ui_is_inserted(void) {
    char path[160];
    char err[256];
    struct stat st;
    char *text;
    char *ui;
    char *zoom;

    open_log();
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    snprintf(path, sizeof(path), "%s/profiles", g_cfg_dir);
    TEST_ASSERT_EQUAL_INT(0, mkdir(path, 0700));
    snprintf(path, sizeof(path), "%s/Pavillion.json", g_cfg_dir);
    write_index(path, "default.json", "profiles");
    snprintf(path, sizeof(path), "%s/profiles/default.json", g_cfg_dir);
    write_text(path,
        "{\"kind\":\"pavillion-profile\",\"schema\":1,\"id\":\"default\",\"name\":\"Default\","
        "\"note\":\"keep-me\","
        "\"storage\":{\"engine\":\"sqlite\",\"path\":\"default.sqlite\"},"
        "\"logs\":\"logs/default.log\",\"blobs\":\"blobs/default\",\"cache\":\"cache/default\","
        "\"accounts\":[],"
        "\"preferences\":{\"timezone\":\"America/Vancouver\",\"week_start\":\"monday\","
        "\"time_format\":\"24h\",\"theme\":\"system\",\"density\":\"comfortable\","
        "\"zoom\":{\"shell\":1},"
        "\"window\":{\"maximized\":false,\"width\":1200,\"height\":800,"
        "\"x\":null,\"y\":null,\"chrome\":\"system\"}}}");
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(0, pavillion_ui_get(&ui, err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(ui, "\"shell\": 1"));
    TEST_ASSERT_NULL(strstr(ui, "navButtons"));
    TEST_ASSERT_NULL(strstr(ui, "\"width\""));
    free(ui);
    text = slurp(path);
    TEST_ASSERT_NOT_NULL(strstr(text, "keep-me"));
    TEST_ASSERT_NOT_NULL(strstr(text, "\"schema\": 1"));
    zoom = strstr(text, "\"zoom\"");
    ui = strstr(text, "\"ui\"");
    TEST_ASSERT_NOT_NULL(zoom);
    TEST_ASSERT_NOT_NULL(ui);
    TEST_ASSERT_TRUE(zoom > ui);
    free(text);
    TEST_ASSERT_EQUAL_INT(0, stat(path, &st));
    ASSERT_MODE(0600, st);
    pavillion_stop();
}

void test_ui_blob_round_trip(void) {
    char err[256];
    char *ui = NULL;
    char *big;
    size_t i;

    open_log();
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(0, pavillion_ui_get(&ui, err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(ui, "\"width\": 320"));
    TEST_ASSERT_NOT_NULL(strstr(ui, "\"shell\": 1"));
    free(ui);
    ui = NULL;

    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(0, pavillion_ui_set("{\"zoom\":{\"shell\":9},\"nav\":{\"width\":1},\"note\":\"kept\"}",
                                              err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(0, pavillion_ui_get(&ui, err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(ui, "\"shell\": 9"));
    TEST_ASSERT_NOT_NULL(strstr(ui, "\"width\": 1"));
    TEST_ASSERT_NOT_NULL(strstr(ui, "kept"));
    free(ui);
    ui = NULL;

    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, pavillion_ui_set("[]", err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(err, "object"));
    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, pavillion_ui_set("{\"password\":\"x\"}", err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(err, "password"));
    TEST_ASSERT_EQUAL_INT(0, pavillion_ui_get(&ui, err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(ui, "\"shell\": 9"));
    TEST_ASSERT_NULL(strstr(ui, "password"));
    free(ui);

    big = malloc(PAV_UI_JSON_MAX + 64);
    TEST_ASSERT_NOT_NULL(big);
    memcpy(big, "{\"pad\":\"", 8);
    for (i = 8; i < PAV_UI_JSON_MAX + 8; i++) {
        big[i] = 'a';
    }
    memcpy(big + PAV_UI_JSON_MAX + 8, "\"}", 3);
    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, pavillion_ui_set(big, err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(err, "too large"));
    free(big);
    pavillion_stop();
}

void test_profile_migrates_old_zoom_into_ui(void) {
    char path[160];
    char err[256];
    char *fixture;
    char *text;
    char *again;
    char *ui;
    char *zoom;
    char *blob = NULL;

    open_log();
    fixture = slurp("tests/fixtures/default-2026-10-16.json");
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    snprintf(path, sizeof(path), "%s/profiles", g_cfg_dir);
    TEST_ASSERT_EQUAL_INT(0, mkdir(path, 0700));
    snprintf(path, sizeof(path), "%s/Pavillion.json", g_cfg_dir);
    write_index(path, "default.json", "profiles");
    snprintf(path, sizeof(path), "%s/profiles/default.json", g_cfg_dir);
    write_text(path, fixture);
    free(fixture);
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(0, pavillion_ui_get(&blob, err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(blob, "1.7"));
    TEST_ASSERT_NOT_NULL(strstr(blob, "1.45"));
    TEST_ASSERT_NOT_NULL(strstr(blob, "354"));
    TEST_ASSERT_NOT_NULL(strstr(blob, "paper-plane"));
    free(blob);
    text = slurp(path);
    TEST_ASSERT_NOT_NULL(strstr(text, "\"schema\": 1"));
    TEST_ASSERT_NOT_NULL(strstr(text, "2361"));
    zoom = strstr(text, "\"zoom\"");
    ui = strstr(text, "\"ui\"");
    TEST_ASSERT_NOT_NULL(zoom);
    TEST_ASSERT_NOT_NULL(ui);
    TEST_ASSERT_TRUE(zoom > ui);
    pavillion_stop();
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    again = slurp(path);
    TEST_ASSERT_EQUAL_STRING(text, again);
    free(text);
    free(again);
    pavillion_stop();
}

void test_kit_and_icons_read_after_start(void) {
    char err[512];
    char kit_buf[256];
    char *icons_json = NULL;

    open_log();
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));

    err[0] = '\0';
    kit_buf[0] = '\0';
    TEST_ASSERT_EQUAL_INT(0, pavillion_kit_get(kit_buf, sizeof(kit_buf), err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("45801522ef", kit_buf);

    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(0, pavillion_icons_get(&icons_json, err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(icons_json);
    free(icons_json);
    icons_json = NULL;

    pavillion_stop();
}

void test_kit_and_icons_apply_round_trip(void) {
    char err[512];
    char kit_buf[256];
    char *icons_json = NULL;

    open_log();
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));

    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(0, pavillion_kit_apply("abc123", err, sizeof(err)));
    kit_buf[0] = '\0';
    TEST_ASSERT_EQUAL_INT(0, pavillion_kit_get(kit_buf, sizeof(kit_buf), err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("abc123", kit_buf);

    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(0, pavillion_icons_apply("{\"save\":\"fa-solid fa-save\"}", err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(0, pavillion_icons_get(&icons_json, err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(icons_json);
    TEST_ASSERT_NOT_NULL(strstr(icons_json, "fa-solid fa-save"));
    free(icons_json);
    icons_json = NULL;

    pavillion_stop();
}

void test_api_version_matches_header(void) {
    int version = pavillion_api_version();

    TEST_ASSERT_EQUAL_INT(PAVILLION_API_VERSION, version);
    TEST_ASSERT_TRUE(version >= 1);
}

void test_cfg_str_defaults_and_rejects_wrong_type(void) {
    json_t *obj = json_object();
    char out[16];
    char err[64];

    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(0, cfg_str(obj, "name", "fallback", out, sizeof(out), err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("fallback", out);

    TEST_ASSERT_EQUAL_INT(0, cfg_set_str(obj, "name", "kept", err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(0, cfg_str(obj, "name", "fallback", out, sizeof(out), err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("kept", out);

    TEST_ASSERT_EQUAL_INT(0, cfg_set_int(obj, "name", 1, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(-1, cfg_str(obj, "name", "fallback", out, sizeof(out), err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(err, "name"));

    json_object_del(obj, "name");
    TEST_ASSERT_EQUAL_INT(-1, cfg_str(obj, "name", NULL, out, sizeof(out), err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(err, "name"));

    TEST_ASSERT_EQUAL_INT(0, cfg_set_str(obj, "name", "this-value-is-too-long", err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(-1, cfg_str(obj, "name", NULL, out, sizeof(out), err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(err, "too long"));

    json_decref(obj);
}

void test_cfg_num_range_clamps_and_steps(void) {
    json_t *obj = json_object();
    double out = -1;
    double written = -1;
    char err[64];

    TEST_ASSERT_EQUAL_INT(0, cfg_num_range(obj, "n", 0.5, 0.0, 1.0, 0.25, &out, err, sizeof(err)));
    TEST_ASSERT_EQUAL_DOUBLE(0.5, out);

    TEST_ASSERT_EQUAL_INT(0, cfg_set_num(obj, "n", 0.4, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(0, cfg_num_range(obj, "n", 0.5, 0.0, 1.0, 0, &out, err, sizeof(err)));
    TEST_ASSERT_EQUAL_DOUBLE(0.4, out);

    TEST_ASSERT_EQUAL_INT(0, cfg_num_range(obj, "n", 0.5, 0.0, 1.0, 0.25, &out, err, sizeof(err)));
    TEST_ASSERT_EQUAL_DOUBLE(0.5, out);

    TEST_ASSERT_EQUAL_INT(0, cfg_set_num(obj, "n", 2.0, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(0, cfg_num_range(obj, "n", 0.5, 0.0, 1.0, 0, &out, err, sizeof(err)));
    TEST_ASSERT_EQUAL_DOUBLE(1.0, out);

    TEST_ASSERT_EQUAL_INT(0, cfg_set_num(obj, "n", -1.0, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(0, cfg_num_range(obj, "n", 0.5, 0.0, 1.0, 0, &out, err, sizeof(err)));
    TEST_ASSERT_EQUAL_DOUBLE(0.0, out);

    TEST_ASSERT_EQUAL_INT(0, cfg_set_str(obj, "n", "nope", err, sizeof(err)));
    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, cfg_num_range(obj, "n", 0.5, 0.0, 1.0, 0, &out, err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(err, "n"));
    TEST_ASSERT_EQUAL_INT(0, cfg_set_num(obj, "n", 0.4, err, sizeof(err)));
    TEST_ASSERT_EQUAL_DOUBLE(0.4, json_real_value(json_object_get(obj, "n")));

    out = 0;
    TEST_ASSERT_EQUAL_INT(-1, cfg_set_num_range(obj, "n", 0.0 / 0.0, 0.0, 1.0, 0, &out, err, sizeof(err)));
    TEST_ASSERT_EQUAL_DOUBLE(0.4, json_real_value(json_object_get(obj, "n")));
    TEST_ASSERT_EQUAL_DOUBLE(0, out);

    TEST_ASSERT_EQUAL_INT(0, cfg_set_num_range(obj, "n", 0.4, 0.0, 1.0, 0.25, &written, err, sizeof(err)));
    TEST_ASSERT_EQUAL_DOUBLE(0.5, written);
    TEST_ASSERT_EQUAL_DOUBLE(0.5, json_real_value(json_object_get(obj, "n")));

    json_decref(obj);
}

void test_cfg_int_range_clamps_and_steps(void) {
    json_t *obj = json_object();
    int out = -1;
    int written = -1;
    char err[64];

    TEST_ASSERT_EQUAL_INT(0, cfg_int_range(obj, "n", 14, 0, 100, 10, &out, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(10, out);

    TEST_ASSERT_EQUAL_INT(0, cfg_set_int(obj, "n", 14, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(0, cfg_int_range(obj, "n", 0, 0, 100, 0, &out, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(14, out);
    TEST_ASSERT_EQUAL_INT(0, cfg_int_range(obj, "n", 0, 0, 100, 10, &out, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(10, out);

    TEST_ASSERT_EQUAL_INT(0, cfg_set_int(obj, "n", 15, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(0, cfg_int_range(obj, "n", 0, 0, 100, 10, &out, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(20, out);

    TEST_ASSERT_EQUAL_INT(0, cfg_set_int(obj, "n", 250, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(0, cfg_int_range(obj, "n", 0, 0, 100, 0, &out, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(100, out);

    TEST_ASSERT_EQUAL_INT(0, cfg_set_num(obj, "n", 1.0, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(-1, cfg_int_range(obj, "n", 0, 0, 100, 0, &out, err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(err, "n"));

    TEST_ASSERT_EQUAL_INT(0, cfg_set_int_range(obj, "n", 14, 0, 100, 10, &written, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(10, written);
    TEST_ASSERT_EQUAL_INT(10, (int)json_integer_value(json_object_get(obj, "n")));

    json_decref(obj);
}

void test_cfg_bool_defaults_and_rejects_wrong_type(void) {
    json_t *obj = json_object();
    int out = -1;
    char err[64];

    TEST_ASSERT_EQUAL_INT(0, cfg_bool(obj, "on", 1, &out, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(1, out);

    TEST_ASSERT_EQUAL_INT(0, cfg_set_bool(obj, "on", 0, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(0, cfg_bool(obj, "on", 1, &out, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(0, out);
    TEST_ASSERT_TRUE(json_is_false(json_object_get(obj, "on")));

    TEST_ASSERT_EQUAL_INT(0, cfg_set_str(obj, "on", "yes", err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(-1, cfg_bool(obj, "on", 1, &out, err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(err, "on"));
    TEST_ASSERT_TRUE(json_is_string(json_object_get(obj, "on")));

    json_decref(obj);
}

void test_cfg_enum_rejects_unknown(void) {
    json_t *obj = json_object();
    char out[16];
    char err[80];
    static const char *const allowed[] = {"system", "hidden", NULL};

    TEST_ASSERT_EQUAL_INT(0, cfg_enum(obj, "chrome", allowed, "system", out, sizeof(out), err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("system", out);

    TEST_ASSERT_EQUAL_INT(0, cfg_set_enum(obj, "chrome", "hidden", allowed, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(0, cfg_enum(obj, "chrome", allowed, "system", out, sizeof(out), err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("hidden", out);

    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, cfg_set_enum(obj, "chrome", "nope", allowed, err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(err, "chrome"));
    TEST_ASSERT_EQUAL_STRING("hidden", json_string_value(json_object_get(obj, "chrome")));

    TEST_ASSERT_EQUAL_INT(0, cfg_set_int(obj, "chrome", 1, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(-1, cfg_enum(obj, "chrome", allowed, "system", out, sizeof(out), err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(err, "chrome"));

    json_object_del(obj, "chrome");
    TEST_ASSERT_EQUAL_INT(-1, cfg_enum(obj, "chrome", allowed, NULL, out, sizeof(out), err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(err, "chrome"));

    json_decref(obj);
}

void test_cfg_obj_and_at_do_not_replace_or_create(void) {
    json_t *root = json_object();
    json_t *prefs;
    json_t *ui;
    json_t *nav;
    char err[64];

    TEST_ASSERT_NULL(cfg_at(root, "preferences.ui.nav"));
    TEST_ASSERT_NULL(json_object_get(root, "preferences"));

    prefs = cfg_obj(root, "preferences", 1);
    TEST_ASSERT_TRUE(json_is_object(prefs));
    TEST_ASSERT_EQUAL_PTR(prefs, cfg_obj(root, "preferences", 0));

    TEST_ASSERT_EQUAL_INT(0, cfg_set_str(prefs, "ui", "nope", err, sizeof(err)));
    TEST_ASSERT_NULL(cfg_obj(prefs, "ui", 1));
    TEST_ASSERT_TRUE(json_is_string(json_object_get(prefs, "ui")));

    json_object_del(prefs, "ui");
    ui = cfg_obj(prefs, "ui", 1);
    nav = cfg_obj(ui, "nav", 1);
    TEST_ASSERT_EQUAL_INT(0, cfg_set_int(nav, "width", 320, err, sizeof(err)));
    TEST_ASSERT_EQUAL_PTR(nav, cfg_at(root, "preferences.ui.nav"));
    TEST_ASSERT_EQUAL_INT(320, (int)json_integer_value(cfg_at(root, "preferences.ui.nav.width")));

    TEST_ASSERT_EQUAL_INT(0, cfg_set_null(nav, "x", err, sizeof(err)));
    TEST_ASSERT_TRUE(json_is_null(cfg_at(root, "preferences.ui.nav.x")));
    TEST_ASSERT_NULL(cfg_at(root, "preferences.ui.missing"));
    TEST_ASSERT_NULL(json_object_get(ui, "missing"));
    TEST_ASSERT_NULL(cfg_at(root, "preferences.ui.nav."));

    json_decref(root);
}

void test_profile_read_kit_skips_zoom_window(void) {
    PavProfile *profile = NULL;
    char err[256];
    char kit_buf[64];
    char *icons = NULL;

    TEST_ASSERT_EQUAL_INT(0, pav_profile_parse(k_default_profile, &profile, err, sizeof(err)));

    kit_buf[0] = '\0';
    TEST_ASSERT_EQUAL_INT(0, pav_profile_read_kit(profile, kit_buf, sizeof(kit_buf), err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("45801522ef", kit_buf);

    TEST_ASSERT_EQUAL_INT(0, pav_profile_read_icons(profile, &icons, err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(icons);
    free(icons);

    pav_profile_free(profile);
}

static void *platform_thread_fn(void *arg) {
    int *flag = arg;

    *flag = 1;
    return NULL;
}

void test_profile_write_is_atomic(void) {
    char path[160];
    char tmp[180];
    char err[256];
    char *body = NULL;
    char *expected = NULL;
    char *file = NULL;
    char *after = NULL;
    PavConfigLoad *load = NULL;
    struct stat st;
    size_t n;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(0, pav_config_ensure(g_cfg_dir, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(0, pav_config_load(g_cfg_dir, &load, err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("ready", load->state);
    TEST_ASSERT_EQUAL_INT(0, json_object_set_new(load->profile->doc, "name", json_string("Renamed")));

    snprintf(path, sizeof(path), "%s/profiles/default.json", g_cfg_dir);
    TEST_ASSERT_EQUAL_INT(0, pav_profile_store(path, load->profile, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(0, stat(path, &st));
    ASSERT_MODE(0600, st);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    TEST_ASSERT_TRUE(stat(tmp, &st) != 0);

    TEST_ASSERT_EQUAL_INT(0, pav_profile_rewrite(load->profile, &body));
    n = strlen(body);
    expected = malloc(n + 2);
    TEST_ASSERT_NOT_NULL(expected);
    memcpy(expected, body, n);
    expected[n] = '\n';
    expected[n + 1] = '\0';
    file = slurp(path);
    TEST_ASSERT_EQUAL_STRING(expected, file);

    TEST_ASSERT_EQUAL_INT(0, json_object_set_new(load->profile->doc, "name", json_string("Again")));
    pav_platform_fake_abort_next_write();
    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, pav_profile_store(path, load->profile, err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(err, "cannot write"));
    after = slurp(path);
    TEST_ASSERT_EQUAL_STRING(file, after);
    TEST_ASSERT_EQUAL_INT(0, stat(path, &st));
    ASSERT_MODE(0600, st);
    TEST_ASSERT_TRUE(stat(tmp, &st) != 0);

    free(body);
    free(expected);
    free(file);
    free(after);
    pav_config_load_free(load);
}

void test_platform_fake_dirs_secrets_and_clock(void) {
    const PavPlatform *platform = pav_platform();
    char data[256];
    char runtime[256];
    char stamp[32];
    char err[256];
    char *secret = NULL;
    struct stat st;
    void *thread = NULL;
    int flag = 0;
    PavillionStatus status;

    TEST_ASSERT_EQUAL_INT(0, platform->dir(PAV_DIR_DATA, data, sizeof(data)));
    TEST_ASSERT_EQUAL_INT(0, stat(data, &st));
    TEST_ASSERT_TRUE(S_ISDIR(st.st_mode));
    ASSERT_MODE(0700, st);
    TEST_ASSERT_EQUAL_INT(0, platform->dir(PAV_DIR_RUNTIME, runtime, sizeof(runtime)));
    TEST_ASSERT_TRUE(strcmp(data, runtime) != 0);
    TEST_ASSERT_EQUAL_INT(0, stat(runtime, &st));
    TEST_ASSERT_TRUE(S_ISDIR(st.st_mode));
    platform->on_network_change(NULL, NULL);
    platform->on_resume(NULL, NULL);

    TEST_ASSERT_EQUAL_INT(PAV_SECRET_MISSING, platform->secret_get("pavillion", "default/a", &secret));
    TEST_ASSERT_NULL(secret);
    TEST_ASSERT_EQUAL_INT(0,
                          platform->secret_set("pavillion", "default/a", "Account", "s3cret"));
    TEST_ASSERT_EQUAL_INT(0, platform->secret_get("pavillion", "default/a", &secret));
    TEST_ASSERT_EQUAL_STRING("s3cret", secret);
    free(secret);
    secret = NULL;
    TEST_ASSERT_EQUAL_INT(0, platform->secret_delete("pavillion", "default/a"));
    TEST_ASSERT_EQUAL_INT(PAV_SECRET_MISSING, platform->secret_get("pavillion", "default/a", &secret));

    pav_platform_fake_set_now_ms(1700000000123LL);
    TEST_ASSERT_TRUE(platform->now_ms() == 1700000000123LL);
    TEST_ASSERT_EQUAL_INT(0, pav_format_utc(stamp, sizeof(stamp), platform->now_ms()));
    TEST_ASSERT_EQUAL_STRING("2023-11-14T22:13:20.123Z", stamp);

    TEST_ASSERT_EQUAL_INT(0, pav_thread_create(&thread, platform_thread_fn, &flag));
    TEST_ASSERT_EQUAL_INT(0, pav_thread_join(thread));
    TEST_ASSERT_EQUAL_INT(1, flag);

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(0, pavillion_status(&status));
    TEST_ASSERT_EQUAL_STRING("ready", status.state);
    TEST_ASSERT_EQUAL_STRING("fake", pav_platform()->name);
}

static json_t *call_reply(const char *cmd, const char *args) {
    char *reply = NULL;
    json_error_t jerr;
    json_t *root;

    TEST_ASSERT_EQUAL_INT(0, pavillion_call(cmd, args, &reply));
    TEST_ASSERT_NOT_NULL(reply);
    root = json_loads(reply, 0, &jerr);
    pavillion_free(reply);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(json_is_object(root));
    return root;
}

static void assert_call_error(json_t *root, const char *code, const char *fragment) {
    json_t *error = json_object_get(root, "error");
    const char *message;

    TEST_ASSERT_TRUE(json_is_false(json_object_get(root, "ok")));
    TEST_ASSERT_NULL(json_object_get(root, "data"));
    TEST_ASSERT_TRUE(json_is_object(error));
    TEST_ASSERT_EQUAL_STRING(code, json_string_value(json_object_get(error, "code")));
    message = json_string_value(json_object_get(error, "message"));
    TEST_ASSERT_NOT_NULL(message);
    if (fragment != NULL) {
        TEST_ASSERT_NOT_NULL(strstr(message, fragment));
    }
    TEST_ASSERT_TRUE(json_is_false(json_object_get(error, "retryable")));
}

void test_call_unknown_command(void) {
    json_t *root = NULL;

    TEST_ASSERT_EQUAL_INT(-1, pavillion_call("system.status", "{}", NULL));
    root = call_reply("system.status", "{}");
    assert_call_error(root, "not_ready", "not running");
    json_decref(root);

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    root = call_reply("no.such", "{}");
    assert_call_error(root, "bad_args", "no.such");
    json_decref(root);
    root = call_reply("NOPE", "{}");
    assert_call_error(root, "bad_args", "invalid command");
    json_decref(root);
}

void test_call_bad_args(void) {
    json_t *root;
    char big[2002];

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));

    root = call_reply("system.status", "[]");
    assert_call_error(root, "bad_args", "not an object");
    json_decref(root);
    root = call_reply("system.status", "null");
    assert_call_error(root, "bad_args", "not an object");
    json_decref(root);
    root = call_reply("system.status", "{\"extra\":1}");
    assert_call_error(root, "bad_args", "extra");
    json_decref(root);
    root = call_reply("prefs.set", "{}");
    assert_call_error(root, "bad_args", "nothing to save");
    json_decref(root);
    root = call_reply("prefs.set", "{\"nope\":1}");
    assert_call_error(root, "bad_args", "nope");
    json_decref(root);
    root = call_reply("prefs.set", "{\"windowWidth\":100}");
    assert_call_error(root, "bad_args", "out of range");
    json_decref(root);
    root = call_reply("log.client", "{\"subsystem\":\"UI\",\"level\":\"info\",\"message\":\"x\"}");
    assert_call_error(root, "bad_args", "subsystem");
    json_decref(root);

    memset(big, 'a', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    {
        char args[2100];

        snprintf(args, sizeof(args),
                 "{\"subsystem\":\"ui\",\"level\":\"info\",\"message\":\"%s\"}", big);
        root = call_reply("log.client", args);
        assert_call_error(root, "too_large", "too large");
        json_decref(root);
    }
}

void test_call_status_roundtrip(void) {
    json_t *root;
    json_t *data;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    root = call_reply("system.status", "{}");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    data = json_object_get(root, "data");
    TEST_ASSERT_EQUAL_STRING("Default", json_string_value(json_object_get(data, "profileName")));
    TEST_ASSERT_EQUAL_STRING("default", json_string_value(json_object_get(data, "profileId")));
    TEST_ASSERT_EQUAL_STRING("ready", json_string_value(json_object_get(data, "state")));
    TEST_ASSERT_EQUAL_STRING("", json_string_value(json_object_get(data, "error")));
    TEST_ASSERT_TRUE(json_is_boolean(json_object_get(data, "debug")));
    TEST_ASSERT_TRUE(json_is_boolean(json_object_get(data, "selftest")));
    json_decref(root);

    root = call_reply("prefs.get", "{}");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    data = json_object_get(root, "data");
    TEST_ASSERT_EQUAL_STRING("America/Vancouver", json_string_value(json_object_get(data, "timezone")));
    TEST_ASSERT_EQUAL_STRING("system", json_string_value(json_object_get(data, "theme")));
    TEST_ASSERT_EQUAL_STRING("system", json_string_value(json_object_get(data, "chrome")));
    TEST_ASSERT_TRUE(json_is_false(json_object_get(data, "windowMaximized")));
    TEST_ASSERT_EQUAL_INT(1200, (int)json_integer_value(json_object_get(data, "windowWidth")));
    TEST_ASSERT_TRUE(json_is_null(json_object_get(data, "windowX")));
    json_decref(root);
}

void test_call_prefs_ui_and_log(void) {
    json_t *root;
    json_t *data;
    json_t *zoom;
    char *text;
    char path[160];

    open_log();
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));

    root = call_reply("prefs.ui.get", "{}");
    data = json_object_get(root, "data");
    TEST_ASSERT_EQUAL_INT(320, (int)json_integer_value(json_object_get(json_object_get(data, "nav"), "width")));
    json_decref(root);

    root = call_reply("prefs.set", "{\"theme\":\"light\",\"windowWidth\":900}");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    data = json_object_get(root, "data");
    TEST_ASSERT_EQUAL_STRING("light", json_string_value(json_object_get(data, "theme")));
    TEST_ASSERT_EQUAL_INT(900, (int)json_integer_value(json_object_get(data, "windowWidth")));
    TEST_ASSERT_EQUAL_STRING("America/Vancouver", json_string_value(json_object_get(data, "timezone")));
    json_decref(root);

    snprintf(path, sizeof(path), "%s/profiles/default.json", g_cfg_dir);
    {
        FILE *f = fopen(path, "rb");
        long n;
        char *buf;

        TEST_ASSERT_NOT_NULL(f);
        TEST_ASSERT_EQUAL_INT(0, fseek(f, 0, SEEK_END));
        n = ftell(f);
        TEST_ASSERT_TRUE(n > 0);
        rewind(f);
        buf = calloc((size_t)n + 1, 1);
        TEST_ASSERT_NOT_NULL(buf);
        TEST_ASSERT_EQUAL_INT((int)n, (int)fread(buf, 1, (size_t)n, f));
        fclose(f);
        TEST_ASSERT_NOT_NULL(strstr(buf, "\"theme\": \"light\""));
        TEST_ASSERT_NOT_NULL(strstr(buf, "\"width\": 900"));
        free(buf);
    }

    root = call_reply("prefs.ui.set", "{\"zoom\":{\"shell\":1.5}}");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    data = json_object_get(root, "data");
    zoom = json_object_get(data, "zoom");
    TEST_ASSERT_TRUE(json_is_number(json_object_get(zoom, "shell")));
    TEST_ASSERT_TRUE(json_real_value(json_object_get(zoom, "shell")) > 1.49);
    TEST_ASSERT_NULL(json_object_get(data, "nav"));
    json_decref(root);

    root = call_reply("prefs.ui.set", "{\"password\":\"nope\"}");
    assert_call_error(root, "forbidden", "password");
    json_decref(root);

    root = call_reply("log.client",
                      "{\"subsystem\":\"ui\",\"level\":\"info\",\"message\":\"line-one\\nline-two\"}");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    TEST_ASSERT_TRUE(json_is_object(json_object_get(root, "data")));
    json_decref(root);
    text = read_log();
    TEST_ASSERT_NOT_NULL(strstr(text, " ui  line-one\xE2\x8E\xAAline-two"));
    free(text);
}

static pthread_mutex_t g_ev_test_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_ev_test_cv = PTHREAD_COND_INITIALIZER;

struct LinesGot {
    int hit;
    char json[8192];
};

static struct LinesGot g_lines_got;

static void lines_test_cb(const char *name, const char *json, void *user) {
    struct LinesGot *got = user;

    if (name == NULL || strcmp(name, "log.lines") != 0 || json == NULL) {
        return;
    }
    if (strstr(json, "batch-a-h1") == NULL || strstr(json, "batch-b-h1") == NULL) {
        return;
    }
    pthread_mutex_lock(&g_ev_test_mu);
    got->hit = 1;
    snprintf(got->json, sizeof(got->json), "%s", json);
    pthread_cond_signal(&g_ev_test_cv);
    pthread_mutex_unlock(&g_ev_test_mu);
}

struct EvGot {
    int hit;
    int on_core;
    int same_as_caller;
    char name[64];
    char json[128];
};

static struct EvGot g_ev_got;
static pthread_t g_ev_caller;

static void ev_test_cb(const char *name, const char *json, void *user) {
    struct EvGot *got = user;

    /* Boot also emits log.lines. Recording that name would replace the
       core.state sample this test is waiting on. */
    if (name == NULL || strcmp(name, "core.state") != 0) {
        return;
    }
    pthread_mutex_lock(&g_ev_test_mu);
    got->hit = 1;
    got->on_core = pav_on_core_thread();
    got->same_as_caller = pthread_equal(pthread_self(), g_ev_caller);
    snprintf(got->name, sizeof(got->name), "%s", name != NULL ? name : "");
    snprintf(got->json, sizeof(got->json), "%s", json != NULL ? json : "");
    pthread_cond_signal(&g_ev_test_cv);
    pthread_mutex_unlock(&g_ev_test_mu);
}

void test_event_delivered_off_core_thread(void) {
    struct timespec ts;
    int rc;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, pav_event_post("core.state", "{\"state\":\"ready\"}"));
    TEST_ASSERT_TRUE(pav_event_dropped() >= 1);

    memset(&g_ev_got, 0, sizeof(g_ev_got));
    g_ev_caller = pthread_self();
    pav_on_event(ev_test_cb, &g_ev_got);
    TEST_ASSERT_EQUAL_INT(0, pav_event_post("core.state", "{\"state\":\"ready\"}"));

    pthread_mutex_lock(&g_ev_test_mu);
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 2;
    while (!g_ev_got.hit) {
        rc = pthread_cond_timedwait(&g_ev_test_cv, &g_ev_test_mu, &ts);
        if (rc != 0) {
            break;
        }
    }
    pthread_mutex_unlock(&g_ev_test_mu);
    pav_on_event(NULL, NULL);

    TEST_ASSERT_EQUAL_INT(1, g_ev_got.hit);
    TEST_ASSERT_EQUAL_INT(0, g_ev_got.on_core);
    TEST_ASSERT_EQUAL_INT(0, g_ev_got.same_as_caller);
    TEST_ASSERT_EQUAL_STRING("core.state", g_ev_got.name);
    TEST_ASSERT_EQUAL_STRING("{\"state\":\"ready\"}", g_ev_got.json);
}

#define API_SLOTS 128
#define API_NAME_LEN 64

static char g_api_real[API_SLOTS][API_NAME_LEN];
static char g_api_plan[API_SLOTS][API_NAME_LEN];
static int g_api_real_n;
static int g_api_plan_n;

static void rstrip(char *line) {
    size_t n = strlen(line);

    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' ' ||
                     line[n - 1] == '\t')) {
        line[--n] = '\0';
    }
}

static int api_has(char names[][API_NAME_LEN], int count, const char *name) {
    int i;

    for (i = 0; i < count; i++) {
        if (strcmp(names[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static void api_add(char names[][API_NAME_LEN], int *count, const char *name) {
    TEST_ASSERT_TRUE(*count < API_SLOTS);
    TEST_ASSERT_TRUE(strlen(name) < API_NAME_LEN);
    TEST_ASSERT_TRUE(name[0] != '\0');
    TEST_ASSERT_FALSE(api_has(names, *count, name));
    memcpy(names[*count], name, strlen(name) + 1);
    (*count)++;
}

static int api_name_cmp(const void *a, const void *b) {
    return strcmp(a, b);
}

void test_api_md_lists_every_command(void) {
    FILE *f;
    char line[2048];
    char version[64];
    char heading[API_NAME_LEN];
    int in_commands = 0;
    int have = 0;
    int state_seen = 0;
    int state_real = 0;
    int saw_version = 0;
    int i;
    int count;

    f = fopen("docs/API.md", "rb");
    TEST_ASSERT_NOT_NULL(f);
    g_api_real_n = 0;
    g_api_plan_n = 0;
    heading[0] = '\0';
    snprintf(version, sizeof(version), "API version: %d", PAVILLION_API_VERSION);

    while (fgets(line, sizeof(line), f) != NULL) {
        if (strlen(line) == sizeof(line) - 1 && strchr(line, '\n') == NULL) {
            TEST_FAIL_MESSAGE("docs/API.md line is too long for the test reader");
        }
        rstrip(line);
        if (strcmp(line, version) == 0) {
            saw_version = 1;
        }
        if (strncmp(line, "### ", 4) == 0) {
            if (in_commands && have) {
                TEST_ASSERT_TRUE(state_seen);
                if (state_real) {
                    api_add(g_api_real, &g_api_real_n, heading);
                } else {
                    api_add(g_api_plan, &g_api_plan_n, heading);
                }
            }
            if (in_commands) {
                TEST_ASSERT_TRUE(strlen(line + 4) < API_NAME_LEN);
                memcpy(heading, line + 4, strlen(line + 4) + 1);
                have = 1;
                state_seen = 0;
                state_real = 0;
            }
            continue;
        }
        if (strncmp(line, "## ", 3) == 0) {
            if (in_commands && have) {
                TEST_ASSERT_TRUE(state_seen);
                if (state_real) {
                    api_add(g_api_real, &g_api_real_n, heading);
                } else {
                    api_add(g_api_plan, &g_api_plan_n, heading);
                }
                have = 0;
            }
            in_commands = strcmp(line, "## Commands") == 0;
            continue;
        }
        if (in_commands && have && strncmp(line, "State: ", 7) == 0) {
            TEST_ASSERT_FALSE(state_seen);
            if (strncmp(line, "State: real", 11) == 0) {
                state_real = 1;
            } else {
                TEST_ASSERT_TRUE(strncmp(line, "State: planned", 14) == 0);
                state_real = 0;
            }
            state_seen = 1;
        }
    }
    if (in_commands && have) {
        TEST_ASSERT_TRUE(state_seen);
        if (state_real) {
            api_add(g_api_real, &g_api_real_n, heading);
        } else {
            api_add(g_api_plan, &g_api_plan_n, heading);
        }
    }
    fclose(f);

    TEST_ASSERT_TRUE(saw_version);
    TEST_ASSERT_TRUE(g_api_plan_n > 0);
    qsort(g_api_real, (size_t)g_api_real_n, API_NAME_LEN, api_name_cmp);
    count = pav_command_count();
    TEST_ASSERT_EQUAL_INT(count, g_api_real_n);
    for (i = 0; i < count; i++) {
        const char *name = pav_command_name(i);

        TEST_ASSERT_NOT_NULL(name);
        if (i > 0) {
            TEST_ASSERT_TRUE(strcmp(pav_command_name(i - 1), name) < 0);
        }
        TEST_ASSERT_EQUAL_STRING(g_api_real[i], name);
        TEST_ASSERT_FALSE(api_has(g_api_plan, g_api_plan_n, name));
    }
    TEST_ASSERT_NULL(pav_command_name(-1));
    TEST_ASSERT_NULL(pav_command_name(count));
}

static json_t *profile_window(void) {
    char path[160];
    json_error_t jerr;
    json_t *root;
    json_t *window;

    snprintf(path, sizeof(path), "%s/profiles/default.json", g_cfg_dir);
    root = json_load_file(path, 0, &jerr);
    TEST_ASSERT_NOT_NULL(root);
    window = json_object_get(json_object_get(root, "preferences"), "window");
    TEST_ASSERT_TRUE(json_is_object(window));
    json_incref(window);
    json_decref(root);
    return window;
}

void test_window_initial_default_1200x800(void) {
    json_t *root;
    json_t *data;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    root = call_reply("window.initial", "{}");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    data = json_object_get(root, "data");
    TEST_ASSERT_TRUE(json_is_object(data));
    TEST_ASSERT_EQUAL_INT(1200, (int)json_integer_value(json_object_get(data, "w")));
    TEST_ASSERT_EQUAL_INT(800, (int)json_integer_value(json_object_get(data, "h")));
    TEST_ASSERT_TRUE(json_is_false(json_object_get(data, "maximized")));
    TEST_ASSERT_TRUE(json_is_null(json_object_get(data, "x")));
    TEST_ASSERT_TRUE(json_is_null(json_object_get(data, "y")));
    json_decref(root);
}

void test_window_report_debounced_write(void) {
    json_t *root;
    json_t *window;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    root = call_reply("window.report",
                      "{\"x\":40,\"y\":50,\"w\":1100,\"h\":700,\"maximized\":false}");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    json_decref(root);
    window = profile_window();
    TEST_ASSERT_EQUAL_INT(1200, (int)json_integer_value(json_object_get(window, "width")));
    json_decref(window);
    usleep(200000);
    root = call_reply("window.report",
                      "{\"x\":40,\"y\":50,\"w\":1300,\"h\":750,\"maximized\":false}");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    json_decref(root);
    window = profile_window();
    TEST_ASSERT_EQUAL_INT(1200, (int)json_integer_value(json_object_get(window, "width")));
    json_decref(window);
    usleep(800000);
    window = profile_window();
    TEST_ASSERT_EQUAL_INT(1300, (int)json_integer_value(json_object_get(window, "width")));
    TEST_ASSERT_EQUAL_INT(750, (int)json_integer_value(json_object_get(window, "height")));
    TEST_ASSERT_EQUAL_INT(40, (int)json_integer_value(json_object_get(window, "x")));
    TEST_ASSERT_EQUAL_INT(50, (int)json_integer_value(json_object_get(window, "y")));
    TEST_ASSERT_TRUE(json_is_false(json_object_get(window, "maximized")));
    json_decref(window);
}

void test_window_clamped_to_monitor(void) {
    json_t *root;
    json_t *window;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    root = call_reply("window.report",
                      "{\"x\":-400,\"y\":10,\"w\":3000,\"h\":500,\"maximized\":false,"
                      "\"monitors\":[{\"x\":0,\"y\":0,\"w\":1920,\"h\":1080}]}");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    json_decref(root);
    pavillion_stop();
    window = profile_window();
    TEST_ASSERT_EQUAL_INT(1920, (int)json_integer_value(json_object_get(window, "width")));
    TEST_ASSERT_EQUAL_INT(500, (int)json_integer_value(json_object_get(window, "height")));
    TEST_ASSERT_EQUAL_INT(0, (int)json_integer_value(json_object_get(window, "x")));
    TEST_ASSERT_EQUAL_INT(10, (int)json_integer_value(json_object_get(window, "y")));
    TEST_ASSERT_TRUE(json_is_false(json_object_get(window, "maximized")));
    json_decref(window);
}

void test_status_reports_host_flags(void) {
    json_t *root;
    json_t *data;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    setenv("PAVILLION_DEBUG", "1", 1);
    setenv("PAVILLION_SELFTEST", "1", 1);
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    root = call_reply("system.status", "{}");
    data = json_object_get(root, "data");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(data, "debug")));
    TEST_ASSERT_TRUE(json_is_true(json_object_get(data, "selftest")));
    json_decref(root);
    pavillion_stop();

    setenv("PAVILLION_DEBUG", "0", 1);
    unsetenv("PAVILLION_SELFTEST");
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    root = call_reply("system.status", "{}");
    data = json_object_get(root, "data");
    TEST_ASSERT_TRUE(json_is_false(json_object_get(data, "debug")));
    TEST_ASSERT_TRUE(json_is_false(json_object_get(data, "selftest")));
    json_decref(root);
}

static struct EvGot g_link_got;

static void link_test_cb(const char *name, const char *json, void *user) {
    struct EvGot *got = user;

    if (name == NULL || strcmp(name, "link.clicked") != 0) {
        return;
    }
    pthread_mutex_lock(&g_ev_test_mu);
    got->hit = 1;
    snprintf(got->name, sizeof(got->name), "%s", name);
    snprintf(got->json, sizeof(got->json), "%s", json != NULL ? json : "");
    pthread_cond_signal(&g_ev_test_cv);
    pthread_mutex_unlock(&g_ev_test_mu);
}

static void assert_link_404(const char *req) {
    PavBytes out;

    memset(&g_link_got, 0, sizeof(g_link_got));
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_EQUAL_INT(0, pav_protocol(req, &out));
    TEST_ASSERT_EQUAL_INT(404, out.status);
    pavillion_free(out.headers_json);
    pavillion_free(out.body);
    usleep(50000);
    TEST_ASSERT_EQUAL_INT(0, g_link_got.hit);
}

void test_protocol_pav_link_emits_and_204(void) {
    PavBytes out;
    struct timespec ts;
    char pane[65];
    char url[160];
    char req[256];
    int rc;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    memset(&g_link_got, 0, sizeof(g_link_got));
    pav_on_event(link_test_cb, &g_link_got);

    memset(&out, 0, sizeof(out));
    TEST_ASSERT_EQUAL_INT(0, pav_protocol("{\"scheme\":\"pav-link\",\"url\":\"pav-link://isolation/0\","
                                          "\"method\":\"GET\",\"range\":\"\"}",
                                          &out));
    TEST_ASSERT_EQUAL_INT(204, out.status);
    TEST_ASSERT_EQUAL_UINT(0, (unsigned)out.body_len);
    TEST_ASSERT_NULL(out.body);
    TEST_ASSERT_NOT_NULL(strstr(out.headers_json, "no-store"));
    pavillion_free(out.headers_json);
    pavillion_free(out.body);

    pthread_mutex_lock(&g_ev_test_mu);
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 2;
    while (!g_link_got.hit) {
        rc = pthread_cond_timedwait(&g_ev_test_cv, &g_ev_test_mu, &ts);
        if (rc != 0) {
            break;
        }
    }
    pthread_mutex_unlock(&g_ev_test_mu);
    TEST_ASSERT_EQUAL_INT(1, g_link_got.hit);
    TEST_ASSERT_EQUAL_STRING("link.clicked", g_link_got.name);
    TEST_ASSERT_EQUAL_STRING("{\"paneId\":\"isolation\",\"n\":0}", g_link_got.json);

    memset(&g_link_got, 0, sizeof(g_link_got));
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_EQUAL_INT(0, pav_protocol("{\"scheme\":\"pav-link\",\"url\":\"pav-link://isolation/00\","
                                          "\"method\":\"GET\",\"range\":\"\"}",
                                          &out));
    TEST_ASSERT_EQUAL_INT(204, out.status);
    pavillion_free(out.headers_json);
    pavillion_free(out.body);
    pthread_mutex_lock(&g_ev_test_mu);
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 2;
    while (!g_link_got.hit) {
        rc = pthread_cond_timedwait(&g_ev_test_cv, &g_ev_test_mu, &ts);
        if (rc != 0) {
            break;
        }
    }
    pthread_mutex_unlock(&g_ev_test_mu);
    TEST_ASSERT_EQUAL_STRING("{\"paneId\":\"isolation\",\"n\":0}", g_link_got.json);

    memset(pane, 'a', 64);
    pane[64] = '\0';
    snprintf(url, sizeof(url), "pav-link://%s/9007199254740991", pane);
    snprintf(req, sizeof(req), "{\"scheme\":\"pav-link\",\"url\":\"%s\",\"method\":\"GET\",\"range\":\"\"}", url);
    memset(&g_link_got, 0, sizeof(g_link_got));
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_EQUAL_INT(0, pav_protocol(req, &out));
    TEST_ASSERT_EQUAL_INT(204, out.status);
    pavillion_free(out.headers_json);
    pavillion_free(out.body);
    pthread_mutex_lock(&g_ev_test_mu);
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 2;
    while (!g_link_got.hit) {
        rc = pthread_cond_timedwait(&g_ev_test_cv, &g_ev_test_mu, &ts);
        if (rc != 0) {
            break;
        }
    }
    pthread_mutex_unlock(&g_ev_test_mu);
    TEST_ASSERT_EQUAL_INT(1, g_link_got.hit);
    TEST_ASSERT_NOT_NULL(strstr(g_link_got.json, "\"n\":9007199254740991"));

    assert_link_404("{\"scheme\":\"pav-link\",\"url\":\"pav-link://isolation\",\"method\":\"GET\"}");
    assert_link_404("{\"scheme\":\"pav-link\",\"url\":\"pav-link:///0\",\"method\":\"GET\"}");
    assert_link_404("{\"scheme\":\"pav-link\",\"url\":\"pav-link://bad pane/0\",\"method\":\"GET\"}");
    assert_link_404("{\"scheme\":\"pav-link\",\"url\":\"pav-link://isolation/0/extra\",\"method\":\"GET\"}");
    assert_link_404("{\"scheme\":\"pav-link\",\"url\":\"pav-link://isolation/-1\",\"method\":\"GET\"}");
    assert_link_404("{\"scheme\":\"pav-link\",\"url\":\"pav-link://isolation/0?x=1\",\"method\":\"GET\"}");
    assert_link_404("{\"scheme\":\"pav-link\",\"url\":\"pav-blob://isolation/0\",\"method\":\"GET\"}");
    assert_link_404("{\"scheme\":\"pav-link\",\"url\":\"pav-link://isolation/9007199254740992\",\"method\":\"GET\"}");
    {
        char long_pane[80];

        memset(long_pane, 'b', 65);
        long_pane[65] = '\0';
        snprintf(url, sizeof(url), "pav-link://%s/1", long_pane);
        snprintf(req, sizeof(req), "{\"scheme\":\"pav-link\",\"url\":\"%s\",\"method\":\"GET\"}", url);
        assert_link_404(req);
    }

    pav_on_event(NULL, NULL);
}

void test_protocol_unknown_scheme_404(void) {
    PavBytes out;

    memset(&out, 0, sizeof(out));
    TEST_ASSERT_EQUAL_INT(0, pav_protocol("{\"scheme\":\"nope\"}", &out));
    TEST_ASSERT_EQUAL_INT(404, out.status);
    TEST_ASSERT_NOT_NULL(out.headers_json);
    TEST_ASSERT_EQUAL_UINT(9, (unsigned)out.body_len);
    TEST_ASSERT_EQUAL_MEMORY("not found", out.body, 9);
    pavillion_free(out.headers_json);
    pavillion_free(out.body);

    memset(&out, 0, sizeof(out));
    TEST_ASSERT_EQUAL_INT(0, pav_protocol("{\"scheme\":\"pav-blob\",\"url\":\"pav-blob://a/b\","
                                          "\"method\":\"GET\",\"range\":\"\"}",
                                          &out));
    TEST_ASSERT_EQUAL_INT(404, out.status);
    TEST_ASSERT_EQUAL_UINT(9, (unsigned)out.body_len);
    TEST_ASSERT_EQUAL_MEMORY("not found", out.body, 9);
    pavillion_free(out.headers_json);
    pavillion_free(out.body);
}

void test_themes_catalog_and_asset(void) {
    json_t *root;
    json_t *data;
    json_t *themes;
    json_t *wrote;
    char path[256];
    char brand[256];
    char index_path[256];
    char *index_text = NULL;
    FILE *file;
    struct stat st;
    PavBytes out;
    unsigned char png[8] = {0x89, 'P', 'N', 'G', 0x00, 0x0d, 0x0a, 0x1a};
    char stored[64];
    size_t got;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));

    root = call_reply("themes.list", "{}");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    data = json_object_get(root, "data");
    TEST_ASSERT_EQUAL_STRING(g_cfg_dir, json_string_value(json_object_get(data, "folder")));
    TEST_ASSERT_EQUAL_STRING("", json_string_value(json_object_get(data, "chosen")));
    TEST_ASSERT_EQUAL_STRING(g_cfg_dir, json_string_value(json_object_get(data, "fallback")));
    TEST_ASSERT_EQUAL_INT(0, (int)json_array_size(json_object_get(data, "themes")));
    TEST_ASSERT_NULL(json_object_get(data, "missing"));
    json_decref(root);

    root = call_reply("themes.list", "{\"extra\":1}");
    assert_call_error(root, "bad_args", "unknown argument extra");
    json_decref(root);

    root = call_reply("themes.ensure", "{\"files\":[{\"name\":\"../x.theme\",\"text\":\"a\"}]}");
    assert_call_error(root, "bad_args", "bad theme name");
    json_decref(root);

    root = call_reply("themes.ensure",
                      "{\"files\":[{\"name\":\"Pavillion Dark.theme\",\"text\":\"/* Title: Dark */\\n\"}]}");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    wrote = json_object_get(json_object_get(root, "data"), "wrote");
    TEST_ASSERT_EQUAL_INT(1, (int)json_array_size(wrote));
    TEST_ASSERT_EQUAL_STRING("Pavillion Dark.theme", json_string_value(json_array_get(wrote, 0)));
    json_decref(root);

    TEST_ASSERT_TRUE(snprintf(path, sizeof(path), "%s/Pavillion Dark.theme", g_cfg_dir) < (int)sizeof(path));
    TEST_ASSERT_EQUAL_INT(0, stat(path, &st));
    ASSERT_MODE(0600, st);

    root = call_reply("themes.ensure",
                      "{\"files\":[{\"name\":\"Pavillion Dark.theme\",\"text\":\"changed\"}]}");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    TEST_ASSERT_EQUAL_INT(0, (int)json_array_size(json_object_get(json_object_get(root, "data"), "wrote")));
    json_decref(root);
    file = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL(file);
    got = fread(stored, 1, sizeof(stored) - 1, file);
    stored[got] = '\0';
    fclose(file);
    TEST_ASSERT_EQUAL_STRING("/* Title: Dark */\n", stored);

    root = call_reply("themes.list", "{}");
    themes = json_object_get(json_object_get(root, "data"), "themes");
    TEST_ASSERT_EQUAL_INT(1, (int)json_array_size(themes));
    TEST_ASSERT_EQUAL_STRING("/* Title: Dark */\n", json_string_value(json_object_get(json_array_get(themes, 0), "text")));
    json_decref(root);

    TEST_ASSERT_TRUE(snprintf(brand, sizeof(brand), "%s/brand", g_cfg_dir) < (int)sizeof(brand));
    TEST_ASSERT_EQUAL_INT(0, mkdir(brand, 0700));
    root = call_reply("themes.folder.set", "{\"folder\":\"brand\"}");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    TEST_ASSERT_EQUAL_STRING(brand, json_string_value(json_object_get(json_object_get(root, "data"), "folder")));
    json_decref(root);

    TEST_ASSERT_TRUE(snprintf(index_path, sizeof(index_path), "%s/Pavillion.json", g_cfg_dir) < (int)sizeof(index_path));
    file = fopen(index_path, "rb");
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL_INT(0, fseek(file, 0, SEEK_END));
    got = (size_t)ftell(file);
    TEST_ASSERT_TRUE(got > 0 && got < 100000);
    rewind(file);
    index_text = malloc(got + 1);
    TEST_ASSERT_NOT_NULL(index_text);
    TEST_ASSERT_EQUAL_UINT(got, fread(index_text, 1, got, file));
    index_text[got] = '\0';
    fclose(file);
    TEST_ASSERT_NOT_NULL(strstr(index_text, "\"themes_folder\": \"brand\""));
    free(index_text);

    root = call_reply("themes.list", "{}");
    data = json_object_get(root, "data");
    TEST_ASSERT_EQUAL_STRING(brand, json_string_value(json_object_get(data, "folder")));
    TEST_ASSERT_EQUAL_STRING("brand", json_string_value(json_object_get(data, "chosen")));
    TEST_ASSERT_EQUAL_STRING(g_cfg_dir, json_string_value(json_object_get(data, "fallback")));
    TEST_ASSERT_EQUAL_INT(0, (int)json_array_size(json_object_get(data, "themes")));
    json_decref(root);

    TEST_ASSERT_TRUE(snprintf(path, sizeof(path), "%s/Extra.theme", brand) < (int)sizeof(path));
    write_text(path, "/* Title: Extra */\n");
    root = call_reply("themes.list", "{}");
    themes = json_object_get(json_object_get(root, "data"), "themes");
    TEST_ASSERT_EQUAL_INT(1, (int)json_array_size(themes));
    TEST_ASSERT_EQUAL_STRING("Extra.theme", json_string_value(json_object_get(json_array_get(themes, 0), "name")));
    json_decref(root);

    TEST_ASSERT_TRUE(snprintf(path, sizeof(path), "%s/logo.png", brand) < (int)sizeof(path));
    file = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL_UINT(sizeof(png), fwrite(png, 1, sizeof(png), file));
    fclose(file);

    memset(&out, 0, sizeof(out));
    TEST_ASSERT_EQUAL_INT(0, pav_protocol("{\"scheme\":\"pav-theme\",\"url\":\"pav-theme://logo.png\"}", &out));
    TEST_ASSERT_EQUAL_INT(200, out.status);
    TEST_ASSERT_EQUAL_UINT(sizeof(png), (unsigned)out.body_len);
    TEST_ASSERT_EQUAL_MEMORY(png, out.body, sizeof(png));
    TEST_ASSERT_NOT_NULL(strstr(out.headers_json, "image/png"));
    pavillion_free(out.headers_json);
    pavillion_free(out.body);

    memset(&out, 0, sizeof(out));
    TEST_ASSERT_EQUAL_INT(0, pav_protocol("{\"scheme\":\"pav-theme\",\"url\":\"pav-theme://../logo.png\"}", &out));
    TEST_ASSERT_EQUAL_INT(404, out.status);
    pavillion_free(out.headers_json);
    pavillion_free(out.body);

    root = call_reply("themes.folder.set", "{\"folder\":\"no-such\"}");
    assert_call_error(root, "bad_args", "not a directory");
    json_decref(root);

    unlink(path);
    TEST_ASSERT_TRUE(snprintf(path, sizeof(path), "%s/Extra.theme", brand) < (int)sizeof(path));
    unlink(path);
    TEST_ASSERT_EQUAL_INT(0, rmdir(brand));
    root = call_reply("themes.list", "{}");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    data = json_object_get(root, "data");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(data, "missing")));
    TEST_ASSERT_EQUAL_INT(0, (int)json_array_size(json_object_get(data, "themes")));
    json_decref(root);

    root = call_reply("themes.folder.set", "{\"folder\":\"\"}");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    TEST_ASSERT_EQUAL_STRING(g_cfg_dir, json_string_value(json_object_get(json_object_get(root, "data"), "folder")));
    json_decref(root);
    root = call_reply("themes.list", "{}");
    themes = json_object_get(json_object_get(root, "data"), "themes");
    TEST_ASSERT_EQUAL_INT(1, (int)json_array_size(themes));
    TEST_ASSERT_EQUAL_STRING("Pavillion Dark.theme", json_string_value(json_object_get(json_array_get(themes, 0), "name")));
    json_decref(root);
}

void test_start_creates_workdir_0700(void) {
    char parent[] = "/tmp/pav-g8-XXXXXX";
    char nested[96];
    struct stat st;
    char err[128];

    TEST_ASSERT_NOT_NULL(mkdtemp(parent));
    memcpy(g_cfg_dir, parent, strlen(parent) + 1);
    snprintf(nested, sizeof(nested), "%s/fresh", parent);
    TEST_ASSERT_TRUE(stat(nested, &st) != 0);
    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(nested, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(0, stat(nested, &st));
    TEST_ASSERT_TRUE(S_ISDIR(st.st_mode));
    ASSERT_MODE(0700, st);
}

void test_logring_wraps_at_capacity(void) {
    int i;
    json_t *root = NULL;
    json_t *lines;
    json_t *first;
    json_t *last;
    char line[80];

    pav_logring_reset();
    pav_log_client_rate_reset();
    for (i = 1; i <= PAV_LOGRING_CAP + 1; i++) {
        snprintf(line, sizeof(line), "2026-10-16T00:00:00.000Z INFO  core  n-%d\n", i);
        TEST_ASSERT_EQUAL_INT(0, pav_logring_push((uint64_t)i, line));
    }
    TEST_ASSERT_EQUAL_INT(0, pav_logring_read(0, PAV_LOGRING_CAP, PAV_DEBUG, NULL, &root));
    TEST_ASSERT_NOT_NULL(root);
    lines = json_object_get(root, "lines");
    TEST_ASSERT_TRUE(json_is_array(lines));
    TEST_ASSERT_EQUAL_INT(PAV_LOGRING_CAP, (int)json_array_size(lines));
    first = json_array_get(lines, 0);
    last = json_array_get(lines, (size_t)PAV_LOGRING_CAP - 1);
    TEST_ASSERT_EQUAL_INT(2, (int)json_integer_value(json_object_get(first, "seq")));
    TEST_ASSERT_EQUAL_STRING("n-2", json_string_value(json_object_get(first, "text")));
    TEST_ASSERT_EQUAL_INT(10001, (int)json_integer_value(json_object_get(last, "seq")));
    TEST_ASSERT_EQUAL_STRING("n-10001", json_string_value(json_object_get(last, "text")));
    json_decref(root);
}

void test_log_tail_since_seq(void) {
    json_t *root;
    json_t *lines;
    json_int_t seq_a;
    char args[64];
    size_t i;
    int seen_b = 0;
    int seen_c = 0;

    root = call_reply("log.tail", "{}");
    assert_call_error(root, "not_ready", "not running");
    json_decref(root);

    open_log();
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    pav_log("core", PAV_INFO, 0, "tail-a");
    pav_log("core", PAV_INFO, 0, "tail-b");
    pav_log("core", PAV_INFO, 0, "tail-c");

    root = call_reply("log.tail", "{}");
    lines = tail_lines(root);
    seq_a = seq_of(lines, "tail-a");
    TEST_ASSERT_TRUE(seq_a > 0);
    json_decref(root);

    snprintf(args, sizeof(args), "{\"sinceSeq\":%lld}", (long long)seq_a);
    root = call_reply("log.tail", args);
    lines = tail_lines(root);
    TEST_ASSERT_FALSE(lines_have(lines, "tail-a"));
    for (i = 0; i < json_array_size(lines); i++) {
        const char *text = line_text(json_array_get(lines, i));

        if (text != NULL && strcmp(text, "tail-b") == 0) {
            TEST_ASSERT_FALSE(seen_c);
            seen_b = 1;
        }
        if (text != NULL && strcmp(text, "tail-c") == 0) {
            TEST_ASSERT_TRUE(seen_b);
            seen_c = 1;
        }
    }
    TEST_ASSERT_TRUE(seen_b);
    TEST_ASSERT_TRUE(seen_c);
    json_decref(root);

    root = call_reply("log.tail", "{\"limit\":1}");
    lines = tail_lines(root);
    TEST_ASSERT_EQUAL_INT(1, (int)json_array_size(lines));
    TEST_ASSERT_EQUAL_STRING("tail-c", line_text(json_array_get(lines, 0)));
    json_decref(root);

    root = call_reply("log.tail", "{\"limit\":0}");
    assert_call_error(root, "bad_args", "limit");
    json_decref(root);
    root = call_reply("log.tail", "{\"limit\":10001}");
    assert_call_error(root, "bad_args", "limit");
    json_decref(root);
}

void test_log_tail_filters_level(void) {
    json_t *root;
    json_t *lines;

    open_log();
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    pav_log("core", PAV_WARN, 0, "filter-warn");
    pav_log("core", PAV_INFO, 0, "filter-info");
    pav_log("config", PAV_WARN, 0, "filter-config");

    root = call_reply("log.tail", "{\"minLevel\":\"warn\"}");
    lines = tail_lines(root);
    TEST_ASSERT_TRUE(lines_have(lines, "filter-warn"));
    TEST_ASSERT_TRUE(lines_have(lines, "filter-config"));
    TEST_ASSERT_FALSE(lines_have(lines, "filter-info"));
    TEST_ASSERT_FALSE(lines_have(lines, "core ready"));
    json_decref(root);

    root = call_reply("log.tail", "{\"minLevel\":\"warn\",\"subsystems\":[\"config\"]}");
    lines = tail_lines(root);
    TEST_ASSERT_EQUAL_INT(1, (int)json_array_size(lines));
    TEST_ASSERT_EQUAL_STRING("filter-config", line_text(json_array_get(lines, 0)));
    TEST_ASSERT_EQUAL_STRING("WARN", json_string_value(json_object_get(json_array_get(lines, 0), "level")));
    TEST_ASSERT_EQUAL_STRING("config",
                             json_string_value(json_object_get(json_array_get(lines, 0), "subsystem")));
    json_decref(root);
}

/* H-4 commit 2: log.db. */
static int64_t db_int(const char *path, const char *sql) {
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    int64_t v = -1;

    TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL));
    TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_prepare_v2(db, sql, -1, &stmt, NULL));
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        v = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return v;
}

static char *db_text_where(const char *needle) {
    char sql[256];

    snprintf(sql, sizeof(sql), "SELECT count(*) FROM log WHERE text LIKE '%%%s%%'", needle);
    return strdup(sql);
}

static int64_t db_count(const char *path, const char *needle) {
    char *sql = db_text_where(needle);
    int64_t n = db_int(path, sql);

    free(sql);
    return n;
}

/* Every row as "subsystem  text\n", oldest first. Caller frees. */
static char *db_text(const char *path) {
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    size_t len = 0;
    char *out = calloc(1, 1);

    TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL));
    TEST_ASSERT_EQUAL_INT(SQLITE_OK,
                          sqlite3_prepare_v2(db, "SELECT subsystem, text FROM log ORDER BY seq", -1, &stmt, NULL));
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *sub = (const char *)sqlite3_column_text(stmt, 0);
        const char *txt = (const char *)sqlite3_column_text(stmt, 1);
        size_t add = strlen(sub) + strlen(txt) + 4;

        out = realloc(out, len + add);
        TEST_ASSERT_NOT_NULL(out);
        len += (size_t)snprintf(out + len, add, "%s  %s\n", sub, txt);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return out;
}

static void attach_db(char *path, size_t path_len) {
    TEST_ASSERT_EQUAL_INT(0, pav_log_set_workdir(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pav_log_thread_start());
    TEST_ASSERT_TRUE(snprintf(path, path_len, "%s/default.log.db", g_cfg_dir) < (int)path_len);
    TEST_ASSERT_EQUAL_INT(0, pav_log_attach_profile(path));
}

static void log_megabytes(int mb, const char *tag) {
    char line[1001];
    int i;

    memset(line, 'x', 1000);
    line[1000] = '\0';
    for (i = 0; i < mb * 1024; i++) {
        pav_log("core", PAV_INFO, 3, "%s-%d %s", tag, i, line);
    }
}

void test_logdb_opens_private_wal(void) {
    char path[200];
    struct stat st;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    attach_db(path, sizeof(path));
    pav_log("core", PAV_INFO, 0, "wal-line");
    pav_log_flush();
    TEST_ASSERT_EQUAL_INT(0, stat(path, &st));
    ASSERT_MODE(0600, st);
    TEST_ASSERT_EQUAL_INT64(2, db_int(path, "PRAGMA auto_vacuum"));
    {
        sqlite3 *db = NULL;
        sqlite3_stmt *stmt = NULL;

        TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL));
        TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_prepare_v2(db, "PRAGMA journal_mode", -1, &stmt, NULL));
        TEST_ASSERT_EQUAL_INT(SQLITE_ROW, sqlite3_step(stmt));
        TEST_ASSERT_EQUAL_STRING("wal", (const char *)sqlite3_column_text(stmt, 0));
        sqlite3_finalize(stmt);
        sqlite3_close(db);
    }
}

void test_logdb_migrate_fresh_to_v1(void) {
    char path[200];

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    attach_db(path, sizeof(path));
    pav_log_flush();
    TEST_ASSERT_EQUAL_INT64(1, db_int(path, "PRAGMA user_version"));
    TEST_ASSERT_EQUAL_INT64(3, db_int(path, "SELECT count(*) FROM sqlite_master WHERE type='index' AND name IN "
                                            "('log_ts','log_session_seq','log_level_seq')"));
    TEST_ASSERT_EQUAL_INT64(1, db_int(path, "SELECT count(*) FROM session WHERE source='run'"));
}

void test_logdb_newer_is_error(void) {
    char path[200];
    sqlite3 *db = NULL;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_TRUE(snprintf(path, sizeof(path), "%s/default.log.db", g_cfg_dir) < (int)sizeof(path));
    TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_open(path, &db));
    TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_exec(db, "PRAGMA user_version=99", NULL, NULL, NULL));
    sqlite3_close(db);
    TEST_ASSERT_EQUAL_INT(0, pav_log_set_workdir(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pav_log_thread_start());
    capture_stderr();
    TEST_ASSERT_EQUAL_INT(-1, pav_log_attach_profile(path));
    pav_log("core", PAV_DEBUG, 0, "after-newer");
    pav_log_flush();
    {
        char *err = take_stderr();

        /* No record this run: every level goes to stderr. */
        TEST_ASSERT_EQUAL_INT(1, count_text(err, "after-newer"));
        free(err);
    }
    TEST_ASSERT_EQUAL_INT64(99, db_int(path, "PRAGMA user_version"));
}

void test_logdb_batches_within_100ms(void) {
    char path[200];
    struct timespec ts = {0, 300L * 1000000L};

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    attach_db(path, sizeof(path));
    pav_log("core", PAV_INFO, 0, "batched-line");
    nanosleep(&ts, NULL);
    TEST_ASSERT_EQUAL_INT64(1, db_count(path, "batched-line"));
}

void test_logdb_error_flushes_sync(void) {
    char path[200];
    char *err;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    attach_db(path, sizeof(path));
    capture_stderr();
    pav_log("core", PAV_ERROR, 0, "sync-error-line");
    TEST_ASSERT_EQUAL_INT64(1, db_count(path, "sync-error-line"));
    err = take_stderr();
    TEST_ASSERT_EQUAL_INT(0, count_text(err, "sync-error-line")); /* DB only (H Lock) */
    free(err);
}

void test_logdb_shutdown_flushes(void) {
    char path[200];
    int i;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    attach_db(path, sizeof(path));
    for (i = 0; i < 700; i++) {
        pav_log("core", PAV_DEBUG, 1, "shutdown-row-%d", i);
    }
    pav_log_thread_shutdown();
    TEST_ASSERT_EQUAL_INT64(700, db_count(path, "shutdown-row-"));
    TEST_ASSERT_EQUAL_INT64(700, db_int(path, "SELECT count(DISTINCT seq) FROM log WHERE text LIKE 'shutdown-row-%'"));
}

void test_log_pre_attach_written_once(void) {
    char path[200];
    char *err;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pav_log_set_workdir(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pav_log_thread_start());
    capture_stderr();
    pav_log("core", PAV_INFO, 0, "early-marker-h1");
    TEST_ASSERT_TRUE(snprintf(path, sizeof(path), "%s/default.log.db", g_cfg_dir) < (int)sizeof(path));
    TEST_ASSERT_EQUAL_INT(0, pav_log_attach_profile(path));
    pav_log("core", PAV_INFO, 0, "late-marker-h1");
    pav_log_flush();
    err = take_stderr();
    free(err);
    TEST_ASSERT_EQUAL_INT64(1, db_count(path, "early-marker-h1"));
    TEST_ASSERT_EQUAL_INT64(1, db_count(path, "late-marker-h1"));
    TEST_ASSERT_TRUE(db_int(path, "SELECT seq FROM log WHERE text='early-marker-h1'") <
                     db_int(path, "SELECT seq FROM log WHERE text='late-marker-h1'"));
}

void test_log_seq_continues_across_runs(void) {
    char path[200];
    int64_t first_max;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    attach_db(path, sizeof(path));
    pav_log("core", PAV_INFO, 0, "run-one");
    pav_log_thread_shutdown();
    first_max = db_int(path, "SELECT max(seq) FROM log");
    TEST_ASSERT_TRUE(first_max > 0);

    pav_logring_reset();
    pav_log_client_rate_reset();
    TEST_ASSERT_EQUAL_INT(0, pav_log_set_workdir(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pav_log_thread_start());
    pav_log("core", PAV_INFO, 0, "run-two-early");
    TEST_ASSERT_EQUAL_INT(0, pav_log_attach_profile(path));
    pav_log("core", PAV_INFO, 0, "run-two-late");
    pav_log_thread_shutdown();
    TEST_ASSERT_EQUAL_INT64(first_max + 1, db_int(path, "SELECT seq FROM log WHERE text='run-two-early'"));
    TEST_ASSERT_EQUAL_INT64(first_max + 2, db_int(path, "SELECT seq FROM log WHERE text='run-two-late'"));
    TEST_ASSERT_EQUAL_INT64(2, db_int(path, "SELECT count(*) FROM session"));
}

void test_log_db_only_no_stderr(void) {
    char path[200];
    char *err;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    capture_stderr();
    TEST_ASSERT_EQUAL_INT(0, pav_log_set_workdir(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pav_log_thread_start());
    pav_log("core", PAV_INFO, 0, "pre-info-h4");
    pav_log("core", PAV_WARN, 0, "pre-warn-h4");
    TEST_ASSERT_TRUE(snprintf(path, sizeof(path), "%s/default.log.db", g_cfg_dir) < (int)sizeof(path));
    TEST_ASSERT_EQUAL_INT(0, pav_log_attach_profile(path));
    pav_log("core", PAV_DEBUG, 0, "post-debug-h4");
    pav_log("core", PAV_ERROR, 0, "post-error-h4");
    pav_log_flush();
    err = take_stderr();
    TEST_ASSERT_EQUAL_INT(0, count_text(err, "pre-info-h4"));
    /* H Lock 2026-10-19: once a workdir exists nothing reaches stderr. */
    TEST_ASSERT_EQUAL_INT(0, count_text(err, "pre-warn-h4"));
    TEST_ASSERT_EQUAL_INT(0, count_text(err, "post-debug-h4"));
    TEST_ASSERT_EQUAL_INT(0, count_text(err, "post-error-h4"));
    free(err);
    pav_log_thread_shutdown();
    TEST_ASSERT_EQUAL_INT64(1, db_int(path, "SELECT count(*) FROM log WHERE text='pre-warn-h4'"));
    TEST_ASSERT_EQUAL_INT64(1, db_int(path, "SELECT count(*) FROM log WHERE text='post-error-h4'"));
}

void test_log_before_workdir_stderr_only(void) {
    char *err;
    char path[160];
    struct stat st;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    capture_stderr();
    pav_log("core", PAV_INFO, 0, "stderr-only-h1");
    pav_log_flush();
    err = take_stderr();
    TEST_ASSERT_EQUAL_INT(1, count_text(err, "stderr-only-h1"));
    free(err);
    TEST_ASSERT_TRUE(snprintf(path, sizeof(path), "%s/default.log.db", g_cfg_dir) < (int)sizeof(path));
    TEST_ASSERT_TRUE(stat(path, &st) != 0);
}

void test_log_no_duplicate_after_profile_attach(void) {
    char path[200];
    char *err;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    capture_stderr();
    TEST_ASSERT_EQUAL_INT(0, pav_log_set_workdir(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pav_log_thread_start());
    pav_log("core", PAV_WARN, 0, "early-marker-h1");
    TEST_ASSERT_TRUE(snprintf(path, sizeof(path), "%s/default.log.db", g_cfg_dir) < (int)sizeof(path));
    TEST_ASSERT_EQUAL_INT(0, pav_log_attach_profile(path));
    pav_log("core", PAV_WARN, 0, "late-marker-h1");
    pav_log_thread_shutdown();
    err = take_stderr();
    TEST_ASSERT_EQUAL_INT(0, count_text(err, "early-marker-h1"));
    TEST_ASSERT_EQUAL_INT(0, count_text(err, "late-marker-h1"));
    free(err);
    TEST_ASSERT_EQUAL_INT64(1, db_count(path, "early-marker-h1"));
    TEST_ASSERT_EQUAL_INT64(1, db_count(path, "late-marker-h1"));
}

void test_logdb_error_wait_capped(void) {
    char path[200];
    sqlite3 *db = NULL;
    int64_t t0;
    int64_t took;
    char *err;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    attach_db(path, sizeof(path));
    pav_log_flush();
    /* Another writer holds the lock, so the commit cannot finish. */
    TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_open(path, &db));
    TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, NULL));
    capture_stderr();
    t0 = pav_now_ms();
    pav_log("core", PAV_ERROR, 0, "capped-error-line");
    took = pav_now_ms() - t0;
    TEST_ASSERT_TRUE(took >= 200);
    TEST_ASSERT_TRUE(took < 600);
    TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_exec(db, "COMMIT", NULL, NULL, NULL));
    sqlite3_close(db);
    pav_log_flush();
    err = take_stderr();
    free(err);
    TEST_ASSERT_EQUAL_INT64(1, db_count(path, "capped-error-line"));
}

void test_log_before_workdir_stderr_all_levels(void) {
    char *err;
    char path[160];
    struct stat st;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    capture_stderr();
    pav_log("core", PAV_DEBUG, 0, "stderr-debug-h4");
    pav_log("core", PAV_TRACE, 0, "stderr-trace-h4");
    pav_log_flush();
    err = take_stderr();
    TEST_ASSERT_EQUAL_INT(1, count_text(err, "stderr-debug-h4"));
    TEST_ASSERT_EQUAL_INT(1, count_text(err, "stderr-trace-h4"));
    free(err);
    TEST_ASSERT_TRUE(snprintf(path, sizeof(path), "%s/logs", g_cfg_dir) < (int)sizeof(path));
    TEST_ASSERT_TRUE(stat(path, &st) != 0);
}

void test_no_logs_dir_created(void) {
    char path[200];
    struct stat st;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    capture_stderr();
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    pavillion_stop();
    pav_log_thread_shutdown();
    free(take_stderr());
    TEST_ASSERT_TRUE(snprintf(path, sizeof(path), "%s/logs", g_cfg_dir) < (int)sizeof(path));
    TEST_ASSERT_TRUE(stat(path, &st) != 0);
    TEST_ASSERT_TRUE(snprintf(path, sizeof(path), "%s/default.log.db", g_cfg_dir) < (int)sizeof(path));
    TEST_ASSERT_EQUAL_INT64(1, db_count(path, "core ready"));
}

void test_log_old_files_deleted(void) {
    char path[200];
    char logs[160];
    struct stat st;
    const char *names[] = {"default.log", "default.log.1", "pavillion.log"};
    size_t i;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_TRUE(snprintf(logs, sizeof(logs), "%s/logs", g_cfg_dir) < (int)sizeof(logs));
    TEST_ASSERT_EQUAL_INT(0, mkdir(logs, 0700));
    for (i = 0; i < 3; i++) {
        TEST_ASSERT_TRUE(snprintf(path, sizeof(path), "%s/%s", logs, names[i]) < (int)sizeof(path));
        write_text(path, "old-file-line-h4\n");
    }
    capture_stderr();
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    pavillion_stop();
    pav_log_thread_shutdown();
    free(take_stderr());
    TEST_ASSERT_TRUE(stat(logs, &st) != 0);
    TEST_ASSERT_TRUE(snprintf(path, sizeof(path), "%s/default.log.db", g_cfg_dir) < (int)sizeof(path));
    TEST_ASSERT_EQUAL_INT64(0, db_count(path, "old-file-line-h4"));
    TEST_ASSERT_EQUAL_INT64(1, db_count(path, "core ready"));
}

void test_log_old_files_delete_failure_warns(void) {
    char path[200];
    char logs[160];
    char dbpath[200];
    char *err;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_TRUE(snprintf(logs, sizeof(logs), "%s/logs", g_cfg_dir) < (int)sizeof(logs));
    TEST_ASSERT_EQUAL_INT(0, mkdir(logs, 0700));
    /* default.log is a non-empty directory, so remove() fails with ENOTEMPTY. */
    TEST_ASSERT_TRUE(snprintf(path, sizeof(path), "%s/default.log", logs) < (int)sizeof(path));
    TEST_ASSERT_EQUAL_INT(0, mkdir(path, 0700));
    TEST_ASSERT_TRUE(snprintf(path, sizeof(path), "%s/default.log/keep", logs) < (int)sizeof(path));
    write_text(path, "x\n");
    capture_stderr();
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    pavillion_stop();
    pav_log_thread_shutdown();
    err = take_stderr();
    TEST_ASSERT_NULL(strstr(err, "cannot remove old log files")); /* DB only (H Lock) */
    free(err);
    TEST_ASSERT_TRUE(snprintf(dbpath, sizeof(dbpath), "%s/default.log.db", g_cfg_dir) < (int)sizeof(dbpath));
    TEST_ASSERT_EQUAL_INT64(1, db_count(dbpath, "cannot remove old log files"));
    TEST_ASSERT_EQUAL_INT64(1, db_count(dbpath, "core ready"));
}

void test_logdb_trim_to_cap_at_shutdown(void) {
    char path[200];
    int64_t bytes;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pav_log_set_cap_mb(1));
    attach_db(path, sizeof(path));
    log_megabytes(3, "cap");
    pav_log_thread_shutdown();
    bytes = db_int(path, "SELECT (SELECT page_count FROM pragma_page_count()) - "
                         "(SELECT freelist_count FROM pragma_freelist_count())") *
            db_int(path, "PRAGMA page_size");
    TEST_ASSERT_TRUE(bytes <= 1024 * 1024);
    TEST_ASSERT_TRUE(bytes > 512 * 1024);
    TEST_ASSERT_EQUAL_INT64(1, db_count(path, "cap-3071 "));
    TEST_ASSERT_EQUAL_INT64(0, db_count(path, "cap-0 "));
    /* incremental_vacuum gave the pages back. */
    TEST_ASSERT_TRUE(db_int(path, "PRAGMA freelist_count") < 16);
}

void test_logdb_trim_drops_empty_sessions(void) {
    char path[200];

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    attach_db(path, sizeof(path));
    pav_log("core", PAV_INFO, 0, "old-session-line");
    pav_log_thread_shutdown();
    TEST_ASSERT_EQUAL_INT(0, pav_log_set_cap_mb(1));
    TEST_ASSERT_EQUAL_INT(0, pav_log_set_workdir(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pav_log_thread_start());
    TEST_ASSERT_EQUAL_INT(0, pav_log_attach_profile(path));
    log_megabytes(2, "drop");
    pav_log_thread_shutdown();
    TEST_ASSERT_EQUAL_INT64(0, db_count(path, "old-session-line"));
    TEST_ASSERT_EQUAL_INT64(1, db_int(path, "SELECT count(*) FROM session"));
}

void test_log_stats_reports_size_records_oldest(void) {
    char path[200];
    PavLogStats st;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    attach_db(path, sizeof(path));
    pav_log("core", PAV_INFO, 0, "stats-a");
    pav_log("core", PAV_INFO, 0, "stats-b");
    pav_log_flush();
    TEST_ASSERT_EQUAL_INT(0, pav_log_stats(&st));
    TEST_ASSERT_EQUAL_INT64(2, st.records);
    TEST_ASSERT_TRUE(st.bytes > 0);
    TEST_ASSERT_EQUAL_INT64(db_int(path, "SELECT seq FROM log WHERE text='stats-a'"), st.oldest_seq);
    TEST_ASSERT_EQUAL_INT64(db_int(path, "SELECT ts FROM log WHERE text='stats-a'"), st.oldest_ts);
    TEST_ASSERT_EQUAL_INT(10, st.cap_mb);
}

void test_log_trim_preview_deletes_nothing(void) {
    char path[200];
    PavLogStats before;
    PavLogStats preview;
    PavLogStats after;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    attach_db(path, sizeof(path));
    log_megabytes(2, "prev");
    pav_log_flush();
    TEST_ASSERT_EQUAL_INT(0, pav_log_stats(&before));
    TEST_ASSERT_EQUAL_INT(0, pav_log_trim_preview(1, &preview));
    TEST_ASSERT_TRUE(preview.bytes <= 1024 * 1024);
    TEST_ASSERT_TRUE(preview.records > 0 && preview.records < before.records);
    TEST_ASSERT_TRUE(preview.oldest_seq > before.oldest_seq);
    TEST_ASSERT_EQUAL_INT(0, pav_log_stats(&after));
    TEST_ASSERT_EQUAL_INT64(before.records, after.records);
    TEST_ASSERT_EQUAL_INT(10, after.cap_mb);
}

void test_log_trim_now_uses_saved_cap(void) {
    char path[200];
    PavLogStats st;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pav_log_set_cap_mb(1));
    attach_db(path, sizeof(path));
    log_megabytes(3, "now");
    pav_log_flush();
    TEST_ASSERT_EQUAL_INT(0, pav_log_stats(&st));
    TEST_ASSERT_TRUE(st.bytes > 2 * 1024 * 1024);
    TEST_ASSERT_EQUAL_INT(0, pav_log_trim(0, &st));
    TEST_ASSERT_TRUE(st.bytes <= 1024 * 1024);
    TEST_ASSERT_EQUAL_INT(1, st.cap_mb);
    TEST_ASSERT_EQUAL_INT64(st.records, db_int(path, "SELECT count(*) FROM log"));
}

void test_log_trim_bad_mb(void) {
    char path[200];
    PavLogStats st;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    attach_db(path, sizeof(path));
    TEST_ASSERT_EQUAL_INT(-1, pav_log_trim(101, &st));
    TEST_ASSERT_EQUAL_INT(-1, pav_log_trim(-1, &st));
    TEST_ASSERT_EQUAL_INT(-1, pav_log_trim_preview(0, &st));
    TEST_ASSERT_EQUAL_INT(-1, pav_log_set_cap_mb(0));
    TEST_ASSERT_EQUAL_INT(-1, pav_log_set_cap_mb(101));
    TEST_ASSERT_EQUAL_INT(0, pav_log_stats(&st));
    TEST_ASSERT_EQUAL_INT(10, st.cap_mb);
}

void test_log_lines_one_batch(void) {
    struct timespec ts;
    int rc;

    memset(&g_lines_got, 0, sizeof(g_lines_got));
    open_log();
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    /* Flush the boot lines, then let their batch leave, so the next two
       lines share a fresh 100 ms batch. */
    pav_log_flush();
    usleep(200000);
    pav_on_event(lines_test_cb, &g_lines_got);
    pav_log("core", PAV_INFO, 0, "batch-a-h1");
    pav_log("core", PAV_INFO, 0, "batch-b-h1");

    pthread_mutex_lock(&g_ev_test_mu);
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 2;
    while (!g_lines_got.hit) {
        rc = pthread_cond_timedwait(&g_ev_test_cv, &g_ev_test_mu, &ts);
        if (rc != 0) {
            break;
        }
    }
    pthread_mutex_unlock(&g_ev_test_mu);
    pav_on_event(NULL, NULL);

    TEST_ASSERT_EQUAL_INT(1, g_lines_got.hit);
    TEST_ASSERT_NOT_NULL(strstr(g_lines_got.json, "\"seq\""));
    TEST_ASSERT_NOT_NULL(strstr(g_lines_got.json, "batch-a-h1"));
    TEST_ASSERT_NOT_NULL(strstr(g_lines_got.json, "batch-b-h1"));
}

void test_log_client_sanitizes(void) {
    json_t *root;
    char msg[2001];
    char args[2100];
    char *text;
    char *at;

    open_log();
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));

    root = call_reply("log.client",
                      "{\"subsystem\":\"ui\",\"level\":\"error\",\"message\":\"a\\r\\nb\"}");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    json_decref(root);

    memset(msg, 'b', 2000);
    msg[2000] = '\0';
    snprintf(args, sizeof(args),
             "{\"subsystem\":\"ui\",\"level\":\"info\",\"message\":\"%s\"}", msg);
    root = call_reply("log.client", args);
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    json_decref(root);

    text = read_log();
    TEST_ASSERT_NOT_NULL(strstr(text, "a\xE2\x8E\xAA" "b"));
    at = strstr(text, msg);
    TEST_ASSERT_NOT_NULL(at);
    TEST_ASSERT_EQUAL_CHAR('\n', at[2000]);
    free(text);
}

static int line_with(const char *text, const char *needle, char *out, size_t out_len) {
    const char *hit;
    const char *start;
    const char *end;
    size_t n;

    if (text == NULL || needle == NULL) {
        return -1;
    }
    hit = strstr(text, needle);
    if (hit == NULL) {
        return -1;
    }
    start = hit;
    while (start > text && start[-1] != '\n') {
        start--;
    }
    end = strchr(hit, '\n');
    n = end == NULL ? strlen(start) : (size_t)(end - start) + 1;
    if (n >= out_len) {
        return -1;
    }
    memcpy(out, start, n);
    out[n] = '\0';
    return 0;
}

void test_log_export_filter(void) {
    char args[700];
    char path[160];
    char line[512];
    char other[512];
    char dotted[512];
    char *log_text;
    char *exported;
    json_t *root;
    json_t *data;
    struct stat st;

    open_log();
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    pav_log("config", PAV_WARN, 0, "h3-alpha saved");
    pav_log("config", PAV_INFO, 0, "h3-alpha hidden-level");
    pav_log("core", PAV_WARN, 0, "h3-alpha other");
    pav_log("config", PAV_WARN, 0, "h3-beta");
    pav_log("config", PAV_WARN, 0, "h3 a.b kept");
    pav_log("config", PAV_WARN, 0, "h3 axb dropped");
    pav_log_flush();
    log_text = slurp(g_log_path);
    TEST_ASSERT_EQUAL_INT(0, line_with(log_text, "h3-alpha saved", line, sizeof(line)));
    TEST_ASSERT_EQUAL_INT(0, line_with(log_text, "h3-alpha other", other, sizeof(other)));
    TEST_ASSERT_EQUAL_INT(0, line_with(log_text, "h3 a.b kept", dotted, sizeof(dotted)));

    TEST_ASSERT_TRUE(snprintf(path, sizeof(path), "%s/h3-export.log", g_cfg_dir) < (int)sizeof(path));
    TEST_ASSERT_TRUE(snprintf(args, sizeof(args),
                              "{\"path\":\"%s\",\"filter\":{\"levels\":[\"WARN\"],\"subsystems\":[\"config\"],"
                              "\"text\":\"h3-alpha\"}}",
                              path) < (int)sizeof(args));
    root = call_reply("log.export", args);
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    data = json_object_get(root, "data");
    TEST_ASSERT_EQUAL_INT(1, (int)json_integer_value(json_object_get(data, "lines")));
    json_decref(root);
    exported = slurp(path);
    TEST_ASSERT_EQUAL_STRING(line, exported);
    free(exported);
    TEST_ASSERT_EQUAL_INT(0, stat(path, &st));
    ASSERT_MODE(0600, st);

    TEST_ASSERT_TRUE(snprintf(args, sizeof(args),
                              "{\"path\":\"%s\",\"filter\":{\"levels\":[\"WARN\"],\"subsystems\":[\"config\"],"
                              "\"text\":\"H3-ALPHA\"}}",
                              path) < (int)sizeof(args));
    root = call_reply("log.export", args);
    TEST_ASSERT_EQUAL_INT(1, (int)json_integer_value(json_object_get(json_object_get(root, "data"), "lines")));
    json_decref(root);
    exported = slurp(path);
    TEST_ASSERT_EQUAL_STRING(line, exported);
    free(exported);

    TEST_ASSERT_TRUE(snprintf(args, sizeof(args),
                              "{\"path\":\"%s\",\"filter\":{\"levels\":[\"WARN\"],\"subsystems\":null,"
                              "\"text\":\"h3-alpha\"}}",
                              path) < (int)sizeof(args));
    root = call_reply("log.export", args);
    data = json_object_get(root, "data");
    TEST_ASSERT_EQUAL_INT(2, (int)json_integer_value(json_object_get(data, "lines")));
    json_decref(root);
    exported = slurp(path);
    TEST_ASSERT_EQUAL_INT(0, strncmp(exported, line, strlen(line)));
    TEST_ASSERT_EQUAL_STRING(other, exported + strlen(line));
    TEST_ASSERT_NULL(strstr(exported, "hidden-level"));
    free(exported);

    TEST_ASSERT_TRUE(snprintf(args, sizeof(args),
                              "{\"path\":\"%s\",\"filter\":{\"levels\":[\"WARN\"],\"subsystems\":[\"config\"],"
                              "\"text\":\"a.b\"}}",
                              path) < (int)sizeof(args));
    root = call_reply("log.export", args);
    TEST_ASSERT_EQUAL_INT(1, (int)json_integer_value(json_object_get(json_object_get(root, "data"), "lines")));
    json_decref(root);
    exported = slurp(path);
    TEST_ASSERT_EQUAL_STRING(dotted, exported);
    TEST_ASSERT_NULL(strstr(exported, "axb"));
    free(exported);

    TEST_ASSERT_TRUE(snprintf(args, sizeof(args),
                              "{\"path\":\"%s\",\"filter\":{\"levels\":[\"WARN\"],\"subsystems\":[],\"text\":\"\"}}",
                              path) < (int)sizeof(args));
    root = call_reply("log.export", args);
    TEST_ASSERT_EQUAL_INT(0, (int)json_integer_value(json_object_get(json_object_get(root, "data"), "lines")));
    json_decref(root);
    exported = slurp(path);
    TEST_ASSERT_EQUAL_STRING("", exported);
    free(exported);

    root = call_reply("log.export", "{\"path\":\"h3-export.log\",\"filter\":{\"levels\":[\"WARN\"]}}");
    assert_call_error(root, "bad_args", "absolute");
    json_decref(root);
    free(log_text);
}

/* API.md Conventions: absolute paths per platform. */
void test_path_is_absolute_per_platform(void) {
    char parent[64];

    TEST_ASSERT_TRUE(pav_path_is_absolute("/tmp/a.pem"));
    TEST_ASSERT_FALSE(pav_path_is_absolute("rel/a.pem"));
    TEST_ASSERT_FALSE(pav_path_is_absolute(""));
    TEST_ASSERT_FALSE(pav_path_is_absolute(NULL));
#ifdef _WIN32
    TEST_ASSERT_TRUE(pav_path_is_absolute("C:/a/ca.pem"));
    TEST_ASSERT_TRUE(pav_path_is_absolute("d:\\a\\ca.pem"));
    TEST_ASSERT_TRUE(pav_path_is_absolute("\\\\host\\share\\ca.pem"));
    TEST_ASSERT_FALSE(pav_path_is_absolute("C:ca.pem"));
    TEST_ASSERT_EQUAL_INT(0, pav_path_parent(parent, sizeof(parent), "D:\\a\\b.csv"));
    TEST_ASSERT_EQUAL_STRING("D:\\a", parent);
    TEST_ASSERT_EQUAL_INT(0, pav_path_parent(parent, sizeof(parent), "D:/b.csv"));
    TEST_ASSERT_EQUAL_STRING("D:/", parent);
#else
    TEST_ASSERT_FALSE(pav_path_is_absolute("C:/a/ca.pem"));
    TEST_ASSERT_EQUAL_INT(0, pav_path_parent(parent, sizeof(parent), "/a/b.csv"));
    TEST_ASSERT_EQUAL_STRING("/a", parent);
#endif
}

/* D-10: export.write_text writes the text whole, 0600, and checks the path. */
void test_export_write_text(void) {
    char args[700];
    char path[160];
    char *written;
    json_t *root;
    struct stat st;

    open_log();
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    TEST_ASSERT_TRUE(snprintf(path, sizeof(path), "%s/accounts.csv", g_cfg_dir) < (int)sizeof(path));
    TEST_ASSERT_TRUE(snprintf(args, sizeof(args), "{\"path\":\"%s\",\"text\":\"Number,Name\\n001,Caf\\u00e9\\n\"}", path) <
                     (int)sizeof(args));
    root = call_reply("export.write_text", args);
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    TEST_ASSERT_EQUAL_INT(22, (int)json_integer_value(json_object_get(json_object_get(root, "data"), "bytes")));
    json_decref(root);
    written = slurp(path);
    TEST_ASSERT_EQUAL_STRING("Number,Name\n001,Caf\xc3\xa9\n", written);
    free(written);
    TEST_ASSERT_EQUAL_INT(0, stat(path, &st));
    ASSERT_MODE(0600, st);

    root = call_reply("export.write_text", "{\"path\":\"rel.csv\",\"text\":\"x\"}");
    assert_call_error(root, "bad_args", "absolute");
    json_decref(root);
    TEST_ASSERT_TRUE(snprintf(args, sizeof(args), "{\"path\":\"%s/no-such-dir/a.csv\",\"text\":\"x\"}", g_cfg_dir) <
                     (int)sizeof(args));
    root = call_reply("export.write_text", args);
    assert_call_error(root, "bad_args", "directory");
    json_decref(root);
    TEST_ASSERT_TRUE(snprintf(args, sizeof(args), "{\"path\":\"%s\",\"text\":\"x\"}", g_cfg_dir) < (int)sizeof(args));
    root = call_reply("export.write_text", args);
    assert_call_error(root, "bad_args", "directory");
    json_decref(root);
    TEST_ASSERT_TRUE(snprintf(args, sizeof(args), "{\"path\":\"%s\",\"text\":1}", path) < (int)sizeof(args));
    root = call_reply("export.write_text", args);
    assert_call_error(root, "bad_args", "text");
    json_decref(root);
    root = call_reply("export.write_text", "{\"text\":\"x\"}");
    assert_call_error(root, "bad_args", "requires");
    json_decref(root);
}

void test_prefs_set_log_level(void) {
    char profile_path[160];
    char index_path[160];
    char *before;
    char *after;
    char *text;
    json_t *root;
    json_t *data;

    open_log();
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    TEST_ASSERT_TRUE(snprintf(profile_path, sizeof(profile_path), "%s/profiles/default.json", g_cfg_dir) <
                     (int)sizeof(profile_path));
    TEST_ASSERT_TRUE(snprintf(index_path, sizeof(index_path), "%s/Pavillion.json", g_cfg_dir) <
                     (int)sizeof(index_path));
    before = slurp(profile_path);
    root = call_reply("prefs.get", "{}");
    data = json_object_get(root, "data");
    TEST_ASSERT_EQUAL_STRING("info", json_string_value(json_object_get(data, "logLevel")));
    json_decref(root);

    root = call_reply("prefs.set", "{\"logLevel\":\"warn\"}");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    data = json_object_get(root, "data");
    TEST_ASSERT_EQUAL_STRING("warn", json_string_value(json_object_get(data, "logLevel")));
    TEST_ASSERT_EQUAL_STRING("system", json_string_value(json_object_get(data, "theme")));
    json_decref(root);
    after = slurp(profile_path);
    TEST_ASSERT_EQUAL_STRING(before, after);
    free(before);
    free(after);
    text = slurp(index_path);
    TEST_ASSERT_NOT_NULL(strstr(text, "\"log_level\": \"warn\""));
    free(text);

    /* logLevel is the view default only: an INFO line is still captured. */
    pav_log("core", PAV_INFO, 0, "h3-level-captured");
    pav_log("core", PAV_WARN, 0, "h3-level-kept");
    pav_log_flush();
    text = slurp(g_log_path);
    TEST_ASSERT_NOT_NULL(strstr(text, "h3-level-captured"));
    TEST_ASSERT_NOT_NULL(strstr(text, "h3-level-kept"));
    free(text);

    root = call_reply("prefs.set", "{\"logLevel\":\"loud\"}");
    assert_call_error(root, "bad_args", "logLevel");
    json_decref(root);

    root = call_reply("prefs.set", "{\"logLevel\":\"trace\"}");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    TEST_ASSERT_EQUAL_STRING("trace", json_string_value(json_object_get(json_object_get(root, "data"), "logLevel")));
    json_decref(root);
}

void test_index_log_level_and_prefs_saved(void) {
    char path[160];
    char *text;
    json_t *root;

    open_log();
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    pav_log("core", PAV_DEBUG, 0, "debug-hidden-h2");
    root = call_reply("prefs.ui.set", "{\"zoom\":{\"log\":1}}");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    json_decref(root);
    pav_log_flush();
    text = slurp(g_log_path);
    /* log_level info no longer filters capture (H-4 Lock, Levels). */
    TEST_ASSERT_NOT_NULL(strstr(text, "debug-hidden-h2"));
    TEST_ASSERT_NOT_NULL(strstr(text, " DEBUG config  prefs saved"));
    TEST_ASSERT_NOT_NULL(strstr(text, " DEBUG config  prefs saved: zoom.log"));
    free(text);

    pavillion_stop();
    TEST_ASSERT_TRUE(snprintf(path, sizeof(path), "%s/Pavillion.json", g_cfg_dir) < (int)sizeof(path));
    write_text(path,
               "{\n"
               "  \"kind\": \"pavillion-index\",\n"
               "  \"schema\": 1,\n"
               "  \"active\": \"default.json\",\n"
               "  \"profile_dir\": \"profiles\",\n"
               "  \"log_level\": \"debug\"\n"
               "}\n");
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    pav_log("core", PAV_DEBUG, 0, "debug-visible-h2");
    root = call_reply("prefs.ui.set", "{\"zoom\":{\"log\":1.05}}");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    json_decref(root);
    pav_log_flush();
    text = slurp(g_log_path);
    TEST_ASSERT_NOT_NULL(strstr(text, "debug-visible-h2"));
    TEST_ASSERT_NOT_NULL(strstr(text, " DEBUG config  prefs saved"));
    free(text);
}

void test_log_redacts_known_secret_shapes(void) {
    json_t *root;
    char *text;

    open_log();
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    pav_log("http", PAV_DEBUG, 1, "%s", "header Authorization: Basic dXNlcjpwYXNzd29yZA==");
    pav_log("http", PAV_DEBUG, 1, "%s", "sent Bearer abcdef0123456789xyz now");
    pav_log("http", PAV_TRACE, 1, "%s", "GET https://mail.example.com/jmap?access_token=s3cr3tQ&x=1 -> 200");
    pav_log("config", PAV_DEBUG, 1, "%s", "pref apiKey=hunter2value saved");
    pav_log("config", PAV_DEBUG, 1, "%s", "{\"password\": \"pw-fixture-9\"}");
    pav_log("account", PAV_INFO, 1, "%s", "added user@example.com");
    pav_log("account", PAV_DEBUG, 1, "%s", "debug user2@example.com");
    pav_log("auth", PAV_INFO, 1, "%s", "auth basic apply ok");
    pav_log("http", PAV_DEBUG, 1, "%s", "GET https://alice:pa55-userinfo@mail.example.com/jmap -> 401");
    pav_log("http", PAV_INFO, 1, "%s", "server http://bob@files.example.com:8080/dav");
    root = call_reply("log.client",
                      "{\"subsystem\":\"ui\",\"level\":\"info\",\"message\":\"token=tok-fixture-7 for c@d.org\"}");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    json_decref(root);
    root = call_reply("prefs.ui.set", "{\"zoom\":{\"log\":1.1},\"proxyPassword\":\"pref-secret-5\"}");
    json_decref(root);
    text = read_log();
    TEST_ASSERT_NULL(strstr(text, "dXNlcjpwYXNzd29yZA"));
    TEST_ASSERT_NULL(strstr(text, "abcdef0123456789xyz"));
    TEST_ASSERT_NULL(strstr(text, "s3cr3tQ"));
    TEST_ASSERT_NULL(strstr(text, "access_token"));
    TEST_ASSERT_NULL(strstr(text, "hunter2value"));
    TEST_ASSERT_NULL(strstr(text, "pw-fixture-9"));
    TEST_ASSERT_NULL(strstr(text, "tok-fixture-7"));
    TEST_ASSERT_NULL(strstr(text, "pref-secret-5"));
    TEST_ASSERT_NULL(strstr(text, "user@example.com"));
    TEST_ASSERT_NULL(strstr(text, "c@d.org"));
    TEST_ASSERT_NOT_NULL(strstr(text, "debug user2@example.com"));
    TEST_ASSERT_NOT_NULL(strstr(text, "https://mail.example.com/jmap?[redacted]"));
    TEST_ASSERT_NOT_NULL(strstr(text, "added [email]"));
    TEST_ASSERT_NOT_NULL(strstr(text, "auth basic apply ok"));
    TEST_ASSERT_NULL(strstr(text, "pa55-userinfo"));
    TEST_ASSERT_NULL(strstr(text, "alice"));
    TEST_ASSERT_NULL(strstr(text, "bob@"));
    TEST_ASSERT_NOT_NULL(strstr(text, "https://[redacted]@mail.example.com/jmap -> 401"));
    TEST_ASSERT_NOT_NULL(strstr(text, "http://[redacted]@files.example.com:8080/dav"));
    free(text);
}

void test_log_client_rate_limited(void) {
    json_t *root;
    char *text;
    char *at;
    int i;
    int lines = 0;

    open_log();
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    /* Start at a fresh second so all 60 land in one window. */
    {
        struct timespec ts;
        time_t start;

        clock_gettime(CLOCK_MONOTONIC, &ts);
        start = ts.tv_sec;
        do {
            usleep(2000);
            clock_gettime(CLOCK_MONOTONIC, &ts);
        } while (ts.tv_sec == start);
    }
    pav_log_client_rate_reset();
    for (i = 0; i < 60; i++) {
        char body[128];

        /* Distinct lines: identical ones would collapse (H-4 Lock). */
        snprintf(body, sizeof(body), "{\"subsystem\":\"ui\",\"level\":\"info\",\"message\":\"rate-line %d\"}", i);
        root = call_reply("log.client", body);
        TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
        json_decref(root);
    }
    usleep(1100000);
    root = call_reply("log.client", "{\"subsystem\":\"ui\",\"level\":\"info\",\"message\":\"rate-after\"}");
    json_decref(root);
    text = read_log();
    for (at = strstr(text, "rate-line"); at != NULL; at = strstr(at + 1, "rate-line")) {
        lines++;
    }
    TEST_ASSERT_EQUAL_INT(50, lines);
    TEST_ASSERT_NOT_NULL(strstr(text, " WARN  ui  log.client dropped 10 lines over 50 per second"));
    TEST_ASSERT_NOT_NULL(strstr(text, "rate-after"));
    free(text);
}

void test_log_points_lifecycle_and_commands(void) {
    json_t *root;
    char *text;

    open_log();
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    root = call_reply("no.such.command", "{}");
    json_decref(root);
    root = call_reply("prefs.set", "{\"timezone\":\"UTC\"}");
    json_decref(root);
    pavillion_stop();
    text = read_log();
    TEST_ASSERT_NOT_NULL(strstr(text, " INFO  core  started: api "));
    TEST_ASSERT_NOT_NULL(strstr(text, " INFO  core  profile default attaching"));
    TEST_ASSERT_NOT_NULL(strstr(text, " INFO  core  core ready: profile default"));
    TEST_ASSERT_NOT_NULL(strstr(text, " WARN  dispatch  unknown command no.such.command"));
    TEST_ASSERT_NOT_NULL(strstr(text, " DEBUG config  prefs set: timezone"));
    TEST_ASSERT_NOT_NULL(strstr(text, " INFO  core  stopping"));
    TEST_ASSERT_NOT_NULL(strstr(text, " INFO  core  stopped"));
    free(text);
}

static int count_in(const char *text, const char *needle) {
    int n = 0;
    const char *at;

    for (at = strstr(text, needle); at != NULL; at = strstr(at + 1, needle)) {
        n++;
    }
    return n;
}

void test_log_repeats_collapse_on_different_line(void) {
    char *text;
    int i;

    open_log();
    for (i = 0; i < 5; i++) {
        pav_log("window", PAV_DEBUG, 0, "same-line");
    }
    pav_log("window", PAV_DEBUG, 0, "other-line");
    pav_log("window", PAV_INFO, 0, "other-line");
    pav_log("core", PAV_INFO, 0, "other-line");
    text = read_log();
    TEST_ASSERT_EQUAL_INT(1, count_in(text, "same-line"));
    TEST_ASSERT_NOT_NULL(strstr(text, " DEBUG window  previous line repeated 4 times"));
    /* Same text at another level or subsystem is a different line. */
    TEST_ASSERT_EQUAL_INT(3, count_in(text, "other-line"));
    TEST_ASSERT_EQUAL_INT(1, count_in(text, "previous line repeated"));
    TEST_ASSERT_TRUE(strstr(text, "repeated 4 times") < strstr(text, "other-line"));
    free(text);
}

void test_log_repeats_flush_on_idle(void) {
    char *text;
    int i;

    open_log();
    for (i = 0; i < 3; i++) {
        pav_log("http", PAV_ERROR, 0, "error-repeat");
    }
    usleep(1500000);
    /* The idle summary went out; the same line now starts a new run. */
    pav_log("http", PAV_ERROR, 0, "error-repeat");
    text = read_log();
    TEST_ASSERT_EQUAL_INT(2, count_in(text, "error-repeat"));
    TEST_ASSERT_NOT_NULL(strstr(text, " ERROR http  previous line repeated 2 times"));
    TEST_ASSERT_EQUAL_INT(1, count_in(text, "previous line repeated"));
    TEST_ASSERT_TRUE(strstr(text, "repeated 2 times") < strstr(strstr(text, "error-repeat") + 1, "error-repeat"));
    free(text);
}

void test_log_repeats_flush_at_shutdown(void) {
    char *text;
    int i;

    open_log();
    pav_log("store", PAV_WARN, 0, "warn-repeat");
    for (i = 0; i < 9; i++) {
        pav_log("store", PAV_WARN, 0, "warn-repeat");
    }
    text = read_log();
    TEST_ASSERT_EQUAL_INT(1, count_in(text, "warn-repeat"));
    TEST_ASSERT_NOT_NULL(strstr(text, " WARN  store  previous line repeated 9 times"));
    free(text);
}

typedef struct JmapWait {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int done;
    int status;
} JmapWait;

static void jmap_wait_done(PavJmapCtx *ctx, int status, const char *body, size_t body_len,
                           const json_t *headers, void *user) {
    JmapWait *w = user;

    (void)ctx;
    (void)body;
    (void)body_len;
    (void)headers;
    pthread_mutex_lock(&w->mu);
    w->done = 1;
    w->status = status;
    pthread_cond_broadcast(&w->cv);
    pthread_mutex_unlock(&w->mu);
}

static void scribble_stack(void) {
    volatile char junk[8192];
    size_t i;

    for (i = 0; i < sizeof(junk); i++) {
        junk[i] = 'x';
    }
}

void test_jmap_post_url_outlives_call(void) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    PavJmapCtx ctx;
    JmapWait w;
    char session[512];
    struct timespec ts;

    TEST_ASSERT_NOT_NULL_MESSAGE(url, "PAV_FIXTURE_URL must be set by make test");
    TEST_ASSERT_EQUAL_INT(0, pav_net_start());
    memset(&ctx, 0, sizeof(ctx));
    memset(&w, 0, sizeof(w));
    pthread_mutex_init(&w.mu, NULL);
    pthread_cond_init(&w.cv, NULL);
    snprintf(session, sizeof(session), "%s/jmap/session", url);
    ctx.session_url = session;
    ctx.request_json = strdup("{\"using\":[],\"methodCalls\":[]}");
    ctx.ca_file = ca;
    ctx.on_done = jmap_wait_done;
    ctx.user = &w;
    TEST_ASSERT_EQUAL_INT(0, pav_jmap_post(&ctx));
    /* The request must point at memory that outlives pav_jmap_post. */
    TEST_ASSERT_EQUAL_PTR(session, ctx.req.url);
    scribble_stack();
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 10;
    pthread_mutex_lock(&w.mu);
    while (!w.done && pthread_cond_timedwait(&w.cv, &w.mu, &ts) == 0) {
    }
    pthread_mutex_unlock(&w.mu);
    TEST_ASSERT_TRUE_MESSAGE(w.done, "jmap post did not finish");
    TEST_ASSERT_TRUE(w.status > 0);
    pav_net_stop();
    pthread_mutex_destroy(&w.mu);
    pthread_cond_destroy(&w.cv);
}

#ifndef _WIN32
static int lock_profile_fd(void) {
    char path[220];
    int fd;

    TEST_ASSERT_TRUE(snprintf(path, sizeof(path), "%s/profiles/default.json.lock", g_cfg_dir) < (int)sizeof(path));
    fd = open(path, O_RDWR | O_CREAT, 0600);
    TEST_ASSERT_TRUE(fd >= 0);
    return fd;
}

void test_profile_lock_refuses_second_attach(void) {
    PavillionStatus status;
    int fd;

    open_log();
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    pavillion_stop();
    /* Another process holds the profile: flock is per open file, so this
       descriptor stands in for it. */
    fd = lock_profile_fd();
    TEST_ASSERT_EQUAL_INT(0, flock(fd, LOCK_EX | LOCK_NB));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, pavillion_status(&status));
    TEST_ASSERT_EQUAL_STRING("error", status.state);
    TEST_ASSERT_EQUAL_STRING("This profile is open in another Pavillion window.", status.error);
    pavillion_stop();
    close(fd);
}

void test_profile_lock_held_while_attached_and_released_at_stop(void) {
    PavillionStatus status;
    int fd;

    open_log();
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, pavillion_status(&status));
    TEST_ASSERT_EQUAL_STRING("ready", status.state);
    fd = lock_profile_fd();
    TEST_ASSERT_EQUAL_INT(-1, flock(fd, LOCK_EX | LOCK_NB));
    pavillion_stop();
    TEST_ASSERT_EQUAL_INT(0, flock(fd, LOCK_EX | LOCK_NB));
    TEST_ASSERT_EQUAL_INT(0, flock(fd, LOCK_UN));
    close(fd);
    /* A fresh start takes it again. */
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, pavillion_status(&status));
    TEST_ASSERT_EQUAL_STRING("ready", status.state);
}
#else
void test_profile_lock_refuses_second_attach(void) {
    TEST_IGNORE_MESSAGE("flock test is POSIX; Windows uses LockFileEx");
}

void test_profile_lock_held_while_attached_and_released_at_stop(void) {
    TEST_IGNORE_MESSAGE("flock test is POSIX; Windows uses LockFileEx");
}
#endif

static void act_text(char *dst, size_t dst_len, int64_t id) {
    snprintf(dst, dst_len, "act-%lld", (long long)id);
}

static json_t *activity_rows(int history) {
    json_t *root = call_reply("activity.list", history ? "{\"includeHistory\":true}" : "{}");
    json_t *rows;

    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    rows = json_object_get(json_object_get(root, "data"), "activities");
    TEST_ASSERT_TRUE(json_is_array(rows));
    json_incref(rows);
    json_decref(root);
    return rows;
}

static json_t *find_act(json_t *rows, const char *id) {
    size_t i;

    for (i = 0; i < json_array_size(rows); i++) {
        json_t *item = json_array_get(rows, i);
        const char *got = json_string_value(json_object_get(item, "id"));

        if (got != NULL && strcmp(got, id) == 0) {
            return item;
        }
    }
    return NULL;
}

static json_t *find_kind(json_t *rows, const char *kind) {
    size_t i;

    for (i = 0; i < json_array_size(rows); i++) {
        json_t *item = json_array_get(rows, i);
        const char *got = json_string_value(json_object_get(item, "kind"));

        if (got != NULL && strcmp(got, kind) == 0) {
            return item;
        }
    }
    return NULL;
}

static int user_version(const char *path) {
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    int version = -1;

    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        if (db != NULL) {
            sqlite3_close(db);
        }
        return -1;
    }
    if (sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &stmt, NULL) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_ROW) {
        version = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return version;
}

void test_activity_begin_progress_end(void) {
    json_t *rows;
    json_t *item;
    json_t *root;
    int64_t parent;
    int64_t child;
    char parent_id[32];
    char child_id[32];
    char *log_text;
    char path[160];

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, (int)pav_activity_begin("not.a.kind", NULL, "nope", 1, 0));
    parent = pav_activity_begin("sync.mail", "a-500foods-andrew", "Sync andrew@500foods.com", 1, 0);
    TEST_ASSERT_TRUE(parent > 0);
    act_text(parent_id, sizeof(parent_id), parent);
    TEST_ASSERT_EQUAL_INT(0, (int)pav_activity_begin("body.fetch", NULL, "orphan", 1, 99999));
    child = pav_activity_begin("body.fetch", NULL, "Fetch body", 1, parent);
    TEST_ASSERT_TRUE(child > parent);
    act_text(child_id, sizeof(child_id), child);

    rows = activity_rows(0);
    item = find_act(rows, parent_id);
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_EQUAL_STRING("running", json_string_value(json_object_get(item, "state")));
    TEST_ASSERT_EQUAL_STRING("sync.mail", json_string_value(json_object_get(item, "kind")));
    TEST_ASSERT_EQUAL_STRING("a-500foods-andrew", json_string_value(json_object_get(item, "accountId")));
    TEST_ASSERT_TRUE(json_is_null(json_object_get(item, "progress")));
    TEST_ASSERT_TRUE(json_is_null(json_object_get(item, "parentId")));
    TEST_ASSERT_TRUE(json_is_true(json_object_get(item, "cancellable")));
    TEST_ASSERT_TRUE(json_is_false(json_object_get(item, "dismissed")));
    item = find_act(rows, child_id);
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_EQUAL_STRING(parent_id, json_string_value(json_object_get(item, "parentId")));
    json_decref(rows);

    pav_activity_progress(parent, 3, 10, "messages", "Inbox");
    rows = activity_rows(0);
    item = find_act(rows, parent_id);
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_EQUAL_INT(3, (int)json_integer_value(json_object_get(json_object_get(item, "progress"), "done")));
    TEST_ASSERT_EQUAL_INT(10, (int)json_integer_value(json_object_get(json_object_get(item, "progress"), "total")));
    TEST_ASSERT_EQUAL_STRING("messages",
                             json_string_value(json_object_get(json_object_get(item, "progress"), "unit")));
    TEST_ASSERT_EQUAL_STRING("Inbox", json_string_value(json_object_get(item, "detail")));
    json_decref(rows);

    pav_activity_end(parent, PAV_ACT_DONE, NULL);
    rows = activity_rows(0);
    TEST_ASSERT_NULL(find_act(rows, parent_id));
    TEST_ASSERT_NOT_NULL(find_act(rows, child_id));
    json_decref(rows);
    rows = activity_rows(1);
    item = find_act(rows, parent_id);
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_EQUAL_STRING("done", json_string_value(json_object_get(item, "state")));
    TEST_ASSERT_TRUE(json_is_integer(json_object_get(item, "endedMs")));
    json_decref(rows);

    root = call_reply("activity.list", "{\"includeHistory\":\"yes\"}");
    assert_call_error(root, "bad_args", "includeHistory");
    json_decref(root);

    pav_log_flush();
    snprintf(path, sizeof(path), "%s/default.log.db", g_cfg_dir);
    log_text = db_text(path);
    TEST_ASSERT_NOT_NULL(strstr(log_text, "activity  begin "));
    TEST_ASSERT_NOT_NULL(strstr(log_text, parent_id));
    TEST_ASSERT_NOT_NULL(strstr(log_text, "sync.mail"));
    TEST_ASSERT_NOT_NULL(strstr(log_text, "end "));
    TEST_ASSERT_NOT_NULL(strstr(log_text, "done"));
    free(log_text);
    pav_activity_end(child, PAV_ACT_DONE, NULL);
}

static int wait_hist_state(const char *id, const char *state) {
    int i;

    for (i = 0; i < 50; i++) {
        json_t *rows = activity_rows(1);
        json_t *item = find_act(rows, id);
        const char *got = item != NULL ? json_string_value(json_object_get(item, "state")) : NULL;
        int match = got != NULL && strcmp(got, state) == 0;

        json_decref(rows);
        if (match) {
            return 1;
        }
        usleep(20000);
    }
    return 0;
}

void test_activity_cancel_is_cooperative(void) {
    json_t *rows;
    json_t *item;
    json_t *root;
    int64_t id;
    char text[32];
    char args[64];
    struct timespec t0;
    struct timespec t1;
    int64_t elapsed_ms;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));

    id = pav_activity_begin("send", NULL, "keep", 0, 0);
    TEST_ASSERT_TRUE(id > 0);
    act_text(text, sizeof(text), id);
    snprintf(args, sizeof(args), "{\"id\":\"%s\"}", text);
    root = call_reply("activity.cancel", args);
    assert_call_error(root, "bad_args", "cannot be cancelled");
    json_decref(root);
    TEST_ASSERT_EQUAL_INT(0, pav_activity_cancelled(id));
    pav_activity_end(id, PAV_ACT_DONE, NULL);

    id = pav_activity_begin("send", NULL, "stop me", 1, 0);
    act_text(text, sizeof(text), id);
    snprintf(args, sizeof(args), "{\"id\":\"%s\"}", text);
    root = call_reply("activity.cancel", args);
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    item = json_object_get(root, "data");
    TEST_ASSERT_EQUAL_STRING("running", json_string_value(json_object_get(item, "state")));
    json_decref(root);
    TEST_ASSERT_EQUAL_INT(1, pav_activity_cancelled(id));
    rows = activity_rows(0);
    item = find_act(rows, text);
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_EQUAL_STRING("running", json_string_value(json_object_get(item, "state")));
    json_decref(rows);
    pav_activity_end(id, PAV_ACT_CANCELLED, NULL);
    rows = activity_rows(1);
    item = find_act(rows, text);
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_EQUAL_STRING("cancelled", json_string_value(json_object_get(item, "state")));
    json_decref(rows);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    root = call_reply("debug.activity_demo", "{\"steps\":5,\"ms\":200}");
    clock_gettime(CLOCK_MONOTONIC, &t1);
    elapsed_ms = (int64_t)(t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
    TEST_ASSERT_TRUE(elapsed_ms < 180);
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    item = json_object_get(root, "data");
    TEST_ASSERT_NOT_NULL(json_string_value(json_object_get(item, "id")));
    snprintf(text, sizeof(text), "%s", json_string_value(json_object_get(item, "id")));
    json_decref(root);
    rows = activity_rows(0);
    item = find_act(rows, text);
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_EQUAL_STRING("running", json_string_value(json_object_get(item, "state")));
    TEST_ASSERT_EQUAL_STRING("sync.mail", json_string_value(json_object_get(item, "kind")));
    json_decref(rows);
    snprintf(args, sizeof(args), "{\"id\":\"%s\"}", text);
    root = call_reply("activity.cancel", args);
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    json_decref(root);
    TEST_ASSERT_TRUE(wait_hist_state(text, "cancelled"));
}

void test_activity_eta_after_two_samples(void) {
    json_t *rows;
    json_t *item;
    int64_t id;
    char text[32];

    pav_platform_fake_set_now_ms(1000000);
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    id = pav_activity_begin("index.rebuild", NULL, "Rebuild", 1, 0);
    TEST_ASSERT_TRUE(id > 0);
    act_text(text, sizeof(text), id);
    pav_activity_progress(id, 0, 1000, "messages", NULL);
    rows = activity_rows(0);
    item = find_act(rows, text);
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_TRUE(json_is_null(json_object_get(item, "etaMs")));
    json_decref(rows);

    pav_platform_fake_set_now_ms(1011000);
    pav_activity_progress(id, 100, 1000, "messages", NULL);
    rows = activity_rows(0);
    item = find_act(rows, text);
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_EQUAL_INT(99000, (int)json_integer_value(json_object_get(item, "etaMs")));
    json_decref(rows);

    pav_platform_fake_set_now_ms(1021000);
    pav_activity_progress(id, 150, -1, "messages", NULL);
    rows = activity_rows(0);
    item = find_act(rows, text);
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_TRUE(json_is_null(json_object_get(item, "etaMs")));
    TEST_ASSERT_TRUE(json_is_null(json_object_get(json_object_get(item, "progress"), "total")));
    json_decref(rows);
    pav_activity_end(id, PAV_ACT_DONE, NULL);
}

struct ActEv {
    int n;
    char last[2048];
};

static struct ActEv g_act_ev;

static void act_ev_cb(const char *name, const char *json, void *user) {
    struct ActEv *got = user;

    if (name == NULL || strcmp(name, "activity.changed") != 0 || json == NULL) {
        return;
    }
    pthread_mutex_lock(&g_ev_test_mu);
    got->n++;
    snprintf(got->last, sizeof(got->last), "%s", json);
    pthread_cond_signal(&g_ev_test_cv);
    pthread_mutex_unlock(&g_ev_test_mu);
}

static void wait_act_events(int want) {
    struct timespec ts;
    int rc;

    pthread_mutex_lock(&g_ev_test_mu);
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 2;
    while (g_act_ev.n < want) {
        rc = pthread_cond_timedwait(&g_ev_test_cv, &g_ev_test_mu, &ts);
        if (rc != 0) {
            break;
        }
    }
    pthread_mutex_unlock(&g_ev_test_mu);
}

void test_activity_events_throttled(void) {
    int64_t id;

    memset(&g_act_ev, 0, sizeof(g_act_ev));
    pav_platform_fake_set_now_ms(5000000);
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    pav_on_event(act_ev_cb, &g_act_ev);
    id = pav_activity_begin("rules.run", NULL, "Rules", 1, 0);
    TEST_ASSERT_TRUE(id > 0);
    wait_act_events(1);
    TEST_ASSERT_EQUAL_INT(1, g_act_ev.n);

    pav_activity_progress(id, 1, 10, "rows", NULL);
    usleep(80000);
    TEST_ASSERT_EQUAL_INT(1, g_act_ev.n);

    pav_platform_fake_set_now_ms(5000249);
    pav_activity_progress(id, 2, 10, "rows", NULL);
    usleep(80000);
    TEST_ASSERT_EQUAL_INT(1, g_act_ev.n);

    pav_platform_fake_set_now_ms(5000250);
    pav_activity_progress(id, 3, 10, "rows", NULL);
    wait_act_events(2);
    TEST_ASSERT_EQUAL_INT(2, g_act_ev.n);
    TEST_ASSERT_NOT_NULL(strstr(g_act_ev.last, "\"activity\""));

    pav_activity_end(id, PAV_ACT_DONE, NULL);
    wait_act_events(3);
    TEST_ASSERT_EQUAL_INT(3, g_act_ev.n);
    pav_on_event(NULL, NULL);
}

void test_activity_history_capped(void) {
    json_t *rows;
    json_t *item;
    json_t *root;
    int64_t first = 0;
    int64_t failed;
    int64_t id;
    int i;
    char first_text[32];
    char failed_text[32];
    char last_text[32];
    char args[64];
    char path[160];
    int failed_seen;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    snprintf(path, sizeof(path), "%s/default.sqlite", g_cfg_dir);
    TEST_ASSERT_EQUAL_INT(2, user_version(path));
    TEST_ASSERT_EQUAL_INT(PAV_STORE_TABLES, user_table_count(path));

    for (i = 0; i < 501; i++) {
        id = pav_activity_begin("export", NULL, "cap", 0, 0);
        TEST_ASSERT_TRUE(id > 0);
        if (i == 0) {
            first = id;
        }
        pav_activity_end(id, PAV_ACT_DONE, NULL);
    }
    act_text(first_text, sizeof(first_text), first);
    rows = activity_rows(1);
    TEST_ASSERT_EQUAL_INT(500, (int)json_array_size(rows));
    TEST_ASSERT_NULL(find_act(rows, first_text));
    json_decref(rows);

    failed = pav_activity_begin("import", NULL, "broken", 1, 0);
    TEST_ASSERT_TRUE(failed > 0);
    pav_activity_end(failed, PAV_ACT_FAILED, "disk full");
    act_text(failed_text, sizeof(failed_text), failed);
    for (i = 0; i < 500; i++) {
        id = pav_activity_begin("export", NULL, "cap", 0, 0);
        TEST_ASSERT_TRUE(id > 0);
        pav_activity_end(id, PAV_ACT_DONE, NULL);
    }
    act_text(last_text, sizeof(last_text), id);
    rows = activity_rows(1);
    TEST_ASSERT_EQUAL_INT(501, (int)json_array_size(rows));
    item = find_act(rows, failed_text);
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_EQUAL_STRING("failed", json_string_value(json_object_get(item, "state")));
    TEST_ASSERT_EQUAL_STRING("disk full", json_string_value(json_object_get(item, "error")));
    TEST_ASSERT_NOT_NULL(find_act(rows, last_text));
    TEST_ASSERT_NULL(find_act(rows, first_text));
    json_decref(rows);
    rows = activity_rows(0);
    TEST_ASSERT_EQUAL_INT(1, (int)json_array_size(rows));
    TEST_ASSERT_NOT_NULL(find_act(rows, failed_text));
    json_decref(rows);

    pavillion_stop();
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    TEST_ASSERT_EQUAL_INT(2, user_version(path));
    rows = activity_rows(1);
    TEST_ASSERT_EQUAL_INT(501, (int)json_array_size(rows));
    TEST_ASSERT_NOT_NULL(find_act(rows, failed_text));
    TEST_ASSERT_NOT_NULL(find_act(rows, last_text));
    TEST_ASSERT_NULL(find_act(rows, first_text));
    json_decref(rows);

    snprintf(args, sizeof(args), "{\"id\":\"%s\"}", failed_text);
    root = call_reply("activity.dismiss", args);
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    TEST_ASSERT_TRUE(json_is_true(json_object_get(json_object_get(root, "data"), "dismissed")));
    json_decref(root);
    rows = activity_rows(0);
    failed_seen = find_act(rows, failed_text) != NULL;
    json_decref(rows);
    TEST_ASSERT_FALSE(failed_seen);
    rows = activity_rows(1);
    TEST_ASSERT_NULL(find_act(rows, failed_text));
    TEST_ASSERT_EQUAL_INT(500, (int)json_array_size(rows));
    json_decref(rows);
}

void test_sync_now_does_nothing(void) {
    json_t *root;
    json_t *data;
    json_t *rows;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    root = call_reply("sync.now", "{}");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    data = json_object_get(root, "data");
    TEST_ASSERT_TRUE(json_is_object(data));
    TEST_ASSERT_EQUAL_INT(0, (int)json_object_size(data));
    json_decref(root);
    root = call_reply("sync.now", "{\"accountId\":\"a-1\"}");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    json_decref(root);
    root = call_reply("sync.now", "{\"accountId\":1}");
    assert_call_error(root, "bad_args", "accountId");
    json_decref(root);
    rows = activity_rows(1);
    TEST_ASSERT_EQUAL_INT(0, (int)json_array_size(rows));
    json_decref(rows);
}

static int g_seq[8];
static int g_seq_n;
static int g_batch_ids[8];
static int g_batch_n;
static int g_busy_calls;
static int g_write_fail;
static int g_read_fail;
static int g_busy_hits;

static void close_test_store(void) {
    if (g_test_store != NULL) {
        pav_store_close(g_test_store);
        g_test_store = NULL;
    }
}

static PavStore *open_v1(const char *path) {
    char err[256];
    PavStore *store = NULL;

    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, pav_store_open(&store, "sqlite", path, err, sizeof(err)), err);
    g_test_store = store;
    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, pav_store_migrate(store, err, sizeof(err)), err);
    return store;
}

static int table_exists(const char *path, const char *name) {
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    int found = 0;

    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        if (db != NULL) {
            sqlite3_close(db);
        }
        return 0;
    }
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?", -1, &stmt, NULL) ==
            SQLITE_OK &&
        sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT) == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        found = 1;
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return found;
}

static int scalar_int(const char *path, const char *sql) {
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    int value = -1;

    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        if (db != NULL) {
            sqlite3_close(db);
        }
        return -1;
    }
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        value = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return value;
}

static int64_t scalar_i64(const char *path, const char *sql) {
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    int64_t value = 0;

    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        if (db != NULL) {
            sqlite3_close(db);
        }
        return 0;
    }
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        value = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return value;
}

static int journal_is_wal(const char *path) {
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    int wal = 0;

    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        if (db != NULL) {
            sqlite3_close(db);
        }
        return 0;
    }
    if (sqlite3_prepare_v2(db, "PRAGMA journal_mode", -1, &stmt, NULL) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_ROW) {
        const char *mode = (const char *)sqlite3_column_text(stmt, 0);
        wal = mode != NULL && strcmp(mode, "wal") == 0;
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return wal;
}

static void db_path_for_cfg(char *path, size_t n) {
    snprintf(path, n, "%s/default.sqlite", g_cfg_dir);
}

static void account_state_text(const char *path, const char *id, char *out, size_t n) {
    char sql[320];
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;

    snprintf(out, n, "(unread)");
    snprintf(sql, sizeof(sql),
             "SELECT state, session_state, last_error, "
             "CASE WHEN session_json IS NULL THEN -1 ELSE length(session_json) END "
             "FROM account_state WHERE account_id='%s'",
             id);
    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        snprintf(out, n, "(no db)");
        if (db != NULL) {
            sqlite3_close(db);
        }
        return;
    }
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        const char *state = (const char *)sqlite3_column_text(stmt, 0);
        const char *session = (const char *)sqlite3_column_text(stmt, 1);
        const char *err = (const char *)sqlite3_column_text(stmt, 2);

        snprintf(out, n, "state=%s session=%s err=%s json=%d", state != NULL ? state : "(null)",
                 session != NULL ? session : "(null)", err != NULL ? err : "(null)", sqlite3_column_int(stmt, 3));
    } else {
        snprintf(out, n, "(no row)");
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

static int wait_count(const char *path, const char *sql, int expect, const char *id, char *why, size_t why_n) {
    int i;

    for (i = 0; i < 160; i++) {
        if (scalar_int(path, sql) == expect) {
            return 0;
        }
        usleep(50000);
    }
    if (why != NULL && why_n > 0) {
        account_state_text(path, id, why, why_n);
    }
    return -1;
}

static int wait_account_state(const char *path, const char *id, const char *state, char *why, size_t why_n) {
    char sql[192];

    snprintf(sql, sizeof(sql),
             "SELECT count(*) FROM account_state WHERE account_id='%s' AND state='%s'", id, state);
    return wait_count(path, sql, 1, id, why, why_n);
}

static int wait_count_tries(const char *path, const char *sql, int expect, const char *id, char *why, size_t why_n,
                            int tries) {
    int i;

    for (i = 0; i < tries; i++) {
        if (scalar_int(path, sql) == expect) {
            return 0;
        }
        usleep(50000);
    }
    if (why != NULL && why_n > 0) {
        account_state_text(path, id, why, why_n);
    }
    return -1;
}

static void assert_ok(json_t *root);

static json_t *find_sync_act(json_t *rows, const char *account_id) {
    size_t i;

    for (i = 0; i < json_array_size(rows); i++) {
        json_t *item = json_array_get(rows, i);
        const char *kind = json_string_value(json_object_get(item, "kind"));
        const char *acct = json_string_value(json_object_get(item, "accountId"));

        if (kind != NULL && acct != NULL && strcmp(kind, "sync.mail") == 0 && strcmp(acct, account_id) == 0) {
            return item;
        }
    }
    return NULL;
}

/* activity.list orders by id. A later sync.now is the last matching row. */
static json_t *find_sync_act_last(json_t *rows, const char *account_id) {
    size_t i;
    json_t *found = NULL;

    for (i = 0; i < json_array_size(rows); i++) {
        json_t *item = json_array_get(rows, i);
        const char *kind = json_string_value(json_object_get(item, "kind"));
        const char *acct = json_string_value(json_object_get(item, "accountId"));

        if (kind != NULL && acct != NULL && strcmp(kind, "sync.mail") == 0 && strcmp(acct, account_id) == 0) {
            found = item;
        }
    }
    return found;
}

static void add_fixture_account(const char *id, const char *session_url, const char *ca) {
    char body[1400];
    json_t *root;

    snprintf(body, sizeof(body),
             "{\"kind\":\"jmap\",\"sessionUrl\":\"%s\",\"username\":\"u\",\"secret\":\"Zq9-sync-pw\","
             "\"id\":\"%s\",\"name\":\"Box\",\"caFile\":\"%s\"}",
             session_url, id, ca);
    root = call_reply("account.add", body);
    assert_ok(root);
    json_decref(root);
}

static void add_fixture_account_color(const char *id, const char *session_url, const char *ca, const char *color) {
    char body[1400];
    json_t *root;
    int n;

    n = snprintf(body, sizeof(body),
                 "{\"kind\":\"jmap\",\"sessionUrl\":\"%s\",\"username\":\"u\",\"secret\":\"Zq9-sync-pw\","
                 "\"id\":\"%s\",\"name\":\"Box\",\"caFile\":\"%s\",\"color\":\"%s\"}",
                 session_url, id, ca, color);
    TEST_ASSERT_TRUE(n > 0 && n < (int)sizeof(body));
    root = call_reply("account.add", body);
    assert_ok(root);
    json_decref(root);
}

void test_sync_session_stored(void) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    char session_url[512];
    char path[160];
    char got[192];
    json_t *rows;
    json_t *item;

    TEST_ASSERT_NOT_NULL_MESSAGE(url, "PAV_FIXTURE_URL must be set by make test");
    TEST_ASSERT_NOT_NULL_MESSAGE(ca, "PAV_FIXTURE_CA must be set by make test");
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    pav_sync_set_poll_ms(3600000);
    snprintf(session_url, sizeof(session_url), "%s/.well-known/jmap", url);
    add_fixture_account("box", session_url, ca);
    db_path_for_cfg(path, sizeof(path));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, wait_count(path,
        "SELECT count(*) FROM account_state WHERE account_id='box' AND state='idle' "
        "AND session_state='state-1' AND session_json LIKE '%apiUrl%'",
        1, "box", got, sizeof(got)), got);
    TEST_ASSERT_EQUAL_INT(2, scalar_int(path, "SELECT count(*) FROM remote_account WHERE account_id='box'"));
    TEST_ASSERT_EQUAL_INT(6, scalar_int(path, "SELECT count(*) FROM mailbox WHERE ra_id LIKE 'box/%'"));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path,
        "SELECT count(*) FROM remote_account WHERE id='box/a1' AND is_personal=1 "
        "AND caps LIKE '%urn:ietf:params:jmap:mail%'"));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path,
        "SELECT count(*) FROM remote_account WHERE id='box/a2' AND is_personal=1 "
        "AND caps LIKE '%urn:ietf:params:jmap:mail%'"));
    rows = activity_rows(1);
    item = find_sync_act(rows, "box");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_EQUAL_STRING("done", json_string_value(json_object_get(item, "state")));
    TEST_ASSERT_EQUAL_STRING("Messages", json_string_value(json_object_get(item, "detail")));
    json_decref(rows);
}

void test_sync_401_stops_account(void) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    char session_url[512];
    char path[160];
    char got[192];
    json_t *root;
    int n;

    TEST_ASSERT_NOT_NULL_MESSAGE(url, "PAV_FIXTURE_URL must be set by make test");
    TEST_ASSERT_NOT_NULL_MESSAGE(ca, "PAV_FIXTURE_CA must be set by make test");
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    pav_sync_set_poll_ms(3600000);
    snprintf(session_url, sizeof(session_url), "%s/jmap/session?error=401", url);
    add_fixture_account("e401", session_url, ca);
    db_path_for_cfg(path, sizeof(path));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, wait_account_state(path, "e401", "error:auth", got, sizeof(got)), got);
    n = pav_sync_attempt_count("e401");
    TEST_ASSERT_TRUE(n >= 1);
    usleep(400000);
    TEST_ASSERT_EQUAL_INT(n, pav_sync_attempt_count("e401"));
    root = call_reply("sync.now", "{\"accountId\":\"e401\"}");
    assert_ok(root);
    json_decref(root);
    TEST_ASSERT_EQUAL_INT(n + 1, pav_sync_attempt_count("e401"));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, wait_account_state(path, "e401", "error:auth", got, sizeof(got)), got);
    usleep(400000);
    TEST_ASSERT_EQUAL_INT(n + 1, pav_sync_attempt_count("e401"));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path,
        "SELECT count(*) FROM account_state WHERE account_id='e401' AND state='error:auth'"));
}

void test_sync_locked_keyring_no_retry(void) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    char session_url[512];
    char path[160];
    char got[192];
    json_t *root;
    int n;

    TEST_ASSERT_NOT_NULL_MESSAGE(url, "PAV_FIXTURE_URL must be set by make test");
    TEST_ASSERT_NOT_NULL_MESSAGE(ca, "PAV_FIXTURE_CA must be set by make test");
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    pav_platform_fake_set_locked(0);
    pav_sync_set_poll_ms(3600000);
    snprintf(session_url, sizeof(session_url), "%s/jmap/session", url);
    add_fixture_account("lock", session_url, ca);
    db_path_for_cfg(path, sizeof(path));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, wait_count(path,
        "SELECT count(*) FROM account_state WHERE account_id='lock' AND state='idle' "
        "AND session_state='state-1'",
        1, "lock", got, sizeof(got)), got);
    n = pav_sync_attempt_count("lock");
    TEST_ASSERT_EQUAL_INT(1, n);

    pav_platform_fake_set_locked(1);
    root = call_reply("sync.now", "{\"accountId\":\"lock\"}");
    assert_ok(root);
    json_decref(root);
    TEST_ASSERT_EQUAL_INT(n + 1, pav_sync_attempt_count("lock"));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, wait_account_state(path, "lock", "locked", got, sizeof(got)), got);
    usleep(400000);
    TEST_ASSERT_EQUAL_INT(n + 1, pav_sync_attempt_count("lock"));

    pav_platform_fake_set_locked(0);
    root = call_reply("sync.now", "{\"accountId\":\"lock\"}");
    assert_ok(root);
    json_decref(root);
    TEST_ASSERT_EQUAL_INT(n + 2, pav_sync_attempt_count("lock"));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, wait_count(path,
        "SELECT count(*) FROM account_state WHERE account_id='lock' AND state='idle' "
        "AND session_state='state-1'",
        1, "lock", got, sizeof(got)), got);
    usleep(400000);
    TEST_ASSERT_EQUAL_INT(n + 2, pav_sync_attempt_count("lock"));
}

static int seq_job(void *ctx, sqlite3 *db) {
    (void)db;
    g_seq[g_seq_n++] = *(int *)ctx;
    return 0;
}

static int note_batch_job(void *ctx, sqlite3 *db) {
    int fail = ctx != NULL && *(int *)ctx;

    (void)db;
    if (g_batch_n < (int)(sizeof(g_batch_ids) / sizeof(g_batch_ids[0]))) {
        g_batch_ids[g_batch_n++] = pav_db_writer_batch();
    }
    return fail ? 1 : 0;
}

static int insert_key_job(void *ctx, sqlite3 *db) {
    const char *key = ctx;
    sqlite3_stmt *stmt = NULL;
    int rc;

    if (g_batch_n < (int)(sizeof(g_batch_ids) / sizeof(g_batch_ids[0]))) {
        g_batch_ids[g_batch_n++] = pav_db_writer_batch();
    }
    if (pav_db_prepare(db, "kvins", "INSERT INTO kv(key,value) VALUES(?, '1')", &stmt) != 0) {
        return -1;
    }
    if (pav_db_bind_text(stmt, 1, key) != 0) {
        return -1;
    }
    rc = pav_db_step(stmt);
    return rc == SQLITE_DONE ? 0 : (rc == SQLITE_BUSY ? SQLITE_BUSY : -1);
}

static int fail_key_job(void *ctx, sqlite3 *db) {
    (void)ctx;
    (void)db;
    if (g_batch_n < (int)(sizeof(g_batch_ids) / sizeof(g_batch_ids[0]))) {
        g_batch_ids[g_batch_n++] = pav_db_writer_batch();
    }
    return 1;
}

void test_writes_are_serial_fifo(void) {
    char path[160];
    static int ids[4] = {0, 1, 2, 3};
    int i;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    snprintf(path, sizeof(path), "%s/mail.sqlite", g_cfg_dir);
    open_v1(path);
    g_seq_n = 0;
    pav_db_writer_hold(1);
    for (i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_INT(0, pav_db_write(seq_job, &ids[i], NULL, NULL));
    }
    pav_db_writer_hold(0);
    TEST_ASSERT_EQUAL_INT(0, pav_db_drain());
    TEST_ASSERT_EQUAL_INT(4, g_seq_n);
    for (i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_INT(i, g_seq[i]);
    }
    close_test_store();
}

void test_write_batch_one_transaction(void) {
    char path[160];
    int i;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    snprintf(path, sizeof(path), "%s/mail.sqlite", g_cfg_dir);
    open_v1(path);
    g_batch_n = 0;
    pav_db_writer_hold(1);
    for (i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_INT(0, pav_db_write(note_batch_job, NULL, NULL, NULL));
    }
    pav_db_writer_hold(0);
    TEST_ASSERT_EQUAL_INT(0, pav_db_drain());
    TEST_ASSERT_EQUAL_INT(3, g_batch_n);
    TEST_ASSERT_TRUE(g_batch_ids[0] > 0);
    TEST_ASSERT_EQUAL_INT(g_batch_ids[0], g_batch_ids[1]);
    TEST_ASSERT_EQUAL_INT(g_batch_ids[0], g_batch_ids[2]);
    close_test_store();
}

void test_failed_job_rolls_back_alone(void) {
    char path[160];
    static int fail = 1;
    int first;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    snprintf(path, sizeof(path), "%s/mail.sqlite", g_cfg_dir);
    open_v1(path);
    g_batch_n = 0;
    pav_db_writer_hold(1);
    TEST_ASSERT_EQUAL_INT(0, pav_db_write(insert_key_job, "a", NULL, NULL));
    TEST_ASSERT_EQUAL_INT(0, pav_db_write(insert_key_job, "b", NULL, NULL));
    TEST_ASSERT_EQUAL_INT(0, pav_db_write(fail_key_job, &fail, NULL, NULL));
    pav_db_writer_hold(0);
    TEST_ASSERT_EQUAL_INT(0, pav_db_drain());
    TEST_ASSERT_EQUAL_INT(6, g_batch_n);
    first = g_batch_ids[0];
    TEST_ASSERT_EQUAL_INT(first, g_batch_ids[1]);
    TEST_ASSERT_EQUAL_INT(first, g_batch_ids[2]);
    TEST_ASSERT_TRUE(g_batch_ids[3] != first);
    TEST_ASSERT_TRUE(g_batch_ids[4] != g_batch_ids[3]);
    TEST_ASSERT_TRUE(g_batch_ids[5] != g_batch_ids[4]);
    close_test_store();
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path, "SELECT count(*) FROM kv WHERE key='a'"));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path, "SELECT count(*) FROM kv WHERE key='b'"));
    TEST_ASSERT_EQUAL_INT(0, scalar_int(path, "SELECT count(*) FROM kv WHERE key='c'"));
}

void test_read_conn_refuses_insert(void) {
    char path[160];
    PavDbRead *read = NULL;
    sqlite3_stmt *stmt = NULL;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    snprintf(path, sizeof(path), "%s/mail.sqlite", g_cfg_dir);
    open_v1(path);
    TEST_ASSERT_EQUAL_INT(0, pav_db_read_begin(&read));
    TEST_ASSERT_EQUAL_INT(
        -1, pav_db_prepare(pav_db_read_handle(read), "badins", "INSERT INTO kv(key,value) VALUES('a','b')", &stmt));
    TEST_ASSERT_NULL(stmt);
    TEST_ASSERT_EQUAL_INT(
        0, pav_db_prepare(pav_db_read_handle(read), "oksel", "SELECT count(*) FROM kv", &stmt));
    TEST_ASSERT_EQUAL_INT(SQLITE_ROW, pav_db_step(stmt));
    pav_db_read_end(read);
    close_test_store();
}

static int put_n_job(void *ctx, sqlite3 *db) {
    int n = *(int *)ctx;
    char key[32];
    sqlite3_stmt *stmt = NULL;
    int rc;

    snprintf(key, sizeof(key), "w%d", n);
    if (pav_db_prepare(db, "wput", "INSERT INTO kv(key,value) VALUES(?, '1')", &stmt) != 0) {
        __sync_fetch_and_add(&g_write_fail, 1);
        return -1;
    }
    if (pav_db_bind_text(stmt, 1, key) != 0) {
        __sync_fetch_and_add(&g_write_fail, 1);
        return -1;
    }
    rc = pav_db_step(stmt);
    if (rc == SQLITE_BUSY) {
        __sync_fetch_and_add(&g_busy_hits, 1);
        return SQLITE_BUSY;
    }
    if (rc != SQLITE_DONE) {
        __sync_fetch_and_add(&g_write_fail, 1);
        return -1;
    }
    return 0;
}

static void *read_loop(void *arg) {
    int i;

    (void)arg;
    for (i = 0; i < 2000; i++) {
        PavDbRead *read = NULL;
        sqlite3_stmt *stmt = NULL;

        if (pav_db_read_begin(&read) != 0) {
            __sync_fetch_and_add(&g_read_fail, 1);
            continue;
        }
        if (pav_db_prepare(pav_db_read_handle(read), "kvcnt", "SELECT count(*) FROM kv", &stmt) != 0 ||
            pav_db_step(stmt) != SQLITE_ROW) {
            __sync_fetch_and_add(&g_read_fail, 1);
        }
        pav_db_read_end(read);
    }
    return NULL;
}

void test_concurrent_reads_during_write_no_busy(void) {
    char path[160];
    static int ids[10000];
    pthread_t threads[4];
    int i;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    snprintf(path, sizeof(path), "%s/mail.sqlite", g_cfg_dir);
    open_v1(path);
    g_write_fail = 0;
    g_read_fail = 0;
    g_busy_hits = 0;
    pav_db_writer_hold(1);
    for (i = 0; i < 10000; i++) {
        ids[i] = i;
        TEST_ASSERT_EQUAL_INT(0, pav_db_write(put_n_job, &ids[i], NULL, NULL));
    }
    for (i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_INT(0, pthread_create(&threads[i], NULL, read_loop, NULL));
    }
    pav_db_writer_hold(0);
    TEST_ASSERT_EQUAL_INT(0, pav_db_drain());
    for (i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }
    TEST_ASSERT_EQUAL_INT(0, g_busy_hits);
    TEST_ASSERT_EQUAL_INT(0, g_write_fail);
    TEST_ASSERT_EQUAL_INT(0, g_read_fail);
    close_test_store();
    TEST_ASSERT_EQUAL_INT(10000, scalar_int(path, "SELECT count(*) FROM kv"));
}

static int busy_job(void *ctx, sqlite3 *db) {
    (void)ctx;
    (void)db;
    g_busy_calls++;
    return SQLITE_BUSY;
}

void test_writer_busy_retries_once(void) {
    char path[160];
    int before;
    int rc;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    snprintf(path, sizeof(path), "%s/mail.sqlite", g_cfg_dir);
    open_v1(path);
    g_busy_calls = 0;
    before = pav_db_busy_checkpoints();
    rc = pav_db_call(busy_job, NULL);
    TEST_ASSERT_EQUAL_INT(SQLITE_BUSY, rc);
    TEST_ASSERT_EQUAL_INT(2, g_busy_calls);
    TEST_ASSERT_EQUAL_INT(before + 1, pav_db_busy_checkpoints());
    close_test_store();
}

void test_store_writer_stack_is_512kb(void) {
#ifdef _WIN32
    TEST_IGNORE_MESSAGE("winpthreads cannot read a thread stack size");
#endif
    char path[160];
    size_t stack = 0;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    snprintf(path, sizeof(path), "%s/mail.sqlite", g_cfg_dir);
    open_v1(path);
    TEST_ASSERT_EQUAL_INT(0, pav_db_writer_stack(&stack));
#if defined(__APPLE__)
    /* pthread_get_stacksize_np counts the guard page; the request is still 512 KiB. */
    TEST_ASSERT_TRUE(stack >= 512u * 1024u && stack < 512u * 1024u + 64u * 1024u);
#else
    TEST_ASSERT_EQUAL_INT(512 * 1024, (int)stack);
#endif
    close_test_store();
}

void test_migrate_fresh_to_v1(void) {
    char path[160];
    static const char *names[] = {
        "activity", "account_state", "remote_account", "identity", "mailbox", "sync_state", "kv",
        "email", "body_cache", "email_fts", "outbox", "draft_local",
    };
    size_t i;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    snprintf(path, sizeof(path), "%s/mail.sqlite", g_cfg_dir);
    open_v1(path);
    TEST_ASSERT_EQUAL_INT(2, user_version(path));
    TEST_ASSERT_EQUAL_INT(PAV_STORE_TABLES, user_table_count(path));
    TEST_ASSERT_TRUE(journal_is_wal(path));
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        TEST_ASSERT_TRUE(table_exists(path, names[i]));
    }
    close_test_store();
}

void test_migrate_is_idempotent(void) {
    char path[160];
    char err[256];
    sqlite3 *raw = NULL;
    PavStore *store = NULL;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    snprintf(path, sizeof(path), "%s/mail.sqlite", g_cfg_dir);
    TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_open_v2(path, &raw, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL));
    TEST_ASSERT_EQUAL_INT(
        SQLITE_OK,
        sqlite3_exec(raw,
                     "CREATE TABLE activity ("
                     "id INTEGER PRIMARY KEY, kind TEXT NOT NULL, account_id TEXT, title TEXT NOT NULL, "
                     "detail TEXT, state TEXT NOT NULL, started_ms INTEGER NOT NULL, ended_ms INTEGER, "
                     "prog_done INTEGER, prog_total INTEGER, prog_unit TEXT, eta_ms INTEGER, "
                     "cancellable INTEGER NOT NULL, error TEXT, parent_id INTEGER, "
                     "dismissed INTEGER NOT NULL DEFAULT 0)",
                     NULL, NULL, NULL));
    TEST_ASSERT_EQUAL_INT(
        SQLITE_OK,
        sqlite3_exec(raw,
                     "INSERT INTO activity (id, kind, title, state, started_ms, ended_ms, cancellable) "
                     "VALUES (7, 'sync.mail', 'kept', 'done', 1, 2, 0)",
                     NULL, NULL, NULL));
    sqlite3_close(raw);
    raw = NULL;
    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, pav_store_open(&store, "sqlite", path, err, sizeof(err)), err);
    g_test_store = store;
    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, pav_store_migrate(store, err, sizeof(err)), err);
    TEST_ASSERT_EQUAL_INT(2, user_version(path));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path, "SELECT count(*) FROM activity WHERE id=7 AND title='kept'"));
    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, pav_store_migrate(store, err, sizeof(err)), err);
    TEST_ASSERT_EQUAL_INT(2, user_version(path));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path, "SELECT count(*) FROM activity WHERE id=7 AND title='kept'"));
    TEST_ASSERT_EQUAL_INT(PAV_STORE_TABLES, user_table_count(path));
    close_test_store();
}

void test_newer_db_is_error(void) {
    char path[160];
    char err[256];
    sqlite3 *raw = NULL;
    PavStore *store = NULL;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    snprintf(path, sizeof(path), "%s/mail.sqlite", g_cfg_dir);
    TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_open_v2(path, &raw, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL));
    TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_exec(raw, "PRAGMA user_version = 9", NULL, NULL, NULL));
    sqlite3_close(raw);
    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, pav_store_open(&store, "sqlite", path, err, sizeof(err)), err);
    g_test_store = store;
    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, pav_store_migrate(store, err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(strstr(err, "9"));
    TEST_ASSERT_NOT_NULL(strstr(err, "2"));
    TEST_ASSERT_NOT_NULL(strstr(err, "newer"));
    close_test_store();
}

static char g_sql_err[256];

static int exec_sql_job(void *ctx, sqlite3 *db) {
    char *errmsg = NULL;
    int rc = sqlite3_exec(db, ctx, NULL, NULL, &errmsg);

    g_sql_err[0] = '\0';
    if (errmsg != NULL) {
        snprintf(g_sql_err, sizeof(g_sql_err), "%s", errmsg);
        sqlite3_free(errmsg);
    }
    return rc == SQLITE_OK ? 0 : -1;
}

static void write_private_text(const char *path, const char *text) {
    const PavPlatform *platform = pav_platform();

    TEST_ASSERT_NOT_NULL(platform);
    TEST_ASSERT_NOT_NULL(platform->write_private);
    TEST_ASSERT_EQUAL_INT(0, platform->write_private(path, text, strlen(text)));
}

static void expect_segments(char *out, size_t len, const char *root, const char *ra, const char *shard,
                            const char *id) {
    char one[4096];
    char two[4096];

    TEST_ASSERT_EQUAL_INT(0, pav_path_join(one, sizeof(one), root, ra));
    TEST_ASSERT_EQUAL_INT(0, pav_path_join(two, sizeof(two), one, shard));
    TEST_ASSERT_EQUAL_INT(0, pav_path_join(out, len, two, id));
}

static void bind_slot_blobs(char *root, size_t len) {
    TEST_ASSERT_EQUAL_INT(0, pav_path_join(root, len, g_cfg_dir, "blobs"));
    TEST_ASSERT_EQUAL_INT(0, pav_blob_bind(root));
}

void test_migrate_v1_to_v2(void) {
    char path[160];
    char err[256];
    char *sql;
    char *errmsg = NULL;
    sqlite3 *raw = NULL;
    PavStore *store = NULL;
    int rc;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    snprintf(path, sizeof(path), "%s/mail.sqlite", g_cfg_dir);
    sql = slurp("core/src/migrations/001_accounts.sql");
    TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_open_v2(path, &raw, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL));
    rc = sqlite3_exec(raw, sql, NULL, NULL, &errmsg);
    free(sql);
    TEST_ASSERT_EQUAL_INT_MESSAGE(SQLITE_OK, rc, errmsg != NULL ? errmsg : "");
    sqlite3_free(errmsg);
    TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_exec(raw, "PRAGMA user_version = 1", NULL, NULL, NULL));
    TEST_ASSERT_EQUAL_INT(
        SQLITE_OK,
        sqlite3_exec(raw,
                     "INSERT INTO activity (id, kind, title, state, started_ms, ended_ms, cancellable) "
                     "VALUES (4, 'sync.mail', 'kept-v1', 'done', 1, 2, 0);"
                     "INSERT INTO kv (key, value) VALUES ('keep', 'yes');",
                     NULL, NULL, NULL));
    sqlite3_close(raw);
    TEST_ASSERT_EQUAL_INT(1, user_version(path));
    TEST_ASSERT_FALSE(table_exists(path, "email"));

    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, pav_store_open(&store, "sqlite", path, err, sizeof(err)), err);
    g_test_store = store;
    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, pav_store_migrate(store, err, sizeof(err)), err);
    TEST_ASSERT_EQUAL_INT(2, user_version(path));
    TEST_ASSERT_TRUE(table_exists(path, "email"));
    TEST_ASSERT_TRUE(table_exists(path, "body_cache"));
    TEST_ASSERT_TRUE(table_exists(path, "email_fts"));
    TEST_ASSERT_TRUE(table_exists(path, "outbox"));
    TEST_ASSERT_TRUE(table_exists(path, "draft_local"));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path, "SELECT count(*) FROM activity WHERE id=4 AND title='kept-v1'"));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path, "SELECT count(*) FROM kv WHERE key='keep' AND value='yes'"));
    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, pav_store_migrate(store, err, sizeof(err)), err);
    TEST_ASSERT_EQUAL_INT(2, user_version(path));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path, "SELECT count(*) FROM activity WHERE id=4 AND title='kept-v1'"));
    close_test_store();
}

void test_blob_path_sharding(void) {
    char root[160];
    char path[4096];
    char expect[4096];

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    bind_slot_blobs(root, sizeof(root));
    TEST_ASSERT_EQUAL_INT(0, pav_blob_path("a/b", "foo", path, sizeof(path)));
    expect_segments(expect, sizeof(expect), root, "YS9i", "Zm", "Zm9v");
    TEST_ASSERT_EQUAL_STRING(expect, path);
    TEST_ASSERT_EQUAL_INT(0, pav_blob_path("a/b", "f", path, sizeof(path)));
    expect_segments(expect, sizeof(expect), root, "YS9i", "Zg", "Zg");
    TEST_ASSERT_EQUAL_STRING(expect, path);
    TEST_ASSERT_NULL(strstr(path, "/a/"));
    pav_blob_unbind();
}

void test_blob_id_is_base64url(void) {
    char root[160];
    char path[4096];

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    bind_slot_blobs(root, sizeof(root));
    TEST_ASSERT_EQUAL_INT(0, pav_blob_path("a", "foo", path, sizeof(path)));
    TEST_ASSERT_NOT_NULL(strstr(path, "Zm9v"));
    TEST_ASSERT_NULL(strchr(path, '='));
    TEST_ASSERT_EQUAL_INT(0, pav_blob_path("a", "/", path, sizeof(path)));
    TEST_ASSERT_NOT_NULL(strstr(path, "Lw"));
    TEST_ASSERT_EQUAL_INT(0, pav_blob_path("a", "..", path, sizeof(path)));
    TEST_ASSERT_NOT_NULL(strstr(path, "Li4"));
    TEST_ASSERT_NULL(strstr(path, "/../"));
    TEST_ASSERT_EQUAL_INT(-1, pav_blob_path("a", "", path, sizeof(path)));
    TEST_ASSERT_EQUAL_INT(-1, pav_blob_path("", "foo", path, sizeof(path)));
    TEST_ASSERT_EQUAL_INT(-1, pav_blob_path("a", NULL, path, sizeof(path)));
    pav_blob_unbind();
}

void test_blob_modes(void) {
    char root[160];
    char path[4096];
    char parent[4096];
    char ra_dir[4096];
    struct stat st;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    bind_slot_blobs(root, sizeof(root));
    TEST_ASSERT_EQUAL_INT(0, pav_blob_write("a/b", "foo", "hello", 5));
    TEST_ASSERT_EQUAL_INT(0, pav_blob_path("a/b", "foo", path, sizeof(path)));
    TEST_ASSERT_EQUAL_INT(0, stat(path, &st));
    TEST_ASSERT_TRUE(S_ISREG(st.st_mode));
    ASSERT_MODE(0600, st);
    TEST_ASSERT_EQUAL_INT(0, pav_path_parent(parent, sizeof(parent), path));
    TEST_ASSERT_EQUAL_INT(0, stat(parent, &st));
    TEST_ASSERT_TRUE(S_ISDIR(st.st_mode));
    ASSERT_MODE(0700, st);
    TEST_ASSERT_EQUAL_INT(0, pav_path_parent(ra_dir, sizeof(ra_dir), parent));
    TEST_ASSERT_EQUAL_INT(0, stat(ra_dir, &st));
    TEST_ASSERT_TRUE(S_ISDIR(st.st_mode));
    ASSERT_MODE(0700, st);
    TEST_ASSERT_EQUAL_INT(1, pav_blob_exists("a/b", "foo"));
    TEST_ASSERT_EQUAL_INT(0, pav_blob_remove("a/b", "foo"));
    TEST_ASSERT_EQUAL_INT(0, pav_blob_exists("a/b", "foo"));
    TEST_ASSERT_EQUAL_INT(0, pav_blob_remove("a/b", "foo"));
    pav_blob_unbind();
}

void test_evict_skips_flagged_and_drafts(void) {
    char db[160];
    char body[4][256];
    char blob_normal[4096];
    char sql[4096];
    const char *leaves[] = {"b-normal", "b-flagged", "b-draft", "b-outbox"};
    json_t *rows;
    json_t *item;
    int wrote;
    int i;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, pav_path_join(db, sizeof(db), g_cfg_dir, "default.sqlite"));
    for (i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_INT(0, pav_path_join(body[i], sizeof(body[i]), g_cfg_dir, leaves[i]));
        write_private_text(body[i], "body-bytes");
    }
    TEST_ASSERT_EQUAL_INT(0, pav_blob_write("acct/ra", "normal-blob", "blob-bytes", 10));
    TEST_ASSERT_EQUAL_INT(0, pav_blob_write("acct/ra", "flagged-blob", "blob-bytes", 10));
    TEST_ASSERT_EQUAL_INT(0, pav_blob_path("acct/ra", "normal-blob", blob_normal, sizeof(blob_normal)));
    wrote = snprintf(sql, sizeof(sql),
                     "INSERT INTO account_state (account_id) VALUES ('acct');"
                     "INSERT INTO remote_account (id, account_id, jmap_account_id) "
                     "VALUES ('acct/ra', 'acct', 'j1');"
                     "INSERT INTO email (rowid, ra_id, id, blob_id, draft, flagged, body_state, last_opened) VALUES "
                     "(1, 'acct/ra', 'normal', 'normal-blob', 0, 0, 'fetched', 50),"
                     "(2, 'acct/ra', 'flagged', 'flagged-blob', 0, 1, 'fetched', 1),"
                     "(3, 'acct/ra', 'draft', NULL, 1, 0, 'fetched', 1),"
                     "(4, 'acct/ra', 'queued', NULL, 0, 0, 'fetched', 1);"
                     "INSERT INTO outbox (ra_id, draft_email_id) VALUES ('acct/ra', 'queued');"
                     "INSERT INTO body_cache (email_rowid, text_plain_path) VALUES "
                     "(1, '%s'), (2, '%s'), (3, '%s'), (4, '%s');"
                     "INSERT INTO attachment (email_rowid, part_id, blob_id, local_path) "
                     "VALUES (1, '1', 'normal-blob', '%s');",
                     body[0], body[1], body[2], body[3], blob_normal);
    TEST_ASSERT_TRUE(wrote > 0 && (size_t)wrote < sizeof(sql));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, pav_db_call(exec_sql_job, sql), g_sql_err);

    pav_cache_set_limits(0, 0);
    TEST_ASSERT_EQUAL_INT(0, pav_cache_evict());
    pav_cache_set_limits(PAV_CACHE_BODIES_MB, PAV_CACHE_BLOBS_MB);

    TEST_ASSERT_FALSE(pav_exists(body[0]));
    TEST_ASSERT_TRUE(pav_exists(body[1]));
    TEST_ASSERT_TRUE(pav_exists(body[2]));
    TEST_ASSERT_TRUE(pav_exists(body[3]));
    TEST_ASSERT_EQUAL_INT(0, pav_blob_exists("acct/ra", "normal-blob"));
    TEST_ASSERT_EQUAL_INT(1, pav_blob_exists("acct/ra", "flagged-blob"));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(db, "SELECT count(*) FROM email WHERE id='normal' AND blob_id='normal-blob' "
                                            "AND body_state='evicted'"));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(db, "SELECT count(*) FROM email WHERE id='flagged' AND body_state='fetched'"));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(db, "SELECT count(*) FROM email WHERE id='draft' AND body_state='fetched'"));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(db, "SELECT count(*) FROM email WHERE id='queued' AND body_state='fetched'"));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(db, "SELECT count(*) FROM body_cache WHERE email_rowid=1 "
                                            "AND text_plain_path IS NULL"));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(db, "SELECT count(*) FROM body_cache WHERE email_rowid=2 "
                                            "AND text_plain_path IS NOT NULL"));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(db, "SELECT count(*) FROM attachment WHERE email_rowid=1 "
                                            "AND blob_id='normal-blob' AND local_path IS NULL"));

    rows = activity_rows(1);
    item = find_kind(rows, "cache.evict");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_EQUAL_STRING("done", json_string_value(json_object_get(item, "state")));
    TEST_ASSERT_EQUAL_STRING("Evict cache", json_string_value(json_object_get(item, "title")));
    TEST_ASSERT_TRUE(json_is_false(json_object_get(item, "cancellable")));
    json_decref(rows);
}

void test_evict_clears_body_paths(void) {
    char db[160];
    char plain[256];
    char raw[256];
    char clean[256];
    char sql[2048];
    json_t *rows;
    int wrote;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, pav_path_join(db, sizeof(db), g_cfg_dir, "default.sqlite"));
    TEST_ASSERT_EQUAL_INT(0, pav_path_join(plain, sizeof(plain), g_cfg_dir, "plain"));
    TEST_ASSERT_EQUAL_INT(0, pav_path_join(raw, sizeof(raw), g_cfg_dir, "raw"));
    TEST_ASSERT_EQUAL_INT(0, pav_path_join(clean, sizeof(clean), g_cfg_dir, "clean"));
    write_private_text(plain, "plain");
    write_private_text(raw, "raw");
    write_private_text(clean, "clean");
    wrote = snprintf(sql, sizeof(sql),
                     "INSERT INTO account_state (account_id) VALUES ('acct');"
                     "INSERT INTO remote_account (id, account_id, jmap_account_id) "
                     "VALUES ('acct/ra', 'acct', 'j1');"
                     "INSERT INTO email (rowid, ra_id, id, blob_id, body_state) "
                     "VALUES (1, 'acct/ra', 'one', 'keep-me', 'fetched');"
                     "INSERT INTO body_cache (email_rowid, text_plain_path, html_raw_path, html_sanitized_path) "
                     "VALUES (1, '%s', '%s', '%s');",
                     plain, raw, clean);
    TEST_ASSERT_TRUE(wrote > 0 && (size_t)wrote < sizeof(sql));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, pav_db_call(exec_sql_job, sql), g_sql_err);

    TEST_ASSERT_EQUAL_INT(0, pav_cache_evict());
    TEST_ASSERT_TRUE(pav_exists(plain));
    rows = activity_rows(1);
    TEST_ASSERT_NULL(find_kind(rows, "cache.evict"));
    json_decref(rows);

    pav_cache_set_limits(0, PAV_CACHE_BLOBS_MB);
    TEST_ASSERT_EQUAL_INT(0, pav_cache_evict());
    pav_cache_set_limits(PAV_CACHE_BODIES_MB, PAV_CACHE_BLOBS_MB);

    TEST_ASSERT_FALSE(pav_exists(plain));
    TEST_ASSERT_FALSE(pav_exists(raw));
    TEST_ASSERT_FALSE(pav_exists(clean));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(db, "SELECT count(*) FROM email WHERE id='one' AND blob_id='keep-me' "
                                            "AND body_state='evicted'"));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(db, "SELECT count(*) FROM body_cache WHERE email_rowid=1 "
                                            "AND text_plain_path IS NULL AND html_raw_path IS NULL "
                                            "AND html_sanitized_path IS NULL"));
    rows = activity_rows(1);
    TEST_ASSERT_NOT_NULL(find_kind(rows, "cache.evict"));
    json_decref(rows);
}

static int using_has(json_t *root, const char *urn) {
    size_t i;
    json_t *item;

    json_array_foreach(json_object_get(root, "using"), i, item) {
        const char *text = json_string_value(item);

        if (text != NULL && strcmp(text, urn) == 0) {
            return 1;
        }
    }
    return 0;
}

static json_t *method_named(json_t *root, const char *name) {
    size_t i;
    json_t *call;

    json_array_foreach(json_object_get(root, "methodCalls"), i, call) {
        const char *have = json_string_value(json_array_get(call, 0));

        if (have != NULL && strcmp(have, name) == 0) {
            return call;
        }
    }
    return NULL;
}

static void assert_blank_figure(json_t *row, const char *name) {
    json_t *fig = json_object_get(row, name);

    TEST_ASSERT_TRUE(json_is_object(fig));
    TEST_ASSERT_TRUE(json_is_null(json_object_get(fig, "count")));
    TEST_ASSERT_TRUE(json_is_null(json_object_get(fig, "bytes")));
}

static json_t *usage_row(json_t *data, const char *id) {
    size_t i;
    json_t *row;

    json_array_foreach(json_object_get(data, "accounts"), i, row) {
        const char *have = json_string_value(json_object_get(row, "id"));

        if (have != NULL && strcmp(have, id) == 0) {
            return row;
        }
    }
    return NULL;
}

void test_quota_request_is_a_get(void) {
    json_error_t jerr;
    json_t *root;
    json_t *calls;
    json_t *call;
    json_t *args;
    char *text = pav_quota_request("acc-1");

    TEST_ASSERT_NOT_NULL(text);
    root = json_loads(text, 0, &jerr);
    free(text);
    TEST_ASSERT_NOT_NULL(root);
    calls = json_object_get(root, "methodCalls");
    TEST_ASSERT_TRUE(json_is_array(calls));
    TEST_ASSERT_EQUAL_INT(1, (int)json_array_size(calls));
    call = json_array_get(calls, 0);
    TEST_ASSERT_EQUAL_STRING("Quota/get", json_string_value(json_array_get(call, 0)));
    args = json_array_get(call, 1);
    TEST_ASSERT_EQUAL_STRING("acc-1", json_string_value(json_object_get(args, "accountId")));
    TEST_ASSERT_TRUE(json_is_null(json_object_get(args, "ids")));
    TEST_ASSERT_EQUAL_STRING("q0", json_string_value(json_array_get(call, 2)));
    json_decref(root);
    TEST_ASSERT_NULL(pav_quota_request(NULL));
}

void test_quota_pick_prefers_mail(void) {
    int64_t used = -1;
    const char *mail =
        "{\"methodResponses\":[[\"Quota/get\",{\"list\":["
        "{\"resourceType\":\"count\",\"used\":3,\"types\":[\"Mail\"]},"
        "{\"resourceType\":\"octets\",\"used\":99,\"types\":[\"Calendar\"]},"
        "{\"resourceType\":\"octets\",\"used\":50,\"types\":[\"Mail\",\"Calendar\"]}"
        "]},\"q0\"]]}";
    const char *other =
        "{\"methodResponses\":[[\"Quota/get\",{\"list\":["
        "{\"resourceType\":\"octets\",\"used\":99,\"types\":[\"Calendar\"]}"
        "]},\"q0\"]]}";

    TEST_ASSERT_EQUAL_INT(1, pav_quota_pick_octets(mail, &used));
    TEST_ASSERT_EQUAL_INT(50, (int)used);
    used = -1;
    TEST_ASSERT_EQUAL_INT(1, pav_quota_pick_octets(other, &used));
    TEST_ASSERT_EQUAL_INT(99, (int)used);
    TEST_ASSERT_EQUAL_INT(0, pav_quota_pick_octets("{}", &used));
    TEST_ASSERT_EQUAL_INT(0, pav_quota_pick_octets(NULL, &used));
}

void test_usage_parse_classifies_quotas(void) {
    PavUsageParse parsed;
    const char *mixed =
        "{\"methodResponses\":[[\"Quota/get\",{\"list\":[{"
        "\"resourceType\":\"octets\",\"used\":5000,"
        "\"types\":[\"Email\",\"SieveScript\",\"FileNode\",\"CalendarEvent\",\"ContactCard\"]"
        "}]},\"q0\"],[\"Email/query\",{\"total\":12,\"ids\":[\"a\"]},\"m\"]]}";
    const char *single_then_query =
        "{\"methodResponses\":[[\"Quota/get\",{\"list\":["
        "{\"resourceType\":\"count\",\"used\":3,\"types\":[\"Mail\"]},"
        "{\"resourceType\":\"octets\",\"used\":40,\"types\":[\"Email\",\"Mail\"]}"
        "]},\"q0\"],[\"Email/query\",{\"total\":7,\"ids\":[\"a\"]},\"m\"]]}";
    const char *query_then_count =
        "{\"methodResponses\":[[\"Email/query\",{\"total\":7,\"ids\":[\"a\"]},\"m\"],"
        "[\"Quota/get\",{\"list\":[{\"resourceType\":\"count\",\"used\":3,\"types\":[\"Mail\"]}]},\"q0\"]]}";
    const char *split =
        "{\"methodResponses\":[[\"Quota/get\",{\"list\":["
        "{\"resourceType\":\"octets\",\"used\":5000,\"types\":[\"Email\",\"SieveScript\"]},"
        "{\"resourceType\":\"octets\",\"used\":40,\"types\":[\"Email\"]},"
        "{\"resourceType\":\"count\",\"used\":4,\"types\":[\"Calendar\"]}"
        "]},\"q0\"],[\"CalendarEvent/query\",{\"total\":9,\"ids\":[\"e\"]},\"e\"]]}";
    const char *page =
        "{\"methodResponses\":["
        "[\"Email/query\",{\"total\":250,\"ids\":[\"a\",\"b\"]},\"q\"],"
        "[\"Email/get\",{\"list\":[{\"id\":\"a\",\"size\":10},{\"id\":\"b\",\"size\":5}],\"notFound\":[]},\"g\"]]}";
    const char *unsized =
        "{\"methodResponses\":["
        "[\"FileNode/query\",{\"total\":2,\"ids\":[\"a\",\"b\"]},\"q\"],"
        "[\"FileNode/get\",{\"list\":[{\"id\":\"a\",\"size\":10},{\"id\":\"b\"}],\"notFound\":[\"c\"]},\"g\"]]}";

    TEST_ASSERT_EQUAL_INT(-1, pav_usage_parse(NULL, &parsed));
    TEST_ASSERT_EQUAL_INT(-1, pav_usage_parse("", &parsed));
    TEST_ASSERT_EQUAL_INT(0, pav_usage_parse("{}", &parsed));
    TEST_ASSERT_EQUAL_INT(0, parsed.mail.has_count);
    TEST_ASSERT_EQUAL_INT(0, parsed.has_server_bytes);

    TEST_ASSERT_EQUAL_INT(0, pav_usage_parse(mixed, &parsed));
    TEST_ASSERT_EQUAL_INT(1, parsed.has_server_bytes);
    TEST_ASSERT_EQUAL_INT(5000, (int)parsed.server_bytes);
    TEST_ASSERT_EQUAL_INT(0, parsed.mail.has_bytes);
    TEST_ASSERT_EQUAL_INT(1, parsed.mail.has_count);
    TEST_ASSERT_EQUAL_INT(12, (int)parsed.mail.count);
    TEST_ASSERT_EQUAL_INT(1, parsed.mail.count_from_query);

    TEST_ASSERT_EQUAL_INT(0, pav_usage_parse(single_then_query, &parsed));
    TEST_ASSERT_EQUAL_INT(0, parsed.has_server_bytes);
    TEST_ASSERT_EQUAL_INT(1, parsed.mail.has_bytes);
    TEST_ASSERT_EQUAL_INT(40, (int)parsed.mail.bytes);
    TEST_ASSERT_EQUAL_INT(7, (int)parsed.mail.count);

    TEST_ASSERT_EQUAL_INT(0, pav_usage_parse(query_then_count, &parsed));
    TEST_ASSERT_EQUAL_INT(7, (int)parsed.mail.count);
    TEST_ASSERT_EQUAL_INT(1, parsed.mail.count_from_query);
    TEST_ASSERT_EQUAL_INT(0, parsed.mail.has_bytes);

    TEST_ASSERT_EQUAL_INT(0, pav_usage_parse(split, &parsed));
    TEST_ASSERT_EQUAL_INT(1, parsed.has_server_bytes);
    TEST_ASSERT_EQUAL_INT(5000, (int)parsed.server_bytes);
    TEST_ASSERT_EQUAL_INT(40, (int)parsed.mail.bytes);
    TEST_ASSERT_EQUAL_INT(1, parsed.calendars.has_count);
    TEST_ASSERT_EQUAL_INT(9, (int)parsed.calendars.count);

    TEST_ASSERT_EQUAL_INT(0, pav_usage_parse(page, &parsed));
    TEST_ASSERT_EQUAL_INT(1, parsed.page_ok);
    TEST_ASSERT_EQUAL_INT(0, parsed.page_error);
    TEST_ASSERT_EQUAL_INT(250, (int)parsed.page_total);
    TEST_ASSERT_EQUAL_INT(2, parsed.page_ids);
    TEST_ASSERT_EQUAL_INT(15, (int)parsed.page_bytes);
    TEST_ASSERT_EQUAL_INT(2, parsed.page_sized);
    TEST_ASSERT_EQUAL_INT(0, parsed.page_unsized);
    TEST_ASSERT_EQUAL_INT(0, parsed.page_not_found);

    TEST_ASSERT_EQUAL_INT(0, pav_usage_parse(unsized, &parsed));
    TEST_ASSERT_EQUAL_INT(1, parsed.page_unsized);
    TEST_ASSERT_EQUAL_INT(1, parsed.page_not_found);
    TEST_ASSERT_EQUAL_INT(10, (int)parsed.page_bytes);
    TEST_ASSERT_TRUE(parsed.page_list != parsed.page_ids || parsed.page_unsized > 0);
}

void test_usage_quick_request_matches_capabilities(void) {
    PavUsageAsk ask;
    json_t *root;
    json_t *args;
    json_t *call;
    char *text;

    memset(&ask, 0, sizeof(ask));
    TEST_ASSERT_NULL(pav_usage_quick_request(NULL));
    TEST_ASSERT_NULL(pav_usage_quick_request(&ask));

    ask.quota_id = "q-acc";
    ask.mail_id = "m-acc";
    ask.contacts_id = "c-acc";
    ask.calendars_id = "e-acc";
    ask.files_id = "f-acc";
    ask.files_urn = "urn:apache:jmap:files";
    text = pav_usage_quick_request(&ask);
    TEST_ASSERT_NOT_NULL(text);
    root = json_loads(text, 0, NULL);
    free(text);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(using_has(root, "urn:ietf:params:jmap:core"));
    TEST_ASSERT_TRUE(using_has(root, "urn:ietf:params:jmap:quota"));
    TEST_ASSERT_TRUE(using_has(root, "urn:ietf:params:jmap:mail"));
    TEST_ASSERT_TRUE(using_has(root, "urn:ietf:params:jmap:contacts"));
    TEST_ASSERT_TRUE(using_has(root, "urn:ietf:params:jmap:calendars"));
    TEST_ASSERT_TRUE(using_has(root, "urn:apache:jmap:files"));
    TEST_ASSERT_FALSE(using_has(root, "urn:ietf:params:jmap:filenode"));
    TEST_ASSERT_EQUAL_INT(5, (int)json_array_size(json_object_get(root, "methodCalls")));
    call = method_named(root, "Quota/get");
    TEST_ASSERT_NOT_NULL(call);
    TEST_ASSERT_EQUAL_STRING("q-acc", json_string_value(json_object_get(json_array_get(call, 1), "accountId")));
    TEST_ASSERT_TRUE(json_is_null(json_object_get(json_array_get(call, 1), "ids")));
    call = method_named(root, "Email/query");
    args = json_array_get(call, 1);
    TEST_ASSERT_EQUAL_STRING("m-acc", json_string_value(json_object_get(args, "accountId")));
    TEST_ASSERT_TRUE(json_is_true(json_object_get(args, "calculateTotal")));
    TEST_ASSERT_EQUAL_INT(1, (int)json_integer_value(json_object_get(args, "limit")));
    TEST_ASSERT_NULL(json_object_get(args, "position"));
    TEST_ASSERT_NOT_NULL(method_named(root, "ContactCard/query"));
    TEST_ASSERT_NOT_NULL(method_named(root, "CalendarEvent/query"));
    TEST_ASSERT_NOT_NULL(method_named(root, "FileNode/query"));
    json_decref(root);

    memset(&ask, 0, sizeof(ask));
    ask.mail_id = "only";
    text = pav_usage_quick_request(&ask);
    root = json_loads(text, 0, NULL);
    free(text);
    TEST_ASSERT_EQUAL_INT(1, (int)json_array_size(json_object_get(root, "methodCalls")));
    TEST_ASSERT_NOT_NULL(method_named(root, "Email/query"));
    TEST_ASSERT_NULL(method_named(root, "Quota/get"));
    TEST_ASSERT_FALSE(using_has(root, "urn:ietf:params:jmap:quota"));
    json_decref(root);
}

void test_usage_measure_request_uses_result_reference(void) {
    json_t *root;
    json_t *call;
    json_t *args;
    json_t *ref;
    char *text;

    TEST_ASSERT_NULL(pav_usage_measure_request(NULL, "mail", NULL, 0));
    TEST_ASSERT_NULL(pav_usage_measure_request("acc", "contacts", NULL, 0));
    TEST_ASSERT_NULL(pav_usage_measure_request("acc", "mail", NULL, -1));

    text = pav_usage_measure_request("acc", "mail", NULL, 100);
    TEST_ASSERT_NOT_NULL(text);
    root = json_loads(text, 0, NULL);
    free(text);
    TEST_ASSERT_TRUE(using_has(root, "urn:ietf:params:jmap:mail"));
    call = method_named(root, "Email/query");
    args = json_array_get(call, 1);
    TEST_ASSERT_EQUAL_INT(100, (int)json_integer_value(json_object_get(args, "limit")));
    TEST_ASSERT_EQUAL_INT(100, (int)json_integer_value(json_object_get(args, "position")));
    TEST_ASSERT_TRUE(json_is_true(json_object_get(args, "calculateTotal")));
    call = method_named(root, "Email/get");
    args = json_array_get(call, 1);
    ref = json_object_get(args, "#ids");
    TEST_ASSERT_EQUAL_STRING("q", json_string_value(json_object_get(ref, "resultOf")));
    TEST_ASSERT_EQUAL_STRING("Email/query", json_string_value(json_object_get(ref, "name")));
    TEST_ASSERT_EQUAL_STRING("/ids", json_string_value(json_object_get(ref, "path")));
    TEST_ASSERT_EQUAL_STRING("size", json_string_value(json_array_get(json_object_get(args, "properties"), 0)));
    TEST_ASSERT_EQUAL_INT(4, (int)json_array_size(json_object_get(args, "properties")));
    TEST_ASSERT_EQUAL_STRING("textBody", json_string_value(json_array_get(json_object_get(args, "properties"), 1)));
    TEST_ASSERT_EQUAL_STRING("htmlBody", json_string_value(json_array_get(json_object_get(args, "properties"), 2)));
    TEST_ASSERT_EQUAL_STRING("attachments", json_string_value(json_array_get(json_object_get(args, "properties"), 3)));
    TEST_ASSERT_EQUAL_STRING("size", json_string_value(json_array_get(json_object_get(args, "bodyProperties"), 0)));
    json_decref(root);

    text = pav_usage_measure_request("acc", "files", "urn:apache:jmap:files", 0);
    root = json_loads(text, 0, NULL);
    free(text);
    TEST_ASSERT_TRUE(using_has(root, "urn:apache:jmap:files"));
    TEST_ASSERT_FALSE(using_has(root, "urn:ietf:params:jmap:filenode"));
    call = method_named(root, "FileNode/get");
    ref = json_object_get(json_array_get(call, 1), "#ids");
    TEST_ASSERT_EQUAL_STRING("FileNode/query", json_string_value(json_object_get(ref, "name")));
    json_decref(root);
}

void test_usage_measure_batch_packs_windows(void) {
    int pos[2] = {0, 500};
    int many[9] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
    json_t *root;
    json_t *calls;
    json_t *call;
    json_t *args;
    json_t *ref;
    char *text;

    TEST_ASSERT_NULL(pav_usage_measure_batch("acc", "mail", NULL, 500, NULL, 2));
    TEST_ASSERT_NULL(pav_usage_measure_batch("acc", "mail", NULL, 0, pos, 1));
    TEST_ASSERT_NULL(pav_usage_measure_batch("acc", "mail", NULL, 500, pos, 0));
    TEST_ASSERT_NULL(pav_usage_measure_batch("acc", "mail", NULL, 500, many, 9));

    text = pav_usage_measure_batch("acc", "mail", NULL, 500, pos, 2);
    TEST_ASSERT_NOT_NULL(text);
    root = json_loads(text, 0, NULL);
    free(text);
    calls = json_object_get(root, "methodCalls");
    TEST_ASSERT_EQUAL_INT(4, (int)json_array_size(calls));
    call = json_array_get(calls, 0);
    args = json_array_get(call, 1);
    TEST_ASSERT_EQUAL_STRING("Email/query", json_string_value(json_array_get(call, 0)));
    TEST_ASSERT_EQUAL_STRING("q0", json_string_value(json_array_get(call, 2)));
    TEST_ASSERT_EQUAL_INT(500, (int)json_integer_value(json_object_get(args, "limit")));
    TEST_ASSERT_EQUAL_INT(0, (int)json_integer_value(json_object_get(args, "position")));
    call = json_array_get(calls, 1);
    args = json_array_get(call, 1);
    ref = json_object_get(args, "#ids");
    TEST_ASSERT_EQUAL_STRING("g0", json_string_value(json_array_get(call, 2)));
    TEST_ASSERT_EQUAL_STRING("q0", json_string_value(json_object_get(ref, "resultOf")));
    call = json_array_get(calls, 2);
    args = json_array_get(call, 1);
    TEST_ASSERT_EQUAL_STRING("q1", json_string_value(json_array_get(call, 2)));
    TEST_ASSERT_EQUAL_INT(500, (int)json_integer_value(json_object_get(args, "position")));
    call = json_array_get(calls, 3);
    ref = json_object_get(json_array_get(call, 1), "#ids");
    TEST_ASSERT_EQUAL_STRING("q1", json_string_value(json_object_get(ref, "resultOf")));
    json_decref(root);
}

void test_usage_parse_batch_sums_windows(void) {
    const char *ok_body =
        "{\"methodResponses\":["
        "[\"Email/query\",{\"ids\":[\"a\",\"b\"],\"total\":4},\"q0\"],"
        "[\"Email/get\",{\"list\":["
        "{\"id\":\"a\",\"size\":10,\"textBody\":[{\"size\":3}],\"htmlBody\":[{\"size\":4}],\"attachments\":[{\"size\":2}]},"
        "{\"id\":\"b\",\"size\":5,\"textBody\":[{\"size\":1}],\"htmlBody\":[],\"attachments\":[]}"
        "],\"notFound\":[]},\"g0\"],"
        "[\"Email/query\",{\"ids\":[\"c\",\"d\"]},\"q1\"],"
        "[\"Email/get\",{\"list\":["
        "{\"id\":\"c\",\"size\":1,\"textBody\":[],\"htmlBody\":[{\"size\":1}],\"attachments\":[{\"size\":0}]},"
        "{\"id\":\"d\",\"size\":2,\"textBody\":[{\"size\":2}],\"htmlBody\":[{\"size\":2}],\"attachments\":[]}"
        "],\"notFound\":[]},\"g1\"]"
        "]}";
    const char *same_id =
        "{\"methodResponses\":["
        "[\"Email/query\",{\"ids\":[\"a\",\"b\"],\"total\":4},\"q0\"],"
        "[\"Email/get\",{\"list\":["
        "{\"id\":\"a\",\"size\":10,\"textBody\":[{\"size\":1}],\"htmlBody\":[],\"attachments\":[]},"
        "{\"id\":\"b\",\"size\":5,\"textBody\":[],\"htmlBody\":[],\"attachments\":[]}"
        "],\"notFound\":[]},\"g0\"],"
        "[\"Email/query\",{\"ids\":[\"a\",\"d\"]},\"q1\"],"
        "[\"Email/get\",{\"list\":["
        "{\"id\":\"a\",\"size\":1,\"textBody\":[],\"htmlBody\":[],\"attachments\":[]},"
        "{\"id\":\"d\",\"size\":2,\"textBody\":[],\"htmlBody\":[],\"attachments\":[]}"
        "],\"notFound\":[]},\"g1\"]"
        "]}";
    const char *missing_part =
        "{\"methodResponses\":["
        "[\"Email/query\",{\"ids\":[\"a\"],\"total\":1},\"q0\"],"
        "[\"Email/get\",{\"list\":[{\"id\":\"a\",\"size\":10,\"htmlBody\":[{\"size\":4}],\"attachments\":[]}],"
        "\"notFound\":[]},\"g0\"]"
        "]}";
    const char *files_body =
        "{\"methodResponses\":["
        "[\"FileNode/query\",{\"ids\":[\"f\"],\"total\":1},\"q0\"],"
        "[\"FileNode/get\",{\"list\":[{\"id\":\"f\",\"size\":9}],\"notFound\":[]},\"g0\"]"
        "]}";
    PavUsageBatch batch;

    TEST_ASSERT_EQUAL_INT(-1, pav_usage_parse_batch(NULL, "mail", &batch));
    TEST_ASSERT_EQUAL_INT(-1, pav_usage_parse_batch("{}", "contacts", &batch));
    TEST_ASSERT_EQUAL_INT(0, pav_usage_parse_batch(ok_body, "mail", &batch));
    TEST_ASSERT_EQUAL_INT(1, batch.ok);
    TEST_ASSERT_EQUAL_INT(2, batch.windows);
    TEST_ASSERT_EQUAL_INT(4, batch.ids);
    TEST_ASSERT_EQUAL_INT(18, (int)batch.bytes);
    TEST_ASSERT_EQUAL_INT(13, (int)batch.body_bytes);
    TEST_ASSERT_EQUAL_INT(2, (int)batch.attachment_bytes);
    TEST_ASSERT_EQUAL_INT(2, (int)batch.attachment_count);
    TEST_ASSERT_EQUAL_INT(1, batch.has_total);
    TEST_ASSERT_EQUAL_INT(4, (int)batch.total);
    TEST_ASSERT_EQUAL_INT(2, batch.last_ids);
    TEST_ASSERT_EQUAL_STRING("a", batch.first_id);

    TEST_ASSERT_EQUAL_INT(0, pav_usage_parse_batch(same_id, "mail", &batch));
    TEST_ASSERT_EQUAL_INT(0, batch.ok);
    TEST_ASSERT_EQUAL_INT(0, pav_usage_parse_batch(missing_part, "mail", &batch));
    TEST_ASSERT_EQUAL_INT(0, batch.ok);
    TEST_ASSERT_EQUAL_INT(0, pav_usage_parse_batch(files_body, "files", &batch));
    TEST_ASSERT_EQUAL_INT(1, batch.ok);
    TEST_ASSERT_EQUAL_INT(9, (int)batch.bytes);
    TEST_ASSERT_EQUAL_INT(0, (int)batch.body_bytes);
    TEST_ASSERT_EQUAL_INT(0, (int)batch.attachment_count);
}

int pav_posix_volume_free(const char *path, int64_t *out);

void test_volume_free_reads_a_directory(void) {
    int64_t bytes = 0;

    TEST_ASSERT_EQUAL_INT(-1, pav_posix_volume_free(NULL, &bytes));
    TEST_ASSERT_EQUAL_INT(-1, pav_posix_volume_free("", &bytes));
    TEST_ASSERT_EQUAL_INT(-1, pav_posix_volume_free("/no/such/pav-volume", &bytes));
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
#ifndef _WIN32
    TEST_ASSERT_EQUAL_INT(0, pav_posix_volume_free(g_cfg_dir, &bytes));
    TEST_ASSERT_TRUE(bytes > 0);
#else
    if (pav_posix_volume_free(g_cfg_dir, &bytes) == 0) {
        TEST_ASSERT_TRUE(bytes > 0);
    }
#endif
}

void test_cache_usage_sums_local_files(void) {
    char body_a[256];
    char body_b[256];
    char sql[2048];
    json_t *root;
    json_t *data;
    json_t *row;
    int wrote;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, pav_path_join(body_a, sizeof(body_a), g_cfg_dir, "body-a"));
    TEST_ASSERT_EQUAL_INT(0, pav_path_join(body_b, sizeof(body_b), g_cfg_dir, "body-b"));
    write_private_text(body_a, "hello");
    write_private_text(body_b, "abcd");
    TEST_ASSERT_EQUAL_INT(0, pav_blob_write("acct-a/ra", "same-blob", "blob-bytes", 10));
    wrote = snprintf(sql, sizeof(sql),
                     "INSERT INTO account_state (account_id, session_json) VALUES "
                     "('acct-a', '{\"capabilities\":{}}'), ('acct-b', NULL);"
                     "INSERT INTO remote_account (id, account_id, jmap_account_id) VALUES "
                     "('acct-a/ra', 'acct-a', 'j1'), ('acct-b/ra', 'acct-b', 'j2');"
                     "INSERT INTO email (rowid, ra_id, id, blob_id, body_state) VALUES "
                     "(1, 'acct-a/ra', 'one', 'same-blob', 'fetched'),"
                     "(2, 'acct-b/ra', 'two', NULL, 'fetched');"
                     "INSERT INTO body_cache (email_rowid, text_plain_path) VALUES (1, '%s'), (2, '%s');"
                     "INSERT INTO attachment (email_rowid, part_id, blob_id) VALUES (1, '1', 'same-blob');",
                     body_a, body_b);
    TEST_ASSERT_TRUE(wrote > 0 && (size_t)wrote < sizeof(sql));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, pav_db_call(exec_sql_job, sql), g_sql_err);

    root = call_reply("cache.usage", "{}");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    data = json_object_get(root, "data");
    TEST_ASSERT_EQUAL_INT(9, (int)json_integer_value(json_object_get(data, "bodiesBytes")));
    TEST_ASSERT_EQUAL_INT(10, (int)json_integer_value(json_object_get(data, "blobsBytes")));
    TEST_ASSERT_TRUE(json_is_null(json_object_get(data, "volumeFreeBytes")));
    row = usage_row(data, "acct-a");
    TEST_ASSERT_NOT_NULL(row);
    TEST_ASSERT_EQUAL_INT(5, (int)json_integer_value(json_object_get(row, "bodiesBytes")));
    TEST_ASSERT_EQUAL_INT(10, (int)json_integer_value(json_object_get(row, "blobsBytes")));
    TEST_ASSERT_TRUE(json_is_null(json_object_get(row, "serverBytes")));
    assert_blank_figure(row, "mail");
    assert_blank_figure(row, "contacts");
    assert_blank_figure(row, "calendars");
    assert_blank_figure(row, "files");
    TEST_ASSERT_TRUE(json_is_null(json_object_get(data, "sizesCheckedAt")));
    row = usage_row(data, "acct-b");
    TEST_ASSERT_NOT_NULL(row);
    TEST_ASSERT_EQUAL_INT(4, (int)json_integer_value(json_object_get(row, "bodiesBytes")));
    TEST_ASSERT_EQUAL_INT(0, (int)json_integer_value(json_object_get(row, "blobsBytes")));
    json_decref(root);

    pav_platform_fake_set_volume_free(4096);
    root = call_reply("cache.usage", "{}");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    TEST_ASSERT_EQUAL_INT(4096, (int)json_integer_value(json_object_get(json_object_get(root, "data"), "volumeFreeBytes")));
    json_decref(root);

    root = call_reply("cache.usage", "{\"measure\":true}");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    data = json_object_get(root, "data");
    row = usage_row(data, "acct-a");
    assert_blank_figure(row, "mail");
    TEST_ASSERT_TRUE(json_is_null(json_object_get(data, "sizesCheckedAt")));
    json_decref(root);

    root = call_reply("cache.usage", "{\"measure\":1}");
    assert_call_error(root, "bad_args", "measure");
    json_decref(root);

    root = call_reply("cache.usage", "{\"nope\":1}");
    assert_call_error(root, "bad_args", "nope");
    json_decref(root);

    root = call_reply("cache.usage", "{\"cancel\":true}");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    TEST_ASSERT_TRUE(json_is_true(json_object_get(json_object_get(root, "data"), "cancelled")));
    json_decref(root);

    root = call_reply("cache.usage", "{\"cancel\":1}");
    assert_call_error(root, "bad_args", "cancel");
    json_decref(root);

    root = call_reply("cache.usage", "{\"measure\":true,\"cancel\":true}");
    assert_call_error(root, "bad_args", "cancel");
    json_decref(root);
}

void test_cache_usage_keeps_measured_sizes(void) {
    json_t *root;
    json_t *data;
    json_t *row;
    json_t *mail;
    const char *sql =
        "UPDATE account_state SET session_json = "
        "'{\"capabilities\":{\"urn:ietf:params:jmap:mail\":{}},"
        "\"primaryAccounts\":{\"urn:ietf:params:jmap:mail\":\"j1\"},"
        "\"accounts\":{\"j1\":{\"isPersonal\":true}}}' "
        "WHERE account_id = 'acct-a';"
        "INSERT OR REPLACE INTO kv (key, value) VALUES ('limits.server', "
        "'{\"checkedAt\":1710000000000,\"accounts\":{"
        "\"acct-a\":{\"mailBytes\":42,\"mailBodiesBytes\":11,\"mailAttachmentsBytes\":30,"
        "\"mailAttachmentsCount\":3,\"filesBytes\":7},"
        "\"acct-imap\":{\"mailBytes\":99}}}');";

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    root = call_reply("account.add",
                      "{\"kind\":\"jmap\",\"sessionUrl\":\"https://example.com/jmap\","
                      "\"username\":\"a@example.com\",\"secret\":\"x\",\"id\":\"acct-a\"}");
    assert_ok(root);
    json_decref(root);
    root = call_reply("account.add",
                      "{\"kind\":\"imap\",\"sessionUrl\":\"https://example.com/jmap\","
                      "\"username\":\"imap@example.com\",\"secret\":\"x\",\"id\":\"acct-imap\"}");
    assert_ok(root);
    json_decref(root);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, pav_db_call(exec_sql_job, (void *)sql), g_sql_err);

    root = call_reply("cache.usage", "{}");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    data = json_object_get(root, "data");
    TEST_ASSERT_TRUE(json_integer_value(json_object_get(data, "sizesCheckedAt")) == 1710000000000LL);
    row = usage_row(data, "acct-a");
    mail = json_object_get(row, "mail");
    TEST_ASSERT_TRUE(json_is_null(json_object_get(mail, "count")));
    TEST_ASSERT_EQUAL_INT(42, (int)json_integer_value(json_object_get(mail, "bytes")));
    TEST_ASSERT_EQUAL_INT(11, (int)json_integer_value(json_object_get(mail, "bodiesBytes")));
    TEST_ASSERT_EQUAL_INT(30, (int)json_integer_value(json_object_get(mail, "attachmentsBytes")));
    TEST_ASSERT_EQUAL_INT(3, (int)json_integer_value(json_object_get(mail, "attachmentsCount")));
    TEST_ASSERT_TRUE(json_is_null(json_object_get(json_object_get(row, "files"), "bytes")));
    TEST_ASSERT_TRUE(json_is_null(json_object_get(row, "serverBytes")));
    row = usage_row(data, "acct-imap");
    assert_blank_figure(row, "mail");
    TEST_ASSERT_TRUE(json_is_null(json_object_get(json_object_get(row, "mail"), "bodiesBytes")));
    TEST_ASSERT_TRUE(json_is_null(json_object_get(json_object_get(row, "mail"), "attachmentsBytes")));
    TEST_ASSERT_TRUE(json_is_null(json_object_get(json_object_get(row, "mail"), "attachmentsCount")));
    assert_blank_figure(row, "files");
    json_decref(root);

    root = call_reply("cache.usage", "{\"measure\":true}");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    data = json_object_get(root, "data");
    TEST_ASSERT_TRUE(json_integer_value(json_object_get(data, "sizesCheckedAt")) == 1710000000000LL);
    row = usage_row(data, "acct-a");
    TEST_ASSERT_EQUAL_INT(42, (int)json_integer_value(json_object_get(json_object_get(row, "mail"), "bytes")));
    json_decref(root);
}

void test_runtime_dir_removed_on_stop(void) {
    const PavPlatform *platform = pav_platform();
    char runtime[4096];
    char open_dir[4096];
    char child[4096];
    struct stat st;

    TEST_ASSERT_NOT_NULL(platform);
    TEST_ASSERT_NOT_NULL(platform->dir);
    TEST_ASSERT_NOT_NULL(platform->write_private);
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, platform->dir(PAV_DIR_RUNTIME, runtime, sizeof(runtime)));
    TEST_ASSERT_EQUAL_INT(0, pav_path_join(open_dir, sizeof(open_dir), runtime, "open"));
    TEST_ASSERT_EQUAL_INT(0, stat(open_dir, &st));
    TEST_ASSERT_TRUE(S_ISDIR(st.st_mode));
    ASSERT_MODE(0700, st);
    TEST_ASSERT_EQUAL_INT(0, pav_path_join(child, sizeof(child), open_dir, "note"));
    TEST_ASSERT_EQUAL_INT(0, platform->write_private(child, "x", 1));
    TEST_ASSERT_TRUE(pav_exists(child));
    pavillion_stop();
    TEST_ASSERT_FALSE(pav_exists(open_dir));
    TEST_ASSERT_FALSE(pav_exists(child));
}

void test_prefs_cache_round_trip(void) {
    json_t *root;
    json_t *cache;
    char dir[160];
    char path[320];
    char *text;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    root = call_reply("prefs.get", "{}");
    cache = json_object_get(json_object_get(root, "data"), "cache");
    TEST_ASSERT_TRUE(json_is_object(cache));
    TEST_ASSERT_EQUAL_INT(2048, (int)json_integer_value(json_object_get(cache, "bodiesMb")));
    TEST_ASSERT_EQUAL_INT(4096, (int)json_integer_value(json_object_get(cache, "blobsMb")));
    json_decref(root);

    root = call_reply("prefs.set", "{\"cache\":{\"bodiesMb\":100}}");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    cache = json_object_get(json_object_get(root, "data"), "cache");
    TEST_ASSERT_EQUAL_INT(100, (int)json_integer_value(json_object_get(cache, "bodiesMb")));
    TEST_ASSERT_EQUAL_INT(4096, (int)json_integer_value(json_object_get(cache, "blobsMb")));
    json_decref(root);

    root = call_reply("prefs.set", "{\"cache\":{\"blobsMb\":50}}");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    cache = json_object_get(json_object_get(root, "data"), "cache");
    TEST_ASSERT_EQUAL_INT(100, (int)json_integer_value(json_object_get(cache, "bodiesMb")));
    TEST_ASSERT_EQUAL_INT(50, (int)json_integer_value(json_object_get(cache, "blobsMb")));
    json_decref(root);

    root = call_reply("prefs.set", "{\"cache\":{\"bodiesMb\":0}}");
    assert_call_error(root, "bad_args", "out of range");
    json_decref(root);
    root = call_reply("prefs.set", "{\"cache\":{\"bodiesMb\":\"no\"}}");
    assert_call_error(root, "bad_args", "integer");
    json_decref(root);
    root = call_reply("prefs.set", "{\"cache\":{}}");
    assert_call_error(root, "bad_args", "empty");
    json_decref(root);
    root = call_reply("prefs.set", "{\"cache\":{\"nope\":1}}");
    assert_call_error(root, "bad_args", "cache.nope");
    json_decref(root);
    root = call_reply("prefs.set", "{\"cache\":1}");
    assert_call_error(root, "bad_args", "object");
    json_decref(root);

    TEST_ASSERT_EQUAL_INT(0, pav_path_join(dir, sizeof(dir), g_cfg_dir, "profiles"));
    TEST_ASSERT_EQUAL_INT(0, pav_path_join(path, sizeof(path), dir, "default.json"));
    text = slurp(path);
    TEST_ASSERT_NOT_NULL(strstr(text, "\"bodiesMb\": 100"));
    TEST_ASSERT_NOT_NULL(strstr(text, "\"blobsMb\": 50"));
    free(text);
}

void test_db_bind_never_concatenates(void) {
    char path[160];
    sqlite3 *db = NULL;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    snprintf(path, sizeof(path), "%s/mail.sqlite", g_cfg_dir);
    TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL));
    TEST_ASSERT_EQUAL_INT(0, pav_db_exec(db, 0, "CREATE TABLE kv(key TEXT PRIMARY KEY, value TEXT)"));
    TEST_ASSERT_EQUAL_INT(-1, pav_db_exec(db, 1, "INSERT INTO kv(key,value) VALUES('%s','x')", "pwn"));
    TEST_ASSERT_EQUAL_INT(0, pav_db_exec(db, 0, "INSERT INTO kv(key,value) VALUES('ok','1')"));
    sqlite3_close(db);
    TEST_ASSERT_EQUAL_INT(0, scalar_int(path, "SELECT count(*) FROM kv WHERE key='pwn'"));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path, "SELECT count(*) FROM kv WHERE key='ok'"));
}

static int fk_job(void *ctx, sqlite3 *db) {
    sqlite3_stmt *stmt = NULL;
    int *on = ctx;

    if (pav_db_prepare(db, "fkpragma", "PRAGMA foreign_keys", &stmt) != 0) {
        return -1;
    }
    if (pav_db_step(stmt) != SQLITE_ROW) {
        return -1;
    }
    *on = sqlite3_column_int(stmt, 0);
    if (sqlite3_exec(db, "INSERT INTO identity(ra_id, id, email) VALUES('missing','i','a@b')", NULL, NULL, NULL) ==
        SQLITE_OK) {
        return -1;
    }
    return 0;
}

static int log_has(const char *needle) {
    json_t *out = NULL;
    json_t *lines;
    size_t i;
    int found = 0;

    if (pav_logring_read(0, 10000, PAV_DEBUG, NULL, &out) != 0 || out == NULL) {
        return 0;
    }
    lines = json_object_get(out, "lines");
    if (!json_is_array(lines)) {
        json_decref(out);
        return 0;
    }
    for (i = 0; i < json_array_size(lines); i++) {
        const char *text = json_string_value(json_object_get(json_array_get(lines, i), "text"));
        if (text != NULL && strstr(text, needle) != NULL) {
            found = 1;
            break;
        }
    }
    json_decref(out);
    return found;
}

void test_secret_roundtrip(void) {
    char *secret = NULL;

    pav_platform_fake_set_locked(0);
    TEST_ASSERT_EQUAL_INT(0, pav_secret_set("pavillion", "default/a", "Pavillion 001 a", "fixture-secret"));
    TEST_ASSERT_EQUAL_INT(0, pav_secret_get("pavillion", "default/a", &secret));
    TEST_ASSERT_EQUAL_STRING("fixture-secret", secret);
    pav_secret_free(secret);
    TEST_ASSERT_EQUAL_INT(0, pav_secret_delete("pavillion", "default/a"));
}

void test_secret_missing(void) {
    char *secret = NULL;

    pav_platform_fake_set_locked(0);
    TEST_ASSERT_EQUAL_INT(PAV_SECRET_MISSING, pav_secret_get("pavillion", "default/missing", &secret));
    TEST_ASSERT_NULL(secret);
}

void test_secret_locked_reports_locked(void) {
    char *secret = NULL;

    pav_platform_fake_set_locked(1);
    TEST_ASSERT_EQUAL_INT(PAV_SECRET_LOCKED, pav_secret_get("pavillion", "default/a", &secret));
    TEST_ASSERT_NULL(secret);
    TEST_ASSERT_EQUAL_INT(PAV_SECRET_LOCKED,
                          pav_secret_set("pavillion", "default/a", "label", "fixture-secret"));
    pav_platform_fake_set_locked(0);
    TEST_ASSERT_EQUAL_INT(PAV_SECRET_MISSING, pav_secret_get("pavillion", "default/a", &secret));
}

void test_secret_never_logged(void) {
    char *secret = NULL;

    pav_logring_reset();
    pav_log_client_rate_reset();
    pav_platform_fake_set_locked(0);
    TEST_ASSERT_EQUAL_INT(0, pav_secret_set("pavillion", "default/a", "label", "fixture-secret"));
    TEST_ASSERT_EQUAL_INT(0, pav_secret_get("pavillion", "default/a", &secret));
    pav_secret_free(secret);
    TEST_ASSERT_EQUAL_INT(0, pav_secret_delete("pavillion", "default/a"));
    TEST_ASSERT_EQUAL_INT(PAV_SECRET_MISSING, pav_secret_get("pavillion", "default/a", &secret));
    TEST_ASSERT_FALSE(log_has("fixture-secret"));
    TEST_ASSERT_TRUE(log_has("secret missing"));
}

void test_auth_basic_header(void) {
    const PavAuth *auth;
    char err[256];
    PavAccount account = {.username = "alice", .auth = "basic"};
    PavReq req = {.headers = NULL};
    PavSecret secret = {.val = "p@ssw0rd", .len = 0};
    json_t *header;
    const char *value;

    pav_platform_fake_set_locked(0);
    err[0] = '\0';
    auth = pav_auth_for(&account, err, sizeof(err));
    TEST_ASSERT_NOT_NULL(auth);
    TEST_ASSERT_EQUAL_STRING("basic", auth->method);
    TEST_ASSERT_EQUAL_INT(0, auth->apply(&account, &req, &secret));
    TEST_ASSERT_NOT_NULL(req.headers);
    header = json_object_get(req.headers, "Authorization");
    TEST_ASSERT_NOT_NULL(header);
    TEST_ASSERT_TRUE(json_is_string(header));
    value = json_string_value(header);
    TEST_ASSERT_EQUAL_STRING("Basic YWxpY2U6cEBzc3cwcmQ=", value);
    json_decref(req.headers);
}

void test_auth_unknown_method_rejected(void) {
    char err[256];
    PavAccount account = {.username = "alice", .auth = "kerberos"};

    err[0] = '\0';
    TEST_ASSERT_NULL(pav_auth_for(&account, err, sizeof(err)));
    TEST_ASSERT_TRUE(err[0] != '\0');
    TEST_ASSERT_NOT_NULL(strstr(err, "kerberos"));
}

void test_auth_missing_method_rejected(void) {
    char err[256];
    PavAccount account = {.username = "alice", .auth = NULL};

    err[0] = '\0';
    TEST_ASSERT_NULL(pav_auth_for(&account, err, sizeof(err)));
    TEST_ASSERT_TRUE(err[0] != '\0');
}

void test_auth_basic_on_unauthorized_fails(void) {
    const PavAuth *auth;
    char err[256];
    PavAccount account = {.username = "alice", .auth = "basic"};

    err[0] = '\0';
    auth = pav_auth_for(&account, err, sizeof(err));
    TEST_ASSERT_NOT_NULL(auth);
    TEST_ASSERT_EQUAL_INT(-1, auth->on_unauthorized(&account));
}

void test_foreign_keys_on(void) {
    char path[160];
    int on = 0;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    snprintf(path, sizeof(path), "%s/mail.sqlite", g_cfg_dir);
    open_v1(path);
    TEST_ASSERT_EQUAL_INT(0, pav_db_call(fk_job, &on));
    TEST_ASSERT_EQUAL_INT(1, on);
    close_test_store();
}

static void assert_ok(json_t *root) {
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    TEST_ASSERT_NULL(json_object_get(root, "error"));
}

void test_account_list_empty(void) {
    json_t *root = NULL;
    json_t *data;
    json_t *accounts;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    root = call_reply("account.list", "{}");
    assert_ok(root);
    data = json_object_get(root, "data");
    TEST_ASSERT_NOT_NULL(data);
    accounts = json_object_get(data, "accounts");
    TEST_ASSERT_NOT_NULL(accounts);
    TEST_ASSERT_TRUE(json_is_array(accounts));
    TEST_ASSERT_EQUAL_INT(0, json_array_size(accounts));
    json_decref(root);
    pavillion_stop();
}

void test_account_list_has_no_secret(void) {
    json_t *root = NULL;
    char *text;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    root = call_reply("account.add",
        "{\"kind\":\"jmap\",\"sessionUrl\":\"https://example.com/jmap\","
        "\"username\":\"alice\",\"secret\":\"p@ssw0rd\",\"id\":\"mail\"}");
    assert_ok(root);
    json_decref(root);
    root = call_reply("account.list", "{}");
    assert_ok(root);
    TEST_ASSERT_EQUAL_INT(1, json_array_size(
        json_object_get(json_object_get(root, "data"), "accounts")));
    text = json_dumps(root, JSON_COMPACT);
    TEST_ASSERT_NULL(strstr(text, "p@ssw0rd"));
    TEST_ASSERT_NULL(strstr(text, "\"secret\""));
    free(text);
    json_decref(root);
    pavillion_stop();
}

void test_account_add_assigns_next_number(void) {
    json_t *root = NULL;
    json_t *acct;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    pav_platform_fake_set_locked(0);
    root = call_reply("account.add",
        "{\"kind\":\"jmap\",\"sessionUrl\":\"https://example.com/jmap\","
        "\"username\":\"alice\",\"secret\":\"p@ssw0rd\",\"id\":\"mail\"}");
    assert_ok(root);
    acct = json_object_get(root, "data");
    TEST_ASSERT_NOT_NULL(acct);
    TEST_ASSERT_EQUAL_STRING("001", json_string_value(json_object_get(acct, "number")));
    TEST_ASSERT_EQUAL_STRING("mail", json_string_value(json_object_get(acct, "id")));
    TEST_ASSERT_EQUAL_STRING("jmap", json_string_value(json_object_get(acct, "kind")));
    TEST_ASSERT_EQUAL_STRING("https://example.com/jmap",
        json_string_value(json_object_get(acct, "sessionUrl")));
    TEST_ASSERT_EQUAL_STRING("alice",
        json_string_value(json_object_get(acct, "username")));
    TEST_ASSERT_EQUAL_STRING("basic",
        json_string_value(json_object_get(acct, "auth")));
    json_decref(root);
    pavillion_stop();
}

void test_account_rejects_http_url(void) {
    json_t *root = NULL;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    pav_platform_fake_set_locked(0);
    root = call_reply("account.add",
        "{\"kind\":\"jmap\",\"sessionUrl\":\"http://insecure.com/jmap\","
        "\"username\":\"bob\",\"secret\":\"s3cret\",\"id\":\"sec\"}");
    assert_call_error(root, "bad_args", "https");
    json_decref(root);
    pavillion_stop();
}

void test_account_numbers_never_reused(void) {
    json_t *root = NULL;
    json_t *acct;
    json_t *list_root;
    json_t *accounts;
    const char *num1;
    const char *num2;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    pav_platform_fake_set_locked(0);
    root = call_reply("account.add",
        "{\"kind\":\"jmap\",\"sessionUrl\":\"https://a.com/jmap\","
        "\"username\":\"u\",\"secret\":\"s\",\"id\":\"a\"}");
    assert_ok(root);
    json_decref(root);
    root = call_reply("account.add",
        "{\"kind\":\"imap\",\"sessionUrl\":\"https://b.com/imap\","
        "\"username\":\"u2\",\"secret\":\"s2\",\"id\":\"b\"}");
    assert_ok(root);
    json_decref(root);
    root = call_reply("account.list", "{}");
    accounts = json_object_get(json_object_get(root, "data"), "accounts");
    TEST_ASSERT_EQUAL_INT(2, json_array_size(accounts));
    num1 = json_string_value(
        json_object_get(json_array_get(accounts, 0), "number"));
    num2 = json_string_value(
        json_object_get(json_array_get(accounts, 1), "number"));
    TEST_ASSERT_EQUAL_STRING("001", num1);
    TEST_ASSERT_EQUAL_STRING("002", num2);
    json_decref(root);
    root = call_reply("account.remove", "{\"id\":\"a\"}");
    assert_ok(root);
    json_decref(root);
    root = call_reply("account.add",
        "{\"kind\":\"imap\",\"sessionUrl\":\"https://c.com/imap\","
        "\"username\":\"u3\",\"secret\":\"s3\",\"id\":\"c\"}");
    assert_ok(root);
    json_decref(root);
    list_root = call_reply("account.list", "{}");
    accounts = json_object_get(json_object_get(list_root, "data"), "accounts");
    for (size_t i = 0; i < json_array_size(accounts); i++) {
        acct = json_array_get(accounts, i);
        const char *id = json_string_value(json_object_get(acct, "id"));
        if (strcmp(id, "c") == 0) {
            const char *num = json_string_value(json_object_get(acct, "number"));
            TEST_ASSERT_EQUAL_STRING("003", num);
        }
    }
    json_decref(list_root);
    pavillion_stop();
}

void test_account_add_rolls_back_on_profile_write_failure(void) {
    json_t *root = NULL;
    char *secret_val = NULL;
    char db_path[160];

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    pav_platform_fake_set_locked(0);
    pav_platform_fake_abort_next_write();
    root = call_reply("account.add",
        "{\"kind\":\"jmap\",\"sessionUrl\":\"https://example.com/jmap\","
        "\"username\":\"alice\",\"secret\":\"p@ssw0rd\",\"id\":\"fail\"}");
    TEST_ASSERT_TRUE(json_is_false(json_object_get(root, "ok")));
    json_decref(root);
    root = call_reply("account.list", "{}");
    assert_ok(root);
    TEST_ASSERT_EQUAL_INT(0,
        json_array_size(json_object_get(json_object_get(root, "data"), "accounts")));
    json_decref(root);
    snprintf(db_path, sizeof(db_path), "%s/default.sqlite", g_cfg_dir);
    TEST_ASSERT_EQUAL_INT(0,
        scalar_int(db_path, "SELECT count(*) FROM account_state WHERE account_id='fail'"));
    TEST_ASSERT_EQUAL_INT(PAV_SECRET_MISSING,
        pav_secret_get("pavillion", "default/fail", &secret_val));
    TEST_ASSERT_NULL(secret_val);
    pavillion_stop();
}

static int exec_sql(const char *path, const char *sql);

void test_account_state_restored_on_boot(void) {
    json_t *root = NULL;
    json_t *accounts;
    char db_path[160];

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    pav_platform_fake_set_locked(0);
    root = call_reply("account.add",
        "{\"kind\":\"jmap\",\"sessionUrl\":\"https://example.com/jmap\","
        "\"username\":\"alice\",\"secret\":\"p@ssw0rd\",\"id\":\"wipe\"}");
    assert_ok(root);
    json_decref(root);
    snprintf(db_path, sizeof(db_path), "%s/default.sqlite", g_cfg_dir);
    TEST_ASSERT_EQUAL_INT(1,
        scalar_int(db_path, "SELECT count(*) FROM account_state WHERE account_id='wipe'"));
    pavillion_stop();
    TEST_ASSERT_EQUAL_INT(1,
        exec_sql(db_path, "DELETE FROM account_state WHERE account_id='wipe'"));
    TEST_ASSERT_EQUAL_INT(0,
        scalar_int(db_path, "SELECT count(*) FROM account_state WHERE account_id='wipe'"));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    TEST_ASSERT_EQUAL_INT(1,
        scalar_int(db_path, "SELECT count(*) FROM account_state WHERE account_id='wipe'"));
    root = call_reply("account.list", "{}");
    assert_ok(root);
    accounts = json_object_get(json_object_get(root, "data"), "accounts");
    TEST_ASSERT_EQUAL_INT(1, (int)json_array_size(accounts));
    json_decref(root);
    root = call_reply("account.remove", "{\"id\":\"wipe\"}");
    assert_ok(root);
    json_decref(root);
    pavillion_stop();
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    TEST_ASSERT_EQUAL_INT(0,
        scalar_int(db_path, "SELECT count(*) FROM account_state WHERE account_id='wipe'"));
    root = call_reply("account.list", "{}");
    assert_ok(root);
    accounts = json_object_get(json_object_get(root, "data"), "accounts");
    TEST_ASSERT_EQUAL_INT(0, (int)json_array_size(accounts));
    json_decref(root);
    pavillion_stop();
}

void test_account_remove_purges(void) {
    json_t *root = NULL;
    char db_path[160];

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    pav_platform_fake_set_locked(0);
    root = call_reply("account.add",
        "{\"kind\":\"jmap\",\"sessionUrl\":\"https://example.com/jmap\","
        "\"username\":\"alice\",\"secret\":\"p@ssw0rd\",\"id\":\"rm\"}");
    assert_ok(root);
    json_decref(root);
    snprintf(db_path, sizeof(db_path), "%s/default.sqlite", g_cfg_dir);
    TEST_ASSERT_EQUAL_INT(1,
        scalar_int(db_path, "SELECT count(*) FROM account_state WHERE account_id='rm'"));
    root = call_reply("account.remove", "{\"id\":\"rm\"}");
    assert_ok(root);
    json_decref(root);
    TEST_ASSERT_EQUAL_INT(0,
        scalar_int(db_path, "SELECT count(*) FROM account_state WHERE account_id='rm'"));
    pavillion_stop();
}

void test_account_remove_not_found(void) {
    json_t *root = NULL;
    json_t *error;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    root = call_reply("account.remove", "{\"id\":\"nope\"}");
    TEST_ASSERT_TRUE(json_is_false(json_object_get(root, "ok")));
    error = json_object_get(root, "error");
    TEST_ASSERT_NOT_NULL(error);
    TEST_ASSERT_EQUAL_STRING("not_found",
        json_string_value(json_object_get(error, "code")));
    json_decref(root);
    pavillion_stop();
}

void test_account_update_patch(void) {
    json_t *root = NULL;
    json_t *acct;
    json_t *accounts;
    json_t *found = NULL;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    pav_platform_fake_set_locked(0);
    root = call_reply("account.add",
        "{\"kind\":\"jmap\",\"sessionUrl\":\"https://example.com/jmap\","
        "\"username\":\"alice\",\"secret\":\"p@ssw0rd\",\"id\":\"upd\"}");
    assert_ok(root);
    json_decref(root);
    root = call_reply("account.update",
        "{\"id\":\"upd\",\"patch\":{\"name\":\"UPDATED\",\"color\":\"#ff0000\"}}");
    assert_ok(root);
    json_decref(root);
    root = call_reply("account.list", "{}");
    assert_ok(root);
    accounts = json_object_get(json_object_get(root, "data"), "accounts");
    for (size_t i = 0; i < json_array_size(accounts); i++) {
        acct = json_array_get(accounts, i);
        if (strcmp(json_string_value(json_object_get(acct, "id")), "upd") == 0) {
            found = acct;
            break;
        }
    }
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_STRING("UPDATED",
        json_string_value(json_object_get(found, "name")));
    TEST_ASSERT_EQUAL_STRING("#ff0000",
        json_string_value(json_object_get(found, "color")));
    json_decref(root);
    pavillion_stop();
}

void test_account_ca_file_round_trip(void) {
    json_t *root = NULL;
    json_t *data;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    pav_platform_fake_set_locked(0);

    root = call_reply("account.add",
        "{\"kind\":\"jmap\",\"sessionUrl\":\"https://example.com/jmap\","
        "\"username\":\"alice\",\"secret\":\"pw\",\"id\":\"ca\",\"caFile\":7}");
    assert_call_error(root, "bad_args", "caFile");
    json_decref(root);
    root = call_reply("account.add",
        "{\"kind\":\"jmap\",\"sessionUrl\":\"https://example.com/jmap\","
        "\"username\":\"alice\",\"secret\":\"pw\",\"id\":\"ca\",\"caFile\":\"rel/ca.crt\"}");
    assert_call_error(root, "bad_args", "caFile");
    json_decref(root);

    root = call_reply("account.add",
        "{\"kind\":\"jmap\",\"sessionUrl\":\"https://example.com/jmap\","
        "\"username\":\"alice\",\"secret\":\"pw\",\"id\":\"ca\",\"caFile\":\"/tmp/a.crt\"}");
    assert_ok(root);
    data = json_object_get(root, "data");
    TEST_ASSERT_EQUAL_STRING("/tmp/a.crt", json_string_value(json_object_get(data, "caFile")));
    json_decref(root);

    root = call_reply("account.update", "{\"id\":\"ca\",\"patch\":{\"caFile\":false}}");
    assert_call_error(root, "bad_args", "caFile");
    json_decref(root);

    root = call_reply("account.update", "{\"id\":\"ca\",\"patch\":{\"caFile\":\"/tmp/b.crt\"}}");
    assert_ok(root);
    json_decref(root);
    root = call_reply("account.list", "{}");
    assert_ok(root);
    data = json_array_get(json_object_get(json_object_get(root, "data"), "accounts"), 0);
    TEST_ASSERT_EQUAL_STRING("/tmp/b.crt", json_string_value(json_object_get(data, "caFile")));
    json_decref(root);

    root = call_reply("account.update", "{\"id\":\"ca\",\"patch\":{\"caFile\":\"\"}}");
    assert_ok(root);
    TEST_ASSERT_NULL(json_object_get(json_object_get(root, "data"), "caFile"));
    json_decref(root);
    pavillion_stop();
}

void test_account_set_secret(void) {
    char *secret = NULL;
    json_t *root;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    pav_platform_fake_set_locked(0);
    root = call_reply("account.add",
        "{\"kind\":\"jmap\",\"sessionUrl\":\"https://example.com/jmap\","
        "\"username\":\"alice\",\"secret\":\"old-val\",\"id\":\"ss\"}");
    assert_ok(root);
    json_decref(root);
    root = call_reply("account.set_secret",
        "{\"id\":\"ss\",\"secret\":\"new-val\"}");
    assert_ok(root);
    json_decref(root);
    TEST_ASSERT_EQUAL_INT(0,
        pav_secret_get("pavillion", "default/ss", &secret));
    TEST_ASSERT_EQUAL_STRING("new-val", secret);
    pav_secret_free(secret);
    pavillion_stop();
}

static pthread_mutex_t g_net_test_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_net_test_cv = PTHREAD_COND_INITIALIZER;

struct NetDone {
    int hit;
    int status_code;
    size_t body_len;
};
typedef struct NetDone NetDone;

static void net_done_cb(PavReq *req, void *user) {
    struct NetDone *done = user;
    pthread_mutex_lock(&g_net_test_mu);
    if (done) {
        done->hit = 1;
        done->status_code = req->status_code;
        done->body_len = req->body_out_len;
    }
    pthread_cond_broadcast(&g_net_test_cv);
    pthread_mutex_unlock(&g_net_test_mu);
}

static void net_done_init(struct NetDone *done) {
    done->hit = 0;
    done->status_code = 0;
    done->body_len = 0;
}

static int net_wait_done(struct NetDone *done, int timeout_sec) {
    struct timespec ts;
    int rc;
    int ok = 0;

    pthread_mutex_lock(&g_net_test_mu);
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_sec;
    while (!done->hit) {
        rc = pthread_cond_timedwait(&g_net_test_cv, &g_net_test_mu, &ts);
        if (rc != 0) break;
    }
    ok = done->hit;
    pthread_mutex_unlock(&g_net_test_mu);
    return ok;
}

/* A failed wait leaves the request in flight, and its callback would write into
   this test's stack after Unity unwinds it. Stop the net thread first. */
#define NET_CHECK(cond) \
    do { \
        if (!(cond)) { \
            pav_net_stop(); \
            TEST_FAIL_MESSAGE("net check failed: " #cond); \
        } \
    } while (0)

void test_net_start_and_stop(void) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");

    TEST_ASSERT_NOT_NULL_MESSAGE(url, "PAV_FIXTURE_URL must be set by make test");
    TEST_ASSERT_NOT_NULL_MESSAGE(ca, "PAV_FIXTURE_CA must be set by make test");

    TEST_ASSERT_EQUAL_INT(0, pav_net_start());
    pav_net_stop();
}

void test_net_server_for_matches_url(void) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    PavNetServer *s1;
    PavNetServer *s2;
    PavNetServer *s3;
    char same_url[512];

    TEST_ASSERT_NOT_NULL_MESSAGE(url, "PAV_FIXTURE_URL must be set by make test");
    TEST_ASSERT_NOT_NULL_MESSAGE(ca, "PAV_FIXTURE_CA must be set by make test");

    TEST_ASSERT_EQUAL_INT(0, pav_net_start());
    s1 = pav_net_server_for(url);
    TEST_ASSERT_NOT_NULL(s1);

    s2 = pav_net_server_for(url);
    TEST_ASSERT_EQUAL_PTR(s1, s2);

    /* A different port is a different server. */
    snprintf(same_url, sizeof(same_url), "%s/v1", url);
    s3 = pav_net_server_for(same_url);
    TEST_ASSERT_EQUAL_PTR(s1, s3);

    pav_net_stop();
}

void test_net_simple_get(void) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    PavNetServer *s;
    PavReq req;
    NetDone done;
    char full[512];

    TEST_ASSERT_NOT_NULL_MESSAGE(url, "PAV_FIXTURE_URL must be set by make test");
    TEST_ASSERT_NOT_NULL_MESSAGE(ca, "PAV_FIXTURE_CA must be set by make test");

    TEST_ASSERT_EQUAL_INT(0, pav_net_start());
    s = pav_net_server_for(url);
    TEST_ASSERT_NOT_NULL(s);

    net_done_init(&done);
    memset(&req, 0, sizeof(req));
    req.method = "GET";
    req.ca_file = ca;
    req.on_done = net_done_cb;
    req.user = &done;
    snprintf(full, sizeof(full), "%s/jmap/session", url);
    req.url = full;
    TEST_ASSERT_EQUAL_INT(0, pav_net_request(&req));

    NET_CHECK(net_wait_done(&done, 5));
    TEST_ASSERT_EQUAL_INT(1, done.hit);
    TEST_ASSERT_EQUAL_INT(200, req.status_code);
    TEST_ASSERT_NOT_NULL(req.body_out);
    TEST_ASSERT_NOT_NULL(strstr(req.body_out, "\"apiUrl\""));
    pav_net_free(req.body_out);
    pav_net_stop();
}

void test_net_post_with_body(void) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    PavNetServer *s;
    PavReq req;
    NetDone done;
    char full[512];
    const char *body = "{\"methodCalls\":[[\"Mailbox/get\",{},\"c1\"]]}";

    TEST_ASSERT_NOT_NULL_MESSAGE(url, "PAV_FIXTURE_URL must be set by make test");
    TEST_ASSERT_NOT_NULL_MESSAGE(ca, "PAV_FIXTURE_CA must be set by make test");

    TEST_ASSERT_EQUAL_INT(0, pav_net_start());
    s = pav_net_server_for(url);
    TEST_ASSERT_NOT_NULL(s);

    snprintf(full, sizeof(full), "%s/jmap/api", url);
    net_done_init(&done);
    memset(&req, 0, sizeof(req));
    req.method = "POST";
    req.url = full;
    req.ca_file = ca;
    req.body = body;
    req.body_len = strlen(body);
    req.on_done = net_done_cb;
    req.user = &done;
    TEST_ASSERT_EQUAL_INT(0, pav_net_request(&req));

    NET_CHECK(net_wait_done(&done, 5));
    TEST_ASSERT_EQUAL_INT(1, done.hit);
    TEST_ASSERT_EQUAL_INT(200, req.status_code);
    TEST_ASSERT_NOT_NULL(req.body_out);
    TEST_ASSERT_NOT_NULL(strstr(req.body_out, "methodResponses"));
    pav_net_free(req.body_out);
    pav_net_stop();
}

void test_net_ca_file_rejected_for_wrong_ca(void) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    PavReq req;
    NetDone done;
    char full[512];
    char bogus_ca[] = "/tmp/pav-bogus-ca-XXXXXX";
    int fd;

    TEST_ASSERT_NOT_NULL_MESSAGE(url, "PAV_FIXTURE_URL must be set by make test");
    TEST_ASSERT_NOT_NULL_MESSAGE(ca, "PAV_FIXTURE_CA must be set by make test");

    /* Create a bogus CA file: real PEM, but signed with a different key
       so it does NOT match the fixture server's certificate. */
    fd = mkstemp(bogus_ca);
    TEST_ASSERT_TRUE(fd >= 0);
    {
        const char *pem =
            "-----BEGIN CERTIFICATE-----\n"
            "MIIBhTCCASugAwIBAgIRAOt3j5p2lJ5lJ5lJ5lJ5lJcEMA0GCSqGSIb3DQEB\n"
            "CwMAAaMjBBIwFAYDVQQDEw1CYWRDYW5vbHlzIDAxggEiMA0GCSqGSIb3DQEB\n"
            "BQADggEPADCCAQoCghEBANi3j5p2lJ5lJ5lJ5lJ5lJcEMA0GCSqGSIb3DQEB\n"
            "BQHAP9G1j5p2lJ5lJ5lJ5lJ5lJcEMA0GCSqGSIb3DQEB\n"
            "-----END CERTIFICATE-----\n";
        int len = (int)strlen(pem);
        TEST_ASSERT_EQUAL_INT(len, (int)fwrite(pem, 1, len, fdopen(fd, "w")));
    }
    close(fd);

    TEST_ASSERT_EQUAL_INT(0, pav_net_start());
    net_done_init(&done);
    memset(&req, 0, sizeof(req));
    req.method = "GET";
    snprintf(full, sizeof(full), "%s/jmap/session", url);
    req.url = full;
    req.ca_file = bogus_ca;
    req.on_done = net_done_cb;
    req.user = &done;
    TEST_ASSERT_EQUAL_INT(0, pav_net_request(&req));

    /* With a wrong CA, curl should fail SSL verification. No retry (SSL errors are fatal). */
    NET_CHECK(net_wait_done(&done, 15));
    TEST_ASSERT_EQUAL_INT(1, done.hit);
    TEST_ASSERT_EQUAL_INT(0, req.status_code);
    TEST_ASSERT_EQUAL_INT(0, req.retry_count);

    pav_net_free(req.body_out);
    unlink(bogus_ca);
    pav_net_stop();
}

void test_net_streaming_download_to_file(void) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    PavNetServer *s;
    PavReq req;
    NetDone done;
    char full[512];
    char dl_path[256];
    struct stat st;

    TEST_ASSERT_NOT_NULL_MESSAGE(url, "PAV_FIXTURE_URL must be set by make test");
    TEST_ASSERT_NOT_NULL_MESSAGE(ca, "PAV_FIXTURE_CA must be set by make test");

    TEST_ASSERT_EQUAL_INT(0, pav_net_start());
    s = pav_net_server_for(url);
    TEST_ASSERT_NOT_NULL(s);

    snprintf(full, sizeof(full), "%s/jmap/session", url);
    snprintf(dl_path, sizeof(dl_path), "/tmp/pav-dl-test-XXXXXX");
    int fd = mkstemp(dl_path);
    TEST_ASSERT_TRUE(fd >= 0);
    close(fd);

    net_done_init(&done);
    memset(&req, 0, sizeof(req));
    req.method = "GET";
    req.url = full;
    req.ca_file = ca;
    req.download_path = dl_path;
    req.on_done = net_done_cb;
    req.user = &done;
    TEST_ASSERT_EQUAL_INT(0, pav_net_request(&req));

    NET_CHECK(net_wait_done(&done, 5));
    TEST_ASSERT_EQUAL_INT(1, done.hit);
    TEST_ASSERT_EQUAL_INT(200, req.status_code);
    TEST_ASSERT_TRUE(req.body_out_len > 0);
    TEST_ASSERT_EQUAL_INT(0, stat(dl_path, &st));
    TEST_ASSERT_EQUAL_INT((int)req.body_out_len, (int)st.st_size);
    pav_net_free(req.body_out);
    unlink(dl_path);
    pav_net_stop();
}

void test_net_streaming_upload_from_file(void) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    PavNetServer *s;
    PavReq req;
    NetDone done;
    char full[512];
    char up_path[256];
    const char *payload = "hello-upload";

    TEST_ASSERT_NOT_NULL_MESSAGE(url, "PAV_FIXTURE_URL must be set by make test");
    TEST_ASSERT_NOT_NULL_MESSAGE(ca, "PAV_FIXTURE_CA must be set by make test");

    snprintf(up_path, sizeof(up_path), "/tmp/pav-up-test-XXXXXX");
    int fd = mkstemp(up_path);
    TEST_ASSERT_TRUE(fd >= 0);
    TEST_ASSERT_EQUAL_INT(6, (int)fwrite(payload, 1, 6, fdopen(fd, "w")));
    close(fd);

    TEST_ASSERT_EQUAL_INT(0, pav_net_start());
    s = pav_net_server_for(url);
    TEST_ASSERT_NOT_NULL(s);

    snprintf(full, sizeof(full), "%s/jmap/upload", url);
    net_done_init(&done);
    memset(&req, 0, sizeof(req));
    req.method = "POST";
    req.url = full;
    req.ca_file = ca;
    req.upload_path = up_path;
    req.on_done = net_done_cb;
    req.user = &done;
    TEST_ASSERT_EQUAL_INT(0, pav_net_request(&req));

    NET_CHECK(net_wait_done(&done, 5));
    TEST_ASSERT_EQUAL_INT(1, done.hit);
    TEST_ASSERT_EQUAL_INT(200, req.status_code);
    TEST_ASSERT_NOT_NULL(req.body_out);
    TEST_ASSERT_NOT_NULL(strstr(req.body_out, "blobId"));
    pav_net_free(req.body_out);
    unlink(up_path);
    pav_net_stop();
}

void test_net_per_account_cap(void) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    PavNetServer *s;
    PavReq reqs[PAV_NET_PER_ACCOUNT_CAP + 1];
    NetDone dones[PAV_NET_PER_ACCOUNT_CAP + 1];
    char full[512];
    int i;
    int done_count = 0;

    TEST_ASSERT_NOT_NULL_MESSAGE(url, "PAV_FIXTURE_URL must be set by make test");
    TEST_ASSERT_NOT_NULL_MESSAGE(ca, "PAV_FIXTURE_CA must be set by make test");

    TEST_ASSERT_EQUAL_INT(0, pav_net_start());
    s = pav_net_server_for(url);
    TEST_ASSERT_NOT_NULL(s);

    snprintf(full, sizeof(full), "%s/jmap/session?slow=1", url);

    for (i = 0; i <= PAV_NET_PER_ACCOUNT_CAP; i++) {
        net_done_init(&dones[i]);
        memset(&reqs[i], 0, sizeof(reqs[i]));
        reqs[i].method = "GET";
        reqs[i].url = full;
        reqs[i].ca_file = ca;
        reqs[i].account_id = "acct-cap";
        reqs[i].on_done = net_done_cb;
        reqs[i].user = &dones[i];
        TEST_ASSERT_EQUAL_INT(0, pav_net_request(&reqs[i]));
    }

    /* Only PAV_NET_PER_ACCOUNT_CAP should complete within the slow response
       window; the extra request must still be queued.      */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 4;
    for (i = 0; i <= PAV_NET_PER_ACCOUNT_CAP; i++) {
        pthread_mutex_lock(&g_net_test_mu);
        while (!dones[i].hit) {
            if (pthread_cond_timedwait(&g_net_test_cv, &g_net_test_mu, &ts) != 0) break;
        }
        if (dones[i].hit) done_count++;
        pthread_mutex_unlock(&g_net_test_mu);
    }
    NET_CHECK(done_count == PAV_NET_PER_ACCOUNT_CAP);

    /* Wait for the last one to complete. */
    NET_CHECK(net_wait_done(&dones[PAV_NET_PER_ACCOUNT_CAP], 8));
    TEST_ASSERT_EQUAL_INT(1, dones[PAV_NET_PER_ACCOUNT_CAP].hit);

    for (i = 0; i <= PAV_NET_PER_ACCOUNT_CAP; i++) {
        pav_net_free(reqs[i].body_out);
    }
    pav_net_stop();
}

void test_net_per_server_cap(void) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    PavNetServer *s;
    PavReq reqs[PAV_NET_PER_SERVER_CAP + 1];
    NetDone dones[PAV_NET_PER_SERVER_CAP + 1];
    char full[512];
    int i;
    int done_count = 0;

    TEST_ASSERT_NOT_NULL_MESSAGE(url, "PAV_FIXTURE_URL must be set by make test");
    TEST_ASSERT_NOT_NULL_MESSAGE(ca, "PAV_FIXTURE_CA must be set by make test");

    TEST_ASSERT_EQUAL_INT(0, pav_net_start());
    s = pav_net_server_for(url);
    TEST_ASSERT_NOT_NULL(s);

    snprintf(full, sizeof(full), "%s/jmap/session?slow=1", url);

    for (i = 0; i <= PAV_NET_PER_SERVER_CAP; i++) {
        net_done_init(&dones[i]);
        memset(&reqs[i], 0, sizeof(reqs[i]));
        reqs[i].method = "GET";
        reqs[i].url = full;
        reqs[i].ca_file = ca;
        reqs[i].account_id = NULL;
        reqs[i].on_done = net_done_cb;
        reqs[i].user = &dones[i];
        TEST_ASSERT_EQUAL_INT(0, pav_net_request(&reqs[i]));
    }

    struct timespec ts;
     clock_gettime(CLOCK_REALTIME, &ts);
     ts.tv_sec += 8;
     for (i = 0; i <= PAV_NET_PER_SERVER_CAP; i++) {
         pthread_mutex_lock(&g_net_test_mu);
         while (!dones[i].hit) {
             if (pthread_cond_timedwait(&g_net_test_cv, &g_net_test_mu, &ts) != 0) break;
         }
         if (dones[i].hit) done_count++;
         pthread_mutex_unlock(&g_net_test_mu);
     }
     NET_CHECK(done_count == PAV_NET_PER_SERVER_CAP);

    NET_CHECK(net_wait_done(&dones[PAV_NET_PER_SERVER_CAP], 8));
    TEST_ASSERT_EQUAL_INT(1, dones[PAV_NET_PER_SERVER_CAP].hit);

    for (i = 0; i <= PAV_NET_PER_SERVER_CAP; i++) {
        pav_net_free(reqs[i].body_out);
    }
    pav_net_stop();
}

void test_net_retry_on_429_with_retry_after(void) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    PavNetServer *s;
    PavReq req;
    NetDone done;
    char full[512];
    int retry_count;

    TEST_ASSERT_NOT_NULL_MESSAGE(url, "PAV_FIXTURE_URL must be set by make test");
    TEST_ASSERT_NOT_NULL_MESSAGE(ca, "PAV_FIXTURE_CA must be set by make test");

    TEST_ASSERT_EQUAL_INT(0, pav_net_start());
    s = pav_net_server_for(url);
    TEST_ASSERT_NOT_NULL(s);

    /* The fixture returns 429 immediately. With retry, the next attempt
       hits the same endpoint without ?error= and succeeds. */
    snprintf(full, sizeof(full), "%s/jmap/session?error=429", url);
    net_done_init(&done);
    memset(&req, 0, sizeof(req));
    req.method = "GET";
    req.url = full;
    req.ca_file = ca;
    req.on_done = net_done_cb;
    req.user = &done;
    TEST_ASSERT_EQUAL_INT(0, pav_net_request(&req));

    NET_CHECK(net_wait_done(&done, 20));
    TEST_ASSERT_EQUAL_INT(1, done.hit);
    /* After retry, the server returns 200 from /jmap/session?error=429
       because the second request goes through without the error param.
       Actually: the URL still has ?error=429 on retry. The fixture will
       keep returning 429. So the final status should be 429 and retry_count > 0. */
    TEST_ASSERT_EQUAL_INT(429, req.status_code);
    retry_count = req.retry_count;
    TEST_ASSERT_TRUE(retry_count > 0);
    pav_net_free(req.body_out);
    pav_net_stop();
}

void test_net_retry_on_503_backoff(void) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    PavNetServer *s;
    PavReq req;
    NetDone done;
    char full[512];

    TEST_ASSERT_NOT_NULL_MESSAGE(url, "PAV_FIXTURE_URL must be set by make test");
    TEST_ASSERT_NOT_NULL_MESSAGE(ca, "PAV_FIXTURE_CA must be set by make test");

    TEST_ASSERT_EQUAL_INT(0, pav_net_start());
    s = pav_net_server_for(url);
    TEST_ASSERT_NOT_NULL(s);

    snprintf(full, sizeof(full), "%s/jmap/session?error=503", url);
    net_done_init(&done);
    memset(&req, 0, sizeof(req));
    req.method = "GET";
    req.url = full;
    req.ca_file = ca;
    req.on_done = net_done_cb;
    req.user = &done;
    TEST_ASSERT_EQUAL_INT(0, pav_net_request(&req));

    NET_CHECK(net_wait_done(&done, 20));
    TEST_ASSERT_EQUAL_INT(1, done.hit);
    /* 503 stays at 503 since the URL keeps returning 503. */
    TEST_ASSERT_EQUAL_INT(503, req.status_code);
    TEST_ASSERT_TRUE(req.retry_count > 0);
    pav_net_free(req.body_out);
    pav_net_stop();
}

void test_net_401_not_retried(void) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    PavNetServer *s;
    PavReq req;
    NetDone done;
    char full[512];

    TEST_ASSERT_NOT_NULL_MESSAGE(url, "PAV_FIXTURE_URL must be set by make test");
    TEST_ASSERT_NOT_NULL_MESSAGE(ca, "PAV_FIXTURE_CA must be set by make test");

    TEST_ASSERT_EQUAL_INT(0, pav_net_start());
    s = pav_net_server_for(url);
    TEST_ASSERT_NOT_NULL(s);

    snprintf(full, sizeof(full), "%s/jmap/session?error=401", url);
    net_done_init(&done);
    memset(&req, 0, sizeof(req));
    req.method = "GET";
    req.url = full;
    req.ca_file = ca;
    req.on_done = net_done_cb;
    req.user = &done;
    TEST_ASSERT_EQUAL_INT(0, pav_net_request(&req));

    NET_CHECK(net_wait_done(&done, 8));
    TEST_ASSERT_EQUAL_INT(1, done.hit);
    TEST_ASSERT_EQUAL_INT(401, req.status_code);
    TEST_ASSERT_EQUAL_INT(0, req.retry_count);
    pav_net_free(req.body_out);
    pav_net_stop();
}

void test_net_release_account_exits_thread(void) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    PavNetServer *s;
    PavReq req;
    NetDone done;
    char full[512];

    TEST_ASSERT_NOT_NULL_MESSAGE(url, "PAV_FIXTURE_URL must be set by make test");
    TEST_ASSERT_NOT_NULL_MESSAGE(ca, "PAV_FIXTURE_CA must be set by make test");

    TEST_ASSERT_EQUAL_INT(0, pav_net_start());
    s = pav_net_server_for(url);
    TEST_ASSERT_NOT_NULL(s);

    snprintf(full, sizeof(full), "%s/jmap/session", url);
    net_done_init(&done);
    memset(&req, 0, sizeof(req));
    req.method = "GET";
    req.url = full;
    req.ca_file = ca;
    req.account_id = "release-acct";
    req.on_done = net_done_cb;
    req.user = &done;
    TEST_ASSERT_EQUAL_INT(0, pav_net_request(&req));

    NET_CHECK(net_wait_done(&done, 5));
    TEST_ASSERT_EQUAL_INT(1, done.hit);
    TEST_ASSERT_EQUAL_INT(200, req.status_code);
    pav_net_free(req.body_out);

    /* Releasing the account should let the server thread exit when idle. */
    TEST_ASSERT_EQUAL_INT(0, pav_net_release_account(s, "release-acct"));
    pav_net_stop();
}

void test_net_overall_cap(void) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    PavNetServer *s;
    PavReq reqs[3];
    NetDone dones[3];
    char full[512];
    int i;
    int done_count = 0;

    TEST_ASSERT_NOT_NULL_MESSAGE(url, "PAV_FIXTURE_URL must be set by make test");
    TEST_ASSERT_NOT_NULL_MESSAGE(ca, "PAV_FIXTURE_CA must be set by make test");

    TEST_ASSERT_EQUAL_INT(0, pav_net_start());
    s = pav_net_server_for(url);
    TEST_ASSERT_NOT_NULL(s);

    snprintf(full, sizeof(full), "%s/jmap/session?slow=1", url);

    /* Set an overall cap of 2, then submit 3 slow requests. */
    pav_net_set_overall_cap(2);

    for (i = 0; i < 3; i++) {
        net_done_init(&dones[i]);
        memset(&reqs[i], 0, sizeof(reqs[i]));
        reqs[i].method = "GET";
        reqs[i].url = full;
        reqs[i].ca_file = ca;
        reqs[i].on_done = net_done_cb;
        reqs[i].user = &dones[i];
        TEST_ASSERT_EQUAL_INT(0, pav_net_request(&reqs[i]));
    }

     struct timespec ts;
     clock_gettime(CLOCK_REALTIME, &ts);
     ts.tv_sec += 4;
     for (i = 0; i < 3; i++) {
         pthread_mutex_lock(&g_net_test_mu);
         while (!dones[i].hit) {
             if (pthread_cond_timedwait(&g_net_test_cv, &g_net_test_mu, &ts) != 0) break;
         }
         if (dones[i].hit) done_count++;
         pthread_mutex_unlock(&g_net_test_mu);
     }
     NET_CHECK(done_count == 2);

    NET_CHECK(net_wait_done(&dones[2], 8));
    for (i = 0; i < 3; i++) {
        pav_net_free(reqs[i].body_out);
    }
    pav_net_set_overall_cap(0);
    pav_net_stop();
}

void test_fixture_server_session(void) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    TEST_ASSERT_NOT_NULL_MESSAGE(url, "PAV_FIXTURE_URL must be set by make test");
    TEST_ASSERT_NOT_NULL_MESSAGE(ca, "PAV_FIXTURE_CA must be set by make test");
    TEST_ASSERT_TRUE_MESSAGE(ca[0] != '\0', "PAV_FIXTURE_CA must not be empty");
    /* The CA file should exist and be readable. */
    FILE *caf = fopen(ca, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(caf, "fixture CA file must exist");
    fclose(caf);
    /* Fetch the session endpoint and verify it is a JMAP session. */
    char cmd[512];
    /* Double quotes: popen runs cmd.exe on Windows, which keeps single quotes. */
    snprintf(cmd, sizeof(cmd), "curl -sk \"%s/jmap/session\"", url);
    FILE *f = popen(cmd, "r");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "cannot run curl");
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    int rc = pclose(f);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, "curl must exit 0");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"apiUrl\""), "session must include apiUrl");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "urn:ietf:params:jmap:mail"),
                                 "session must advertise mail capability");
}

static int net_get_once(const char *url, const char *ca, int expect_status,
                        char **body_out, int timeout_sec) {
    PavNetServer *s;
    PavReq req;
    NetDone done;
    int ok;

    if (pav_net_start() != 0) return -1;
    s = pav_net_server_for(url);
    if (s == NULL) {
        pav_net_stop();
        return -1;
    }
    net_done_init(&done);
    memset(&req, 0, sizeof(req));
    req.method = "GET";
    req.url = url;
    req.ca_file = ca;
    req.on_done = net_done_cb;
    req.user = &done;
    if (pav_net_request(&req) != 0) {
        pav_net_stop();
        return -1;
    }
    ok = net_wait_done(&done, timeout_sec);
    if (!ok) {
        pav_net_stop();
        return -1;
    }
    if (done.status_code != expect_status) {
        if (done.body_len > 0) pav_net_free(req.body_out);
        pav_net_stop();
        return -2;
    }
    if (body_out != NULL && req.body_out != NULL) {
        *body_out = strdup(req.body_out);
    }
    if (req.body_out) pav_net_free(req.body_out);
    pav_net_stop();
    return 0;
}

/* POST one JSON body. Starts and stops the net. The caller must not already hold it. */
static int net_post_once(const char *url, const char *ca, const char *body, char **body_out, int timeout_sec) {
    PavReq req;
    NetDone done;
    json_t *headers;
    int ok;

    if (pav_net_start() != 0) {
        return -1;
    }
    if (pav_net_server_for(url) == NULL) {
        pav_net_stop();
        return -1;
    }
    net_done_init(&done);
    memset(&req, 0, sizeof(req));
    headers = json_pack("{s:s}", "Content-Type", "application/json");
    if (headers == NULL) {
        pav_net_stop();
        return -1;
    }
    req.method = "POST";
    req.url = url;
    req.ca_file = ca;
    req.headers = headers;
    req.body = body;
    req.body_len = strlen(body);
    req.on_done = net_done_cb;
    req.user = &done;
    if (pav_net_request(&req) != 0) {
        json_decref(headers);
        pav_net_stop();
        return -1;
    }
    ok = net_wait_done(&done, timeout_sec);
    json_decref(headers);
    if (!ok || done.status_code != 200) {
        if (req.body_out != NULL) {
            pav_net_free(req.body_out);
        }
        pav_net_stop();
        return -1;
    }
    if (body_out != NULL && req.body_out != NULL) {
        *body_out = strdup(req.body_out);
    }
    if (req.body_out != NULL) {
        pav_net_free(req.body_out);
    }
    pav_net_stop();
    return 0;
}

static int g_mbx_watch;
static int g_mbx_hit;
static char g_mbx_expect[80];
static PavReq g_fix_req;
static NetDone g_fix_done;
static char g_fix_url[512];
static char g_fix_ca[1024];
static char g_fix_body[1024];

static void mbx_event_cb(const char *name, const char *json, void *user) {
    (void)user;
    pthread_mutex_lock(&g_ev_test_mu);
    if (g_mbx_watch && name != NULL && json != NULL && strcmp(name, "mail.mailboxes_changed") == 0 &&
        strstr(json, g_mbx_expect) != NULL) {
        g_mbx_hit = 1;
        pthread_cond_signal(&g_ev_test_cv);
    }
    pthread_mutex_unlock(&g_ev_test_mu);
}

static void mbx_watch_begin(const char *account_id) {
    pthread_mutex_lock(&g_ev_test_mu);
    g_mbx_hit = 0;
    snprintf(g_mbx_expect, sizeof(g_mbx_expect), "\"accountId\":\"%s\"", account_id);
    g_mbx_watch = 1;
    pthread_mutex_unlock(&g_ev_test_mu);
    pav_on_event(mbx_event_cb, NULL);
}

static int mbx_watch_end(void) {
    struct timespec ts;
    int hit;
    int rc;

    pthread_mutex_lock(&g_ev_test_mu);
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 2;
    while (g_mbx_watch && !g_mbx_hit) {
        rc = pthread_cond_timedwait(&g_ev_test_cv, &g_ev_test_mu, &ts);
        if (rc != 0) {
            break;
        }
    }
    g_mbx_watch = 0;
    hit = g_mbx_hit;
    pthread_mutex_unlock(&g_ev_test_mu);
    pav_on_event(NULL, NULL);
    return hit;
}

static json_t *node_by_id(json_t *nodes, const char *id) {
    size_t i;

    TEST_ASSERT_TRUE(json_is_array(nodes));
    for (i = 0; i < json_array_size(nodes); i++) {
        json_t *node = json_array_get(nodes, i);
        const char *got = json_string_value(json_object_get(node, "id"));

        if (got != NULL && strcmp(got, id) == 0) {
            return node;
        }
    }
    return NULL;
}

/* Counts a method name in the fixture trace. The file is repo-root relative. */
static int trace_count(const char *needle) {
    FILE *f;
    long n;
    char *buf;
    const char *p;
    int count = 0;
    size_t len;

    f = fopen("build/fixture-run/mailstate.json", "rb");
    if (f == NULL) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    n = ftell(f);
    if (n < 0 || n > 8 * 1024 * 1024 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    buf = malloc((size_t)n + 1);
    if (buf == NULL) {
        fclose(f);
        return -1;
    }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return -1;
    }
    buf[n] = '\0';
    fclose(f);
    len = strlen(needle);
    for (p = buf; len > 0 && (p = strstr(p, needle)) != NULL; p += len) {
        count++;
    }
    free(buf);
    return count;
}

/* Posts to the fixture while the core holds the net. Does not start or stop it. */
static int fixture_post(const char *url, const char *ca, const char *body) {
    json_t *headers;
    size_t len;
    int hit;

    if (url == NULL || ca == NULL || body == NULL) {
        return -1;
    }
    len = strlen(body);
    if (strlen(url) >= sizeof(g_fix_url) || strlen(ca) >= sizeof(g_fix_ca) || len >= sizeof(g_fix_body)) {
        return -1;
    }
    memcpy(g_fix_url, url, strlen(url) + 1);
    memcpy(g_fix_ca, ca, strlen(ca) + 1);
    memcpy(g_fix_body, body, len + 1);
    net_done_init(&g_fix_done);
    memset(&g_fix_req, 0, sizeof(g_fix_req));
    headers = json_pack("{s:s}", "Content-Type", "application/json");
    if (headers == NULL) {
        return -1;
    }
    g_fix_req.method = "POST";
    g_fix_req.url = g_fix_url;
    g_fix_req.ca_file = g_fix_ca;
    g_fix_req.headers = headers;
    g_fix_req.body = g_fix_body;
    g_fix_req.body_len = len;
    g_fix_req.on_done = net_done_cb;
    g_fix_req.user = &g_fix_done;
    if (pav_net_request(&g_fix_req) != 0) {
        json_decref(headers);
        g_fix_req.headers = NULL;
        return -1;
    }
    hit = net_wait_done(&g_fix_done, 8);
    if (!hit) {
        return -1;
    }
    json_decref(g_fix_req.resp_headers);
    json_decref(headers);
    g_fix_req.resp_headers = NULL;
    g_fix_req.headers = NULL;
    if (g_fix_done.status_code != 200 || g_fix_req.body_out == NULL ||
        strstr(g_fix_req.body_out, "Mailbox/set") == NULL) {
        pav_net_free(g_fix_req.body_out);
        g_fix_req.body_out = NULL;
        return -1;
    }
    pav_net_free(g_fix_req.body_out);
    g_fix_req.body_out = NULL;
    return 0;
}

static void scalar_text(const char *path, const char *sql, char *out, size_t n) {
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;

    if (n > 0) {
        out[0] = '\0';
    }
    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        if (db != NULL) {
            sqlite3_close(db);
        }
        return;
    }
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        const char *text = (const char *)sqlite3_column_text(stmt, 0);

        if (text != NULL && n > 0) {
            snprintf(out, n, "%s", text);
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

static int exec_sql(const char *path, const char *sql) {
    sqlite3 *db = NULL;
    char *errmsg = NULL;
    int rc;
    int changes = -1;

    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK) {
        if (db != NULL) {
            sqlite3_close(db);
        }
        return -1;
    }
    sqlite3_busy_timeout(db, 2000);
    rc = sqlite3_exec(db, sql, NULL, NULL, &errmsg);
    sqlite3_free(errmsg);
    if (rc == SQLITE_OK) {
        changes = sqlite3_changes(db);
    }
    sqlite3_close(db);
    return changes;
}

static void assert_bad_account_id(const char *cmd, const char *args) {
    json_t *root = call_reply(cmd, args);

    assert_call_error(root, "bad_args", "accountId must be a non-empty string");
    json_decref(root);
}

static void sync_slot(const char *id, int with_color, char *path, size_t path_n) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    char session_url[512];

    TEST_ASSERT_NOT_NULL_MESSAGE(url, "PAV_FIXTURE_URL must be set by make test");
    TEST_ASSERT_NOT_NULL_MESSAGE(ca, "PAV_FIXTURE_CA must be set by make test");
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    pav_sync_set_poll_ms(3600000);
    snprintf(session_url, sizeof(session_url), "%s/jmap/session", url);
    if (with_color) {
        add_fixture_account_color(id, session_url, ca, "#336699");
    } else {
        add_fixture_account(id, session_url, ca);
    }
    db_path_for_cfg(path, path_n);
}

void test_sync_mailboxes_initial(void) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    char session_url[512];
    char path[160];
    char got[192];
    json_t *root;
    json_t *idents;
    json_t *andrew;
    json_t *rows;
    json_t *item;
    int waited;
    int hit;

    TEST_ASSERT_NOT_NULL_MESSAGE(url, "PAV_FIXTURE_URL must be set by make test");
    TEST_ASSERT_NOT_NULL_MESSAGE(ca, "PAV_FIXTURE_CA must be set by make test");
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    pav_sync_set_poll_ms(3600000);
    mbx_watch_begin("init");
    snprintf(session_url, sizeof(session_url), "%s/jmap/session", url);
    add_fixture_account("init", session_url, ca);
    db_path_for_cfg(path, sizeof(path));
    waited = wait_count(path,
                        "SELECT count(*) FROM mailbox WHERE ra_id='init/a1' AND id='later' AND name='Later' "
                        "AND parent_id='in' AND unread=1 AND total=3",
                        1, "init", got, sizeof(got));
    hit = mbx_watch_end();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, waited, got);
    TEST_ASSERT_EQUAL_INT(1, hit);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, wait_count(path,
        "SELECT count(*) FROM account_state WHERE account_id='init' AND state='idle'", 1, "init", got, sizeof(got)),
        got);
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path,
        "SELECT count(*) FROM sync_state WHERE ra_id='init/a1' AND type='Mailbox' AND state='mbx-1'"));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path,
        "SELECT count(*) FROM sync_state WHERE ra_id='init/a1' AND type='Identity' AND state='idn-1'"));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path,
        "SELECT count(*) FROM sync_state WHERE ra_id='init/a2' AND type='Mailbox' AND state='mbx-1'"));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path,
        "SELECT count(*) FROM sync_state WHERE ra_id='init/a2' AND type='Identity' AND state='idn-1'"));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path,
        "SELECT count(*) FROM identity WHERE ra_id='init/a1' AND email='andrew@test-a.local' AND may_delete=0"));
    TEST_ASSERT_EQUAL_INT(4, scalar_int(path, "SELECT count(*) FROM mailbox WHERE ra_id='init/a1'"));
    TEST_ASSERT_EQUAL_INT(2, scalar_int(path, "SELECT count(*) FROM mailbox WHERE ra_id='init/a2'"));
    TEST_ASSERT_EQUAL_INT(1, pav_sync_attempt_count("init"));

    root = call_reply("identity.list", "{\"accountId\":\"init\"}");
    assert_ok(root);
    idents = json_object_get(json_object_get(root, "data"), "identities");
    TEST_ASSERT_EQUAL_INT(2, json_array_size(idents));
    TEST_ASSERT_EQUAL_STRING("alice@test-a.local",
                             json_string_value(json_object_get(json_array_get(idents, 0), "email")));
    andrew = json_array_get(idents, 1);
    TEST_ASSERT_EQUAL_STRING("init", json_string_value(json_object_get(andrew, "accountId")));
    TEST_ASSERT_EQUAL_STRING("init/a1", json_string_value(json_object_get(andrew, "raId")));
    TEST_ASSERT_EQUAL_STRING("Andrew", json_string_value(json_object_get(andrew, "name")));
    TEST_ASSERT_EQUAL_STRING("andrew@test-a.local", json_string_value(json_object_get(andrew, "email")));
    TEST_ASSERT_TRUE(json_is_false(json_object_get(andrew, "mayDelete")));
    TEST_ASSERT_TRUE(json_is_null(json_object_get(andrew, "replyTo")));
    TEST_ASSERT_TRUE(json_is_null(json_object_get(andrew, "bcc")));
    json_decref(root);

    rows = activity_rows(1);
    item = find_sync_act(rows, "init");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_EQUAL_STRING("done", json_string_value(json_object_get(item, "state")));
    TEST_ASSERT_EQUAL_STRING("Messages", json_string_value(json_object_get(item, "detail")));
    json_decref(rows);
}

/* Session maxObjectsInGet is 2. The fixture cuts an ids-null get at that cap.
   a1 has four mailboxes, so the walk has to query a second page to store Sent. */
void test_sync_mailboxes_paged(void) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    char session_url[512];
    char path[160];
    char got[320];
    int queries;
    int gets;

    TEST_ASSERT_NOT_NULL_MESSAGE(url, "PAV_FIXTURE_URL must be set by make test");
    TEST_ASSERT_NOT_NULL_MESSAGE(ca, "PAV_FIXTURE_CA must be set by make test");
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    pav_sync_set_poll_ms(3600000);
    queries = trace_count("Mailbox/query");
    gets = trace_count("Mailbox/get");
    TEST_ASSERT_TRUE(queries >= 0);
    TEST_ASSERT_TRUE(gets >= 0);
    snprintf(session_url, sizeof(session_url), "%s/jmap/session?maxObjectsInGet=2", url);
    add_fixture_account("page", session_url, ca);
    db_path_for_cfg(path, sizeof(path));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, wait_count(path,
        "SELECT count(*) FROM mailbox WHERE ra_id='page/a1' AND id='sent' AND name='Sent'",
        1, "page", got, sizeof(got)), got);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, wait_count(path,
        "SELECT count(*) FROM account_state WHERE account_id='page' AND state='idle' "
        "AND session_json LIKE '%\"maxObjectsInGet\":2%'",
        1, "page", got, sizeof(got)), got);
    snprintf(got, sizeof(got), "queries %d->%d gets %d->%d boxes %d", queries, trace_count("Mailbox/query"), gets,
             trace_count("Mailbox/get"), scalar_int(path, "SELECT count(*) FROM mailbox WHERE ra_id='page/a1'"));
    TEST_ASSERT_EQUAL_INT_MESSAGE(4, scalar_int(path, "SELECT count(*) FROM mailbox WHERE ra_id='page/a1'"), got);
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path, "SELECT count(*) FROM mailbox WHERE ra_id='page/a1' AND id='drafts'"));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path,
        "SELECT count(*) FROM mailbox WHERE ra_id='page/a1' AND id='later' AND parent_id='in'"));
    TEST_ASSERT_EQUAL_INT(2, scalar_int(path, "SELECT count(*) FROM mailbox WHERE ra_id='page/a2'"));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path,
        "SELECT count(*) FROM sync_state WHERE ra_id='page/a1' AND type='Mailbox' AND state='mbx-1'"));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path,
        "SELECT count(*) FROM sync_state WHERE ra_id='page/a2' AND type='Mailbox' AND state='mbx-1'"));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path,
        "SELECT count(*) FROM identity WHERE ra_id='page/a1' AND email='andrew@test-a.local'"));
    TEST_ASSERT_TRUE(trace_count("Mailbox/query") >= queries + 2);
    TEST_ASSERT_TRUE(trace_count("Mailbox/get") >= gets + 2);
}

/* The cap is 2 and the state is already saved. Deleting Sent and Drafts
   locally must not stick: the server total is still 4, so the next sync
   walks the list again. */
void test_sync_mailboxes_refill(void) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    char session_url[512];
    char path[160];
    char got[192];
    json_t *root;
    int n;

    TEST_ASSERT_NOT_NULL_MESSAGE(url, "PAV_FIXTURE_URL must be set by make test");
    TEST_ASSERT_NOT_NULL_MESSAGE(ca, "PAV_FIXTURE_CA must be set by make test");
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    pav_sync_set_poll_ms(3600000);
    snprintf(session_url, sizeof(session_url), "%s/jmap/session?maxObjectsInGet=2", url);
    add_fixture_account("refill", session_url, ca);
    db_path_for_cfg(path, sizeof(path));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, wait_count(path,
        "SELECT count(*) FROM mailbox m JOIN account_state a ON a.account_id='refill' "
        "WHERE m.ra_id='refill/a1' AND m.id='sent' AND a.state='idle'",
        1, "refill", got, sizeof(got)), got);
    TEST_ASSERT_EQUAL_INT(4, scalar_int(path, "SELECT count(*) FROM mailbox WHERE ra_id='refill/a1'"));
    TEST_ASSERT_TRUE(exec_sql(path,
        "DELETE FROM mailbox WHERE ra_id='refill/a1' AND id IN ('sent','drafts')") >= 2);
    TEST_ASSERT_EQUAL_INT(2, scalar_int(path, "SELECT count(*) FROM mailbox WHERE ra_id='refill/a1'"));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path,
        "SELECT count(*) FROM sync_state WHERE ra_id='refill/a1' AND type='Mailbox' AND state='mbx-1'"));
    n = pav_sync_attempt_count("refill");
    root = call_reply("sync.now", "{\"accountId\":\"refill\"}");
    assert_ok(root);
    json_decref(root);
    TEST_ASSERT_EQUAL_INT(n + 1, pav_sync_attempt_count("refill"));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, wait_count(path,
        "SELECT count(*) FROM mailbox m JOIN account_state a ON a.account_id='refill' "
        "WHERE m.ra_id='refill/a1' AND m.id IN ('sent','drafts') AND a.state='idle'",
        2, "refill", got, sizeof(got)), got);
    TEST_ASSERT_EQUAL_INT(4, scalar_int(path, "SELECT count(*) FROM mailbox WHERE ra_id='refill/a1'"));
    TEST_ASSERT_EQUAL_INT(2, scalar_int(path, "SELECT count(*) FROM mailbox WHERE ra_id='refill/a2'"));
}

void test_sync_mailboxes_delta_rename(void) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    char path[160];
    char got[192];
    char api[512];
    json_t *root;
    int before;
    int mid;
    int after;
    int n;

    sync_slot("delta", 0, path, sizeof(path));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, wait_count(path,
        "SELECT count(*) FROM mailbox m JOIN account_state a ON a.account_id='delta' "
        "WHERE m.ra_id='delta/a1' AND m.id='later' AND m.name='Later' AND a.state='idle'",
        1, "delta", got, sizeof(got)), got);
    before = trace_count("Mailbox/changes");
    TEST_ASSERT_TRUE(before >= 0);
    snprintf(api, sizeof(api), "%s/jmap/api", url);
    TEST_ASSERT_EQUAL_INT(0, fixture_post(api, ca,
        "{\"using\":[\"urn:ietf:params:jmap:core\",\"urn:ietf:params:jmap:mail\"],"
        "\"methodCalls\":[[\"Mailbox/set\",{\"accountId\":\"a1\",\"update\":{\"later\":{\"name\":\"Soon\"}}},\"c1\"]]}"));
    mid = trace_count("Mailbox/changes");
    TEST_ASSERT_EQUAL_INT(before, mid);
    n = pav_sync_attempt_count("delta");
    root = call_reply("sync.now", "{\"accountId\":\"delta\"}");
    assert_ok(root);
    json_decref(root);
    TEST_ASSERT_EQUAL_INT(n + 1, pav_sync_attempt_count("delta"));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, wait_count(path,
        "SELECT count(*) FROM mailbox m JOIN account_state a ON a.account_id='delta' "
        "WHERE m.ra_id='delta/a1' AND m.id='later' AND m.name='Soon' AND m.parent_id='in' AND m.unread=1 "
        "AND a.state='idle'",
        1, "delta", got, sizeof(got)), got);
    after = trace_count("Mailbox/changes");
    TEST_ASSERT_TRUE(after > before);
    TEST_ASSERT_EQUAL_INT(n + 1, pav_sync_attempt_count("delta"));
}

static int g_msg_watch;
static int g_msg_hits;
static char g_msg_expect[80];
static char g_msg_first[4096];

static int fixture_email_hold(int hold) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    char cmd[1600];
    const char *sink;
    int n;

    if (url == NULL || ca == NULL) {
        return -1;
    }
#ifdef _WIN32
    sink = "NUL";
#else
    sink = "/dev/null";
#endif
    /* cmd.exe does not treat '' as an empty word. `--data ''` makes that
       curl read stdin, and it sits there until --max-time. */
    n = snprintf(cmd, sizeof(cmd),
                 "curl -fsS -X POST --data \"{}\" --max-time 10 --cacert \"%s\" -o %s "
                 "\"%s/jmap/proxy?emailHold=%d\" < %s",
                 ca, sink, url, hold ? 1 : 0, sink);
    if (n < 0 || n >= (int)sizeof(cmd)) {
        return -1;
    }
    return system(cmd) == 0 ? 0 : -1;
}

static void msg_event_cb(const char *name, const char *json, void *user) {
    (void)user;
    pthread_mutex_lock(&g_ev_test_mu);
    if (g_msg_watch && name != NULL && json != NULL && strcmp(name, "mail.messages_changed") == 0 &&
        strstr(json, g_msg_expect) != NULL) {
        if (g_msg_hits == 0) {
            snprintf(g_msg_first, sizeof(g_msg_first), "%s", json);
        }
        g_msg_hits++;
        pthread_cond_signal(&g_ev_test_cv);
    }
    pthread_mutex_unlock(&g_ev_test_mu);
}

static void msg_watch_begin(const char *account_id) {
    pthread_mutex_lock(&g_ev_test_mu);
    g_msg_hits = 0;
    g_msg_first[0] = '\0';
    snprintf(g_msg_expect, sizeof(g_msg_expect), "\"accountId\":\"%s\"", account_id);
    g_msg_watch = 1;
    pthread_mutex_unlock(&g_ev_test_mu);
    pav_on_event(msg_event_cb, NULL);
}

static int msg_watch_end(void) {
    struct timespec ts;
    int hits;
    int rc;

    pthread_mutex_lock(&g_ev_test_mu);
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 2;
    while (g_msg_watch && g_msg_hits == 0) {
        rc = pthread_cond_timedwait(&g_ev_test_cv, &g_ev_test_mu, &ts);
        if (rc != 0) {
            break;
        }
    }
    g_msg_watch = 0;
    hits = g_msg_hits;
    pthread_mutex_unlock(&g_ev_test_mu);
    pav_on_event(NULL, NULL);
    return hits;
}

static void start_email_account(const char *id, const char *query, char *path, size_t path_n) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    char session_url[512];
    int n;

    TEST_ASSERT_NOT_NULL_MESSAGE(url, "PAV_FIXTURE_URL must be set by make test");
    TEST_ASSERT_NOT_NULL_MESSAGE(ca, "PAV_FIXTURE_CA must be set by make test");
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    pav_sync_set_poll_ms(3600000);
    n = snprintf(session_url, sizeof(session_url), "%s/jmap/session%s", url, query != NULL ? query : "");
    TEST_ASSERT_TRUE(n > 0 && n < (int)sizeof(session_url));
    add_fixture_account(id, session_url, ca);
    db_path_for_cfg(path, path_n);
}

static char *load_trace(void) {
    FILE *f;
    long n;
    char *buf;

    f = fopen("build/fixture-run/mailstate.json", "rb");
    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    n = ftell(f);
    if (n < 0 || n > 8 * 1024 * 1024 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = malloc((size_t)n + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static int collect_email_queries(int *pos, int *lim, int cap) {
    char *buf = load_trace();
    const char *p;
    int n = 0;

    if (buf == NULL) {
        return -1;
    }
    p = buf;
    while (n < cap) {
        const char *hit = strstr(p, "Email/query:");
        int at = 0;
        int a;
        int b;

        if (hit == NULL) {
            break;
        }
        hit += strlen("Email/query:");
        if (sscanf(hit, "%d:%d%n", &a, &b, &at) == 2 && at > 0) {
            pos[n] = a;
            lim[n] = b;
            n++;
            p = hit + (size_t)at;
        } else {
            p = hit;
        }
    }
    free(buf);
    return n;
}

static void assert_backfill_trace(void) {
    int pos[160];
    int lim[160];
    int n;
    int at = 0;
    int i;
    static const int want[] = {0, 500, 1000, 1500};
    char why[80];

    n = collect_email_queries(pos, lim, 160);
    TEST_ASSERT_TRUE(n >= 0);
    for (i = 0; i < n && at < 4; i++) {
        if (lim[i] == 500 && pos[i] == want[at]) {
            at++;
        }
    }
    snprintf(why, sizeof(why), "backfill matched %d of 4 pages", at);
    TEST_ASSERT_EQUAL_INT_MESSAGE(4, at, why);
}

static void assert_first_page_trace(void) {
    int pos[160];
    int lim[160];
    int n;
    int i;
    int saw = 0;

    n = collect_email_queries(pos, lim, 160);
    TEST_ASSERT_TRUE(n >= 0);
    for (i = 0; i < n; i++) {
        if (!saw && pos[i] == 0 && lim[i] == 2) {
            saw = 1;
        } else if (saw && pos[i] == 2 && lim[i] == 2) {
            return;
        }
    }
    TEST_FAIL_MESSAGE("Email/query:0:2 then Email/query:2:2 not in the trace");
}

static void assert_limit_trace(void) {
    int pos[160];
    int lim[160];
    int n;
    int i;
    int anchor = -1;
    int saw0 = 0;
    int saw2 = 0;
    int saw4 = 0;
    char why[64];

    n = collect_email_queries(pos, lim, 160);
    TEST_ASSERT_TRUE(n >= 0);
    for (i = 0; i < n; i++) {
        if (lim[i] > 2) {
            anchor = i;
        }
    }
    for (i = anchor + 1; i < n; i++) {
        snprintf(why, sizeof(why), "Email/query:%d:%d after the anchor", pos[i], lim[i]);
        TEST_ASSERT_TRUE_MESSAGE(lim[i] <= 2, why);
        if (pos[i] == 0) {
            saw0 = 1;
        }
        if (pos[i] == 2) {
            saw2 = 1;
        }
        if (pos[i] == 4) {
            saw4 = 1;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(saw0 && saw2 && saw4, "positions 0, 2, and 4 were not all queried");
}

/* 2,000 headers, four pages of 500, newest page first. */
void test_email_backfill_pages(void) {
    char path[160];
    char got[192];
    char sql[192];
    json_t *rows;
    json_t *item;
    json_t *prog;
    int64_t hi;
    int64_t lo;
    int hits;

    (void)fixture_email_hold(0);
    g_email_hold = 0;
    msg_watch_begin("back");
    start_email_account("back", "?emails=2000", path, sizeof(path));
    snprintf(sql, sizeof(sql), "SELECT count(*) FROM email WHERE ra_id='back/a1'");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, wait_count_tries(path, sql, 2000, "back", got, sizeof(got), 1200), got);
    hits = msg_watch_end();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, wait_count(path,
        "SELECT count(*) FROM account_state WHERE account_id='back' AND state='idle'", 1, "back", got, sizeof(got)),
        got);
    TEST_ASSERT_TRUE(hits >= 4);
    TEST_ASSERT_EQUAL_INT(2000, scalar_int(path, "SELECT count(*) FROM email WHERE ra_id='back/a1'"));
    TEST_ASSERT_EQUAL_INT(0, scalar_int(path, "SELECT count(*) FROM email WHERE ra_id='back/a2'"));
    hi = scalar_i64(path, "SELECT received_at FROM email WHERE ra_id='back/a1' AND id='e1999'");
    lo = scalar_i64(path, "SELECT received_at FROM email WHERE ra_id='back/a1' AND id='e0'");
    TEST_ASSERT_TRUE(hi > lo);
    TEST_ASSERT_TRUE(hi != 0 && lo != 0);
    rows = activity_rows(1);
    item = find_sync_act(rows, "back");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_EQUAL_STRING("done", json_string_value(json_object_get(item, "state")));
    TEST_ASSERT_EQUAL_STRING("Messages", json_string_value(json_object_get(item, "detail")));
    prog = json_object_get(item, "progress");
    TEST_ASSERT_TRUE(json_is_object(prog));
    TEST_ASSERT_EQUAL_INT(2000, (int)json_integer_value(json_object_get(prog, "done")));
    TEST_ASSERT_EQUAL_INT(2000, (int)json_integer_value(json_object_get(prog, "total")));
    TEST_ASSERT_EQUAL_STRING("messages", json_string_value(json_object_get(prog, "unit")));
    json_decref(rows);
    assert_backfill_trace();
}

/* First page is the newest headers. A cap of 2 stores e3 before e0. */
void test_email_initial_first_page(void) {
    char path[160];
    char got[192];
    char text[256];
    int64_t hi;
    int64_t lo;
    int hits;

    msg_watch_begin("efirst");
    start_email_account("efirst", "?maxObjectsInGet=2&emails=4", path, sizeof(path));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, wait_count(path,
        "SELECT count(*) FROM email WHERE ra_id='efirst/a1'", 4, "efirst", got, sizeof(got)), got);
    hits = msg_watch_end();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, wait_count(path,
        "SELECT count(*) FROM account_state WHERE account_id='efirst' AND state='idle'", 1, "efirst", got,
        sizeof(got)), got);
    TEST_ASSERT_TRUE(hits >= 1);
    TEST_ASSERT_NOT_NULL(strstr(g_msg_first, "\"e3\""));
    TEST_ASSERT_EQUAL_INT(4, scalar_int(path, "SELECT count(*) FROM email WHERE ra_id='efirst/a1'"));
    TEST_ASSERT_EQUAL_INT(0, scalar_int(path, "SELECT count(*) FROM email WHERE ra_id='efirst/a2'"));
    hi = scalar_i64(path, "SELECT received_at FROM email WHERE ra_id='efirst/a1' AND id='e3'");
    lo = scalar_i64(path, "SELECT received_at FROM email WHERE ra_id='efirst/a1' AND id='e0'");
    TEST_ASSERT_TRUE(hi > lo);
    TEST_ASSERT_TRUE(hi != 0 && lo != 0);
    scalar_text(path, "SELECT subject FROM email WHERE ra_id='efirst/a1' AND id='e3'", text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("Hello 3", text);
    scalar_text(path, "SELECT from_json FROM email WHERE ra_id='efirst/a1' AND id='e3'", text, sizeof(text));
    TEST_ASSERT_NOT_NULL(strstr(text, "ann@example.com"));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path,
        "SELECT count(*) FROM email WHERE ra_id='efirst/a1' AND id='e0' AND seen=1 AND flagged=1"));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path,
        "SELECT count(*) FROM email WHERE ra_id='efirst/a1' AND id='e3' AND seen=1 AND flagged=0"));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path,
        "SELECT count(*) FROM email WHERE ra_id='efirst/a1' AND id='e3' AND body_state='none'"));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path,
        "SELECT count(*) FROM email_mailbox WHERE ra_id='efirst/a1' AND mailbox_id='in' "
        "AND email_rowid=(SELECT rowid FROM email WHERE ra_id='efirst/a1' AND id='e3')"));
    scalar_text(path, "SELECT email_ids FROM thread WHERE ra_id='efirst/a1' AND id='t3'", text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("[\"e3\"]", text);
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path, "SELECT count(*) FROM email_fts WHERE email_fts MATCH '\"Hello 3\"'"));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path,
        "SELECT count(*) FROM sync_state WHERE ra_id='efirst/a1' AND type='Email' AND state='em-1'"));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path,
        "SELECT count(*) FROM sync_state WHERE ra_id='efirst/a2' AND type='Email' AND state='em-1'"));
    assert_first_page_trace();
}

/* Every Email/query after the last oversized page stays within maxObjectsInGet. */
void test_email_batches_respect_max_objects(void) {
    char path[160];
    char got[192];

    start_email_account("elim", "?maxObjectsInGet=2&emails=5", path, sizeof(path));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, wait_count(path,
        "SELECT count(*) FROM email WHERE ra_id='elim/a1'", 5, "elim", got, sizeof(got)), got);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, wait_count(path,
        "SELECT count(*) FROM account_state WHERE account_id='elim' AND state='idle'", 1, "elim", got, sizeof(got)),
        got);
    assert_limit_trace();
}

/* A cancel drops the held page and leaves no Email state. sync.now stores the rest. */
void test_email_sync_cancel_resumes(void) {
    char path[160];
    char got[192];
    char body[128];
    char why[64];
    json_t *rows;
    json_t *item;
    json_t *root;
    const char *act;
    int n;
    int i;
    int ready = 0;

    TEST_ASSERT_EQUAL_INT(0, fixture_email_hold(0));
    TEST_ASSERT_EQUAL_INT(0, fixture_email_hold(1));
    g_email_hold = 1;
    start_email_account("can", "?maxObjectsInGet=2&emails=6", path, sizeof(path));
    for (i = 0; i < 200; i++) {
        int rows_n = scalar_int(path, "SELECT count(*) FROM email WHERE ra_id='can/a1'");
        int syncing = scalar_int(path,
            "SELECT count(*) FROM account_state WHERE account_id='can' AND state='syncing'");

        if (rows_n >= 2 && syncing == 1) {
            ready = 1;
            break;
        }
        usleep(50000);
    }
    TEST_ASSERT_TRUE_MESSAGE(ready, "sync did not pause after the first page");
    rows = activity_rows(0);
    item = find_sync_act(rows, "can");
    TEST_ASSERT_NOT_NULL(item);
    act = json_string_value(json_object_get(item, "id"));
    TEST_ASSERT_NOT_NULL(act);
    snprintf(body, sizeof(body), "{\"id\":\"%s\"}", act);
    json_decref(rows);
    root = call_reply("activity.cancel", body);
    assert_ok(root);
    json_decref(root);
    TEST_ASSERT_EQUAL_INT(0, fixture_email_hold(0));
    g_email_hold = 0;
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, wait_count(path,
        "SELECT count(*) FROM account_state WHERE account_id='can' AND state='idle'", 1, "can", got, sizeof(got)),
        got);
    n = scalar_int(path, "SELECT count(*) FROM email WHERE ra_id='can/a1'");
    snprintf(why, sizeof(why), "count %d", n);
    TEST_ASSERT_TRUE_MESSAGE(n >= 2 && n < 6, why);
    TEST_ASSERT_EQUAL_INT(0, scalar_int(path,
        "SELECT count(*) FROM sync_state WHERE ra_id='can/a1' AND type='Email'"));
    rows = activity_rows(1);
    item = find_sync_act(rows, "can");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_EQUAL_STRING("cancelled", json_string_value(json_object_get(item, "state")));
    json_decref(rows);
    root = call_reply("sync.now", "{\"accountId\":\"can\"}");
    assert_ok(root);
    json_decref(root);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, wait_count_tries(path,
        "SELECT count(*) FROM email WHERE ra_id='can/a1'", 6, "can", got, sizeof(got), 400), got);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, wait_count(path,
        "SELECT count(*) FROM account_state WHERE account_id='can' AND state='idle'", 1, "can", got, sizeof(got)),
        got);
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path,
        "SELECT count(*) FROM sync_state WHERE ra_id='can/a1' AND type='Email' AND state='em-1'"));
    rows = activity_rows(1);
    item = find_sync_act_last(rows, "can");
    TEST_ASSERT_NOT_NULL(item);
    TEST_ASSERT_EQUAL_STRING("done", json_string_value(json_object_get(item, "state")));
    json_decref(rows);
}

void test_mailbox_list_tree_shape(void) {
    char path[160];
    char got[192];
    json_t *root;
    json_t *nodes;
    json_t *account;
    json_t *inbox;
    json_t *later;
    json_t *sent;
    json_t *drafts;
    json_int_t inbox_order;
    json_int_t sent_order;
    json_int_t drafts_order;

    sync_slot("tree", 1, path, sizeof(path));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, wait_count(path,
        "SELECT count(*) FROM account_state WHERE account_id='tree' AND state='idle'", 1, "tree", got, sizeof(got)),
        got);

    root = call_reply("mailbox.list", "{\"accountId\":\"tree\"}");
    assert_ok(root);
    nodes = json_object_get(json_object_get(root, "data"), "nodes");
    TEST_ASSERT_EQUAL_INT(7, json_array_size(nodes));
    account = node_by_id(nodes, "tree");
    inbox = node_by_id(nodes, "mbx:tree/a1:in");
    later = node_by_id(nodes, "mbx:tree/a1:later");
    sent = node_by_id(nodes, "mbx:tree/a1:sent");
    drafts = node_by_id(nodes, "mbx:tree/a1:drafts");
    TEST_ASSERT_NOT_NULL(account);
    TEST_ASSERT_NOT_NULL(inbox);
    TEST_ASSERT_NOT_NULL(later);
    TEST_ASSERT_NOT_NULL(sent);
    TEST_ASSERT_NOT_NULL(drafts);

    TEST_ASSERT_EQUAL_STRING("account", json_string_value(json_object_get(account, "kind")));
    TEST_ASSERT_EQUAL_STRING("#336699", json_string_value(json_object_get(account, "swatch")));
    TEST_ASSERT_EQUAL_INT(0, (int)json_integer_value(json_object_get(account, "order")));
    TEST_ASSERT_EQUAL_STRING("Box", json_string_value(json_object_get(account, "label")));
    TEST_ASSERT_TRUE(json_is_true(json_object_get(account, "hasChildren")));
    TEST_ASSERT_EQUAL_INT(1, (int)json_integer_value(json_object_get(account, "unread")));
    TEST_ASSERT_EQUAL_INT(3, (int)json_integer_value(json_object_get(account, "total")));
    TEST_ASSERT_TRUE(json_is_null(json_object_get(account, "role")));
    TEST_ASSERT_TRUE(json_is_null(json_object_get(account, "parentId")));

    TEST_ASSERT_EQUAL_STRING("tree", json_string_value(json_object_get(inbox, "parentId")));
    TEST_ASSERT_TRUE(json_is_true(json_object_get(inbox, "hasChildren")));
    TEST_ASSERT_EQUAL_STRING("inbox", json_string_value(json_object_get(inbox, "role")));
    TEST_ASSERT_TRUE(json_is_null(json_object_get(inbox, "swatch")));
    TEST_ASSERT_EQUAL_STRING("mailbox", json_string_value(json_object_get(inbox, "kind")));

    TEST_ASSERT_EQUAL_STRING("mbx:tree/a1:in", json_string_value(json_object_get(later, "parentId")));
    TEST_ASSERT_EQUAL_INT(1, (int)json_integer_value(json_object_get(later, "unread")));
    TEST_ASSERT_EQUAL_INT(3, (int)json_integer_value(json_object_get(later, "total")));
    TEST_ASSERT_TRUE(json_is_null(json_object_get(later, "role")));
    TEST_ASSERT_TRUE(json_is_false(json_object_get(later, "hasChildren")));
    TEST_ASSERT_EQUAL_STRING("Later", json_string_value(json_object_get(later, "label")));
    TEST_ASSERT_TRUE(json_is_false(json_object_get(sent, "hasChildren")));

    inbox_order = json_integer_value(json_object_get(inbox, "order"));
    sent_order = json_integer_value(json_object_get(sent, "order"));
    drafts_order = json_integer_value(json_object_get(drafts, "order"));
    TEST_ASSERT_TRUE(inbox_order < sent_order);
    TEST_ASSERT_TRUE(sent_order < drafts_order);
    json_decref(root);

    root = call_reply("mailbox.list", "{\"accountId\":\"nope\"}");
    assert_ok(root);
    nodes = json_object_get(json_object_get(root, "data"), "nodes");
    TEST_ASSERT_EQUAL_INT(0, json_array_size(nodes));
    json_decref(root);
    assert_bad_account_id("mailbox.list", "{\"accountId\":1}");
    assert_bad_account_id("mailbox.list", "{\"accountId\":\"\"}");

    root = call_reply("identity.list", "{\"accountId\":\"nope\"}");
    assert_ok(root);
    TEST_ASSERT_EQUAL_INT(0, json_array_size(json_object_get(json_object_get(root, "data"), "identities")));
    json_decref(root);
    assert_bad_account_id("identity.list", "{\"accountId\":1}");
    assert_bad_account_id("identity.list", "{\"accountId\":\"\"}");
}

void test_sync_mailboxes_cannot_calculate(void) {
    char path[160];
    char got[192];
    char mbx_state[64];
    char why[512];
    json_t *root;
    int n;
    int waited_rc;

    sync_slot("calc", 0, path, sizeof(path));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, wait_count(path,
        "SELECT count(*) FROM sync_state s, account_state a "
        "WHERE s.ra_id='calc/a1' AND s.type='Mailbox' AND s.state='mbx-1' "
        "AND a.account_id='calc' AND a.state='idle'",
        1, "calc", got, sizeof(got)), got);
    TEST_ASSERT_TRUE(exec_sql(path,
        "UPDATE sync_state SET state='gone' WHERE ra_id='calc/a1' AND type='Mailbox'") >= 1);
    n = pav_sync_attempt_count("calc");
    root = call_reply("sync.now", "{\"accountId\":\"calc\"}");
    assert_ok(root);
    json_decref(root);
    TEST_ASSERT_EQUAL_INT(n + 1, pav_sync_attempt_count("calc"));
    waited_rc = wait_count(path,
        "SELECT count(*) FROM sync_state s, account_state a "
        "WHERE s.ra_id='calc/a1' AND s.type='Mailbox' AND s.state='mbx-1' "
        "AND a.account_id='calc' AND a.state='idle'",
        1, "calc", got, sizeof(got));
    scalar_text(path, "SELECT state FROM sync_state WHERE ra_id='calc/a1' AND type='Mailbox'", mbx_state,
                sizeof(mbx_state));
    snprintf(why, sizeof(why), "%s mailbox_state=%s gets=%d changes=%d", got,
             mbx_state[0] != '\0' ? mbx_state : "(none)", trace_count("Mailbox/get"), trace_count("Mailbox/changes"));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, waited_rc, why);
    TEST_ASSERT_EQUAL_INT(4, scalar_int(path, "SELECT count(*) FROM mailbox WHERE ra_id='calc/a1'"));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path,
        "SELECT count(*) FROM identity WHERE ra_id='calc/a1' AND email='andrew@test-a.local'"));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path,
        "SELECT count(*) FROM sync_state WHERE ra_id='calc/a1' AND type='Identity' AND state='idn-1'"));
    TEST_ASSERT_EQUAL_INT(n + 1, pav_sync_attempt_count("calc"));
}

/* Reads the recorded Stalwart Mailbox/get and asks the fixture for those
   arguments. The fixture returns the file. It does not go through Mailbox/set. */
void test_recorded_mailbox_get_replay(void) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    char api[512];
    char *text;
    char *request = NULL;
    char *served = NULL;
    json_error_t jerr;
    json_t *file;
    json_t *args;
    json_t *result;
    json_t *list;
    json_t *wrapped = NULL;
    json_t *post = NULL;
    json_t *call;
    json_t *calls;
    char *wrapped_text = NULL;
    PavJmapResp resp;
    char err[256];
    size_t i;
    size_t n;
    int saw_inbox = 0;

    TEST_ASSERT_NOT_NULL_MESSAGE(url, "PAV_FIXTURE_URL must be set by make test");
    TEST_ASSERT_NOT_NULL_MESSAGE(ca, "PAV_FIXTURE_CA must be set by make test");
    text = slurp("tests/support/fixtures/mailbox-get.json");
    TEST_ASSERT_NOT_NULL_MESSAGE(text, "recorded Mailbox/get");
    TEST_ASSERT_NULL(strstr(text, "pavtest-pw"));
    TEST_ASSERT_NULL(strstr(text, "pavtest-admin"));
    TEST_ASSERT_NULL(strstr(text, "Authorization"));
    file = json_loads(text, 0, &jerr);
    free(text);
    TEST_ASSERT_NOT_NULL_MESSAGE(file, jerr.text);
    TEST_ASSERT_EQUAL_STRING("Mailbox/get", json_string_value(json_object_get(file, "method")));
    args = json_object_get(file, "arguments");
    result = json_object_get(file, "response");
    TEST_ASSERT_TRUE(json_is_object(args));
    TEST_ASSERT_TRUE(json_is_string(json_object_get(args, "accountId")));
    TEST_ASSERT_TRUE(json_string_value(json_object_get(args, "accountId"))[0] != '\0');
    TEST_ASSERT_TRUE(json_is_null(json_object_get(args, "ids")));
    TEST_ASSERT_TRUE(json_is_object(result));
    list = json_object_get(result, "list");
    TEST_ASSERT_TRUE(json_is_array(list));
    n = json_array_size(list);
    TEST_ASSERT_TRUE(n > 0);
    for (i = 0; i < n; i++) {
        json_t *box = json_array_get(list, i);
        json_t *id = json_object_get(box, "id");
        json_t *name = json_object_get(box, "name");
        json_t *parent = json_object_get(box, "parentId");
        json_t *role = json_object_get(box, "role");
        json_t *order = json_object_get(box, "sortOrder");
        json_t *total = json_object_get(box, "totalEmails");
        json_t *unread = json_object_get(box, "unreadEmails");
        json_t *subscribed = json_object_get(box, "isSubscribed");
        json_t *rights = json_object_get(box, "myRights");
        const char *role_text;

        TEST_ASSERT_TRUE(json_is_object(box));
        TEST_ASSERT_TRUE(json_is_string(id) && json_string_value(id)[0] != '\0');
        TEST_ASSERT_TRUE(json_is_string(name) && json_string_value(name)[0] != '\0');
        TEST_ASSERT_TRUE(parent == NULL || json_is_null(parent) || json_is_string(parent));
        TEST_ASSERT_TRUE(role == NULL || json_is_null(role) || json_is_string(role));
        TEST_ASSERT_TRUE(order == NULL || json_is_integer(order));
        TEST_ASSERT_TRUE(total == NULL || json_is_integer(total));
        TEST_ASSERT_TRUE(unread == NULL || json_is_integer(unread));
        TEST_ASSERT_TRUE(subscribed == NULL || json_is_boolean(subscribed));
        TEST_ASSERT_TRUE(rights == NULL || json_is_null(rights) || json_is_object(rights));
        role_text = json_string_value(role);
        if (role_text != NULL && strcmp(role_text, "inbox") == 0) {
            saw_inbox = 1;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(saw_inbox, "recorded mailboxes include an inbox");

    wrapped = json_pack("{s:[[s,O,s]], s:s}", "methodResponses", "Mailbox/get", result, "0", "sessionState", "rec");
    TEST_ASSERT_NOT_NULL(wrapped);
    wrapped_text = json_dumps(wrapped, JSON_COMPACT);
    json_decref(wrapped);
    TEST_ASSERT_NOT_NULL(wrapped_text);
    memset(&resp, 0, sizeof(resp));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, pav_jmap_parse_response(wrapped_text, strlen(wrapped_text), &resp, err, sizeof(err)),
                                  err);
    free(wrapped_text);
    TEST_ASSERT_TRUE(resp.is_success);
    TEST_ASSERT_EQUAL_INT(1, (int)json_array_size(resp.method_responses));
    json_decref(resp.method_responses);

    call = json_pack("[s,O,s]", "Mailbox/get", args, "0");
    calls = json_array();
    post = json_object();
    TEST_ASSERT_TRUE(call != NULL && calls != NULL && post != NULL);
    TEST_ASSERT_EQUAL_INT(0, json_array_append_new(calls, call));
    TEST_ASSERT_EQUAL_INT(0, json_object_set_new(post, "using",
                                                 json_pack("[s,s]", "urn:ietf:params:jmap:core", "urn:ietf:params:jmap:mail")));
    TEST_ASSERT_EQUAL_INT(0, json_object_set_new(post, "methodCalls", calls));
    request = json_dumps(post, JSON_COMPACT);
    json_decref(post);
    TEST_ASSERT_NOT_NULL(request);
    snprintf(api, sizeof(api), "%s/jmap/api", url);
    {
        int post_rc = net_post_once(api, ca, request, &served, 8);

        free(request);
        TEST_ASSERT_EQUAL_INT(0, post_rc);
    }
    TEST_ASSERT_NOT_NULL(served);
    memset(&resp, 0, sizeof(resp));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, pav_jmap_parse_response(served, strlen(served), &resp, err, sizeof(err)), err);
    free(served);
    TEST_ASSERT_TRUE(resp.is_success);
    {
        json_t *entry = json_array_get(resp.method_responses, 0);
        json_t *got = json_array_get(entry, 1);
        json_t *got_list = json_object_get(got, "list");

        TEST_ASSERT_EQUAL_STRING("Mailbox/get", json_string_value(json_array_get(entry, 0)));
        TEST_ASSERT_TRUE(json_is_array(got_list));
        TEST_ASSERT_EQUAL_INT((int)n, (int)json_array_size(got_list));
        for (i = 0; i < n; i++) {
            const char *want_id = json_string_value(json_object_get(json_array_get(list, i), "id"));
            const char *got_id = json_string_value(json_object_get(json_array_get(got_list, i), "id"));

            TEST_ASSERT_EQUAL_STRING(want_id, got_id);
        }
    }
    json_decref(resp.method_responses);
    json_decref(file);
}

/* The recorded Session is Stalwart's shape: the account id is the object
   key, and mail is under accountCapabilities. Sync collects a remote
   account only from the parsed id and capabilities. */
void test_recorded_session_keeps_mail_account(void) {
    char *text;
    char *body;
    json_error_t jerr;
    json_t *file;
    json_t *response;
    json_t *accounts;
    json_t *raw;
    json_t *caps;
    PavJmapSession sess;
    char err[256];
    size_t i;
    int found = 0;

    text = slurp("tests/support/fixtures/session.json");
    TEST_ASSERT_NOT_NULL_MESSAGE(text, "recorded Session");
    TEST_ASSERT_NULL(strstr(text, "pavtest-pw"));
    TEST_ASSERT_NULL(strstr(text, "pavtest-admin"));
    TEST_ASSERT_NULL(strstr(text, "Authorization"));
    file = json_loads(text, 0, &jerr);
    free(text);
    TEST_ASSERT_NOT_NULL_MESSAGE(file, jerr.text);
    response = json_object_get(file, "response");
    TEST_ASSERT_TRUE(json_is_object(response));
    accounts = json_object_get(response, "accounts");
    TEST_ASSERT_TRUE(json_is_object(accounts));
    raw = json_object_get(accounts, "b");
    TEST_ASSERT_TRUE_MESSAGE(json_is_object(raw), "recorded account id is the key b");
    TEST_ASSERT_NULL_MESSAGE(json_object_get(raw, "id"), "Stalwart does not repeat the id");
    TEST_ASSERT_NULL(json_object_get(raw, "capabilities"));
    caps = json_object_get(raw, "accountCapabilities");
    TEST_ASSERT_TRUE(json_is_object(caps));
    TEST_ASSERT_NOT_NULL(json_object_get(caps, "urn:ietf:params:jmap:mail"));

    body = json_dumps(response, JSON_COMPACT);
    TEST_ASSERT_NOT_NULL(body);
    memset(&sess, 0, sizeof(sess));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, pav_jmap_parse_session(body, strlen(body), &sess, err, sizeof(err)), err);
    free(body);
    TEST_ASSERT_NOT_NULL(sess.accounts);
    for (i = 0; i < json_array_size(sess.accounts); i++) {
        json_t *acct = json_array_get(sess.accounts, i);
        const char *id = json_string_value(json_object_get(acct, "id"));
        json_t *got;

        if (id == NULL || strcmp(id, "b") != 0) {
            continue;
        }
        found = 1;
        got = json_object_get(acct, "capabilities");
        TEST_ASSERT_TRUE(json_is_object(got));
        TEST_ASSERT_NOT_NULL(json_object_get(got, "urn:ietf:params:jmap:mail"));
        TEST_ASSERT_EQUAL_STRING("andrew@test-a.local",
                                 json_string_value(json_object_get(acct, "name")));
    }
    TEST_ASSERT_TRUE_MESSAGE(found, "parsed session keeps account b");
    pav_jmap_session_free(&sess);

    /* The earlier fixture shape still parses: id and capabilities inside. */
    {
        const char *legacy =
            "{\"accounts\":{\"a1\":{\"id\":\"a1\",\"name\":\"Andrew\","
            "\"capabilities\":{\"urn:ietf:params:jmap:mail\":{}}}}}";

        memset(&sess, 0, sizeof(sess));
        err[0] = '\0';
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, pav_jmap_parse_session(legacy, strlen(legacy), &sess, err, sizeof(err)), err);
        TEST_ASSERT_EQUAL_size_t(1, json_array_size(sess.accounts));
        TEST_ASSERT_EQUAL_STRING("a1",
                                 json_string_value(json_object_get(json_array_get(sess.accounts, 0), "id")));
        TEST_ASSERT_NOT_NULL(json_object_get(
            json_object_get(json_array_get(sess.accounts, 0), "capabilities"), "urn:ietf:params:jmap:mail"));
        pav_jmap_session_free(&sess);
    }
    json_decref(file);
}

void test_session_parse_stalwart(void) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    char session_url[512];
    char *body = NULL;
    PavJmapSession sess;
    char err[512];
    int rc;

    TEST_ASSERT_NOT_NULL_MESSAGE(url, "PAV_FIXTURE_URL must be set by make test");
    TEST_ASSERT_NOT_NULL_MESSAGE(ca, "PAV_FIXTURE_CA must be set by make test");

    snprintf(session_url, sizeof(session_url), "%s/jmap/session", url);
    rc = net_get_once(session_url, ca, 200, &body, 5);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, "fixture session GET must succeed 200");
    TEST_ASSERT_NOT_NULL(body);

    memset(&sess, 0, sizeof(sess));
    rc = pav_jmap_parse_session(body, strlen(body), &sess, err, sizeof(err));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, "pav_jmap_parse_session must succeed");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(sess.api_url, "/jmap/api"), "apiUrl must be parsed");
    TEST_ASSERT_TRUE_MESSAGE(sess.has_mail, "session must have mail capability");
    TEST_ASSERT_TRUE_MESSAGE(sess.has_submission, "session must have submission capability");
    TEST_ASSERT_TRUE_MESSAGE(sess.has_contacts, "session must have contacts capability");
    TEST_ASSERT_TRUE_MESSAGE(sess.has_calendars, "session must have calendars capability");
    TEST_ASSERT_TRUE_MESSAGE(sess.has_sieve == 0, "fixture must not advertise sieve");
    /* 2026-10-19: Stalwart advertises JMAP File Storage as ...:filenode. */
    TEST_ASSERT_TRUE_MESSAGE(sess.has_files, "filenode must map to files");
    TEST_ASSERT_TRUE_MESSAGE(sess.has_websocket == 0, "fixture must not advertise websocket");
    TEST_ASSERT_NOT_NULL_MESSAGE(sess.accounts, "session must have accounts array");
    TEST_ASSERT_TRUE_MESSAGE(json_array_size(sess.accounts) > 0, "accounts array must be non-empty");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(sess.session_state, "state-1"), "sessionState must be parsed");
    pav_jmap_session_free(&sess);
    free(body);
}

void test_session_redirect_wellknown(void) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    char wellknown_url[512];
    json_t *args;
    json_t *data = NULL;
    PavError err;
    int rc;

    TEST_ASSERT_NOT_NULL_MESSAGE(url, "PAV_FIXTURE_URL must be set by make test");
    TEST_ASSERT_NOT_NULL_MESSAGE(ca, "PAV_FIXTURE_CA must be set by make test");

    snprintf(wellknown_url, sizeof(wellknown_url), "%s/.well-known/jmap", url);

    args = json_object();
    TEST_ASSERT_NOT_NULL(args);
    json_object_set_new(args, "sessionUrl", json_string(wellknown_url));
    json_object_set_new(args, "username", json_string("andrew@test-a.local"));
    json_object_set_new(args, "auth", json_string("basic"));
    json_object_set_new(args, "secret", json_string("testpassword"));
    json_object_set_new(args, "caFile", json_string(ca));

    memset(&err, 0, sizeof(err));
    rc = pav_account_test(args, &data, &err);
    json_decref(args);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, "account.test must follow .well-known redirect");
    TEST_ASSERT_NOT_NULL(data);
    if (data != NULL) {
        TEST_ASSERT_TRUE_MESSAGE(json_is_true(json_object_get(data, "ok")),
                                 "ok must be true after following redirect");
        TEST_ASSERT_TRUE_MESSAGE(json_is_true(json_object_get(data, "has_mail")),
                                 "has_mail must be true after redirect");
    }
    json_decref(data);
}

void test_account_test_reports_caps(void) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    json_t *args;
    json_t *data = NULL;
    PavError err;
    int rc;
    char *text;

    TEST_ASSERT_NOT_NULL_MESSAGE(url, "PAV_FIXTURE_URL must be set by make test");
    TEST_ASSERT_NOT_NULL_MESSAGE(ca, "PAV_FIXTURE_CA must be set by make test");

    {
        char session_url[512];
        snprintf(session_url, sizeof(session_url), "%s/jmap/session", url);
        args = json_object();
        TEST_ASSERT_NOT_NULL(args);
        json_object_set_new(args, "sessionUrl", json_string(session_url));
        json_object_set_new(args, "username", json_string("andrew@test-a.local"));
        json_object_set_new(args, "auth", json_string("basic"));
        json_object_set_new(args, "secret", json_string("testpassword"));
        json_object_set_new(args, "caFile", json_string(ca));
    }
    memset(&err, 0, sizeof(err));
    rc = pav_account_test(args, &data, &err);
    json_decref(args);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, "account.test must succeed against fixture");
    TEST_ASSERT_NOT_NULL(data);
    if (data != NULL) {
        TEST_ASSERT_TRUE_MESSAGE(json_is_true(json_object_get(data, "ok")),
                                 "ok must be true");
        {
            json_t *caps = json_object_get(data, "capabilities");
            TEST_ASSERT_TRUE_MESSAGE(caps != NULL && json_is_array(caps),
                                     "capabilities must be an array");
            if (caps != NULL) {
                size_t n = json_array_size(caps);
                size_t i;
                int has_mail = 0, has_submission = 0, has_contacts = 0,
                    has_calendars = 0, has_tasks = 0, has_github = 0, has_files = 0;
                for (i = 0; i < n; i++) {
                    const char *c = json_string_value(json_array_get(caps, i));
                    if (c != NULL) {
                        if (strcmp(c, "mail") == 0) has_mail = 1;
                        else if (strcmp(c, "submission") == 0) has_submission = 1;
                        else if (strcmp(c, "contacts") == 0) has_contacts = 1;
                        else if (strcmp(c, "calendars") == 0) has_calendars = 1;
                        else if (strcmp(c, "tasks") == 0) has_tasks = 1;
                        else if (strcmp(c, "github") == 0) has_github = 1;
                        else if (strcmp(c, "files") == 0) has_files = 1;
                    }
                }
                TEST_ASSERT_TRUE_MESSAGE(has_mail, "capabilities must include mail");
                TEST_ASSERT_TRUE_MESSAGE(has_submission, "capabilities must include submission");
                TEST_ASSERT_TRUE_MESSAGE(has_contacts, "capabilities must include contacts");
                TEST_ASSERT_TRUE_MESSAGE(has_calendars, "capabilities must include calendars");
                TEST_ASSERT_TRUE_MESSAGE(has_files, "filenode must yield files");
                TEST_ASSERT_FALSE_MESSAGE(has_tasks, "capabilities must NOT include tasks");
                TEST_ASSERT_FALSE_MESSAGE(has_github, "capabilities must NOT include github");
            }
        }
        text = json_dumps(data, JSON_COMPACT);
        TEST_ASSERT_NOT_NULL(text);
        TEST_ASSERT_NULL_MESSAGE(strstr(text, "VTODO"), "capabilities must NOT include VTODO");
        free(text);
    }
    json_decref(data);
}

void test_account_test_bad_password(void) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    char auth_url[512];
    json_t *args;
    json_t *data = NULL;
    PavError err;
    int rc;

    TEST_ASSERT_NOT_NULL_MESSAGE(url, "PAV_FIXTURE_URL must be set by make test");
    TEST_ASSERT_NOT_NULL_MESSAGE(ca, "PAV_FIXTURE_CA must be set by make test");

    snprintf(auth_url, sizeof(auth_url), "%s/jmap/session?error=401", url);
    args = json_object();
    TEST_ASSERT_NOT_NULL(args);
    json_object_set_new(args, "sessionUrl", json_string(auth_url));
    json_object_set_new(args, "username", json_string("andrew@test-a.local"));
    json_object_set_new(args, "auth", json_string("basic"));
    json_object_set_new(args, "secret", json_string("wrongpassword"));
    json_object_set_new(args, "caFile", json_string(ca));

    memset(&err, 0, sizeof(err));
    rc = pav_account_test(args, &data, &err);
    json_decref(args);
    TEST_ASSERT_NOT_EQUAL_INT_MESSAGE(0, rc, "bad password must fail");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("auth", err.code, "error must be auth code");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("credentials rejected", err.message, "error must say credentials rejected");
    json_decref(data);
}


static int fixture_account_test(const char *path, json_t **data, PavError *err) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    char full[512];
    json_t *args;
    int rc;

    TEST_ASSERT_NOT_NULL_MESSAGE(url, "PAV_FIXTURE_URL must be set by make test");
    TEST_ASSERT_NOT_NULL_MESSAGE(ca, "PAV_FIXTURE_CA must be set by make test");
    snprintf(full, sizeof(full), "%s%s", url, path);
    args = json_object();
    TEST_ASSERT_NOT_NULL(args);
    json_object_set_new(args, "sessionUrl", json_string(full));
    json_object_set_new(args, "username", json_string("andrew@test-a.local"));
    json_object_set_new(args, "auth", json_string("basic"));
    json_object_set_new(args, "secret", json_string("testpassword"));
    json_object_set_new(args, "caFile", json_string(ca));
    memset(err, 0, sizeof(*err));
    rc = pav_account_test(args, data, err);
    json_decref(args);
    return rc;
}

void test_account_test_relative_redirect(void) {
    json_t *data = NULL;
    PavError err;
    int rc = fixture_account_test("/rel/well-known", &data, &err);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, err.message);
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_TRUE(json_is_true(json_object_get(data, "ok")));
    TEST_ASSERT_TRUE_MESSAGE(json_array_size(json_object_get(data, "capabilities")) > 0,
                             "a relative 307 must reach the session and its capabilities");
    json_decref(data);
}

void test_account_test_early_failure_not_ok(void) {
    json_t *data = NULL;
    PavError err;
    char *reply;
    json_t *parsed;
    int rc = fixture_account_test("/rel/insecure", &data, &err);

    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, rc, "a redirect to http:// must fail, not return ok without data");
    TEST_ASSERT_NULL(data);
    TEST_ASSERT_EQUAL_STRING("server", err.code);
    TEST_ASSERT_NOT_NULL(strstr(err.message, "not https"));
    json_decref(data);

    /* Through the dispatcher the reply is not-ok, never {ok:true,data:null}. */
    {
        const char *url = getenv("PAV_FIXTURE_URL");
        char argsj[1024];
        snprintf(argsj, sizeof(argsj),
                 "{\"sessionUrl\":\"%s/rel/insecure\",\"username\":\"andrew@test-a.local\","
                 "\"auth\":\"basic\",\"secret\":\"testpassword\",\"caFile\":\"%s\"}",
                 url, getenv("PAV_FIXTURE_CA"));
        parsed = json_loads(argsj, 0, NULL);
        TEST_ASSERT_NOT_NULL(parsed);
        reply = pav_dispatch_reply("account.test", parsed);
        json_decref(parsed);
        TEST_ASSERT_NOT_NULL(reply);
        parsed = json_loads(reply, 0, NULL);
        free(reply);
        TEST_ASSERT_NOT_NULL(parsed);
        TEST_ASSERT_TRUE(json_is_false(json_object_get(parsed, "ok")));
        TEST_ASSERT_EQUAL_STRING("server",
            json_string_value(json_object_get(json_object_get(parsed, "error"), "code")));
        json_decref(parsed);
    }
}
void test_account_test_html_is_server_error(void) {
    json_t *data = NULL;
    PavError err;
    int rc = fixture_account_test("/html", &data, &err);

    TEST_ASSERT_NOT_EQUAL_INT(0, rc);
    TEST_ASSERT_NULL(data);
    TEST_ASSERT_EQUAL_STRING("server", err.code);
    TEST_ASSERT_EQUAL_STRING("the server returned a web page, not a JMAP session (check the URL)",
                             err.message);
}

void test_account_test_drops_auth_across_hosts(void) {
    json_t *data = NULL;
    PavError err;
    /* 127.0.0.1 -> localhost: the fixture answers 400 if Authorization arrives. */
    int rc = fixture_account_test("/xhost", &data, &err);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, err.message);
    TEST_ASSERT_TRUE(json_is_true(json_object_get(data, "ok")));
    json_decref(data);
}

static int fixture_port(void) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *c = url != NULL ? strrchr(url, ':') : NULL;
    return c != NULL ? atoi(c + 1) : 0;
}

static int run_discover(const char *email, json_t **data, PavError *err) {
    json_t *args = json_object();
    int rc;

    json_object_set_new(args, "email", json_string(email));
    json_object_set_new(args, "caFile", json_string(getenv("PAV_FIXTURE_CA")));
    memset(err, 0, sizeof(*err));
    *data = NULL;
    rc = pav_account_discover(args, data, err);
    json_decref(args);
    return rc;
}

void test_account_discover_srv(void) {
    json_t *data;
    PavError err;
    char want[128];
    int rc;

    pav_platform_fake_set_srv("_jmap._tcp.test-a.local", "localhost.", fixture_port());
    rc = run_discover("andrew@Test-A.local", &data, &err);
    pav_platform_fake_set_srv(NULL, NULL, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, err.message);
    snprintf(want, sizeof(want), "https://localhost:%d/jmap/session", fixture_port());
    TEST_ASSERT_EQUAL_STRING(want, json_string_value(json_object_get(data, "sessionUrl")));
    TEST_ASSERT_EQUAL_STRING("srv", json_string_value(json_object_get(data, "via")));
    json_decref(data);
}

void test_account_discover_well_known_fallback(void) {
    json_t *data;
    PavError err;
    char want[128];
    int rc;

    /* The SRV target refuses connections, so discovery falls back. */
    pav_platform_fake_set_srv("_jmap._tcp.localhost", "127.0.0.1", 1);
    pav_account_discover_set_port(fixture_port());
    rc = run_discover("andrew@localhost", &data, &err);
    pav_account_discover_set_port(0);
    pav_platform_fake_set_srv(NULL, NULL, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, err.message);
    snprintf(want, sizeof(want), "https://localhost:%d/jmap/session", fixture_port());
    TEST_ASSERT_EQUAL_STRING(want, json_string_value(json_object_get(data, "sessionUrl")));
    TEST_ASSERT_EQUAL_STRING("well-known", json_string_value(json_object_get(data, "via")));
    json_decref(data);
}

void test_account_discover_errors(void) {
    json_t *data;
    PavError err;

    TEST_ASSERT_NOT_EQUAL_INT(0, run_discover("no-at-sign", &data, &err));
    TEST_ASSERT_EQUAL_STRING("bad_args", err.code);
    TEST_ASSERT_NOT_EQUAL_INT(0, run_discover("a@bad_domain!", &data, &err));
    TEST_ASSERT_EQUAL_STRING("bad_args", err.code);
    pav_account_discover_set_port(1); /* nothing listens: not_found, no network */
    TEST_ASSERT_NOT_EQUAL_INT(0, run_discover("a@localhost", &data, &err));
    pav_account_discover_set_port(0);
    TEST_ASSERT_EQUAL_STRING("not_found", err.code);
    TEST_ASSERT_NULL(data);
}

/* K Lock: a password, typed or saved, and its Basic form reach no log line
   at any level (log.db keeps every level). */
void test_password_never_logged(void) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    char body[1024];
    char dbpath[200];
    json_t *root;

    TEST_ASSERT_NOT_NULL_MESSAGE(url, "PAV_FIXTURE_URL must be set by make test");
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    snprintf(body, sizeof(body),
             "{\"kind\":\"jmap\",\"sessionUrl\":\"%s/.well-known/jmap\",\"username\":\"u\","
             "\"secret\":\"Zq9-unique-pw\",\"id\":\"pw\",\"caFile\":\"%s\"}", url, ca);
    root = call_reply("account.add", body);
    assert_ok(root);
    json_decref(root);
    root = call_reply("account.update", "{\"id\":\"pw\",\"patch\":{\"secret\":\"Zq9-unique-pw\"}}");
    json_decref(root);
    snprintf(body, sizeof(body),
             "{\"sessionUrl\":\"%s/.well-known/jmap\",\"username\":\"u\",\"id\":\"pw\",\"secret\":\"\",\"caFile\":\"%s\"}",
             url, ca);
    root = call_reply("account.test", body);
    json_decref(root);
    snprintf(body, sizeof(body),
             "{\"sessionUrl\":\"%s/.well-known/jmap?error=401\",\"username\":\"u\",\"secret\":\"Zq9-unique-pw\",\"caFile\":\"%s\"}",
             url, ca);
    root = call_reply("account.test", body);
    json_decref(root);
    pavillion_stop();
    pav_log_thread_shutdown();
    TEST_ASSERT_TRUE(snprintf(dbpath, sizeof(dbpath), "%s/default.log.db", g_cfg_dir) < (int)sizeof(dbpath));
    TEST_ASSERT_TRUE(db_count(dbpath, "account.test") > 0);
    TEST_ASSERT_EQUAL_INT64(0, db_count(dbpath, "Zq9-unique-pw"));
    TEST_ASSERT_EQUAL_INT64(0, db_count(dbpath, "dTpacTktdW5pcXVlLXB3"));
}

void test_account_test_uses_saved_secret(void) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    char body[1024];
    json_t *root;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    snprintf(body, sizeof(body),
             "{\"kind\":\"jmap\",\"sessionUrl\":\"%s/.well-known/jmap\",\"username\":\"andrew@test-a.local\","
             "\"secret\":\"testpassword\",\"id\":\"saved\",\"caFile\":\"%s\"}", url, ca);
    root = call_reply("account.add", body);
    assert_ok(root);
    json_decref(root);

    /* Blank password plus id: the saved secret is used. */
    snprintf(body, sizeof(body),
             "{\"sessionUrl\":\"%s/.well-known/jmap\",\"username\":\"andrew@test-a.local\","
             "\"id\":\"saved\",\"secret\":\"\",\"caFile\":\"%s\"}", url, ca);
    root = call_reply("account.test", body);
    assert_ok(root);
    json_decref(root);

    /* A typed secret still wins over the saved one. */
    snprintf(body, sizeof(body),
             "{\"sessionUrl\":\"%s/.well-known/jmap?error=401\",\"username\":\"andrew@test-a.local\","
             "\"id\":\"saved\",\"secret\":\"typed\",\"caFile\":\"%s\"}", url, ca);
    root = call_reply("account.test", body);
    TEST_ASSERT_TRUE(json_is_false(json_object_get(root, "ok")));
    json_decref(root);

    /* An unknown id is not_found; no id and no secret is bad_args. */
    snprintf(body, sizeof(body),
             "{\"sessionUrl\":\"%s/.well-known/jmap\",\"username\":\"u\",\"id\":\"nope\"}", url);
    root = call_reply("account.test", body);
    TEST_ASSERT_EQUAL_STRING("not_found",
        json_string_value(json_object_get(json_object_get(root, "error"), "code")));
    json_decref(root);
    snprintf(body, sizeof(body), "{\"sessionUrl\":\"%s/.well-known/jmap\",\"username\":\"u\"}", url);
    root = call_reply("account.test", body);
    TEST_ASSERT_EQUAL_STRING("bad_args",
        json_string_value(json_object_get(json_object_get(root, "error"), "code")));
    json_decref(root);
    pavillion_stop();
}

void test_account_test_late_reply_is_dropped(void) {
    json_t *data = NULL;
    PavError err;
    int rc;

    /* The fixture holds ?slow=1 for 2 s; the waiter gives up after 300 ms. */
    pav_account_set_wait_ms(300);
    rc = fixture_account_test("/jmap/session?slow=1", &data, &err);
    pav_account_set_wait_ms(0);
    TEST_ASSERT_NOT_EQUAL_INT(0, rc);
    TEST_ASSERT_NULL(data);
    TEST_ASSERT_EQUAL_STRING("network", err.code);
    TEST_ASSERT_NOT_NULL(strstr(err.message, "timed out"));

    /* The late reply landed in live memory and was dropped; the next test works. */
    rc = fixture_account_test("/.well-known/jmap", &data, &err);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, err.message);
    json_decref(data);
}

static json_t *first_account(json_t *root) {
    return json_array_get(json_object_get(json_object_get(root, "data"), "accounts"), 0);
}

void test_account_email_stored(void) {
    json_t *root;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    root = call_reply("account.add",
        "{\"kind\":\"jmap\",\"sessionUrl\":\"https://example.com/jmap\",\"username\":\"andrew@500foods.com\","
        "\"secret\":\"x\",\"id\":\"one\"}");
    assert_ok(root);
    TEST_ASSERT_EQUAL_STRING("andrew@500foods.com",
        json_string_value(json_object_get(json_object_get(root, "data"), "email")));
    json_decref(root);
    root = call_reply("account.add",
        "{\"kind\":\"jmap\",\"sessionUrl\":\"https://example.com/jmap\",\"username\":\"andrew\","
        "\"secret\":\"x\",\"id\":\"two\"}");
    assert_ok(root);
    TEST_ASSERT_EQUAL_STRING("", json_string_value(json_object_get(json_object_get(root, "data"), "email")));
    json_decref(root);
    root = call_reply("account.add",
        "{\"kind\":\"jmap\",\"sessionUrl\":\"https://example.com/jmap\",\"username\":\"u\","
        "\"secret\":\"x\",\"id\":\"three\",\"email\":\"not an address\"}");
    TEST_ASSERT_EQUAL_STRING("bad_args",
        json_string_value(json_object_get(json_object_get(root, "error"), "code")));
    json_decref(root);

    root = call_reply("account.update", "{\"id\":\"two\",\"patch\":{\"email\":\"a@b.example\"}}");
    assert_ok(root);
    json_decref(root);
    root = call_reply("account.update", "{\"id\":\"two\",\"patch\":{\"email\":\"a@\"}}");
    TEST_ASSERT_TRUE(json_is_false(json_object_get(root, "ok")));
    json_decref(root);
    root = call_reply("account.list", "{}");
    assert_ok(root);
    TEST_ASSERT_EQUAL_STRING("a@b.example", json_string_value(json_object_get(
        json_array_get(json_object_get(json_object_get(root, "data"), "accounts"), 1), "email")));
    TEST_ASSERT_NOT_NULL(first_account(root));
    json_decref(root);
    pavillion_stop();
}

void test_account_email_migration(void) {
    json_t *doc = json_loads(
        "{\"accounts\":[{\"id\":\"a\",\"username\":\"me@x.example\"},{\"id\":\"b\",\"username\":\"me\"},"
        "{\"id\":\"c\",\"username\":\"me@x.example\",\"email\":\"kept@y.example\"}]}", 0, NULL);
    json_t *acc;

    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_EQUAL_INT(3, pav_account_migrate_email(doc));
    acc = json_object_get(doc, "accounts");
    TEST_ASSERT_EQUAL_STRING("", json_string_value(json_object_get(json_array_get(acc, 2), "icon")));
    TEST_ASSERT_EQUAL_STRING("me@x.example", json_string_value(json_object_get(json_array_get(acc, 0), "email")));
    TEST_ASSERT_EQUAL_STRING("", json_string_value(json_object_get(json_array_get(acc, 1), "email")));
    TEST_ASSERT_EQUAL_STRING("kept@y.example", json_string_value(json_object_get(json_array_get(acc, 2), "email")));
    TEST_ASSERT_EQUAL_size_t(0, json_array_size(json_object_get(json_array_get(acc, 1), "capabilities")));
    TEST_ASSERT_TRUE(json_is_null(json_object_get(json_array_get(acc, 1), "capabilitiesChecked")));
    TEST_ASSERT_EQUAL_INT(0, pav_account_migrate_email(doc));
    json_decref(doc);
}

/* K-Q9: icon is "" by default, else a bounded FA HTML snippet, patchable. */
void test_account_icon_field(void) {
    json_t *root;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    root = call_reply("account.add",
        "{\"kind\":\"jmap\",\"sessionUrl\":\"https://example.com/jmap\",\"username\":\"u\","
        "\"secret\":\"x\",\"id\":\"one\"}");
    assert_ok(root);
    TEST_ASSERT_EQUAL_STRING("", json_string_value(json_object_get(json_object_get(root, "data"), "icon")));
    json_decref(root);
    root = call_reply("account.add",
        "{\"kind\":\"jmap\",\"sessionUrl\":\"https://example.com/jmap\",\"username\":\"u\","
        "\"secret\":\"x\",\"id\":\"two\",\"icon\":\"<script>x</script>\"}");
    TEST_ASSERT_TRUE(json_is_false(json_object_get(root, "ok")));
    json_decref(root);
    root = call_reply("account.update", "{\"id\":\"one\",\"patch\":{\"icon\":\"<i class=\\\"fa-solid fa-briefcase\\\"></i>\"}}");
    assert_ok(root);
    json_decref(root);
    root = call_reply("account.update", "{\"id\":\"one\",\"patch\":{\"icon\":\"<i onclick=\\\"x\\\"></i>\"}}");
    TEST_ASSERT_TRUE(json_is_false(json_object_get(root, "ok")));
    json_decref(root);
    root = call_reply("account.list", "{}");
    assert_ok(root);
    TEST_ASSERT_EQUAL_STRING("<i class=\"fa-solid fa-briefcase\"></i>", json_string_value(json_object_get(
        json_array_get(json_object_get(json_object_get(root, "data"), "accounts"), 0), "icon")));
    json_decref(root);
    pavillion_stop();
}

static int caps_has(json_t *acct, const char *word) {
    size_t i;
    json_t *v;
    json_array_foreach(json_object_get(acct, "capabilities"), i, v) {
        if (strcmp(json_string_value(v), word) == 0) return 1;
    }
    return 0;
}

/* K-Q10: a verified Test of a saved account stores capabilities and the
   checked time; a failed Test leaves them; Add accepts both. */
void test_account_capabilities_stored(void) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    char body[1024];
    char checked[40];
    json_t *root;
    json_t *acct;

    TEST_ASSERT_NOT_NULL_MESSAGE(url, "PAV_FIXTURE_URL must be set by make test");
    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
    snprintf(body, sizeof(body),
             "{\"kind\":\"jmap\",\"sessionUrl\":\"%s/.well-known/jmap\",\"username\":\"andrew@test-a.local\","
             "\"secret\":\"testpassword\",\"id\":\"caps\",\"caFile\":\"%s\"}", url, ca);
    root = call_reply("account.add", body);
    assert_ok(root);
    acct = json_object_get(root, "data");
    TEST_ASSERT_EQUAL_size_t(0, json_array_size(json_object_get(acct, "capabilities")));
    TEST_ASSERT_TRUE(json_is_null(json_object_get(acct, "capabilitiesChecked")));
    json_decref(root);

    snprintf(body, sizeof(body),
             "{\"sessionUrl\":\"%s/.well-known/jmap\",\"username\":\"andrew@test-a.local\","
             "\"id\":\"caps\",\"secret\":\"\",\"caFile\":\"%s\"}", url, ca);
    root = call_reply("account.test", body);
    assert_ok(root);
    json_decref(root);
    root = call_reply("account.list", "{}");
    assert_ok(root);
    acct = first_account(root);
    TEST_ASSERT_TRUE(caps_has(acct, "mail"));
    TEST_ASSERT_TRUE_MESSAGE(caps_has(acct, "files"), "fixture filenode must be stored as files");
    TEST_ASSERT_TRUE(json_is_string(json_object_get(acct, "capabilitiesChecked")));
    snprintf(checked, sizeof(checked), "%s", json_string_value(json_object_get(acct, "capabilitiesChecked")));
    TEST_ASSERT_EQUAL_size_t(24, strlen(checked));
    TEST_ASSERT_EQUAL_CHAR('Z', checked[23]);
    json_decref(root);

    /* A failed Test leaves both untouched. */
    snprintf(body, sizeof(body),
             "{\"sessionUrl\":\"%s/.well-known/jmap?error=401\",\"username\":\"andrew@test-a.local\","
             "\"id\":\"caps\",\"secret\":\"typed\",\"caFile\":\"%s\"}", url, ca);
    root = call_reply("account.test", body);
    TEST_ASSERT_TRUE(json_is_false(json_object_get(root, "ok")));
    json_decref(root);
    root = call_reply("account.list", "{}");
    acct = first_account(root);
    TEST_ASSERT_TRUE(caps_has(acct, "files"));
    TEST_ASSERT_EQUAL_STRING(checked, json_string_value(json_object_get(acct, "capabilitiesChecked")));
    json_decref(root);

    /* An unsaved Test stores nothing; Add carries the form's values. */
    root = call_reply("account.add",
        "{\"kind\":\"jmap\",\"sessionUrl\":\"https://example.com/jmap\",\"username\":\"u\",\"secret\":\"x\","
        "\"id\":\"form\",\"capabilities\":[\"sieve\",\"mail\",\"bogus\"],"
        "\"capabilitiesChecked\":\"2026-10-19T17:05:00Z\"}");
    assert_ok(root);
    json_decref(root);
    root = call_reply("account.add",
        "{\"kind\":\"jmap\",\"sessionUrl\":\"https://example.com/jmap\",\"username\":\"u\",\"secret\":\"x\","
        "\"id\":\"bad\",\"capabilitiesChecked\":\"yesterday\"}");
    TEST_ASSERT_TRUE(json_is_false(json_object_get(root, "ok")));
    json_decref(root);
    root = call_reply("account.list", "{}");
    acct = json_array_get(json_object_get(json_object_get(root, "data"), "accounts"), 1);
    TEST_ASSERT_EQUAL_size_t(2, json_array_size(json_object_get(acct, "capabilities")));
    TEST_ASSERT_EQUAL_STRING("mail", json_string_value(json_array_get(json_object_get(acct, "capabilities"), 0)));
    TEST_ASSERT_EQUAL_STRING("2026-10-19T17:05:00Z", json_string_value(json_object_get(acct, "capabilitiesChecked")));
    json_decref(root);
    pavillion_stop();
}

void test_account_test_bad_args(void) {
    json_t *args;
    json_t *data = NULL;
    PavError err;
    int rc;

    memset(&err, 0, sizeof(err));

    args = json_object();
    TEST_ASSERT_NOT_NULL(args);
    rc = pav_account_test(args, &data, &err);
    TEST_ASSERT_NOT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_STRING("bad_args", err.code);
    pavillion_free(NULL);
    json_decref(args);

    args = json_object();
    json_object_set_new(args, "sessionUrl", json_string("http://insecure.example/.well-known/jmap"));
    json_object_set_new(args, "username", json_string("user"));
    json_object_set_new(args, "secret", json_string("pass"));
    memset(&err, 0, sizeof(err));
    rc = pav_account_test(args, &data, &err);
    TEST_ASSERT_NOT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_STRING("bad_args", err.code);
    json_decref(args);
}

void test_remote_accounts_from_session(void) {
    const char *url = getenv("PAV_FIXTURE_URL");
    const char *ca = getenv("PAV_FIXTURE_CA");
    char session_url[512];
    char *body = NULL;
    PavJmapSession sess;
    char err[512];
    int rc;
    size_t i;
    size_t n;

    TEST_ASSERT_NOT_NULL_MESSAGE(url, "PAV_FIXTURE_URL must be set by make test");
    TEST_ASSERT_NOT_NULL_MESSAGE(ca, "PAV_FIXTURE_CA must be set by make test");

    snprintf(session_url, sizeof(session_url), "%s/jmap/session", url);
    rc = net_get_once(session_url, ca, 200, &body, 5);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, "fixture session GET must succeed");
    TEST_ASSERT_NOT_NULL(body);

    memset(&sess, 0, sizeof(sess));
    rc = pav_jmap_parse_session(body, strlen(body), &sess, err, sizeof(err));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, "parse session for remote accounts");

    TEST_ASSERT_NOT_NULL(sess.accounts);
    n = json_array_size(sess.accounts);
    TEST_ASSERT_TRUE_MESSAGE(n >= 1, "fixture must expose at least 1 remote account");
    for (i = 0; i < n; i++) {
        json_t *acct = json_array_get(sess.accounts, i);
        const char *id = json_string_value(json_object_get(acct, "id"));
        TEST_ASSERT_NOT_NULL(id);
        TEST_ASSERT_TRUE_MESSAGE(id[0] != '\0', "remote account id must be non-empty");
    }

    pav_jmap_session_free(&sess);
    free(body);
}

void test_api_md_lists_every_event(void) {
    static const char *const emitted[] = {
        "accounts.changed", "activity.changed", "cache.change", "link.clicked", "log.lines",
        "mail.mailboxes_changed", "mail.messages_changed", "sync.state"};
    FILE *f = fopen("docs/API.md", "rb");
    char line[2048];
    char heading[API_NAME_LEN] = "";
    int in_events = 0;
    int real = 0;
    size_t i;

    TEST_ASSERT_NOT_NULL(f);
    while (fgets(line, sizeof(line), f) != NULL) {
        rstrip(line);
        if (strncmp(line, "## ", 3) == 0) {
            in_events = strcmp(line, "## Events") == 0;
            continue;
        }
        if (in_events && strncmp(line, "### ", 4) == 0) {
            snprintf(heading, sizeof(heading), "%s", line + 4);
            continue;
        }
        if (in_events && strncmp(line, "State: real", 11) == 0) {
            int known = 0;

            for (i = 0; i < sizeof(emitted) / sizeof(emitted[0]); i++) {
                known |= strcmp(emitted[i], heading) == 0;
            }
            TEST_ASSERT_TRUE_MESSAGE(known, heading);
            real++;
        }
    }
    fclose(f);
    TEST_ASSERT_EQUAL_INT((int)(sizeof(emitted) / sizeof(emitted[0])), real);
}

/* H-4 commit 3: history from log.db through the core. */
static void start_core_db(void) {
    capture_stderr();
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_cfg_dir, NULL, 0));
}

static void stop_core_db(void) {
    pavillion_stop();
    pav_log_thread_shutdown();
    free(take_stderr());
}

static json_t *query_lines(const char *args, json_t **root_out) {
    json_t *root = call_reply("log.query", args);
    json_t *lines;

    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    lines = json_object_get(json_object_get(root, "data"), "lines");
    TEST_ASSERT_TRUE(json_is_array(lines));
    *root_out = root;
    return lines;
}

static json_int_t line_seq(json_t *lines, size_t i) {
    return json_integer_value(json_object_get(json_array_get(lines, i), "seq"));
}

void test_log_query_pages_before_seq(void) {
    json_t *root;
    json_t *lines;
    json_int_t oldest;
    size_t i;
    char args[128];
    int got = 0;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    start_core_db();
    for (i = 0; i < 12000; i++) {
        pav_log("core", PAV_DEBUG, 1, "page-row-%d", (int)i);
    }
    /* The ring holds 10,000 lines; older ones come only from log.db. */
    root = call_reply("log.tail", "{\"limit\":1}");
    json_decref(root);
    lines = query_lines("{\"limit\":5000,\"text\":\"page-row-\"}", &root);
    TEST_ASSERT_EQUAL_INT(5000, (int)json_array_size(lines));
    TEST_ASSERT_EQUAL_STRING("page-row-11999",
                             json_string_value(json_object_get(json_array_get(lines, 4999), "text")));
    for (i = 1; i < 5000; i++) {
        TEST_ASSERT_TRUE(line_seq(lines, i - 1) < line_seq(lines, i));
    }
    oldest = line_seq(lines, 0);
    json_decref(root);
    for (;;) {
        snprintf(args, sizeof(args), "{\"limit\":5000,\"text\":\"page-row-\",\"beforeSeq\":%lld}", (long long)oldest);
        lines = query_lines(args, &root);
        got += (int)json_array_size(lines);
        if (json_array_size(lines) == 0) {
            json_decref(root);
            break;
        }
        TEST_ASSERT_TRUE(line_seq(lines, json_array_size(lines) - 1) < oldest);
        oldest = line_seq(lines, 0);
        if (json_array_size(lines) < 5000) {
            TEST_ASSERT_EQUAL_STRING("page-row-0", json_string_value(json_object_get(json_array_get(lines, 0), "text")));
            json_decref(root);
            break;
        }
        json_decref(root);
    }
    TEST_ASSERT_EQUAL_INT(7000, got);
    stop_core_db();
}

void test_log_query_since_until(void) {
    json_t *root;
    json_t *lines;
    char args[200];
    char path[200];
    sqlite3 *db = NULL;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    start_core_db();
    stop_core_db();
    /* Rows at known times: 1000, 2000, 3000 ms. */
    TEST_ASSERT_TRUE(snprintf(path, sizeof(path), "%s/default.log.db", g_cfg_dir) < (int)sizeof(path));
    TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_open(path, &db));
    TEST_ASSERT_EQUAL_INT(SQLITE_OK, sqlite3_exec(db,
                                                  "INSERT INTO log (seq, ts, session, level, subsystem, text) VALUES "
                                                  "(100001, 1000, 1, 2, 'core', 'range-a'),"
                                                  "(100002, 2000, 1, 2, 'core', 'range-b'),"
                                                  "(100003, 3000, 1, 2, 'core', 'range-c')",
                                                  NULL, NULL, NULL));
    sqlite3_close(db);
    start_core_db();
    snprintf(args, sizeof(args), "{\"limit\":100,\"text\":\"range-\",\"since\":2000,\"until\":3000}");
    lines = query_lines(args, &root);
    TEST_ASSERT_EQUAL_INT(1, (int)json_array_size(lines));
    TEST_ASSERT_EQUAL_STRING("range-b", json_string_value(json_object_get(json_array_get(lines, 0), "text")));
    json_decref(root);
    lines = query_lines("{\"limit\":100,\"text\":\"range-\",\"since\":2000}", &root);
    TEST_ASSERT_EQUAL_INT(2, (int)json_array_size(lines));
    json_decref(root);
    lines = query_lines("{\"limit\":100,\"text\":\"range-\",\"until\":2000}", &root);
    TEST_ASSERT_EQUAL_INT(1, (int)json_array_size(lines));
    TEST_ASSERT_EQUAL_STRING("range-a", json_string_value(json_object_get(json_array_get(lines, 0), "text")));
    json_decref(root);
    stop_core_db();
}

void test_log_query_filters(void) {
    json_t *root;
    json_t *lines;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    start_core_db();
    pav_log("core", PAV_WARN, 0, "filt-warn");
    pav_log("ui", PAV_INFO, 0, "filt-info");
    pav_log("ui", PAV_TRACE, 0, "filt-trace");
    lines = query_lines("{\"limit\":100,\"text\":\"FILT-\",\"levels\":[\"WARN\",\"TRACE\"]}", &root);
    TEST_ASSERT_EQUAL_INT(2, (int)json_array_size(lines));
    TEST_ASSERT_EQUAL_STRING("WARN", json_string_value(json_object_get(json_array_get(lines, 0), "level")));
    TEST_ASSERT_EQUAL_STRING("TRACE", json_string_value(json_object_get(json_array_get(lines, 1), "level")));
    json_decref(root);
    lines = query_lines("{\"limit\":100,\"text\":\"filt-\",\"subsystems\":[\"ui\"]}", &root);
    TEST_ASSERT_EQUAL_INT(2, (int)json_array_size(lines));
    json_decref(root);
    lines = query_lines("{\"limit\":100,\"text\":\"filt-\",\"levels\":[]}", &root);
    TEST_ASSERT_EQUAL_INT(0, (int)json_array_size(lines));
    json_decref(root);
    lines = query_lines("{\"limit\":1,\"text\":\"filt-\"}", &root);
    TEST_ASSERT_EQUAL_INT(1, (int)json_array_size(lines));
    TEST_ASSERT_EQUAL_STRING("filt-trace", json_string_value(json_object_get(json_array_get(lines, 0), "text")));
    json_decref(root);
    stop_core_db();
}

void test_log_query_session_current_previous_all(void) {
    json_t *root;
    json_t *lines;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    start_core_db();
    lines = query_lines("{\"limit\":100,\"session\":\"previous\"}", &root);
    TEST_ASSERT_EQUAL_INT(0, (int)json_array_size(lines));
    json_decref(root);
    pav_log("core", PAV_INFO, 0, "sess-one");
    stop_core_db();
    start_core_db();
    pav_log("core", PAV_INFO, 0, "sess-two");
    lines = query_lines("{\"limit\":100,\"text\":\"sess-\",\"session\":\"current\"}", &root);
    TEST_ASSERT_EQUAL_INT(1, (int)json_array_size(lines));
    TEST_ASSERT_EQUAL_STRING("sess-two", json_string_value(json_object_get(json_array_get(lines, 0), "text")));
    json_decref(root);
    lines = query_lines("{\"limit\":100,\"text\":\"sess-\",\"session\":\"previous\"}", &root);
    TEST_ASSERT_EQUAL_INT(1, (int)json_array_size(lines));
    TEST_ASSERT_EQUAL_STRING("sess-one", json_string_value(json_object_get(json_array_get(lines, 0), "text")));
    json_decref(root);
    lines = query_lines("{\"limit\":100,\"text\":\"sess-\",\"session\":\"all\"}", &root);
    TEST_ASSERT_EQUAL_INT(2, (int)json_array_size(lines));
    json_decref(root);
    lines = query_lines("{\"limit\":100,\"text\":\"sess-\",\"session\":1}", &root);
    TEST_ASSERT_EQUAL_INT(1, (int)json_array_size(lines));
    json_decref(root);
    stop_core_db();
}

void test_log_query_bad_args(void) {
    static const char *const bad_args[] = {
        "{}", "{\"limit\":0}", "{\"limit\":10001}", "{\"limit\":5,\"beforeSeq\":0}",
        "{\"limit\":5,\"since\":-1}", "{\"limit\":5,\"until\":\"x\"}", "{\"limit\":5,\"session\":\"later\"}",
        "{\"limit\":5,\"levels\":[\"LOUD\"]}", "{\"limit\":5,\"subsystems\":7}", "{\"limit\":5,\"bogus\":1}",
    };
    size_t i;
    json_t *root;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    start_core_db();
    for (i = 0; i < sizeof(bad_args) / sizeof(bad_args[0]); i++) {
        root = call_reply("log.query", bad_args[i]);
        assert_call_error(root, "bad_args", NULL);
        json_decref(root);
    }
    stop_core_db();
}

void test_log_export_reads_db(void) {
    json_t *root;
    char out[200];
    char args[400];
    char *text;
    int i;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    start_core_db();
    for (i = 0; i < 10500; i++) {
        pav_log("core", PAV_DEBUG, 1, "exp-row-%d.", i);
    }
    TEST_ASSERT_TRUE(snprintf(out, sizeof(out), "%s/export.log", g_cfg_dir) < (int)sizeof(out));
    snprintf(args, sizeof(args),
             "{\"path\":\"%s\",\"filter\":{\"levels\":[\"DEBUG\"],\"text\":\"exp-row-\",\"session\":\"current\"}}", out);
    root = call_reply("log.export", args);
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    TEST_ASSERT_EQUAL_INT(10500, (int)json_integer_value(json_object_get(json_object_get(root, "data"), "lines")));
    json_decref(root);
    text = slurp(out);
    /* Older than the ring, and in order, as UTC C-CORE lines. */
    TEST_ASSERT_NOT_NULL(strstr(text, " DEBUG core  exp-row-0.\n"));
    TEST_ASSERT_TRUE(strstr(text, "exp-row-0.") < strstr(text, "exp-row-10499."));
    TEST_ASSERT_EQUAL_CHAR('Z', text[23]);
    free(text);
    stop_core_db();
}

void test_log_trim_apply_saves_pref(void) {
    json_t *root;
    json_t *data;
    char path[200];
    json_error_t jerr;
    json_t *profile;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    start_core_db();
    log_megabytes(3, "apply");
    root = call_reply("log.stats", "{}");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    TEST_ASSERT_EQUAL_INT(10, (int)json_integer_value(json_object_get(json_object_get(root, "data"), "capMb")));
    json_decref(root);
    root = call_reply("log.trim_preview", "{\"mb\":1}");
    data = json_object_get(root, "data");
    TEST_ASSERT_TRUE(json_integer_value(json_object_get(data, "bytes")) <= 1024 * 1024);
    TEST_ASSERT_NULL(json_object_get(data, "capMb"));
    json_decref(root);
    root = call_reply("log.trim", "{\"mb\":1}");
    TEST_ASSERT_TRUE(json_is_true(json_object_get(root, "ok")));
    data = json_object_get(root, "data");
    TEST_ASSERT_EQUAL_INT(1, (int)json_integer_value(json_object_get(data, "capMb")));
    TEST_ASSERT_TRUE(json_integer_value(json_object_get(data, "bytes")) <= 1024 * 1024);
    TEST_ASSERT_TRUE(json_is_integer(json_object_get(data, "oldestSeq")));
    json_decref(root);
    snprintf(path, sizeof(path), "%s/profiles/default.json", g_cfg_dir);
    profile = json_load_file(path, 0, &jerr);
    TEST_ASSERT_NOT_NULL(profile);
    TEST_ASSERT_EQUAL_INT(1, (int)json_integer_value(
                                 json_object_get(json_object_get(profile, "preferences"), "log_max_mb")));
    json_decref(profile);
    root = call_reply("log.trim", "{\"mb\":0}");
    assert_call_error(root, "bad_args", "mb");
    json_decref(root);
    root = call_reply("log.trim_preview", "{}");
    assert_call_error(root, "bad_args", "mb");
    json_decref(root);
    stop_core_db();
    /* The saved cap is read at the next start. */
    start_core_db();
    root = call_reply("log.stats", "{}");
    TEST_ASSERT_EQUAL_INT(1, (int)json_integer_value(json_object_get(json_object_get(root, "data"), "capMb")));
    json_decref(root);
    stop_core_db();
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0); /* keep results visible if a test crashes */
    pav_platform_install(pav_platform_fake());
    UNITY_BEGIN();
    RUN_TEST(test_start_reports_ready_and_stop_is_repeatable);
    RUN_TEST(test_core_thread_ready_after_start_and_joined_on_stop);
    RUN_TEST(test_stop_joins_restarts_and_refuses_double_start);
    RUN_TEST(test_log_line_format_and_level_filter);
    RUN_TEST(test_log_trace_captured_like_other_levels);
    RUN_TEST(test_log_formats_args_and_literal_percent);
    RUN_TEST(test_log_two_threads_keep_call_order);
    RUN_TEST(test_log_num_args_mismatch_logs_error);
    RUN_TEST(test_parse_index_and_profile);
    RUN_TEST(test_reject_schema_and_missing_kind);
    RUN_TEST(test_reject_secret_keys);
    RUN_TEST(test_unknown_keys_survive_rewrite);
    RUN_TEST(test_ensure_writes_defaults_once);
    RUN_TEST(test_scan_ignores_stray_json_and_the_index);
    RUN_TEST(test_scan_warns_on_id_mismatch);
    RUN_TEST(test_broken_active_creates_nothing);
    RUN_TEST(test_profile_dir_default_and_absolute);
    RUN_TEST(test_prepare_resolves_paths_and_skips_sqlite);
    RUN_TEST(test_engine_sqlite_or_named_error);
    RUN_TEST(test_sqlite_open_exec_and_reopen);
    RUN_TEST(test_start_loads_default_profile);
    RUN_TEST(test_start_errors_leave_the_thread_up);
    RUN_TEST(test_prefs_round_trip_clamp_and_mask);
    RUN_TEST(test_missing_ui_is_inserted);
    RUN_TEST(test_ui_blob_round_trip);
    RUN_TEST(test_profile_migrates_old_zoom_into_ui);
    RUN_TEST(test_kit_and_icons_read_after_start);
    RUN_TEST(test_kit_and_icons_apply_round_trip);
    RUN_TEST(test_profile_read_kit_skips_zoom_window);
    RUN_TEST(test_api_version_matches_header);
    RUN_TEST(test_cfg_str_defaults_and_rejects_wrong_type);
    RUN_TEST(test_cfg_num_range_clamps_and_steps);
    RUN_TEST(test_cfg_int_range_clamps_and_steps);
    RUN_TEST(test_cfg_bool_defaults_and_rejects_wrong_type);
    RUN_TEST(test_cfg_enum_rejects_unknown);
    RUN_TEST(test_cfg_obj_and_at_do_not_replace_or_create);
    RUN_TEST(test_profile_write_is_atomic);
    RUN_TEST(test_platform_fake_dirs_secrets_and_clock);
    RUN_TEST(test_call_unknown_command);
    RUN_TEST(test_call_bad_args);
    RUN_TEST(test_call_status_roundtrip);
    RUN_TEST(test_call_prefs_ui_and_log);
    RUN_TEST(test_event_delivered_off_core_thread);
    RUN_TEST(test_api_md_lists_every_command);
    RUN_TEST(test_window_initial_default_1200x800);
    RUN_TEST(test_window_report_debounced_write);
    RUN_TEST(test_window_clamped_to_monitor);
    RUN_TEST(test_status_reports_host_flags);
    RUN_TEST(test_protocol_pav_link_emits_and_204);
    RUN_TEST(test_protocol_unknown_scheme_404);
    RUN_TEST(test_themes_catalog_and_asset);
    RUN_TEST(test_start_creates_workdir_0700);
    RUN_TEST(test_logring_wraps_at_capacity);
    RUN_TEST(test_log_tail_since_seq);
    RUN_TEST(test_log_tail_filters_level);
    RUN_TEST(test_logdb_opens_private_wal);
    RUN_TEST(test_logdb_migrate_fresh_to_v1);
    RUN_TEST(test_logdb_newer_is_error);
    RUN_TEST(test_logdb_batches_within_100ms);
    RUN_TEST(test_logdb_error_flushes_sync);
    RUN_TEST(test_logdb_error_wait_capped);
    RUN_TEST(test_logdb_shutdown_flushes);
    RUN_TEST(test_log_pre_attach_written_once);
    RUN_TEST(test_log_seq_continues_across_runs);
    RUN_TEST(test_log_db_only_no_stderr);
    RUN_TEST(test_no_logs_dir_created);
    RUN_TEST(test_log_old_files_deleted);
    RUN_TEST(test_log_old_files_delete_failure_warns);
    RUN_TEST(test_profile_lock_refuses_second_attach);
    RUN_TEST(test_profile_lock_held_while_attached_and_released_at_stop);
    RUN_TEST(test_logdb_trim_to_cap_at_shutdown);
    RUN_TEST(test_logdb_trim_drops_empty_sessions);
    RUN_TEST(test_log_stats_reports_size_records_oldest);
    RUN_TEST(test_log_trim_preview_deletes_nothing);
    RUN_TEST(test_log_trim_now_uses_saved_cap);
    RUN_TEST(test_log_trim_bad_mb);
    RUN_TEST(test_api_md_lists_every_event);
    RUN_TEST(test_log_query_pages_before_seq);
    RUN_TEST(test_log_query_since_until);
    RUN_TEST(test_log_query_filters);
    RUN_TEST(test_log_query_session_current_previous_all);
    RUN_TEST(test_log_query_bad_args);
    RUN_TEST(test_log_export_reads_db);
    RUN_TEST(test_log_trim_apply_saves_pref);
    RUN_TEST(test_log_lines_one_batch);
    RUN_TEST(test_log_client_sanitizes);
    RUN_TEST(test_log_before_workdir_stderr_only);
    RUN_TEST(test_log_before_workdir_stderr_all_levels);
    RUN_TEST(test_log_no_duplicate_after_profile_attach);
    RUN_TEST(test_index_log_level_and_prefs_saved);
    RUN_TEST(test_log_redacts_known_secret_shapes);
    RUN_TEST(test_log_client_rate_limited);
    RUN_TEST(test_log_points_lifecycle_and_commands);
    RUN_TEST(test_log_repeats_collapse_on_different_line);
    RUN_TEST(test_log_repeats_flush_on_idle);
    RUN_TEST(test_log_repeats_flush_at_shutdown);
    RUN_TEST(test_jmap_post_url_outlives_call);
    RUN_TEST(test_log_export_filter);
    RUN_TEST(test_export_write_text);
    RUN_TEST(test_path_is_absolute_per_platform);
    RUN_TEST(test_prefs_set_log_level);
    RUN_TEST(test_activity_begin_progress_end);
    RUN_TEST(test_activity_cancel_is_cooperative);
    RUN_TEST(test_activity_eta_after_two_samples);
    RUN_TEST(test_activity_events_throttled);
    RUN_TEST(test_activity_history_capped);
    RUN_TEST(test_sync_now_does_nothing);
    RUN_TEST(test_sync_session_stored);
    RUN_TEST(test_sync_401_stops_account);
    RUN_TEST(test_sync_locked_keyring_no_retry);
    RUN_TEST(test_sync_mailboxes_initial);
    RUN_TEST(test_sync_mailboxes_paged);
    RUN_TEST(test_sync_mailboxes_refill);
    RUN_TEST(test_mailbox_list_tree_shape);
    RUN_TEST(test_sync_mailboxes_cannot_calculate);
    RUN_TEST(test_sync_mailboxes_delta_rename);
    RUN_TEST(test_email_backfill_pages);
    RUN_TEST(test_email_initial_first_page);
    RUN_TEST(test_email_batches_respect_max_objects);
    RUN_TEST(test_email_sync_cancel_resumes);
    RUN_TEST(test_recorded_mailbox_get_replay);
    RUN_TEST(test_recorded_session_keeps_mail_account);
    RUN_TEST(test_writes_are_serial_fifo);
    RUN_TEST(test_write_batch_one_transaction);
    RUN_TEST(test_failed_job_rolls_back_alone);
    RUN_TEST(test_read_conn_refuses_insert);
    RUN_TEST(test_concurrent_reads_during_write_no_busy);
    RUN_TEST(test_writer_busy_retries_once);
    RUN_TEST(test_store_writer_stack_is_512kb);
    RUN_TEST(test_migrate_fresh_to_v1);
    RUN_TEST(test_migrate_is_idempotent);
    RUN_TEST(test_newer_db_is_error);
    RUN_TEST(test_migrate_v1_to_v2);
    RUN_TEST(test_blob_path_sharding);
    RUN_TEST(test_blob_id_is_base64url);
    RUN_TEST(test_blob_modes);
    RUN_TEST(test_evict_skips_flagged_and_drafts);
    RUN_TEST(test_evict_clears_body_paths);
    RUN_TEST(test_quota_request_is_a_get);
    RUN_TEST(test_quota_pick_prefers_mail);
    RUN_TEST(test_usage_parse_classifies_quotas);
    RUN_TEST(test_usage_quick_request_matches_capabilities);
    RUN_TEST(test_usage_measure_request_uses_result_reference);
    RUN_TEST(test_usage_measure_batch_packs_windows);
    RUN_TEST(test_usage_parse_batch_sums_windows);
    RUN_TEST(test_volume_free_reads_a_directory);
    RUN_TEST(test_cache_usage_sums_local_files);
    RUN_TEST(test_cache_usage_keeps_measured_sizes);
    RUN_TEST(test_runtime_dir_removed_on_stop);
    RUN_TEST(test_prefs_cache_round_trip);
    RUN_TEST(test_db_bind_never_concatenates);
    RUN_TEST(test_secret_roundtrip);
    RUN_TEST(test_secret_missing);
    RUN_TEST(test_secret_locked_reports_locked);
    RUN_TEST(test_secret_never_logged);
    RUN_TEST(test_foreign_keys_on);
    RUN_TEST(test_auth_basic_header);
    RUN_TEST(test_auth_unknown_method_rejected);
    RUN_TEST(test_auth_missing_method_rejected);
    RUN_TEST(test_auth_basic_on_unauthorized_fails);
    RUN_TEST(test_account_list_empty);
    RUN_TEST(test_account_list_has_no_secret);
    RUN_TEST(test_account_add_assigns_next_number);
    RUN_TEST(test_account_rejects_http_url);
    RUN_TEST(test_account_numbers_never_reused);
    RUN_TEST(test_account_add_rolls_back_on_profile_write_failure);
    RUN_TEST(test_account_state_restored_on_boot);
    RUN_TEST(test_account_remove_purges);
    RUN_TEST(test_account_remove_not_found);
    RUN_TEST(test_account_update_patch);
    RUN_TEST(test_account_ca_file_round_trip);
    RUN_TEST(test_account_set_secret);
    RUN_TEST(test_fixture_server_session);
    RUN_TEST(test_net_start_and_stop);
    RUN_TEST(test_net_server_for_matches_url);
    RUN_TEST(test_net_simple_get);
    RUN_TEST(test_net_post_with_body);
    RUN_TEST(test_net_ca_file_rejected_for_wrong_ca);
    RUN_TEST(test_net_streaming_download_to_file);
    RUN_TEST(test_net_streaming_upload_from_file);
    RUN_TEST(test_net_per_account_cap);
    RUN_TEST(test_net_per_server_cap);
    RUN_TEST(test_net_retry_on_429_with_retry_after);
    RUN_TEST(test_net_retry_on_503_backoff);
    RUN_TEST(test_net_401_not_retried);
    RUN_TEST(test_net_release_account_exits_thread);
    RUN_TEST(test_net_overall_cap);
    RUN_TEST(test_session_parse_stalwart);
    RUN_TEST(test_session_redirect_wellknown);
    RUN_TEST(test_account_test_reports_caps);
    RUN_TEST(test_account_test_bad_password);
    RUN_TEST(test_account_test_bad_args);
    RUN_TEST(test_account_test_relative_redirect);
    RUN_TEST(test_account_test_early_failure_not_ok);
    RUN_TEST(test_account_test_html_is_server_error);
    RUN_TEST(test_account_test_drops_auth_across_hosts);
    RUN_TEST(test_account_discover_srv);
    RUN_TEST(test_account_discover_well_known_fallback);
    RUN_TEST(test_account_discover_errors);
    RUN_TEST(test_account_test_uses_saved_secret);
    RUN_TEST(test_password_never_logged);
    RUN_TEST(test_account_test_late_reply_is_dropped);
    RUN_TEST(test_account_email_stored);
    RUN_TEST(test_account_email_migration);
    RUN_TEST(test_account_icon_field);
    RUN_TEST(test_account_capabilities_stored);
    RUN_TEST(test_remote_accounts_from_session);
    return UNITY_END();
}

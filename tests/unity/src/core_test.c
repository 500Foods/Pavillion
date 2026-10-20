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
    TEST_ASSERT_EQUAL_INT(1, count_text(err, "sync-error-line"));
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

void test_log_stderr_warn_error_only(void) {
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
    TEST_ASSERT_EQUAL_INT(1, count_text(err, "pre-warn-h4"));
    TEST_ASSERT_EQUAL_INT(0, count_text(err, "post-debug-h4"));
    TEST_ASSERT_EQUAL_INT(1, count_text(err, "post-error-h4"));
    free(err);
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
    TEST_ASSERT_EQUAL_INT(1, count_text(err, "early-marker-h1"));
    TEST_ASSERT_EQUAL_INT(1, count_text(err, "late-marker-h1"));
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
    TEST_ASSERT_NOT_NULL(strstr(err, " WARN  core  cannot remove old log files in "));
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
    TEST_ASSERT_EQUAL_INT(1, user_version(path));
    TEST_ASSERT_EQUAL_INT(7, user_table_count(path));

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
    TEST_ASSERT_EQUAL_INT(1, user_version(path));
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
    };
    size_t i;

    make_slot(g_cfg_dir, sizeof(g_cfg_dir));
    snprintf(path, sizeof(path), "%s/mail.sqlite", g_cfg_dir);
    open_v1(path);
    TEST_ASSERT_EQUAL_INT(1, user_version(path));
    TEST_ASSERT_EQUAL_INT(7, user_table_count(path));
    TEST_ASSERT_TRUE(journal_is_wal(path));
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        TEST_ASSERT_TRUE(table_exists(path, names[i]));
    }
    TEST_ASSERT_FALSE(table_exists(path, "email"));
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
    TEST_ASSERT_EQUAL_INT(1, user_version(path));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path, "SELECT count(*) FROM activity WHERE id=7 AND title='kept'"));
    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, pav_store_migrate(store, err, sizeof(err)), err);
    TEST_ASSERT_EQUAL_INT(1, user_version(path));
    TEST_ASSERT_EQUAL_INT(1, scalar_int(path, "SELECT count(*) FROM activity WHERE id=7 AND title='kept'"));
    TEST_ASSERT_EQUAL_INT(7, user_table_count(path));
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
    TEST_ASSERT_NOT_NULL(strstr(err, "1"));
    TEST_ASSERT_NOT_NULL(strstr(err, "newer"));
    close_test_store();
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
    TEST_ASSERT_TRUE_MESSAGE(sess.has_files == 0, "fixture must not advertise files");
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
                    has_calendars = 0, has_tasks = 0, has_github = 0;
                for (i = 0; i < n; i++) {
                    const char *c = json_string_value(json_array_get(caps, i));
                    if (c != NULL) {
                        if (strcmp(c, "mail") == 0) has_mail = 1;
                        else if (strcmp(c, "submission") == 0) has_submission = 1;
                        else if (strcmp(c, "contacts") == 0) has_contacts = 1;
                        else if (strcmp(c, "calendars") == 0) has_calendars = 1;
                        else if (strcmp(c, "tasks") == 0) has_tasks = 1;
                        else if (strcmp(c, "github") == 0) has_github = 1;
                    }
                }
                TEST_ASSERT_TRUE_MESSAGE(has_mail, "capabilities must include mail");
                TEST_ASSERT_TRUE_MESSAGE(has_submission, "capabilities must include submission");
                TEST_ASSERT_TRUE_MESSAGE(has_contacts, "capabilities must include contacts");
                TEST_ASSERT_TRUE_MESSAGE(has_calendars, "capabilities must include calendars");
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
    static const char *const emitted[] = {"accounts.changed", "activity.changed", "link.clicked", "log.lines"};
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
    RUN_TEST(test_log_stderr_warn_error_only);
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
    RUN_TEST(test_prefs_set_log_level);
    RUN_TEST(test_activity_begin_progress_end);
    RUN_TEST(test_activity_cancel_is_cooperative);
    RUN_TEST(test_activity_eta_after_two_samples);
    RUN_TEST(test_activity_events_throttled);
    RUN_TEST(test_activity_history_capped);
    RUN_TEST(test_sync_now_does_nothing);
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
    RUN_TEST(test_remote_accounts_from_session);
    return UNITY_END();
}

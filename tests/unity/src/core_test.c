#define _POSIX_C_SOURCE 200809L

#include "pav_config.h"
#include "pav_cfg.h"
#include "pav_platform.h"
#include "pav_store.h"
#include "pavillion.h"
#include "unity.h"

#include <jansson.h>

#include <sqlite3.h>

#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define ALT_LINES 20

static pthread_mutex_t g_turn_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_turn_cv = PTHREAD_COND_INITIALIZER;
static int g_turn;

static char g_log_path[64];

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
    f = fopen(g_log_path, "r");
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
    pav_platform_fake_reset();
    pavillion_stop();
    pav_log_thread_shutdown();
    pav_log_set_output(NULL);
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
    TEST_ASSERT_EQUAL_STRING("ready", status.state);
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
    pav_log("core", PAV_DEBUG, 0, "debug-should-not-appear");
    pav_log("core", PAV_INFO, 0, "core ready");
    text = read_log();

    TEST_ASSERT_NULL(strstr(text, secret));
    TEST_ASSERT_NULL(strstr(text, "debug-should-not-appear"));
    assert_one_line(text, " INFO  core  core ready");
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
    TEST_ASSERT_EQUAL_STRING("logs/default.log", profile->logs);
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
    "  \"log_level\": \"info\"\n"
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
    FILE *f = fopen(path, "r");
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
    TEST_ASSERT_EQUAL_INT(0600, (int)(st.st_mode & 0777));
    TEST_ASSERT_EQUAL_INT(0, stat(profile_path, &st));
    TEST_ASSERT_EQUAL_INT(0600, (int)(st.st_mode & 0777));

    index_text = slurp(index_path);
    profile_text = slurp(profile_path);
    TEST_ASSERT_EQUAL_STRING(k_default_index, index_text);
    TEST_ASSERT_EQUAL_STRING(k_default_profile, profile_text);
    free(index_text);
    free(profile_text);

    out = fopen(profile_path, "w");
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
    FILE *f = fopen(path, "w");

    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_TRUE(fputs(text, f) >= 0);
    TEST_ASSERT_EQUAL_INT(0, fclose(f));
}

static void write_profile_logs(const char *path, const char *id, const char *name, const char *logs) {
    FILE *f = fopen(path, "w");

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
    FILE *f = fopen(path, "w");

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

    snprintf(expect, sizeof(expect), "%s/logs/default.log", g_cfg_dir);
    TEST_ASSERT_EQUAL_STRING(expect, load->profile->resolved_logs);
    TEST_ASSERT_EQUAL_INT(0, stat(expect, &st));
    TEST_ASSERT_EQUAL_INT(0600, (int)(st.st_mode & 0777));

    snprintf(expect, sizeof(expect), "%s/blobs/default", g_cfg_dir);
    TEST_ASSERT_EQUAL_STRING(expect, load->profile->resolved_blobs);
    TEST_ASSERT_EQUAL_INT(0, stat(expect, &st));
    TEST_ASSERT_EQUAL_INT(0700, (int)(st.st_mode & 0777));

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
    snprintf(absolute_log, sizeof(absolute_log), "%s/app.log", g_extra_dir);
    snprintf(path, sizeof(path), "%s/profiles", g_cfg_dir);
    TEST_ASSERT_EQUAL_INT(0, mkdir(path, 0700));
    snprintf(path, sizeof(path), "%s/Pavillion.json", g_cfg_dir);
    write_index(path, "default.json", "profiles");
    snprintf(path, sizeof(path), "%s/profiles/default.json", g_cfg_dir);
    write_profile_logs(path, "default", "Default", absolute_log);
    TEST_ASSERT_EQUAL_INT(0, pav_config_load(g_cfg_dir, &load, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(0, pav_config_prepare(g_cfg_dir, load->profile, err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING(absolute_log, load->profile->resolved_logs);
    TEST_ASSERT_EQUAL_INT(0, stat(absolute_log, &st));
    TEST_ASSERT_EQUAL_INT(0600, (int)(st.st_mode & 0777));
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
    write_profile_logs(path, "default", "Default", "blocked/app.log");
    TEST_ASSERT_EQUAL_INT(0, pav_config_load(g_cfg_dir, &load, err, sizeof(err)));
    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, pav_config_prepare(g_cfg_dir, load->profile, err, sizeof(err)));
    snprintf(expect, sizeof(expect), "%s/blocked/app.log", g_cfg_dir);
    TEST_ASSERT_NOT_NULL(strstr(err, expect));
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

    TEST_ASSERT_EQUAL_INT(0, pav_store_open(&store, "sqlite", path, err, sizeof(err)));
    TEST_ASSERT_NOT_NULL(store);
    TEST_ASSERT_EQUAL_INT(0, stat(path, &st));
    TEST_ASSERT_TRUE(S_ISREG(st.st_mode));
    TEST_ASSERT_EQUAL_INT(0600, (int)(st.st_mode & 0777));
    TEST_ASSERT_EQUAL_INT(0, pav_store_exec(store, "SELECT 1", err, sizeof(err)));
    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, pav_store_exec(store, "SELECT", err, sizeof(err)));
    TEST_ASSERT_TRUE(err[0] != '\0');
    pav_store_close(store);
    store = NULL;

    TEST_ASSERT_EQUAL_INT(0, stat(path, &st));
    TEST_ASSERT_EQUAL_INT(0600, (int)(st.st_mode & 0777));
    TEST_ASSERT_EQUAL_INT(0, user_table_count(path));

    TEST_ASSERT_EQUAL_INT(0, pav_store_open(&store, "sqlite", path, err, sizeof(err)));
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
    TEST_ASSERT_EQUAL_INT(0600, (int)(st.st_mode & 0777));

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
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(path, NULL, 0));
    TEST_ASSERT_EQUAL_INT(1, pav_core_thread_alive());
    TEST_ASSERT_EQUAL_INT(0, pavillion_status(&status));
    TEST_ASSERT_EQUAL_STRING("error", status.state);
    TEST_ASSERT_NOT_NULL(strstr(status.error, "cannot create"));
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
    TEST_ASSERT_EQUAL_INT(0600, (int)(st.st_mode & 0777));

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
    TEST_ASSERT_EQUAL_INT(0600, (int)(st.st_mode & 0777));
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
    TEST_ASSERT_EQUAL_INT(0600, (int)(st.st_mode & 0777));
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
    TEST_ASSERT_EQUAL_INT(0600, (int)(st.st_mode & 0777));
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
    TEST_ASSERT_EQUAL_INT(0700, (int)(st.st_mode & 0777));
    TEST_ASSERT_EQUAL_INT(0, platform->dir(PAV_DIR_RUNTIME, runtime, sizeof(runtime)));
    TEST_ASSERT_TRUE(strcmp(data, runtime) != 0);
    TEST_ASSERT_EQUAL_INT(0, stat(runtime, &st));
    TEST_ASSERT_TRUE(S_ISDIR(st.st_mode));
    platform->on_network_change(NULL, NULL);
    platform->on_resume(NULL, NULL);

    TEST_ASSERT_EQUAL_INT(-1, platform->secret_get("pavillion", "default/a", &secret));
    TEST_ASSERT_NULL(secret);
    TEST_ASSERT_EQUAL_INT(0,
                          platform->secret_set("pavillion", "default/a", "Account", "s3cret"));
    TEST_ASSERT_EQUAL_INT(0, platform->secret_get("pavillion", "default/a", &secret));
    TEST_ASSERT_EQUAL_STRING("s3cret", secret);
    free(secret);
    secret = NULL;
    TEST_ASSERT_EQUAL_INT(0, platform->secret_delete("pavillion", "default/a"));
    TEST_ASSERT_EQUAL_INT(-1, platform->secret_get("pavillion", "default/a", &secret));

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
        FILE *f = fopen(path, "r");
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

    f = fopen("docs/API.md", "r");
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

int main(void) {
    pav_platform_install(pav_platform_fake());
    UNITY_BEGIN();
    RUN_TEST(test_start_reports_ready_and_stop_is_repeatable);
    RUN_TEST(test_core_thread_ready_after_start_and_joined_on_stop);
    RUN_TEST(test_stop_joins_restarts_and_refuses_double_start);
    RUN_TEST(test_log_line_format_and_level_filter);
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
    return UNITY_END();
}

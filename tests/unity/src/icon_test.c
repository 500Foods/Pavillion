#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "icon_test.h"

#include "icon_mark.h"
#include "pav_icon.h"
#include "pavillion.h"
#include "unity.h"

#include <jansson.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char k_png_b64[] =
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVQI12P4z8AAAAADAAEABf4C/gAAAABJRU5ErkJggg==";
static const char k_png2_b64[] =
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVQI12P4z8AAAAADAAEABf4C/gAAAABJRU5ErkJgggo=";
static const char k_svg_b64[] =
    "PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAxIDEiPjxyZWN0IHdpZHRoPSIxIiBoZWlnaHQ9IjEiLz48L3N2Zz4=";
static const char k_bad_svg_b64[] =
    "PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciPjxzY3JpcHQ+MTwvc2NyaXB0Pjwvc3ZnPg==";
static const unsigned char k_png[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53,
    0xde, 0x00, 0x00, 0x00, 0x0c, 0x49, 0x44, 0x41, 0x54, 0x08, 0xd7, 0x63, 0xf8, 0xcf, 0xc0, 0x00,
    0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x05, 0xfe, 0x02, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
    0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

static char g_dir[80];

static void rm_rf(const char *path) {
    DIR *dir = opendir(path);

    if (dir == NULL) {
        unlink(path);
        return;
    }
    for (;;) {
        struct dirent *ent = readdir(dir);
        char child[512];

        if (ent == NULL) {
            break;
        }
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
        rm_rf(child);
    }
    closedir(dir);
    rmdir(path);
}

static void boot(void) {
    char templ[] = "/tmp/pav-icon-XXXXXX";

    g_dir[0] = '\0';
    TEST_ASSERT_NOT_NULL(mkdtemp(templ));
    TEST_ASSERT_TRUE(strlen(templ) < sizeof(g_dir));
    memcpy(g_dir, templ, strlen(templ) + 1);
    TEST_ASSERT_EQUAL_INT(0, pavillion_start(g_dir, NULL, 0));
}

static void halt(void) {
    pavillion_stop();
    if (g_dir[0] != '\0') {
        rm_rf(g_dir);
        g_dir[0] = '\0';
    }
}

static json_t *call_cmd(const char *cmd, const char *args) {
    char *reply = NULL;
    json_error_t jerr;
    json_t *root;

    TEST_ASSERT_EQUAL_INT(0, pavillion_call(cmd, args, &reply));
    TEST_ASSERT_NOT_NULL(reply);
    root = json_loads(reply, 0, &jerr);
    pavillion_free(reply);
    TEST_ASSERT_TRUE(json_is_object(root));
    return root;
}

static json_t *call_ok(const char *cmd, const char *args) {
    json_t *root = call_cmd(cmd, args);
    json_t *data;

    if (!json_is_true(json_object_get(root, "ok"))) {
        char *dump = json_dumps(root, JSON_COMPACT);
        TEST_FAIL_MESSAGE(dump != NULL ? dump : "command failed");
    }
    data = json_object_get(root, "data");
    json_incref(data);
    json_decref(root);
    return data;
}

static void call_fail(const char *cmd, const char *args, const char *code, const char *piece) {
    json_t *root = call_cmd(cmd, args);
    json_t *error = json_object_get(root, "error");
    const char *got = json_string_value(json_object_get(error, "code"));
    const char *msg = json_string_value(json_object_get(error, "message"));

    TEST_ASSERT_TRUE(json_is_false(json_object_get(root, "ok")));
    TEST_ASSERT_EQUAL_STRING(code, got);
    if (piece != NULL) {
        TEST_ASSERT_NOT_NULL(msg);
        TEST_ASSERT_NOT_NULL(strstr(msg, piece));
    }
    json_decref(root);
}

static void assert_mark(const char *url) {
    char req[192];
    PavBytes out;

    snprintf(req, sizeof(req), "{\"scheme\":\"pav-system\",\"url\":\"%s\"}", url);
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_EQUAL_INT(0, pav_protocol(req, &out));
    TEST_ASSERT_EQUAL_INT(200, out.status);
    TEST_ASSERT_EQUAL_size_t(pav_icon_mark_png_len, out.body_len);
    TEST_ASSERT_EQUAL_INT(0, memcmp(out.body, pav_icon_mark_png, out.body_len));
    pavillion_free(out.headers_json);
    pavillion_free(out.body);
}

void test_icon_sha256_vectors(void) {
    char out[65];

    TEST_ASSERT_EQUAL_INT(0, pav_icon_sha256("", 0, out));
    TEST_ASSERT_EQUAL_STRING("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", out);
    TEST_ASSERT_EQUAL_INT(0, pav_icon_sha256("abc", 3, out));
    TEST_ASSERT_EQUAL_STRING("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", out);
}

void test_icon_system_mark(void) {
    PavBytes out;

    TEST_ASSERT_TRUE(pav_icon_mark_png_len > 8);
    TEST_ASSERT_EQUAL_INT(0, memcmp(pav_icon_mark_png, "\x89PNG\r\n\x1a\n", 8));
    assert_mark("pav-system:mark");
    assert_mark("pav-system://mark");
    assert_mark("pav-system://localhost/mark");
    assert_mark("http://pav-system.localhost/mark");
    assert_mark("https://pav-system.localhost/mark");
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_EQUAL_INT(0, pav_protocol("{\"scheme\":\"pav-system\",\"url\":\"pav-system://other\"}", &out));
    TEST_ASSERT_EQUAL_INT(404, out.status);
    pavillion_free(out.headers_json);
    pavillion_free(out.body);
}

static void assert_asset_bytes(const char *id) {
    static const char *const forms[] = {"pav-asset:%s", "pav-asset://%s", "pav-asset://localhost/%s",
                                         "http://pav-asset.localhost/%s"};
    size_t i;

    for (i = 0; i < sizeof(forms) / sizeof(forms[0]); i++) {
        char url[160];
        char req[220];
        PavBytes out;

        snprintf(url, sizeof(url), forms[i], id);
        snprintf(req, sizeof(req), "{\"scheme\":\"pav-asset\",\"url\":\"%s\"}", url);
        memset(&out, 0, sizeof(out));
        TEST_ASSERT_EQUAL_INT(0, pav_protocol(req, &out));
        TEST_ASSERT_EQUAL_INT(200, out.status);
        TEST_ASSERT_EQUAL_size_t(sizeof(k_png), out.body_len);
        TEST_ASSERT_EQUAL_INT(0, memcmp(out.body, k_png, sizeof(k_png)));
        pavillion_free(out.headers_json);
        pavillion_free(out.body);
    }
}

void test_icon_assets_and_catalog(void) {
    char args[1024];
    char path[160];
    json_t *data;
    json_t *assets;
    const char *id;
    const char *id2;
    char id_copy[65];
    char err[160];
    int count = 0;
    FILE *file;

    boot();

    snprintf(args, sizeof(args), "{\"mime\":\"image/png\",\"kind\":\"png-original\",\"base64\":\"%s\"}", k_png_b64);
    data = call_ok("asset.put", args);
    id = json_string_value(json_object_get(data, "id"));
    TEST_ASSERT_NOT_NULL(id);
    TEST_ASSERT_EQUAL_INT(64, (int)strlen(id));
    TEST_ASSERT_TRUE(json_is_false(json_object_get(data, "warn")));
    memcpy(id_copy, id, 65);
    json_decref(data);

    data = call_ok("asset.list", "{}");
    assets = json_object_get(data, "assets");
    TEST_ASSERT_EQUAL_INT(1, (int)json_array_size(assets));
    TEST_ASSERT_EQUAL_STRING(id_copy, json_string_value(json_object_get(json_array_get(assets, 0), "id")));
    TEST_ASSERT_EQUAL_STRING("png-original", json_string_value(json_object_get(json_array_get(assets, 0), "kind")));
    json_decref(data);

    snprintf(args, sizeof(args), "{\"id\":\"%s\",\"offset\":0,\"len\":70}", id_copy);
    data = call_ok("asset.read", args);
    TEST_ASSERT_EQUAL_STRING(k_png_b64, json_string_value(json_object_get(data, "base64")));
    TEST_ASSERT_EQUAL_STRING("image/png", json_string_value(json_object_get(data, "mime")));
    json_decref(data);
    assert_asset_bytes(id_copy);

    snprintf(args, sizeof(args), "{\"mime\":\"image/png\",\"kind\":\"png-original\",\"base64\":\"%s\"}", k_png_b64);
    data = call_ok("asset.put", args);
    TEST_ASSERT_EQUAL_STRING(id_copy, json_string_value(json_object_get(data, "id")));
    json_decref(data);
    data = call_ok("asset.list", "{}");
    TEST_ASSERT_EQUAL_INT(1, (int)json_array_size(json_object_get(data, "assets")));
    json_decref(data);

    snprintf(args, sizeof(args),
             "{\"mime\":\"image/png\",\"kind\":\"png-render\",\"base64\":\"%s\",\"origin\":\"%s\"}", k_png2_b64, id_copy);
    data = call_ok("asset.put", args);
    id2 = json_string_value(json_object_get(data, "id"));
    TEST_ASSERT_NOT_NULL(id2);
    TEST_ASSERT_TRUE(strcmp(id2, id_copy) != 0);
    json_decref(data);
    data = call_ok("asset.list", "{}");
    TEST_ASSERT_EQUAL_INT(2, (int)json_array_size(json_object_get(data, "assets")));
    json_decref(data);

    snprintf(args, sizeof(args), "{\"id\":\"%s\"}", id_copy);
    data = call_ok("asset.delete", args);
    TEST_ASSERT_TRUE(json_is_true(json_object_get(data, "deleted")));
    json_decref(data);
    snprintf(args, sizeof(args), "{\"id\":\"%s\",\"offset\":0,\"len\":8}", id_copy);
    call_fail("asset.read", args, "not_found", NULL);
    call_fail("asset.delete", args, "bad_args", NULL);

    snprintf(path, sizeof(path), "%s/one.svg", g_dir);
    file = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_TRUE(fputs("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1 1\"><rect width=\"1\" height=\"1\"/></svg>",
                           file) >= 0);
    TEST_ASSERT_EQUAL_INT(0, fclose(file));
    snprintf(args, sizeof(args), "{\"path\":\"%s\",\"kind\":\"svg\"}", path);
    data = call_ok("asset.import", args);
    TEST_ASSERT_EQUAL_INT(64, (int)strlen(json_string_value(json_object_get(data, "id"))));
    json_decref(data);

    snprintf(args, sizeof(args), "{\"mime\":\"image/svg+xml\",\"kind\":\"svg\",\"base64\":\"%s\"}", k_svg_b64);
    data = call_ok("asset.put", args);
    json_decref(data);
    snprintf(args, sizeof(args), "{\"mime\":\"image/svg+xml\",\"kind\":\"svg\",\"base64\":\"%s\"}", k_bad_svg_b64);
    call_fail("asset.put", args, "bad_args", NULL);
    call_fail("asset.fetch", "{\"url\":\"http://example.com/a.png\"}", "bad_args", NULL);

    call_fail("prefs.ui.set",
              "{\"iconLibraries\":[{\"id\":\"extra\",\"name\":\"Extra\",\"type\":\"script\","
              "\"url\":\"https://evil.example/a.js\",\"enabled\":true}]}",
              "bad_args", "not shipped");
    call_fail("prefs.ui.set",
              "{\"iconLibraries\":[{\"id\":\"material\",\"name\":\"Material Symbols\",\"type\":\"stylesheet\","
              "\"url\":\"https://user:pass@fonts.googleapis.com/css2?family=x\",\"enabled\":false}]}",
              "bad_args", "credentials");
    data = call_ok("prefs.ui.set",
                   "{\"iconLibraries\":[{\"id\":\"fontawesome\",\"name\":\"Font Awesome\",\"type\":\"script\","
                   "\"url\":\"https://kit.fontawesome.com/4a6b47ce22.js\",\"enabled\":true}]}");
    TEST_ASSERT_TRUE(json_is_array(json_object_get(data, "iconLibraries")));
    json_decref(data);
    call_fail("prefs.ui.set",
              "{\"iconLibraries\":["
              "{\"id\":\"fontawesome\",\"name\":\"Font Awesome\",\"type\":\"script\","
              "\"url\":\"https://kit.fontawesome.com/4a6b47ce22.js\",\"enabled\":true},"
              "{\"id\":\"font-awesome-2\",\"name\":\"Other kit\",\"type\":\"script\","
              "\"url\":\"https://kit.fontawesome.com/abc.js\",\"enabled\":true}]}",
              "bad_args", "only one Font Awesome");
    data = call_ok("prefs.ui.set",
                   "{\"iconLibraries\":["
                   "{\"id\":\"fontawesome\",\"name\":\"Font Awesome\",\"type\":\"script\","
                   "\"url\":\"https://kit.fontawesome.com/4a6b47ce22.js\",\"enabled\":true},"
                   "{\"id\":\"iconify\",\"name\":\"Iconify\",\"type\":\"script\","
                   "\"url\":\"https://code.iconify.design/iconify-icon/3.0.0/iconify-icon.min.js\",\"enabled\":true}]}");
    TEST_ASSERT_TRUE(json_is_array(json_object_get(data, "iconLibraries")));
    json_decref(data);
    data = call_ok("prefs.ui.set",
                   "{\"iconLibraries\":[{\"id\":\"material\",\"name\":\"Material Symbols\",\"type\":\"stylesheet\","
                   "\"url\":\"https://fonts.googleapis.com/css2?family=Material+Symbols+Sharp:opsz,wght,FILL,GRAD@20..48,100..700,0..1,-50..200\","
                   "\"enabled\":false}]}");
    TEST_ASSERT_TRUE(json_is_array(json_object_get(data, "iconLibraries")));
    json_decref(data);
    data = call_ok("prefs.ui.set",
                   "{\"iconLibraries\":[{\"id\":\"iconify\",\"name\":\"Iconify\",\"type\":\"script\","
                   "\"url\":\"https://code.iconify.design/iconify-icon/3.0.0/iconify-icon.min.js\",\"enabled\":false}]}");
    TEST_ASSERT_TRUE(json_is_array(json_object_get(data, "iconLibraries")));
    json_decref(data);
    data = call_ok("prefs.ui.set",
                   "{\"iconLibraries\":[{\"id\":\"bootstrap\",\"name\":\"Bootstrap Icons\",\"type\":\"stylesheet\","
                   "\"url\":\"https://cdn.jsdelivr.net/npm/bootstrap-icons@1.13.1/font/bootstrap-icons.min.css\","
                   "\"enabled\":false}]}");
    TEST_ASSERT_TRUE(json_is_array(json_object_get(data, "iconLibraries")));
    json_decref(data);
    call_fail("prefs.ui.set",
              "{\"iconLibraries\":[{\"id\":\"extra\",\"name\":\"Extra\",\"type\":\"script\","
              "\"url\":\"https://api.iconify.design/all.js\",\"enabled\":true}]}",
              "bad_args", "not shipped");

    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(
        0, pav_iconlib_ingest("fontawesome",
                              "{\"data\":{\"release\":{\"icons\":[{\"id\":\"house\",\"label\":\"House\"},"
                              "{\"id\":\"star\",\"label\":\"Star\"},"
                              "{\"id\":\"facebook\",\"label\":\"Facebook\",\"membership\":{\"free\":[\"brands\"],"
                              "\"pro\":[\"brands\"]}}]}}}",
                              &count, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(3, count);
    TEST_ASSERT_EQUAL_INT(
        0, pav_iconlib_ingest("material",
                              "{\"icons\":[{\"name\":\"home\",\"popularity\":9,\"tags\":[\"house\",\"building\"]},"
                              "{\"name\":\"star\",\"popularity\":1,\"tags\":[\"favorite\"]}]}",
                              &count, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT(2, count);

    data = call_ok("iconlib.search", "{\"id\":\"fontawesome\",\"query\":\"house\"}");
    TEST_ASSERT_EQUAL_INT(1, (int)json_array_size(json_object_get(data, "icons")));
    TEST_ASSERT_EQUAL_STRING("house", json_string_value(json_object_get(json_array_get(json_object_get(data, "icons"), 0), "name")));
    TEST_ASSERT_EQUAL_STRING("", json_string_value(json_object_get(json_array_get(json_object_get(data, "icons"), 0), "family")));
    json_decref(data);
    data = call_ok("iconlib.search", "{\"id\":\"fontawesome\",\"query\":\"facebook\"}");
    TEST_ASSERT_EQUAL_INT(1, (int)json_array_size(json_object_get(data, "icons")));
    TEST_ASSERT_EQUAL_STRING("brands", json_string_value(json_object_get(json_array_get(json_object_get(data, "icons"), 0), "family")));
    json_decref(data);
    data = call_ok("iconlib.search", "{\"id\":\"fontawesome\",\"query\":\"%\"}");
    TEST_ASSERT_EQUAL_INT(0, (int)json_array_size(json_object_get(data, "icons")));
    json_decref(data);
    data = call_ok("iconlib.search", "{\"id\":\"material\"}");
    TEST_ASSERT_EQUAL_STRING("home", json_string_value(json_object_get(json_array_get(json_object_get(data, "icons"), 0), "name")));
    json_decref(data);

    halt();
}

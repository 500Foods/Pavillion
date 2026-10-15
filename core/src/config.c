#define _POSIX_C_SOURCE 200809L

#include "pav_config.h"

#include "pavillion.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <jansson.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_err(char *err, size_t err_len, const char *fmt, ...) {
    va_list ap;

    if (err == NULL || err_len == 0) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(err, err_len, fmt, ap);
    va_end(ap);
}

static int forbidden_key(const json_t *node, const char **name) {
    if (json_is_object(node)) {
        const char *key;
        json_t *value;

        json_object_foreach((json_t *)node, key, value) {
            if (strcmp(key, "password") == 0 || strcmp(key, "token") == 0 ||
                strcmp(key, "secret") == 0 || strcmp(key, "refresh_token") == 0) {
                *name = key;
                return 1;
            }
            if (forbidden_key(value, name)) {
                return 1;
            }
        }
    } else if (json_is_array(node)) {
        size_t i;
        json_t *value;

        json_array_foreach((json_t *)node, i, value) {
            if (forbidden_key(value, name)) {
                return 1;
            }
        }
    }
    return 0;
}

static int copy_string(char *dst, size_t dst_len, const json_t *obj, const char *key,
                       char *err, size_t err_len) {
    const json_t *value = json_object_get(obj, key);
    const char *text;
    size_t n;

    if (!json_is_string(value)) {
        set_err(err, err_len, "missing %s", key);
        return -1;
    }
    text = json_string_value(value);
    n = strlen(text);
    if (n >= dst_len) {
        set_err(err, err_len, "%s is too long", key);
        return -1;
    }
    memcpy(dst, text, n + 1);
    return 0;
}

static int require_kind(const json_t *root, const char *expect, char *kind, size_t kind_len,
                        char *err, size_t err_len) {
    if (copy_string(kind, kind_len, root, "kind", err, err_len) != 0) {
        set_err(err, err_len, "missing kind");
        return -1;
    }
    if (strcmp(kind, expect) != 0) {
        set_err(err, err_len, "kind must be %s", expect);
        return -1;
    }
    return 0;
}

static int require_schema(const json_t *root, int *schema, char *err, size_t err_len) {
    const json_t *value = json_object_get(root, "schema");

    if (!json_is_integer(value) || json_integer_value(value) != 1) {
        set_err(err, err_len, "schema must be 1");
        return -1;
    }
    *schema = 1;
    return 0;
}

static int read_number(const json_t *value, double *out) {
    if (json_is_integer(value)) {
        *out = (double)json_integer_value(value);
        return 0;
    }
    if (json_is_real(value)) {
        *out = json_real_value(value);
        return 0;
    }
    return -1;
}

static int read_window_coord(const json_t *window, const char *key, int *set, int *coord,
                             char *err, size_t err_len) {
    const json_t *value = json_object_get(window, key);

    if (json_is_null(value)) {
        *set = 0;
        *coord = 0;
        return 0;
    }
    if (!json_is_integer(value)) {
        set_err(err, err_len, "missing %s", key);
        return -1;
    }
    *set = 1;
    *coord = (int)json_integer_value(value);
    return 0;
}

static json_t *load_object(const char *text, char *err, size_t err_len) {
    json_error_t jerr;
    json_t *root;
    const char *forbidden = NULL;

    if (text == NULL) {
        set_err(err, err_len, "invalid json");
        return NULL;
    }
    root = json_loads(text, JSON_PRESERVE_ORDER, &jerr);
    if (root == NULL || !json_is_object(root)) {
        json_decref(root);
        set_err(err, err_len, "invalid json");
        return NULL;
    }
    if (forbidden_key(root, &forbidden)) {
        json_decref(root);
        set_err(err, err_len, "forbidden key %s", forbidden);
        return NULL;
    }
    return root;
}

int pav_index_parse(const char *text, PavIndex **out, char *err, size_t err_len) {
    json_t *root;
    PavIndex *index;

    if (out == NULL) {
        set_err(err, err_len, "invalid json");
        return -1;
    }
    *out = NULL;
    root = load_object(text, err, err_len);
    if (root == NULL) {
        return -1;
    }
    index = calloc(1, sizeof(*index));
    if (index == NULL) {
        json_decref(root);
        set_err(err, err_len, "out of memory");
        return -1;
    }
    if (require_kind(root, "pavillion-index", index->kind, sizeof(index->kind), err, err_len) != 0 ||
        require_schema(root, &index->schema, err, err_len) != 0 ||
        copy_string(index->log_level, sizeof(index->log_level), root, "log_level", err, err_len) != 0) {
        json_decref(root);
        free(index);
        return -1;
    }
    if (json_object_get(root, "active") == NULL) {
        index->active[0] = '\0';
    } else if (copy_string(index->active, sizeof(index->active), root, "active", err, err_len) != 0) {
        json_decref(root);
        free(index);
        return -1;
    }
    if (json_object_get(root, "profile_dir") == NULL) {
        memcpy(index->profile_dir, "profiles", sizeof("profiles"));
    } else if (copy_string(index->profile_dir, sizeof(index->profile_dir), root, "profile_dir", err, err_len) != 0) {
        json_decref(root);
        free(index);
        return -1;
    }
    index->doc = root;
    *out = index;
    return 0;
}

int pav_profile_parse(const char *text, PavProfile **out, char *err, size_t err_len) {
    json_t *root;
    json_t *storage;
    json_t *prefs;
    json_t *zoom;
    json_t *window;
    json_t *accounts;
    json_t *maximized;
    PavProfile *profile;

    if (out == NULL) {
        set_err(err, err_len, "invalid json");
        return -1;
    }
    *out = NULL;
    root = load_object(text, err, err_len);
    if (root == NULL) {
        return -1;
    }
    profile = calloc(1, sizeof(*profile));
    if (profile == NULL) {
        json_decref(root);
        set_err(err, err_len, "out of memory");
        return -1;
    }
    if (require_kind(root, "pavillion-profile", profile->kind, sizeof(profile->kind), err, err_len) != 0 ||
        require_schema(root, &profile->schema, err, err_len) != 0) {
        goto fail;
    }

    storage = json_object_get(root, "storage");
    prefs = json_object_get(root, "preferences");
    accounts = json_object_get(root, "accounts");
    if (!json_is_object(storage) || !json_is_object(prefs) || !json_is_array(accounts)) {
        set_err(err, err_len, "missing profile fields");
        goto fail;
    }
    zoom = json_object_get(prefs, "zoom");
    window = json_object_get(prefs, "window");
    if (!json_is_object(zoom) || !json_is_object(window)) {
        set_err(err, err_len, "missing profile fields");
        goto fail;
    }
    maximized = json_object_get(window, "maximized");
    if (!json_is_boolean(maximized)) {
        set_err(err, err_len, "missing maximized");
        goto fail;
    }
    if (read_number(json_object_get(zoom, "shell"), &profile->zoom_shell) != 0) {
        set_err(err, err_len, "missing shell");
        goto fail;
    }
    if (json_object_get(storage, "engine") == NULL) {
        memcpy(profile->engine, "sqlite", sizeof("sqlite"));
    } else if (copy_string(profile->engine, sizeof(profile->engine), storage, "engine", err, err_len) != 0) {
        goto fail;
    }

    if (copy_string(profile->id, sizeof(profile->id), root, "id", err, err_len) != 0 ||
        copy_string(profile->name, sizeof(profile->name), root, "name", err, err_len) != 0 ||
        copy_string(profile->storage_path, sizeof(profile->storage_path), storage, "path", err, err_len) != 0 ||
        copy_string(profile->logs, sizeof(profile->logs), root, "logs", err, err_len) != 0 ||
        copy_string(profile->blobs, sizeof(profile->blobs), root, "blobs", err, err_len) != 0 ||
        copy_string(profile->cache, sizeof(profile->cache), root, "cache", err, err_len) != 0 ||
        copy_string(profile->timezone, sizeof(profile->timezone), prefs, "timezone", err, err_len) != 0 ||
        copy_string(profile->week_start, sizeof(profile->week_start), prefs, "week_start", err, err_len) != 0 ||
        copy_string(profile->time_format, sizeof(profile->time_format), prefs, "time_format", err, err_len) != 0 ||
        copy_string(profile->theme, sizeof(profile->theme), prefs, "theme", err, err_len) != 0 ||
        copy_string(profile->density, sizeof(profile->density), prefs, "density", err, err_len) != 0 ||
        copy_string(profile->chrome, sizeof(profile->chrome), window, "chrome", err, err_len) != 0 ||
        read_window_coord(window, "x", &profile->window_x_set, &profile->window_x, err, err_len) != 0 ||
        read_window_coord(window, "y", &profile->window_y_set, &profile->window_y, err, err_len) != 0) {
        goto fail;
    }
    if (!json_is_integer(json_object_get(window, "width")) ||
        !json_is_integer(json_object_get(window, "height"))) {
        set_err(err, err_len, "missing window size");
        goto fail;
    }
    profile->window_maximized = json_is_true(maximized) ? 1 : 0;
    profile->window_width = (int)json_integer_value(json_object_get(window, "width"));
    profile->window_height = (int)json_integer_value(json_object_get(window, "height"));
    profile->account_count = json_array_size(accounts);
    profile->doc = root;
    *out = profile;
    return 0;

fail:
    json_decref(root);
    free(profile);
    return -1;
}

int pav_index_rewrite(const PavIndex *index, char **text) {
    if (index == NULL || index->doc == NULL || text == NULL) {
        return -1;
    }
    *text = json_dumps(index->doc, JSON_INDENT(2) | JSON_PRESERVE_ORDER);
    return *text == NULL ? -1 : 0;
}

int pav_profile_rewrite(const PavProfile *profile, char **text) {
    if (profile == NULL || profile->doc == NULL || text == NULL) {
        return -1;
    }
    *text = json_dumps(profile->doc, JSON_INDENT(2) | JSON_PRESERVE_ORDER);
    return *text == NULL ? -1 : 0;
}

void pav_index_free(PavIndex *index) {
    if (index == NULL) {
        return;
    }
    json_decref(index->doc);
    free(index);
}

void pav_profile_free(PavProfile *profile) {
    if (profile == NULL) {
        return;
    }
    json_decref(profile->doc);
    free(profile);
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
    "    },\n"
    "    \"ui\": {\n"
    "      \"nav\": { \"width\": 320, \"min\": 200, \"max\": 500 },\n"
    "      \"mail\": { \"split\": 0.5, \"min\": 0.25, \"max\": 0.75 },\n"
    "      \"contacts\": { \"split\": 0.5, \"min\": 0.25, \"max\": 0.75 },\n"
    "      \"files\": { \"split\": 0.5, \"min\": 0.25, \"max\": 0.75 }\n"
    "    }\n"
    "  }\n"
    "}\n";

static int join_path(char *dst, size_t dst_len, const char *dir, const char *name) {
    int n = snprintf(dst, dst_len, "%s/%s", dir, name);
    if (n < 0 || (size_t)n >= dst_len) {
        return -1;
    }
    return 0;
}

static int write_new_file(const char *path, const char *text, char *err, size_t err_len) {
    size_t len = strlen(text);
    size_t off = 0;
    int fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600);

    if (fd < 0) {
        set_err(err, err_len, "cannot create %s", path);
        return -1;
    }
    if (fchmod(fd, 0600) != 0) {
        close(fd);
        unlink(path);
        set_err(err, err_len, "cannot set mode on %s", path);
        return -1;
    }
    while (off < len) {
        ssize_t wrote = write(fd, text + off, len - off);
        if (wrote < 0) {
            close(fd);
            unlink(path);
            set_err(err, err_len, "cannot write %s", path);
            return -1;
        }
        off += (size_t)wrote;
    }
    if (close(fd) != 0) {
        unlink(path);
        set_err(err, err_len, "cannot write %s", path);
        return -1;
    }
    return 0;
}

static int write_replacing(const char *path, const char *text, char *err, size_t err_len) {
    char tmp[4104];
    size_t path_len;
    size_t len;
    size_t off;
    int fd;
    int need_nl;

    path_len = strlen(path);
    if (path_len + 5 > sizeof(tmp)) {
        set_err(err, err_len, "path is too long");
        return -1;
    }
    memcpy(tmp, path, path_len);
    memcpy(tmp + path_len, ".tmp", 5);
    fd = open(tmp, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (fd < 0) {
        set_err(err, err_len, "cannot write %s", path);
        return -1;
    }
    if (fchmod(fd, 0600) != 0) {
        close(fd);
        unlink(tmp);
        set_err(err, err_len, "cannot set mode on %s", path);
        return -1;
    }
    len = strlen(text);
    off = 0;
    while (off < len) {
        ssize_t wrote = write(fd, text + off, len - off);
        if (wrote < 0) {
            close(fd);
            unlink(tmp);
            set_err(err, err_len, "cannot write %s", path);
            return -1;
        }
        off += (size_t)wrote;
    }
    need_nl = len == 0 || text[len - 1] != '\n';
    if (need_nl && write(fd, "\n", 1) != 1) {
        close(fd);
        unlink(tmp);
        set_err(err, err_len, "cannot write %s", path);
        return -1;
    }
    if (fsync(fd) != 0 || close(fd) != 0) {
        unlink(tmp);
        set_err(err, err_len, "cannot write %s", path);
        return -1;
    }
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        set_err(err, err_len, "cannot write %s", path);
        return -1;
    }
    return 0;
}

static double clamp_double(double value, double lo, double hi) {
    if (value < lo) {
        return lo;
    }
    if (value > hi) {
        return hi;
    }
    return value;
}

static int clamp_int(int value, int lo, int hi) {
    if (value < lo) {
        return lo;
    }
    if (value > hi) {
        return hi;
    }
    return value;
}

static int json_set_new(json_t *obj, const char *key, json_t *value, char *err, size_t err_len) {
    if (value == NULL || json_object_set_new(obj, key, value) != 0) {
        set_err(err, err_len, "out of memory");
        return -1;
    }
    return 0;
}

static json_t *number_value(double value, int as_int) {
    if (as_int) {
        return json_integer((json_int_t)value);
    }
    return json_real(value);
}

static int need_number(json_t *obj, const char *key, double fallback, int as_int, int *added,
                        char *err, size_t err_len) {
    json_t *cur = json_object_get(obj, key);
    double ignored;

    if (cur == NULL) {
        if (json_set_new(obj, key, number_value(fallback, as_int), err, err_len) != 0) {
            return -1;
        }
        *added = 1;
        return 0;
    }
    if (read_number(cur, &ignored) != 0) {
        set_err(err, err_len, "missing %s", key);
        return -1;
    }
    return 0;
}

/* A missing pane key reads as 1. A present key that is not a number is an
   error, so a typo in the profile does not silently disable zoom. */
static int read_zoom_key(json_t *zoom, const char *key, double *out, char *err, size_t err_len) {
    json_t *value = json_object_get(zoom, key);
    double parsed;

    if (value == NULL) {
        *out = 1.0;
        return 0;
    }
    if (read_number(value, &parsed) != 0) {
        set_err(err, err_len, "missing %s", key);
        return -1;
    }
    *out = clamp_double(parsed, 0.5, 3.0);
    return 0;
}

static int write_zoom_key(json_t *zoom, const char *key, double value, char *err, size_t err_len) {
    if (value != value) {
        set_err(err, err_len, "missing %s", key);
        return -1;
    }
    return json_set_new(zoom, key, json_real(clamp_double(value, 0.5, 3.0)), err, err_len);
}

static json_t *make_nav(void) {
    json_t *obj = json_object();

    if (obj == NULL) {
        return NULL;
    }
    if (json_object_set_new(obj, "width", json_integer(320)) != 0 ||
        json_object_set_new(obj, "min", json_integer(200)) != 0 ||
        json_object_set_new(obj, "max", json_integer(500)) != 0) {
        json_decref(obj);
        return NULL;
    }
    return obj;
}

static json_t *make_split(void) {
    json_t *obj = json_object();

    if (obj == NULL) {
        return NULL;
    }
    if (json_object_set_new(obj, "split", json_real(0.5)) != 0 ||
        json_object_set_new(obj, "min", json_real(0.25)) != 0 ||
        json_object_set_new(obj, "max", json_real(0.75)) != 0) {
        json_decref(obj);
        return NULL;
    }
    return obj;
}

static int ensure_pane(json_t *ui, const char *name, int pixels, int *added, char *err, size_t err_len) {
    json_t *pane = json_object_get(ui, name);

    if (pane == NULL) {
        pane = pixels ? make_nav() : make_split();
        if (json_set_new(ui, name, pane, err, err_len) != 0) {
            return -1;
        }
        *added = 1;
        return 0;
    }
    if (!json_is_object(pane)) {
        set_err(err, err_len, "preferences.ui is not an object");
        return -1;
    }
    if (pixels) {
        if (need_number(pane, "width", 320, 1, added, err, err_len) != 0 ||
            need_number(pane, "min", 200, 1, added, err, err_len) != 0 ||
            need_number(pane, "max", 500, 1, added, err, err_len) != 0) {
            return -1;
        }
        return 0;
    }
    if (need_number(pane, "split", 0.5, 0, added, err, err_len) != 0 ||
        need_number(pane, "min", 0.25, 0, added, err, err_len) != 0 ||
        need_number(pane, "max", 0.75, 0, added, err, err_len) != 0) {
        return -1;
    }
    return 0;
}

int pav_profile_ensure_ui(PavProfile *profile, int *added, char *err, size_t err_len) {
    json_t *prefs;
    json_t *ui;

    if (added != NULL) {
        *added = 0;
    }
    if (profile == NULL || profile->doc == NULL || added == NULL) {
        set_err(err, err_len, "missing profile");
        return -1;
    }
    prefs = json_object_get(profile->doc, "preferences");
    if (!json_is_object(prefs)) {
        set_err(err, err_len, "missing profile fields");
        return -1;
    }
    ui = json_object_get(prefs, "ui");
    if (ui == NULL) {
        ui = json_object();
        if (json_set_new(prefs, "ui", ui, err, err_len) != 0) {
            return -1;
        }
        *added = 1;
    } else if (!json_is_object(ui)) {
        set_err(err, err_len, "preferences.ui is not an object");
        return -1;
    }
    if (ensure_pane(ui, "nav", 1, added, err, err_len) != 0 ||
        ensure_pane(ui, "mail", 0, added, err, err_len) != 0 ||
        ensure_pane(ui, "contacts", 0, added, err, err_len) != 0 ||
        ensure_pane(ui, "files", 0, added, err, err_len) != 0) {
        return -1;
    }
    return 0;
}

int pav_profile_ensure_zoom(PavProfile *profile, int *added, char *err, size_t err_len) {
    json_t *prefs;
    json_t *zoom;

    if (added != NULL) {
        *added = 0;
    }
    if (profile == NULL || profile->doc == NULL || added == NULL) {
        set_err(err, err_len, "missing profile");
        return -1;
    }
    prefs = json_object_get(profile->doc, "preferences");
    if (!json_is_object(prefs)) {
        set_err(err, err_len, "missing profile fields");
        return -1;
    }
    zoom = json_object_get(prefs, "zoom");
    if (zoom == NULL) {
        zoom = json_object();
        if (json_set_new(prefs, "zoom", zoom, err, err_len) != 0) {
            return -1;
        }
        *added = 1;
    } else if (!json_is_object(zoom)) {
        set_err(err, err_len, "preferences.zoom is not an object");
        return -1;
    }
    if (need_number(zoom, "shell", 1, 0, added, err, err_len) != 0 ||
        need_number(zoom, "navButtons", 1, 0, added, err, err_len) != 0 ||
        need_number(zoom, "navBody", 1, 0, added, err, err_len) != 0 ||
        need_number(zoom, "mailList", 1, 0, added, err, err_len) != 0 ||
        need_number(zoom, "mailDetail", 1, 0, added, err, err_len) != 0) {
        return -1;
    }
    return 0;
}

static int prefs_objects(const PavProfile *profile, json_t **prefs, json_t **zoom, json_t **window,
                         json_t **ui, char *err, size_t err_len) {
    if (profile == NULL || profile->doc == NULL) {
        set_err(err, err_len, "missing profile");
        return -1;
    }
    *prefs = json_object_get(profile->doc, "preferences");
    if (!json_is_object(*prefs)) {
        set_err(err, err_len, "missing profile fields");
        return -1;
    }
    *zoom = json_object_get(*prefs, "zoom");
    *window = json_object_get(*prefs, "window");
    *ui = json_object_get(*prefs, "ui");
    if (!json_is_object(*zoom) || !json_is_object(*window) || !json_is_object(*ui)) {
        set_err(err, err_len, "missing profile fields");
        return -1;
    }
    return 0;
}

static int read_limits(json_t *pane, double *lo, double *hi, int pixels, char *err, size_t err_len) {
    if (read_number(json_object_get(pane, "min"), lo) != 0 ||
        read_number(json_object_get(pane, "max"), hi) != 0) {
        set_err(err, err_len, "missing limits");
        return -1;
    }
    if (*lo != *lo || *hi != *hi || *lo > *hi) {
        set_err(err, err_len, "invalid limits");
        return -1;
    }
    if (pixels) {
        if (*lo < 0.0 || *hi > 10000.0) {
            set_err(err, err_len, "invalid limits");
            return -1;
        }
        return 0;
    }
    if (*lo < 0.0 || *hi > 1.0) {
        set_err(err, err_len, "invalid limits");
        return -1;
    }
    return 0;
}

static int whole_int(double value, int *out) {
    if (value < 0.0 || value > 1000000.0) {
        return -1;
    }
    *out = (int)(value + 0.5);
    return 0;
}

static int read_px(json_t *ui, const char *name, int *width, int *min, int *max, char *err, size_t err_len) {
    json_t *pane = json_object_get(ui, name);
    double lo;
    double hi;
    double value;
    int lo_i;
    int hi_i;
    int value_i;

    if (!json_is_object(pane) || read_limits(pane, &lo, &hi, 1, err, err_len) != 0 ||
        read_number(json_object_get(pane, "width"), &value) != 0 ||
        whole_int(lo, &lo_i) != 0 || whole_int(hi, &hi_i) != 0 || whole_int(value, &value_i) != 0) {
        if (err != NULL && err_len > 0 && err[0] == '\0') {
            set_err(err, err_len, "missing %s", name);
        }
        return -1;
    }
    *min = lo_i;
    *max = hi_i;
    *width = clamp_int(value_i, lo_i, hi_i);
    return 0;
}

static int read_split(json_t *ui, const char *name, double *split, double *min, double *max,
                      char *err, size_t err_len) {
    json_t *pane = json_object_get(ui, name);
    double value;

    if (!json_is_object(pane) || read_limits(pane, min, max, 0, err, err_len) != 0 ||
        read_number(json_object_get(pane, "split"), &value) != 0) {
        if (err != NULL && err_len > 0 && err[0] == '\0') {
            set_err(err, err_len, "missing %s", name);
        }
        return -1;
    }
    *split = clamp_double(value, *min, *max);
    return 0;
}

int pav_profile_read_shell(const PavProfile *profile, PavShellPrefs *out, char *err, size_t err_len) {
    json_t *prefs;
    json_t *zoom;
    json_t *window;
    json_t *ui;

    if (out == NULL) {
        set_err(err, err_len, "missing prefs");
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (prefs_objects(profile, &prefs, &zoom, &window, &ui, err, err_len) != 0) {
        return -1;
    }
    out->zoom_shell = clamp_double(profile->zoom_shell, 0.5, 3.0);
    if (read_zoom_key(zoom, "navButtons", &out->zoom_nav_buttons, err, err_len) != 0 ||
        read_zoom_key(zoom, "navBody", &out->zoom_nav_body, err, err_len) != 0 ||
        read_zoom_key(zoom, "mailList", &out->zoom_mail_list, err, err_len) != 0 ||
        read_zoom_key(zoom, "mailDetail", &out->zoom_mail_detail, err, err_len) != 0) {
        return -1;
    }
    out->window_maximized = profile->window_maximized ? 1 : 0;
    out->window_width = profile->window_width;
    out->window_height = profile->window_height;
    out->window_x_set = profile->window_x_set;
    out->window_y_set = profile->window_y_set;
    out->window_x = profile->window_x;
    out->window_y = profile->window_y;
    memcpy(out->chrome, profile->chrome, sizeof(out->chrome));
    if (read_px(ui, "nav", &out->nav_width, &out->nav_min, &out->nav_max, err, err_len) != 0 ||
        read_split(ui, "mail", &out->mail_split, &out->mail_min, &out->mail_max, err, err_len) != 0 ||
        read_split(ui, "contacts", &out->contacts_split, &out->contacts_min, &out->contacts_max, err, err_len) != 0 ||
        read_split(ui, "files", &out->files_split, &out->files_min, &out->files_max, err, err_len) != 0) {
        return -1;
    }
    return 0;
}

static int write_px(json_t *ui, const char *name, int width, char *err, size_t err_len) {
    json_t *pane = json_object_get(ui, name);
    double lo;
    double hi;
    int lo_i;
    int hi_i;
    int clamped;

    if (!json_is_object(pane) || read_limits(pane, &lo, &hi, 1, err, err_len) != 0 ||
        whole_int(lo, &lo_i) != 0 || whole_int(hi, &hi_i) != 0) {
        if (err != NULL && err_len > 0 && err[0] == '\0') {
            set_err(err, err_len, "missing %s", name);
        }
        return -1;
    }
    clamped = clamp_int(width, lo_i, hi_i);
    return json_set_new(pane, "width", json_integer(clamped), err, err_len);
}

static int write_split(json_t *ui, const char *name, double split, char *err, size_t err_len) {
    json_t *pane = json_object_get(ui, name);
    double lo;
    double hi;

    if (!json_is_object(pane) || read_limits(pane, &lo, &hi, 0, err, err_len) != 0) {
        if (err != NULL && err_len > 0 && err[0] == '\0') {
            set_err(err, err_len, "missing %s", name);
        }
        return -1;
    }
    if (split != split) {
        set_err(err, err_len, "missing %s", name);
        return -1;
    }
    return json_set_new(pane, "split", json_real(clamp_double(split, lo, hi)), err, err_len);
}

int pav_profile_write_shell(PavProfile *profile, const PavShellPrefs *in, unsigned mask,
                            char *err, size_t err_len) {
    json_t *prefs;
    json_t *zoom;
    json_t *window;
    json_t *ui;
    double zoom_shell;

    if (in == NULL) {
        set_err(err, err_len, "missing prefs");
        return -1;
    }
    if ((mask & (PAV_PREFS_ZOOM | PAV_PREFS_WINDOW | PAV_PREFS_UI)) == 0) {
        set_err(err, err_len, "nothing to save");
        return -1;
    }
    if (prefs_objects(profile, &prefs, &zoom, &window, &ui, err, err_len) != 0) {
        return -1;
    }
    if ((mask & PAV_PREFS_ZOOM) != 0) {
        if (in->zoom_shell != in->zoom_shell) {
            set_err(err, err_len, "missing shell");
            return -1;
        }
        zoom_shell = clamp_double(in->zoom_shell, 0.5, 3.0);
        if (json_set_new(zoom, "shell", json_real(zoom_shell), err, err_len) != 0) {
            return -1;
        }
        profile->zoom_shell = zoom_shell;
        if (write_zoom_key(zoom, "navButtons", in->zoom_nav_buttons, err, err_len) != 0 ||
            write_zoom_key(zoom, "navBody", in->zoom_nav_body, err, err_len) != 0 ||
            write_zoom_key(zoom, "mailList", in->zoom_mail_list, err, err_len) != 0 ||
            write_zoom_key(zoom, "mailDetail", in->zoom_mail_detail, err, err_len) != 0) {
            return -1;
        }
    }
    if ((mask & PAV_PREFS_WINDOW) != 0) {
        if (strcmp(in->chrome, "system") != 0 && strcmp(in->chrome, "hidden") != 0) {
            set_err(err, err_len, "chrome must be system or hidden");
            return -1;
        }
        if (in->window_width < 200 || in->window_width > 10000 ||
            in->window_height < 200 || in->window_height > 10000) {
            set_err(err, err_len, "window size is out of range");
            return -1;
        }
        if ((in->window_x_set && (in->window_x < -32000 || in->window_x > 32000)) ||
            (in->window_y_set && (in->window_y < -32000 || in->window_y > 32000))) {
            set_err(err, err_len, "window position is out of range");
            return -1;
        }
        if (json_set_new(window, "maximized", json_boolean(in->window_maximized ? 1 : 0), err, err_len) != 0 ||
            json_set_new(window, "width", json_integer(in->window_width), err, err_len) != 0 ||
            json_set_new(window, "height", json_integer(in->window_height), err, err_len) != 0 ||
            json_set_new(window, "x", in->window_x_set ? json_integer(in->window_x) : json_null(), err, err_len) != 0 ||
            json_set_new(window, "y", in->window_y_set ? json_integer(in->window_y) : json_null(), err, err_len) != 0 ||
            json_set_new(window, "chrome", json_string(in->chrome), err, err_len) != 0) {
            return -1;
        }
        profile->window_maximized = in->window_maximized ? 1 : 0;
        profile->window_width = in->window_width;
        profile->window_height = in->window_height;
        profile->window_x_set = in->window_x_set ? 1 : 0;
        profile->window_y_set = in->window_y_set ? 1 : 0;
        profile->window_x = in->window_x_set ? in->window_x : 0;
        profile->window_y = in->window_y_set ? in->window_y : 0;
        memset(profile->chrome, 0, sizeof(profile->chrome));
        memcpy(profile->chrome, in->chrome, strlen(in->chrome));
    }
    if ((mask & PAV_PREFS_UI) != 0) {
        if (write_px(ui, "nav", in->nav_width, err, err_len) != 0 ||
            write_split(ui, "mail", in->mail_split, err, err_len) != 0 ||
            write_split(ui, "contacts", in->contacts_split, err, err_len) != 0 ||
            write_split(ui, "files", in->files_split, err, err_len) != 0) {
            return -1;
        }
    }
    return 0;
}

int pav_profile_store(const char *path, const PavProfile *profile, char *err, size_t err_len) {
    char *text = NULL;

    if (path == NULL || path[0] == '\0') {
        set_err(err, err_len, "missing profile");
        return -1;
    }
    if (pav_profile_rewrite(profile, &text) != 0) {
        set_err(err, err_len, "cannot write profile");
        return -1;
    }
    if (write_replacing(path, text, err, err_len) != 0) {
        free(text);
        return -1;
    }
    free(text);
    return 0;
}

int pav_config_ensure(const char *workdir, char *err, size_t err_len) {
    char index_path[4096];
    char dir_path[4096];
    char profile_path[4096];
    struct stat st;

    if (workdir == NULL || workdir[0] == '\0') {
        set_err(err, err_len, "missing workdir");
        return -1;
    }
    if (join_path(index_path, sizeof(index_path), workdir, "Pavillion.json") != 0 ||
        join_path(dir_path, sizeof(dir_path), workdir, "profiles") != 0 ||
        join_path(profile_path, sizeof(profile_path), dir_path, "default.json") != 0) {
        set_err(err, err_len, "path is too long");
        return -1;
    }
    if (stat(index_path, &st) == 0) {
        return 0;
    }
    if (mkdir(dir_path, 0700) != 0 && errno != EEXIST) {
        set_err(err, err_len, "cannot create %s", dir_path);
        return -1;
    }
    if (write_new_file(profile_path, k_default_profile, err, err_len) != 0) {
        return -1;
    }
    if (write_new_file(index_path, k_default_index, err, err_len) != 0) {
        unlink(profile_path);
        return -1;
    }
    return 0;
}

static char *read_whole(const char *path) {
    FILE *f = fopen(path, "rb");
    long n;
    char *buf;
    size_t got;

    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    n = ftell(f);
    if (n < 0 || n > 1024 * 1024) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    buf = malloc((size_t)n + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) {
        free(buf);
        return NULL;
    }
    buf[n] = '\0';
    return buf;
}

static int resolve_dir(char *dst, size_t dst_len, const char *workdir, const char *profile_dir) {
    if (profile_dir[0] == '/') {
        if (strlen(profile_dir) >= dst_len) {
            return -1;
        }
        memcpy(dst, profile_dir, strlen(profile_dir) + 1);
        return 0;
    }
    return join_path(dst, dst_len, workdir, profile_dir);
}

static void stem_of(const char *name, char *stem, size_t stem_len) {
    size_t n = strlen(name);

    if (n >= 5 && strcmp(name + n - 5, ".json") == 0) {
        n -= 5;
    }
    if (n >= stem_len) {
        n = stem_len - 1;
    }
    memcpy(stem, name, n);
    stem[n] = '\0';
}

static void load_error(PavConfigLoad *load, const char *fmt, const char *detail) {
    memcpy(load->state, "error", sizeof("error"));
    snprintf(load->error, sizeof(load->error), fmt, detail);
}

static void consider_file(const char *dir, const char *name, const char *active,
                          PavProfile **found, int *broken, char *path_out, size_t path_out_len) {
    char path[4096];
    char stem[64];
    char err[256];
    char *text;
    PavProfile *profile = NULL;
    struct stat st;

    if (strcmp(name, "Pavillion.json") == 0) {
        return;
    }
    if (join_path(path, sizeof(path), dir, name) != 0) {
        pav_log("config", PAV_WARN, 1, "ignored %s: path is too long", name);
        if (strcmp(name, active) == 0) {
            *broken = 1;
        }
        return;
    }
    if (stat(path, &st) != 0 || S_ISDIR(st.st_mode)) {
        return;
    }
    if (strlen(name) < 5 || strcmp(name + strlen(name) - 5, ".json") != 0) {
        pav_log("config", PAV_WARN, 1, "ignored %s: not a profile", name);
        return;
    }

    text = read_whole(path);
    if (text == NULL) {
        pav_log("config", PAV_WARN, 1, "ignored %s: unreadable", name);
        free(text);
        if (strcmp(name, active) == 0) {
            *broken = 1;
        }
        return;
    }
    if (pav_profile_parse(text, &profile, err, sizeof(err)) != 0) {
        pav_log("config", PAV_WARN, 2, "ignored %s: %s", name, err);
        free(text);
        if (strcmp(name, active) == 0) {
            *broken = 1;
        }
        return;
    }
    free(text);
    stem_of(name, stem, sizeof(stem));
    if (strcmp(profile->id, stem) != 0) {
        pav_log("config", PAV_WARN, 1, "ignored %s: id does not match filename", name);
        pav_profile_free(profile);
        if (strcmp(name, active) == 0) {
            *broken = 1;
        }
        return;
    }
    if (strcmp(name, active) != 0) {
        pav_profile_free(profile);
        return;
    }
    pav_profile_free(*found);
    *found = profile;
    if (path_out != NULL && path_out_len > 0) {
        size_t n = strlen(path);
        if (n >= path_out_len) {
            n = path_out_len - 1;
        }
        memcpy(path_out, path, n);
        path_out[n] = '\0';
    }
}

int pav_config_load(const char *workdir, PavConfigLoad **out, char *err, size_t err_len) {
    char index_path[4096];
    char scan_dir[4096];
    char *text;
    PavIndex *index = NULL;
    PavConfigLoad *load;
    DIR *dir;
    struct dirent *ent;

    if (out == NULL) {
        set_err(err, err_len, "missing workdir");
        return -1;
    }
    *out = NULL;
    if (workdir == NULL || workdir[0] == '\0') {
        set_err(err, err_len, "missing workdir");
        return -1;
    }
    if (join_path(index_path, sizeof(index_path), workdir, "Pavillion.json") != 0) {
        set_err(err, err_len, "path is too long");
        return -1;
    }
    text = read_whole(index_path);
    if (text == NULL) {
        set_err(err, err_len, "missing index");
        return -1;
    }
    if (pav_index_parse(text, &index, err, err_len) != 0) {
        free(text);
        return -1;
    }
    free(text);

    load = calloc(1, sizeof(*load));
    if (load == NULL) {
        pav_index_free(index);
        set_err(err, err_len, "out of memory");
        return -1;
    }
    if (index->active[0] == '\0') {
        load_error(load, "active profile missing: %s", "");
        pav_index_free(index);
        *out = load;
        return 0;
    }
    if (resolve_dir(scan_dir, sizeof(scan_dir), workdir, index->profile_dir) != 0) {
        pav_index_free(index);
        free(load);
        set_err(err, err_len, "path is too long");
        return -1;
    }

    dir = opendir(scan_dir);
    if (dir != NULL) {
        int broken = 0;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
                continue;
            }
            consider_file(scan_dir, ent->d_name, index->active, &load->profile, &broken,
                          load->profile_path, sizeof(load->profile_path));
        }
        closedir(dir);
        if (load->profile == NULL) {
            load_error(load, broken ? "active profile broken: %s" : "active profile missing: %s",
                       index->active);
        } else {
            memcpy(load->state, "ready", sizeof("ready"));
        }
    } else {
        load_error(load, "active profile missing: %s", index->active);
    }

    pav_index_free(index);
    *out = load;
    return 0;
}

void pav_config_load_free(PavConfigLoad *load) {
    if (load == NULL) {
        return;
    }
    pav_profile_free(load->profile);
    free(load);
}

static int ensure_dir(const char *path, mode_t mode) {
    struct stat st;

    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }
    if (mkdir(path, mode) != 0) {
        return -1;
    }
    if (chmod(path, mode) != 0) {
        return -1;
    }
    return 0;
}

static int mkdir_p(const char *path, mode_t mode) {
    char buf[4096];
    size_t len;
    size_t i;

    len = strlen(path);
    if (len == 0 || len >= sizeof(buf)) {
        return -1;
    }
    memcpy(buf, path, len + 1);
    for (i = 1; i < len; i++) {
        if (buf[i] != '/') {
            continue;
        }
        buf[i] = '\0';
        if (ensure_dir(buf, mode) != 0) {
            return -1;
        }
        buf[i] = '/';
    }
    return ensure_dir(buf, mode);
}

static int parent_of(char *dst, size_t dst_len, const char *path) {
    const char *slash = strrchr(path, '/');
    size_t len;

    if (slash == NULL) {
        return -1;
    }
    if (slash == path) {
        if (dst_len < 2) {
            return -1;
        }
        dst[0] = '/';
        dst[1] = '\0';
        return 0;
    }
    len = (size_t)(slash - path);
    if (len >= dst_len) {
        return -1;
    }
    memcpy(dst, path, len);
    dst[len] = '\0';
    return 0;
}

static int create_log_file(const char *path, char *err, size_t err_len) {
    char parent[4096];
    int fd;

    if (parent_of(parent, sizeof(parent), path) != 0 || mkdir_p(parent, 0700) != 0) {
        set_err(err, err_len, "cannot create %s", path);
        return -1;
    }
    fd = open(path, O_CREAT | O_WRONLY, 0600);
    if (fd < 0) {
        set_err(err, err_len, "cannot create %s", path);
        return -1;
    }
    if (fchmod(fd, 0600) != 0) {
        close(fd);
        set_err(err, err_len, "cannot create %s", path);
        return -1;
    }
    if (close(fd) != 0) {
        set_err(err, err_len, "cannot create %s", path);
        return -1;
    }
    return 0;
}

int pav_config_prepare(const char *workdir, PavProfile *profile, char *err, size_t err_len) {
    if (workdir == NULL || workdir[0] == '\0' || profile == NULL) {
        set_err(err, err_len, "missing workdir");
        return -1;
    }
    if (resolve_dir(profile->resolved_logs, sizeof(profile->resolved_logs), workdir, profile->logs) != 0 ||
        resolve_dir(profile->resolved_blobs, sizeof(profile->resolved_blobs), workdir, profile->blobs) != 0 ||
        resolve_dir(profile->resolved_cache, sizeof(profile->resolved_cache), workdir, profile->cache) != 0 ||
        resolve_dir(profile->resolved_storage, sizeof(profile->resolved_storage), workdir, profile->storage_path) != 0) {
        set_err(err, err_len, "path is too long");
        return -1;
    }
    if (create_log_file(profile->resolved_logs, err, err_len) != 0) {
        return -1;
    }
    if (mkdir_p(profile->resolved_blobs, 0700) != 0) {
        set_err(err, err_len, "cannot create %s", profile->resolved_blobs);
        return -1;
    }
    if (mkdir_p(profile->resolved_cache, 0700) != 0) {
        set_err(err, err_len, "cannot create %s", profile->resolved_cache);
        return -1;
    }
    return 0;
}

int pav_profile_check_engine(const PavProfile *profile, char *err, size_t err_len) {
    const char *engine;

    if (profile == NULL) {
        set_err(err, err_len, "missing profile");
        return -1;
    }
    engine = profile->engine[0] == '\0' ? "sqlite" : profile->engine;
    if (strcmp(engine, "sqlite") == 0) {
        return 0;
    }
    set_err(err, err_len, "unknown engine %s", engine);
    return -1;
}

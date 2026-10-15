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
    "    \"zoom\": { \"shell\": 1 },\n"
    "    \"window\": {\n"
    "      \"maximized\": false,\n"
    "      \"width\": 1200,\n"
    "      \"height\": 800,\n"
    "      \"x\": null,\n"
    "      \"y\": null,\n"
    "      \"chrome\": \"system\"\n"
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
                          PavProfile **found, int *broken) {
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
            consider_file(scan_dir, ent->d_name, index->active, &load->profile, &broken);
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

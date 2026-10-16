#define _POSIX_C_SOURCE 200809L

#include "pav_config.h"
#include "pav_cfg.h"
#include "config_internal.h"

#include <dirent.h>
#include <jansson.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

static int require_kind(json_t *root, const char *expect, char *kind, size_t kind_len, char *err,
                        size_t err_len) {
    if (cfg_str(root, "kind", NULL, kind, kind_len, err, err_len) != 0) {
        cfg_err(err, err_len, "missing kind");
        return -1;
    }
    if (strcmp(kind, expect) != 0) {
        cfg_err(err, err_len, "kind must be %s", expect);
        return -1;
    }
    return 0;
}

static int require_schema(json_t *root, int *schema, char *err, size_t err_len) {
    const json_t *value = json_object_get(root, "schema");

    if (!json_is_integer(value) || json_integer_value(value) != 1) {
        cfg_err(err, err_len, "schema must be 1");
        return -1;
    }
    *schema = 1;
    return 0;
}

static int read_window_coord(json_t *window, const char *key, int *set, int *coord, char *err,
                             size_t err_len) {
    const json_t *value = json_object_get(window, key);

    if (json_is_null(value)) {
        *set = 0;
        *coord = 0;
        return 0;
    }
    if (!json_is_integer(value)) {
        cfg_err(err, err_len, "missing %s", key);
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
        cfg_err(err, err_len, "invalid json");
        return NULL;
    }
    root = json_loads(text, JSON_PRESERVE_ORDER, &jerr);
    if (root == NULL || !json_is_object(root)) {
        json_decref(root);
        cfg_err(err, err_len, "invalid json");
        return NULL;
    }
    if (forbidden_key(root, &forbidden)) {
        json_decref(root);
        cfg_err(err, err_len, "forbidden key %s", forbidden);
        return NULL;
    }
    return root;
}

int pav_index_parse(const char *text, PavIndex **out, char *err, size_t err_len) {
    json_t *root;
    PavIndex *index;

    if (out == NULL) {
        cfg_err(err, err_len, "invalid json");
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
        cfg_err(err, err_len, "out of memory");
        return -1;
    }
    if (require_kind(root, "pavillion-index", index->kind, sizeof(index->kind), err, err_len) != 0 ||
        require_schema(root, &index->schema, err, err_len) != 0 ||
        cfg_str(root, "log_level", NULL, index->log_level, sizeof(index->log_level), err, err_len) != 0 ||
        cfg_str(root, "active", "", index->active, sizeof(index->active), err, err_len) != 0 ||
        cfg_str(root, "profile_dir", "profiles", index->profile_dir, sizeof(index->profile_dir), err,
                err_len) != 0) {
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
    PavProfile *profile;

    if (out == NULL) {
        cfg_err(err, err_len, "invalid json");
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
        cfg_err(err, err_len, "out of memory");
        return -1;
    }
    if (require_kind(root, "pavillion-profile", profile->kind, sizeof(profile->kind), err, err_len) != 0 ||
        require_schema(root, &profile->schema, err, err_len) != 0) {
        goto fail;
    }

    storage = cfg_obj(root, "storage", 0);
    prefs = cfg_obj(root, "preferences", 0);
    accounts = cfg_at(root, "accounts");
    if (storage == NULL || prefs == NULL || !json_is_array(accounts)) {
        cfg_err(err, err_len, "missing profile fields");
        goto fail;
    }
    zoom = cfg_obj(prefs, "zoom", 0);
    window = cfg_obj(prefs, "window", 0);
    if (zoom == NULL || window == NULL) {
        cfg_err(err, err_len, "missing profile fields");
        goto fail;
    }
    if (json_object_get(window, "maximized") == NULL ||
        cfg_bool(window, "maximized", 0, &profile->window_maximized, err, err_len) != 0) {
        cfg_err(err, err_len, "missing maximized");
        goto fail;
    }
    if (json_object_get(zoom, "shell") == NULL ||
        cfg_num_range(zoom, "shell", 0, -INFINITY, INFINITY, 0, &profile->zoom_shell, err, err_len) != 0) {
        cfg_err(err, err_len, "missing shell");
        goto fail;
    }
    if (cfg_str(storage, "engine", "sqlite", profile->engine, sizeof(profile->engine), err, err_len) != 0 ||
        cfg_str(root, "id", NULL, profile->id, sizeof(profile->id), err, err_len) != 0 ||
        cfg_str(root, "name", NULL, profile->name, sizeof(profile->name), err, err_len) != 0 ||
        cfg_str(storage, "path", NULL, profile->storage_path, sizeof(profile->storage_path), err, err_len) !=
            0 ||
        cfg_str(root, "logs", NULL, profile->logs, sizeof(profile->logs), err, err_len) != 0 ||
        cfg_str(root, "blobs", NULL, profile->blobs, sizeof(profile->blobs), err, err_len) != 0 ||
        cfg_str(root, "cache", NULL, profile->cache, sizeof(profile->cache), err, err_len) != 0 ||
        cfg_str(prefs, "timezone", NULL, profile->timezone, sizeof(profile->timezone), err, err_len) != 0 ||
        cfg_str(prefs, "week_start", NULL, profile->week_start, sizeof(profile->week_start), err, err_len) !=
            0 ||
        cfg_str(prefs, "time_format", NULL, profile->time_format, sizeof(profile->time_format), err,
                err_len) != 0 ||
        cfg_str(prefs, "theme", NULL, profile->theme, sizeof(profile->theme), err, err_len) != 0 ||
        cfg_str(prefs, "density", NULL, profile->density, sizeof(profile->density), err, err_len) != 0 ||
        cfg_str(window, "chrome", NULL, profile->chrome, sizeof(profile->chrome), err, err_len) != 0 ||
        read_window_coord(window, "x", &profile->window_x_set, &profile->window_x, err, err_len) != 0 ||
        read_window_coord(window, "y", &profile->window_y_set, &profile->window_y, err, err_len) != 0) {
        goto fail;
    }
    if (!json_is_integer(json_object_get(window, "width")) ||
        !json_is_integer(json_object_get(window, "height"))) {
        cfg_err(err, err_len, "missing window size");
        goto fail;
    }
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

static void consider_file(const char *dir, const char *name, const char *active, PavProfile **found,
                          int *broken, char *path_out, size_t path_out_len) {
    char path[4096];
    char stem[64];
    char err[256];
    char *text;
    PavProfile *profile = NULL;
    struct stat st;

    if (strcmp(name, "Pavillion.json") == 0) {
        return;
    }
    if (config_join(path, sizeof(path), dir, name) != 0) {
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
        cfg_err(err, err_len, "missing workdir");
        return -1;
    }
    *out = NULL;
    if (workdir == NULL || workdir[0] == '\0') {
        cfg_err(err, err_len, "missing workdir");
        return -1;
    }
    if (config_join(index_path, sizeof(index_path), workdir, "Pavillion.json") != 0) {
        cfg_err(err, err_len, "path is too long");
        return -1;
    }
    text = read_whole(index_path);
    if (text == NULL) {
        cfg_err(err, err_len, "missing index");
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
        cfg_err(err, err_len, "out of memory");
        return -1;
    }
    if (index->active[0] == '\0') {
        load_error(load, "active profile missing: %s", "");
        pav_index_free(index);
        *out = load;
        return 0;
    }
    if (config_resolve(scan_dir, sizeof(scan_dir), workdir, index->profile_dir) != 0) {
        pav_index_free(index);
        free(load);
        cfg_err(err, err_len, "path is too long");
        return -1;
    }

    dir = opendir(scan_dir);
    if (dir != NULL) {
        int broken = 0;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
                continue;
            }
            consider_file(scan_dir, ent->d_name, index->active, &load->profile, &broken, load->profile_path,
                          sizeof(load->profile_path));
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

int pav_profile_check_engine(const PavProfile *profile, char *err, size_t err_len) {
    const char *engine;

    if (profile == NULL) {
        cfg_err(err, err_len, "missing profile");
        return -1;
    }
    engine = profile->engine[0] == '\0' ? "sqlite" : profile->engine;
    if (strcmp(engine, "sqlite") == 0) {
        return 0;
    }
    cfg_err(err, err_len, "unknown engine %s", engine);
    return -1;
}

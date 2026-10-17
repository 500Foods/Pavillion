#define _POSIX_C_SOURCE 200809L

#include "themes.h"

#include "pav_cfg.h"
#include "pav_config.h"
#include "pav_platform.h"
#include "pavillion.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define THEME_NAME_MAX 200
#define THEME_FILE_MAX 128
#define THEME_TEXT_MAX (256u * 1024u)
#define THEME_ASSET_MAX (2u * 1024u * 1024u)
#define FOLDER_MAX 1024

typedef struct NameBag {
    char (*names)[THEME_NAME_MAX + 1];
    int count;
} NameBag;

static int only_keys(json_t *obj, const char *a, const char *b, PavError *err) {
    const char *key;
    json_t *value;
    char buf[512];

    if (!json_is_object(obj)) {
        pav_error_set(err, "bad_args", "arguments are not an object", 0);
        return -1;
    }
    json_object_foreach(obj, key, value) {
        (void)value;
        if ((a != NULL && strcmp(key, a) == 0) || (b != NULL && strcmp(key, b) == 0)) {
            continue;
        }
        snprintf(buf, sizeof(buf), "unknown argument %s", key);
        pav_error_set(err, "bad_args", buf, 0);
        return -1;
    }
    return 0;
}

static int theme_name_ok(const char *name) {
    size_t n;
    size_t i;

    if (name == NULL) {
        return 0;
    }
    n = strlen(name);
    if (n < 7 || n > THEME_NAME_MAX || name[0] == '.') {
        return 0;
    }
    if (strcmp(name + (n - 6), ".theme") != 0) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)name[i];

        if (c < 0x20 || c == 0x7f || c == '/' || c == '\\' || c == ':') {
            return 0;
        }
    }
    return 1;
}

static void *read_limit(const char *path, size_t cap, size_t *out_n, int require_text) {
    FILE *file;
    long n;
    char *buf;
    size_t got;

    file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    n = ftell(file);
    if (n < 0 || (size_t)n > cap || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    buf = malloc((size_t)n + 1);
    if (buf == NULL) {
        fclose(file);
        return NULL;
    }
    got = n > 0 ? fread(buf, 1, (size_t)n, file) : 0;
    fclose(file);
    if (got != (size_t)n) {
        free(buf);
        return NULL;
    }
    buf[n] = '\0';
    if (require_text && strlen(buf) != (size_t)n) {
        free(buf);
        return NULL;
    }
    if (out_n != NULL) {
        *out_n = (size_t)n;
    }
    return buf;
}

static int workdir_ready(PavError *err) {
    const char *dir = pav_core_workdir();

    if (dir == NULL || dir[0] == '\0') {
        pav_error_set(err, "not_ready", "not running", 0);
        return -1;
    }
    return 0;
}

static int load_index(PavIndex **out, PavError *err) {
    char path[4096];
    char *text;
    char cfgerr[512];

    if (workdir_ready(err) != 0) {
        return -1;
    }
    if (pav_path_join(path, sizeof(path), pav_core_workdir(), "Pavillion.json") != 0) {
        pav_error_set(err, "internal", "path is too long", 0);
        return -1;
    }
    text = read_limit(path, 1024u * 1024u, NULL, 1);
    if (text == NULL) {
        pav_error_set(err, "internal", "cannot read index", 0);
        return -1;
    }
    cfgerr[0] = '\0';
    if (pav_index_parse(text, out, cfgerr, sizeof(cfgerr)) != 0) {
        free(text);
        pav_error_set(err, "internal", cfgerr[0] != '\0' ? cfgerr : "cannot read index", 0);
        return -1;
    }
    free(text);
    return 0;
}

static int write_index(PavIndex *index, PavError *err) {
    char path[4096];
    char *rewritten = NULL;
    const PavPlatform *platform;

    if (pav_path_join(path, sizeof(path), pav_core_workdir(), "Pavillion.json") != 0) {
        pav_error_set(err, "internal", "path is too long", 0);
        return -1;
    }
    if (pav_index_rewrite(index, &rewritten) != 0 || rewritten == NULL) {
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    platform = pav_platform();
    if (platform == NULL || platform->write_private == NULL ||
        platform->write_private(path, rewritten, strlen(rewritten)) != 0) {
        free(rewritten);
        pav_error_set(err, "internal", "cannot write index", 0);
        return -1;
    }
    free(rewritten);
    return 0;
}

/* *missing is 1 when the directory cannot be read. out still receives the
   path that was named. An empty themes_folder means the working directory.
   chosen, when not NULL, receives the stored string, or "" when unset. */
static int themes_dir(char *out, size_t len, int *missing, char *chosen, size_t chosen_len, PavError *err) {
    PavIndex *index = NULL;
    json_t *value;
    const char *stored = NULL;

    if (chosen != NULL && chosen_len > 0) {
        chosen[0] = '\0';
    }
    if (load_index(&index, err) != 0) {
        return -1;
    }
    value = json_object_get(index->doc, "themes_folder");
    if (json_is_string(value)) {
        stored = json_string_value(value);
    }
    if (stored != NULL && stored[0] != '\0' && chosen != NULL) {
        if (strlen(stored) + 1 > chosen_len) {
            pav_index_free(index);
            pav_error_set(err, "internal", "path is too long", 0);
            return -1;
        }
        memcpy(chosen, stored, strlen(stored) + 1);
    }
    if (stored == NULL || stored[0] == '\0') {
        if (strlen(pav_core_workdir()) + 1 > len) {
            pav_index_free(index);
            pav_error_set(err, "internal", "path is too long", 0);
            return -1;
        }
        memcpy(out, pav_core_workdir(), strlen(pav_core_workdir()) + 1);
    } else if (pav_path_is_absolute(stored)) {
        if (strlen(stored) + 1 > len) {
            pav_index_free(index);
            pav_error_set(err, "internal", "path is too long", 0);
            return -1;
        }
        memcpy(out, stored, strlen(stored) + 1);
    } else if (pav_path_join(out, len, pav_core_workdir(), stored) != 0) {
        pav_index_free(index);
        pav_error_set(err, "internal", "path is too long", 0);
        return -1;
    }
    pav_index_free(index);
    *missing = pav_is_dir(out) ? 0 : 1;
    return 0;
}

static int collect_name(const char *name, void *user) {
    NameBag *bag = user;

    if (!theme_name_ok(name)) {
        return 0;
    }
    if (bag->count >= THEME_FILE_MAX) {
        return 0;
    }
    memcpy(bag->names[bag->count], name, strlen(name) + 1);
    bag->count++;
    return 0;
}

static int name_cmp(const void *a, const void *b) {
    return strcmp(a, b);
}

static const char *asset_type(const char *name) {
    const char *dot = strrchr(name, '.');
    char ext[8];
    size_t n;
    size_t i;

    if (dot == NULL) {
        return NULL;
    }
    n = strlen(dot);
    if (n >= sizeof(ext)) {
        return NULL;
    }
    for (i = 0; i < n; i++) {
        ext[i] = (char)tolower((unsigned char)dot[i]);
    }
    ext[n] = '\0';
    if (strcmp(ext, ".svg") == 0) {
        return "image/svg+xml";
    }
    if (strcmp(ext, ".png") == 0) {
        return "image/png";
    }
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) {
        return "image/jpeg";
    }
    if (strcmp(ext, ".gif") == 0) {
        return "image/gif";
    }
    if (strcmp(ext, ".webp") == 0) {
        return "image/webp";
    }
    if (strcmp(ext, ".woff") == 0) {
        return "font/woff";
    }
    if (strcmp(ext, ".woff2") == 0) {
        return "font/woff2";
    }
    if (strcmp(ext, ".ttf") == 0) {
        return "font/ttf";
    }
    if (strcmp(ext, ".otf") == 0) {
        return "font/otf";
    }
    return NULL;
}

static int asset_from_url(const char *url, char *out, size_t cap) {
    const char *rest;
    size_t n;
    size_t i;

    if (url == NULL || strncmp(url, "pav-theme:", 10) != 0) {
        return -1;
    }
    rest = url + 10;
    while (*rest == '/') {
        rest++;
    }
    if (strncmp(rest, "localhost/", 10) == 0) {
        rest += 10;
    }
    while (*rest == '/') {
        rest++;
    }
    n = strlen(rest);
    if (n == 0 || n >= cap || strchr(rest, '?') != NULL || strchr(rest, '#') != NULL) {
        return -1;
    }
    if (strchr(rest, '/') != NULL || strchr(rest, '\\') != NULL || strstr(rest, "..") != NULL) {
        return -1;
    }
    if (rest[0] == '.' || asset_type(rest) == NULL) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)rest[i];

        if (c < 0x20 || c == 0x7f || c == '%') {
            return -1;
        }
    }
    memcpy(out, rest, n + 1);
    return 0;
}

int pav_themes_list(json_t *args, json_t **data, PavError *err) {
    char dir[4096];
    char chosen[4096];
    int missing = 0;
    NameBag bag;
    json_t *obj;
    json_t *themes;
    int i;

    if (data == NULL) {
        pav_error_set(err, "internal", "missing data", 0);
        return -1;
    }
    *data = NULL;
    if (only_keys(args, NULL, NULL, err) != 0) {
        return -1;
    }
    chosen[0] = '\0';
    if (themes_dir(dir, sizeof(dir), &missing, chosen, sizeof(chosen), err) != 0) {
        return -1;
    }
    obj = json_object();
    themes = json_array();
    if (obj == NULL || themes == NULL) {
        json_decref(obj);
        json_decref(themes);
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    if (json_object_set_new(obj, "folder", json_string(dir)) != 0 ||
        json_object_set_new(obj, "themes", themes) != 0) {
        json_decref(obj);
        json_decref(themes);
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    if (json_object_set_new(obj, "chosen", json_string(chosen)) != 0 ||
        json_object_set_new(obj, "fallback", json_string(pav_core_workdir())) != 0) {
        json_decref(obj);
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    if (missing) {
        if (json_object_set_new(obj, "missing", json_true()) != 0) {
            json_decref(obj);
            pav_error_set(err, "internal", "out of memory", 0);
            return -1;
        }
        *data = obj;
        return 0;
    }
    bag.names = calloc(THEME_FILE_MAX, sizeof(*bag.names));
    bag.count = 0;
    if (bag.names == NULL) {
        json_decref(obj);
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    if (pav_dir_each(dir, collect_name, &bag) != 0) {
        free(bag.names);
        if (json_object_set_new(obj, "missing", json_true()) != 0) {
            json_decref(obj);
            pav_error_set(err, "internal", "out of memory", 0);
            return -1;
        }
        *data = obj;
        return 0;
    }
    qsort(bag.names, (size_t)bag.count, sizeof(*bag.names), name_cmp);
    for (i = 0; i < bag.count; i++) {
        char path[4096];
        char *text;
        json_t *row;
        json_t *name;
        json_t *body;

        if (pav_path_join(path, sizeof(path), dir, bag.names[i]) != 0) {
            continue;
        }
        text = read_limit(path, THEME_TEXT_MAX, NULL, 1);
        if (text == NULL) {
            continue;
        }
        row = json_object();
        name = json_string(bag.names[i]);
        body = json_string(text);
        free(text);
        if (row == NULL || name == NULL || body == NULL) {
            json_decref(row);
            json_decref(name);
            json_decref(body);
            continue;
        }
        if (json_object_set_new(row, "name", name) != 0) {
            json_decref(body);
            json_decref(row);
            free(bag.names);
            json_decref(obj);
            pav_error_set(err, "internal", "out of memory", 0);
            return -1;
        }
        if (json_object_set_new(row, "text", body) != 0) {
            json_decref(row);
            free(bag.names);
            json_decref(obj);
            pav_error_set(err, "internal", "out of memory", 0);
            return -1;
        }
        if (json_array_append_new(themes, row) != 0) {
            free(bag.names);
            json_decref(obj);
            pav_error_set(err, "internal", "out of memory", 0);
            return -1;
        }
    }
    free(bag.names);
    *data = obj;
    return 0;
}

int pav_themes_ensure(json_t *args, json_t **data, PavError *err) {
    json_t *files;
    json_t *obj;
    json_t *wrote;
    char dir[4096];
    int missing = 0;
    size_t i;
    size_t n;

    if (data == NULL) {
        pav_error_set(err, "internal", "missing data", 0);
        return -1;
    }
    *data = NULL;
    if (only_keys(args, "files", NULL, err) != 0) {
        return -1;
    }
    files = json_object_get(args, "files");
    if (!json_is_array(files)) {
        pav_error_set(err, "bad_args", "files is required", 0);
        return -1;
    }
    n = json_array_size(files);
    for (i = 0; i < n; i++) {
        json_t *row = json_array_get(files, i);
        const char *name;
        const char *text;

        if (!json_is_object(row) || only_keys(row, "name", "text", err) != 0) {
            if (err != NULL && err->code[0] == '\0') {
                pav_error_set(err, "bad_args", "theme file is not an object", 0);
            }
            return -1;
        }
        name = json_string_value(json_object_get(row, "name"));
        text = json_string_value(json_object_get(row, "text"));
        if (!theme_name_ok(name)) {
            pav_error_set(err, "bad_args", "bad theme name", 0);
            return -1;
        }
        if (text == NULL) {
            pav_error_set(err, "bad_args", "theme text is required", 0);
            return -1;
        }
        if (strlen(text) > THEME_TEXT_MAX) {
            pav_error_set(err, "bad_args", "theme file is too large", 0);
            return -1;
        }
    }
    if (themes_dir(dir, sizeof(dir), &missing, NULL, 0, err) != 0) {
        return -1;
    }
    obj = json_object();
    wrote = json_array();
    if (obj == NULL || wrote == NULL || json_object_set_new(obj, "wrote", wrote) != 0) {
        json_decref(obj);
        json_decref(wrote);
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    if (missing) {
        *data = obj;
        return 0;
    }
    for (i = 0; i < n; i++) {
        json_t *row = json_array_get(files, i);
        const char *name = json_string_value(json_object_get(row, "name"));
        const char *text = json_string_value(json_object_get(row, "text"));
        char path[4096];
        const PavPlatform *platform;

        if (pav_path_join(path, sizeof(path), dir, name) != 0) {
            json_decref(obj);
            pav_error_set(err, "internal", "path is too long", 0);
            return -1;
        }
        if (pav_exists(path)) {
            continue;
        }
        platform = pav_platform();
        if (platform == NULL || platform->write_private == NULL ||
            platform->write_private(path, text, strlen(text)) != 0) {
            json_decref(obj);
            pav_error_set(err, "internal", "cannot write theme", 0);
            return -1;
        }
        if (json_array_append_new(wrote, json_string(name)) != 0) {
            json_decref(obj);
            pav_error_set(err, "internal", "out of memory", 0);
            return -1;
        }
    }
    *data = obj;
    return 0;
}

static int folder_text_ok(const char *folder) {
    size_t i;
    size_t n;

    if (folder == NULL) {
        return 0;
    }
    n = strlen(folder);
    if (n > FOLDER_MAX) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)folder[i];

        if (c < 0x20 || c == 0x7f) {
            return 0;
        }
    }
    return 1;
}

int pav_themes_folder_set(json_t *args, json_t **data, PavError *err) {
    const char *folder;
    char resolved[4096];
    PavIndex *index = NULL;
    json_t *obj;

    if (data == NULL) {
        pav_error_set(err, "internal", "missing data", 0);
        return -1;
    }
    *data = NULL;
    if (only_keys(args, "folder", NULL, err) != 0) {
        return -1;
    }
    folder = json_string_value(json_object_get(args, "folder"));
    if (!folder_text_ok(folder)) {
        pav_error_set(err, "bad_args", "folder is required", 0);
        return -1;
    }
    if (workdir_ready(err) != 0) {
        return -1;
    }
    if (folder[0] == '\0') {
        memcpy(resolved, pav_core_workdir(), strlen(pav_core_workdir()) + 1);
    } else if (pav_path_is_absolute(folder)) {
        if (strlen(folder) + 1 > sizeof(resolved)) {
            pav_error_set(err, "bad_args", "path is too long", 0);
            return -1;
        }
        memcpy(resolved, folder, strlen(folder) + 1);
    } else if (pav_path_join(resolved, sizeof(resolved), pav_core_workdir(), folder) != 0) {
        pav_error_set(err, "bad_args", "path is too long", 0);
        return -1;
    }
    if (folder[0] != '\0' && !pav_is_dir(resolved)) {
        pav_error_set(err, "bad_args", "themes folder is not a directory", 0);
        return -1;
    }
    if (load_index(&index, err) != 0) {
        return -1;
    }
    if (folder[0] == '\0') {
        (void)json_object_del(index->doc, "themes_folder");
    } else {
        char cfgerr[512];

        cfgerr[0] = '\0';
        if (cfg_set_str(index->doc, "themes_folder", folder, cfgerr, sizeof(cfgerr)) != 0) {
            pav_index_free(index);
            pav_error_set(err, "internal", cfgerr[0] != '\0' ? cfgerr : "cannot store folder", 0);
            return -1;
        }
    }
    if (write_index(index, err) != 0) {
        pav_index_free(index);
        return -1;
    }
    pav_index_free(index);
    obj = json_object();
    if (obj == NULL || json_object_set_new(obj, "folder", json_string(resolved)) != 0) {
        json_decref(obj);
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    *data = obj;
    return 0;
}

int pav_theme_read_asset(const char *url, void **body, size_t *len, const char **type) {
    char name[THEME_NAME_MAX + 1];
    char dir[4096];
    char path[4096];
    int missing = 0;
    PavError err;
    struct stat st;
    const char *kind;
    void *buf;
    size_t n = 0;

    if (body == NULL || len == NULL || type == NULL) {
        return -1;
    }
    *body = NULL;
    *len = 0;
    *type = NULL;
    if (asset_from_url(url, name, sizeof(name)) != 0) {
        return -1;
    }
    kind = asset_type(name);
    if (kind == NULL) {
        return -1;
    }
    memset(&err, 0, sizeof(err));
    if (themes_dir(dir, sizeof(dir), &missing, NULL, 0, &err) != 0 || missing) {
        return -1;
    }
    if (pav_path_join(path, sizeof(path), dir, name) != 0) {
        return -1;
    }
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        (size_t)st.st_size > THEME_ASSET_MAX) {
        return -1;
    }
    buf = read_limit(path, THEME_ASSET_MAX, &n, 0);
    if (buf == NULL) {
        return -1;
    }
    *body = buf;
    *len = n;
    *type = kind;
    return 0;
}

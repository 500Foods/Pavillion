#include "pav_icon.h"

#include "pav_cfg.h"

#include <string.h>

static const char *const k_script_hosts[] = {"kit.fontawesome.com", NULL};
static const char *const k_style_hosts[] = {"fonts.googleapis.com", NULL};

static int host_listed(const char *host, const char *const *list) {
    size_t i;

    for (i = 0; list[i] != NULL; i++) {
        if (strcmp(host, list[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static void lower_copy(char *dst, const char *src, size_t n) {
    size_t i;

    for (i = 0; i < n; i++) {
        char c = src[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        dst[i] = c;
    }
    dst[n] = '\0';
}

/* 0 and writes the host. -1 when the URL is not https, or the authority
   carries credentials, a space, or a backslash. An @ after ? or # is kept
   (the Material preset uses font-axis syntax in the query). */
static int https_host(const char *url, char *host, size_t host_cap) {
    const char *rest;
    const char *end;
    const char *colon;
    size_t n;

    if (url == NULL || strncmp(url, "https://", 8) != 0) {
        return -1;
    }
    rest = url + 8;
    if (*rest == '\0' || strchr(rest, ' ') != NULL || strchr(rest, '\\') != NULL) {
        return -1;
    }
    end = rest;
    while (*end != '\0' && *end != '/' && *end != '?' && *end != '#') {
        end++;
    }
    if (end == rest || memchr(rest, '@', (size_t)(end - rest)) != NULL) {
        return -1;
    }
    colon = NULL;
    for (const char *p = rest; p < end; p++) {
        if (*p == ':') {
            colon = p;
        }
    }
    n = (size_t)((colon != NULL ? colon : end) - rest);
    if (n == 0 || n + 1 > host_cap) {
        return -1;
    }
    if (colon != NULL) {
        const char *port = colon + 1;
        if (port == end) {
            return -1;
        }
        for (; port < end; port++) {
            if (*port < '0' || *port > '9') {
                return -1;
            }
        }
    }
    lower_copy(host, rest, n);
    return 0;
}

static int id_ok(const char *id) {
    size_t i;
    size_t n;

    if (id == NULL || id[0] < 'a' || id[0] > 'z') {
        return 0;
    }
    n = strlen(id);
    if (n > 32) {
        return 0;
    }
    for (i = 1; i < n; i++) {
        char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
            return 0;
        }
    }
    return 1;
}

int pav_icon_libraries_check(const json_t *ui, char *err, size_t err_len) {
    json_t *list;
    json_t *entry;
    size_t i;
    size_t n;

    if (!json_is_object(ui)) {
        return 0;
    }
    list = json_object_get(ui, "iconLibraries");
    if (list == NULL) {
        return 0;
    }
    if (!json_is_array(list)) {
        cfg_err(err, err_len, "icon libraries must be a list");
        return -1;
    }
    n = json_array_size(list);
    if (n > 16) {
        cfg_err(err, err_len, "at most 16 icon libraries");
        return -1;
    }
    for (i = 0; i < n; i++) {
        const char *id;
        const char *name;
        const char *type;
        const char *url;
        char host[256];
        size_t j;
        const char *const *allowed;

        entry = json_array_get(list, i);
        if (!json_is_object(entry)) {
            cfg_err(err, err_len, "an icon library must be an object");
            return -1;
        }
        id = json_string_value(json_object_get(entry, "id"));
        name = json_string_value(json_object_get(entry, "name"));
        type = json_string_value(json_object_get(entry, "type"));
        url = json_string_value(json_object_get(entry, "url"));
        if (!id_ok(id)) {
            cfg_err(err, err_len, "an icon library id must be a short lowercase name");
            return -1;
        }
        for (j = 0; j < i; j++) {
            const char *other = json_string_value(json_object_get(json_array_get(list, j), "id"));
            if (other != NULL && strcmp(other, id) == 0) {
                cfg_err(err, err_len, "an icon library id is repeated");
                return -1;
            }
        }
        if (name == NULL || name[0] == '\0' || strlen(name) > 64 || strpbrk(name, "\r\n") != NULL) {
            cfg_err(err, err_len, "an icon library needs a name");
            return -1;
        }
        if (!json_is_boolean(json_object_get(entry, "enabled"))) {
            cfg_err(err, err_len, "enabled must be true or false");
            return -1;
        }
        if (type == NULL || (strcmp(type, "script") != 0 && strcmp(type, "stylesheet") != 0)) {
            cfg_err(err, err_len, "type must be script or stylesheet");
            return -1;
        }
        if (https_host(url, host, sizeof(host)) != 0) {
            cfg_err(err, err_len, "the URL must be https and must not carry credentials");
            return -1;
        }
        allowed = strcmp(type, "script") == 0 ? k_script_hosts : k_style_hosts;
        if (!host_listed(host, allowed)) {
            cfg_err(err, err_len, "a new host needs a rebuild (%s is not allowlisted)", host);
            return -1;
        }
    }
    return 0;
}

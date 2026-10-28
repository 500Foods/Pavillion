#include "pavillion.h"
#include "pav_icon.h"
#include "themes.h"

#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fill_bytes(PavBytes *out, int status, const char *content_type, const void *body, size_t n) {
    char headers[160];
    int wrote;

    wrote = snprintf(headers, sizeof(headers), "{\"Content-Type\":\"%s\",\"Cache-Control\":\"no-store\"}",
                     content_type != NULL ? content_type : "text/plain");
    if (wrote < 0 || (size_t)wrote >= sizeof(headers)) {
        return -1;
    }
    out->status = status;
    out->headers_json = malloc((size_t)wrote + 1);
    out->body = n > 0 ? malloc(n) : NULL;
    if (out->headers_json == NULL || (n > 0 && out->body == NULL)) {
        free(out->headers_json);
        free(out->body);
        out->headers_json = NULL;
        out->body = NULL;
        out->body_len = 0;
        return -1;
    }
    memcpy(out->headers_json, headers, (size_t)wrote + 1);
    if (n > 0) {
        memcpy(out->body, body, n);
    }
    out->body_len = n;
    return 0;
}

static int fill_status(PavBytes *out, int status, const char *body) {
    const char *headers = "{\"Content-Type\":\"text/plain\",\"Cache-Control\":\"no-store\"}";
    size_t n = body != NULL ? strlen(body) : 0;

    out->status = status;
    out->headers_json = malloc(strlen(headers) + 1);
    out->body = n > 0 ? malloc(n) : NULL;
    if (out->headers_json == NULL || (n > 0 && out->body == NULL)) {
        free(out->headers_json);
        free(out->body);
        out->headers_json = NULL;
        out->body = NULL;
        out->body_len = 0;
        return -1;
    }
    memcpy(out->headers_json, headers, strlen(headers) + 1);
    if (n > 0) {
        memcpy(out->body, body, n);
    }
    out->body_len = n;
    return 0;
}

static int handle_pav_blob(json_t *root, PavBytes *out) {
    (void)root;
    return fill_status(out, 404, "not found");
}

/* pav-link://<paneId>/<n>. paneId is 1..64 of [A-Za-z0-9_-]. n is a decimal
   integer from 0 through 2^53-1. Anything else is 404 and emits nothing. */
static int pane_char(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '-';
}

static int parse_pav_link(const char *url, char *pane, size_t pane_cap, unsigned long long *n_out) {
    static const char prefix[] = "pav-link://";
    const unsigned long long max_n = 9007199254740991ULL;
    const char *rest;
    const char *slash;
    const char *digits;
    size_t pane_len;
    size_t i;
    unsigned long long n = 0;

    if (url == NULL || strncmp(url, prefix, sizeof(prefix) - 1) != 0) {
        return -1;
    }
    rest = url + (sizeof(prefix) - 1);
    slash = strchr(rest, '/');
    if (slash == NULL || slash == rest) {
        return -1;
    }
    pane_len = (size_t)(slash - rest);
    if (pane_len < 1 || pane_len > 64 || pane_len + 1 > pane_cap) {
        return -1;
    }
    for (i = 0; i < pane_len; i++) {
        if (!pane_char(rest[i])) {
            return -1;
        }
    }
    digits = slash + 1;
    if (digits[0] == '\0') {
        return -1;
    }
    for (; *digits != '\0'; digits++) {
        unsigned digit;

        if (*digits < '0' || *digits > '9') {
            return -1;
        }
        digit = (unsigned)(*digits - '0');
        if (n > (max_n - digit) / 10ULL) {
            return -1;
        }
        n = n * 10ULL + digit;
    }
    memcpy(pane, rest, pane_len);
    pane[pane_len] = '\0';
    *n_out = n;
    return 0;
}

static int handle_pav_theme(json_t *root, PavBytes *out) {
    const char *url = json_string_value(json_object_get(root, "url"));
    void *body = NULL;
    size_t len = 0;
    const char *type = NULL;
    int rc;

    if (pav_theme_read_asset(url, &body, &len, &type) != 0) {
        return fill_status(out, 404, "not found");
    }
    rc = fill_bytes(out, 200, type, body, len);
    free(body);
    return rc;
}

static int handle_pav_asset(json_t *root, PavBytes *out) {
    const char *url = json_string_value(json_object_get(root, "url"));
    void *body = NULL;
    size_t len = 0;
    const char *type = NULL;
    int rc;

    if (pav_icon_read_asset(url, &body, &len, &type) != 0) {
        return fill_status(out, 404, "not found");
    }
    rc = fill_bytes(out, 200, type, body, len);
    free(body);
    return rc;
}

static int handle_pav_system(json_t *root, PavBytes *out) {
    const char *url = json_string_value(json_object_get(root, "url"));
    const void *body = NULL;
    size_t len = 0;
    const char *type = NULL;

    if (pav_icon_read_system(url, &body, &len, &type) != 0) {
        return fill_status(out, 404, "not found");
    }
    return fill_bytes(out, 200, type, body, len);
}

static int handle_pav_link(json_t *root, PavBytes *out) {
    const char *url = json_string_value(json_object_get(root, "url"));
    char pane[65];
    char payload[160];
    unsigned long long n = 0;
    int wrote;

    if (parse_pav_link(url, pane, sizeof(pane), &n) != 0) {
        return fill_status(out, 404, "not found");
    }
    wrote = snprintf(payload, sizeof(payload), "{\"paneId\":\"%s\",\"n\":%llu}", pane, n);
    if (wrote < 0 || (size_t)wrote >= sizeof(payload)) {
        return fill_status(out, 404, "not found");
    }
    (void)pav_event_post("link.clicked", payload);
    return fill_status(out, 204, NULL);
}

int pav_protocol(const char *req_json, PavBytes *out) {
    json_error_t jerr;
    json_t *root;
    const char *scheme;
    int rc;

    if (out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    root = req_json != NULL ? json_loads(req_json, 0, &jerr) : NULL;
    if (!json_is_object(root)) {
        json_decref(root);
        return fill_status(out, 404, "not found");
    }
    scheme = json_string_value(json_object_get(root, "scheme"));
    if (scheme != NULL && strcmp(scheme, "pav-blob") == 0) {
        rc = handle_pav_blob(root, out);
    } else if (scheme != NULL && strcmp(scheme, "pav-link") == 0) {
        rc = handle_pav_link(root, out);
    } else if (scheme != NULL && strcmp(scheme, "pav-theme") == 0) {
        rc = handle_pav_theme(root, out);
    } else if (scheme != NULL && strcmp(scheme, "pav-asset") == 0) {
        rc = handle_pav_asset(root, out);
    } else if (scheme != NULL && strcmp(scheme, "pav-system") == 0) {
        rc = handle_pav_system(root, out);
    } else {
        rc = fill_status(out, 404, "not found");
    }
    json_decref(root);
    return rc;
}

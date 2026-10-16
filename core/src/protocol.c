#include "pavillion.h"

#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    } else {
        rc = fill_status(out, 404, "not found");
    }
    json_decref(root);
    return rc;
}

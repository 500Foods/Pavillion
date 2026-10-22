#define _GNU_SOURCE

#include "pav_secret.h"
#include "dispatch.h"
#include "pavillion.h"

#include <jansson.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *secret_name(int code) {
    switch (code) {
    case 0:
        return "ok";
    case PAV_SECRET_MISSING:
        return "missing";
    case PAV_SECRET_LOCKED:
        return "locked";
    default:
        return "failed";
    }
}

void pav_secret_wipe(void *p, size_t n) {
    volatile unsigned char *q = p;

    if (p == NULL) {
        return;
    }
    while (n-- > 0) {
        *q++ = 0;
    }
}

void pav_secret_wipe_json(json_t *obj, const char *key) {
    json_t *value = json_is_object(obj) ? json_object_get(obj, key) : NULL;

    if (json_is_string(value)) {
        /* jansson owns the buffer; writing zeros in place keeps its length. */
        pav_secret_wipe((void *)json_string_value(value), json_string_length(value));
    }
}

void pav_secret_free(char *value) {
    if (value == NULL) {
        return;
    }
    {
        /* Portable explicit_bzero: the volatile stores are not optimised away. */
        volatile char *p = value;
        size_t n = strlen(value);

        while (n-- > 0) {
            *p++ = 0;
        }
    }
    free(value);
}

int pav_secret_get(const char *service, const char *user, char **out) {
    const PavPlatform *platform = pav_platform();
    int code;

    if (out != NULL) {
        *out = NULL;
    }
    if (platform == NULL || platform->secret_get == NULL || service == NULL || user == NULL || out == NULL) {
        pav_log("secret", PAV_WARN, 1, "secret %s", "failed");
        return PAV_SECRET_FAILED;
    }
    code = platform->secret_get(service, user, out);
    if (code != 0) {
        if (out != NULL && *out != NULL) {
            pav_secret_free(*out);
            *out = NULL;
        }
        pav_log("secret", PAV_WARN, 1, "secret %s", secret_name(code));
    } else if (platform->secret_resaved != NULL && platform->secret_resaved()) {
        pav_log("secret", PAV_INFO, 0, "snapshot re-saved with work factor 0");
    }
    return code;
}

int pav_secret_set(const char *service, const char *user, const char *label, const char *val) {
    const PavPlatform *platform = pav_platform();
    int code;

    if (platform == NULL || platform->secret_set == NULL || service == NULL || user == NULL || label == NULL ||
        val == NULL) {
        pav_log("secret", PAV_WARN, 1, "secret %s", "failed");
        return PAV_SECRET_FAILED;
    }
    code = platform->secret_set(service, user, label, val);
    if (code != 0) {
        pav_log("secret", PAV_WARN, 1, "secret %s", secret_name(code));
    } else {
        pav_log("secret", PAV_DEBUG, 0, "secret set ok");
    }
    return code;
}

int pav_secret_delete(const char *service, const char *user) {
    const PavPlatform *platform = pav_platform();
    int code;

    if (platform == NULL || platform->secret_delete == NULL || service == NULL || user == NULL) {
        pav_log("secret", PAV_WARN, 1, "secret %s", "failed");
        return PAV_SECRET_FAILED;
    }
    code = platform->secret_delete(service, user);
    if (code != 0) {
        pav_log("secret", PAV_WARN, 1, "secret %s", secret_name(code));
    } else {
        pav_log("secret", PAV_DEBUG, 0, "secret delete ok");
    }
    return code;
}

#ifndef NDEBUG
int pav_cmd_secret_smoke(json_t *args, json_t **data, PavError *err) {
    const char *op = json_string_value(json_object_get(args, "op"));
    const char *user = json_string_value(json_object_get(args, "user"));
    const char *label = json_string_value(json_object_get(args, "label"));
    const char *value = json_string_value(json_object_get(args, "value"));
    const char *expect = json_string_value(json_object_get(args, "expect"));
    char *got = NULL;
    int code;
    int match = 0;

    if (op == NULL || user == NULL) {
        pav_error_set(err, "bad_args", "op and user are required", 0);
        return -1;
    }
    if (strcmp(op, "set") == 0) {
        if (label == NULL || value == NULL) {
            pav_error_set(err, "bad_args", "set needs label and value", 0);
            return -1;
        }
        code = pav_secret_set("pavillion", user, label, value);
    } else if (strcmp(op, "get") == 0) {
        code = pav_secret_get("pavillion", user, &got);
        if (code == 0 && expect != NULL && got != NULL && strcmp(got, expect) == 0) {
            match = 1;
        }
        pav_secret_free(got);
    } else if (strcmp(op, "delete") == 0) {
        code = pav_secret_delete("pavillion", user);
    } else {
        pav_error_set(err, "bad_args", "op must be set, get, or delete", 0);
        return -1;
    }
    if (code == PAV_SECRET_LOCKED) {
        pav_error_set(err, "locked", "keyring is locked", 0);
        return -1;
    }
    if (code == PAV_SECRET_FAILED) {
        pav_error_set(err, "failed", "secret store failed", 0);
        return -1;
    }
    *data = json_object();
    if (*data == NULL) {
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    if (strcmp(op, "get") == 0) {
        json_object_set_new(*data, "state", json_string(code == 0 ? "stored" : "missing"));
        json_object_set_new(*data, "match", match ? json_true() : json_false());
    } else {
        json_object_set_new(*data, "state", json_string(code == 0 ? "ok" : "missing"));
    }
    return 0;
}
#endif

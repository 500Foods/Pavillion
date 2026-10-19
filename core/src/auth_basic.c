#define _POSIX_C_SOURCE 200809L

#include "pav_auth.h"
#include "pav_cfg.h"
#include "pavillion.h"

#include <jansson.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char k_auth_scheme_basic[] = "Basic";

static const char *k_b64_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void b64_encode(char *out, const unsigned char *in, size_t len) {
    size_t i;
    size_t o;
    unsigned char b0;
    unsigned char b1;
    unsigned char b2;

    o = 0;
    for (i = 0; i + 2 < len; i += 3) {
        b0 = in[i];
        b1 = in[i + 1];
        b2 = in[i + 2];
        out[o++] = k_b64_chars[b0 >> 2];
        out[o++] = k_b64_chars[((b0 & 0x03) << 4) | (b1 >> 4)];
        out[o++] = k_b64_chars[((b1 & 0x0f) << 2) | (b2 >> 6)];
        out[o++] = k_b64_chars[b2 & 0x3f];
    }
    if (i < len) {
        b0 = in[i];
        out[o++] = k_b64_chars[b0 >> 2];
        if (i + 1 < len) {
            b1 = in[i + 1];
            out[o++] = k_b64_chars[((b0 & 0x03) << 4) | (b1 >> 4)];
            out[o++] = k_b64_chars[(b1 & 0x0f) << 2];
            out[o++] = '=';
        } else {
            out[o++] = k_b64_chars[(b0 & 0x03) << 4];
            out[o++] = '=';
            out[o++] = '=';
        }
    }
    out[o] = '\0';
}

static size_t b64_encoded_len(size_t len) {
    size_t blocks = len / 3;
    size_t rem = len % 3;
    size_t out = blocks * 4;

    if (rem == 1) {
        out += 4;
    } else if (rem == 2) {
        out += 4;
    }
    return out;
}

static int auth_basic_apply(const PavAccount *account, PavReq *req, PavSecret *secret) {
    char *raw = NULL;
    char *encoded = NULL;
    char *header = NULL;
    size_t ulen;
    size_t slen;
    size_t raw_len;
    size_t enc_len;
    size_t header_len;

    if (account == NULL || req == NULL || secret == NULL || account->username == NULL ||
        secret->val == NULL) {
        pav_log("auth", PAV_ERROR, 0, "apply: missing fields");
        return -1;
    }
    ulen = strlen(account->username);
    slen = secret->len > 0 ? secret->len : strlen(secret->val);
    raw_len = ulen + 1 + slen;
    raw = malloc(raw_len + 1);
    if (raw == NULL) {
        return -1;
    }
    memcpy(raw, account->username, ulen);
    raw[ulen] = ':';
    memcpy(raw + ulen + 1, secret->val, slen);
    raw[raw_len] = '\0';

    enc_len = b64_encoded_len(raw_len);
    encoded = malloc(enc_len + 1);
    if (encoded == NULL) {
        free(raw);
        return -1;
    }
    b64_encode(encoded, (const unsigned char *)raw, raw_len);
    free(raw);

    header_len = sizeof(k_auth_scheme_basic) + 1 + enc_len;
    header = malloc(header_len);
    if (header == NULL) {
        free(encoded);
        return -1;
    }
    snprintf(header, header_len, "%s %s", k_auth_scheme_basic, encoded);
    free(encoded);

    if (req->headers == NULL) {
        req->headers = json_object();
        if (req->headers == NULL) {
            free(header);
            return -1;
        }
    }
    if (!json_is_object(req->headers) ||
        json_object_set_new(req->headers, "Authorization", json_string(header)) != 0) {
        free(header);
        return -1;
    }
    free(header);
    return 0;
}

static int auth_basic_on_unauthorized(const PavAccount *account) {
    (void)account;
    return -1;
}

static const PavAuth g_auth_basic = {
    .method = "basic",
    .apply = auth_basic_apply,
    .on_unauthorized = auth_basic_on_unauthorized,
};

const PavAuth *pav_auth_for(const PavAccount *account, char *err, size_t err_len) {
    if (account == NULL || account->auth == NULL) {
        cfg_err(err, err_len, "missing auth method");
        return NULL;
    }
    if (strcmp(account->auth, "basic") == 0) {
        return &g_auth_basic;
    }
    cfg_err(err, err_len, "unknown auth method %s", account->auth);
    return NULL;
}

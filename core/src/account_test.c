#define _POSIX_C_SOURCE 200809L

#include "account.h"
#include "dispatch.h"
#include "pav_auth.h"
#include "pav_cfg.h"
#include "pav_config.h"
#include "pav_net.h"
#include "pav_secret.h"
#include "pav_jmap.h"
#include "pav_platform.h"
#include "pavillion.h"

#include <jansson.h>

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int done;
    int status_code;
    char *body;
    size_t body_len;
    char location[2048];
} TestWait;

static void account_test_done(PavReq *req, void *user) {
    TestWait *w = (TestWait *)user;

    pthread_mutex_lock(&w->mu);
    w->done = 1;
    w->status_code = req->status_code;
    if (req->body_out != NULL) {
        w->body = strdup(req->body_out);
        w->body_len = req->body_out_len;
    }
    if (req->resp_headers != NULL) {
        json_t *loc = json_object_get(req->resp_headers, "Location");
        if (json_is_string(loc)) {
            const char *s = json_string_value(loc);
            if (s != NULL) {
                strncpy(w->location, s, sizeof(w->location) - 1);
                w->location[sizeof(w->location) - 1] = '\0';
            }
        }
    }
    pthread_cond_broadcast(&w->cv);
    pthread_mutex_unlock(&w->mu);

    if (req->resp_headers != NULL) {
        json_decref(req->resp_headers);
        req->resp_headers = NULL;
    }
    pav_net_free(req->body_out);
}

static int session_url_for(const char *email, const char *session_url,
                           char *out, size_t out_len) {
    if (session_url != NULL && session_url[0] != '\0') {
        if (strncmp(session_url, "https://", 8) != 0) {
            return -1;
        }
        strncpy(out, session_url, out_len - 1);
        out[out_len - 1] = '\0';
        return 0;
    }
    if (email == NULL || email[0] == '\0') {
        return -1;
    }
    {
        const char *at = strchr(email, '@');
        if (at == NULL) return -1;
        const char *domain = at + 1;
        size_t dlen = strlen(domain);
        if (dlen == 0 || dlen > 253) return -1;
        snprintf(out, out_len, "https://%s/.well-known/jmap", domain);
    }
    return 0;
}

static const char k_b64chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *b64_encode_basic(const char *username, const char *password) {
    size_t ulen = username ? strlen(username) : 0;
    size_t plen = password ? strlen(password) : 0;
    size_t raw_len = ulen + 1 + plen;
    char *raw = malloc(raw_len + 1);
    char *encoded;
    size_t enc_len;
    size_t i, o;

    if (raw == NULL) return NULL;
    memcpy(raw, username, ulen);
    raw[ulen] = ':';
    memcpy(raw + ulen + 1, password, plen);
    raw[raw_len] = '\0';

    enc_len = ((raw_len + 2) / 3) * 4;
    encoded = malloc(enc_len + 1);
    if (encoded == NULL) {
        free(raw);
        return NULL;
    }

    o = 0;
    for (i = 0; i + 2 < raw_len; i += 3) {
        unsigned char b0 = (unsigned char)raw[i];
        unsigned char b1 = (unsigned char)raw[i + 1];
        unsigned char b2 = (unsigned char)raw[i + 2];
        encoded[o++] = k_b64chars[b0 >> 2];
        encoded[o++] = k_b64chars[((b0 & 0x03) << 4) | (b1 >> 4)];
        encoded[o++] = k_b64chars[((b1 & 0x0f) << 2) | (b2 >> 6)];
        encoded[o++] = k_b64chars[b2 & 0x3f];
    }
    if (i < raw_len) {
        unsigned char b0 = (unsigned char)raw[i];
        encoded[o++] = k_b64chars[b0 >> 2];
        if (i + 1 < raw_len) {
            unsigned char b1 = (unsigned char)raw[i + 1];
            encoded[o++] = k_b64chars[((b0 & 0x03) << 4) | (b1 >> 4)];
            encoded[o++] = k_b64chars[(b1 & 0x0f) << 2];
            encoded[o++] = '=';
        } else {
            encoded[o++] = k_b64chars[(b0 & 0x03) << 4];
            encoded[o++] = '=';
            encoded[o++] = '=';
        }
    }
    encoded[enc_len] = '\0';
    free(raw);
    return encoded;
}

static int do_account_test(const char *initial_url, const char *username,
                           const char *secret_val, const char *ca_file,
                           json_t **data, PavError *err) {
    char current_url[2048];
    char full_url[2048];
    PavReq req;
    TestWait w;
    int rc;
    char errbuf[512];
    PavJmapSession session;
    char *encoded = NULL;
    char *auth_header = NULL;
    int started = 0;
    int redirects = 0;
    const int max_redirects = 3;

    memset(&w, 0, sizeof(w));
    pthread_mutex_init(&w.mu, NULL);
    pthread_cond_init(&w.cv, NULL);
    memset(&session, 0, sizeof(session));
    memset(current_url, 0, sizeof(current_url));
    strncpy(current_url, initial_url, sizeof(current_url) - 1);

    rc = pav_net_start();
    if (rc != 0) {
        pav_error_set(err, "internal", "cannot start net", 0);
        goto done;
    }
    started = 1;

    encoded = b64_encode_basic(username, secret_val);
    if (encoded == NULL) {
        pav_error_set(err, "internal", "out of memory", 0);
        goto done;
    }
    auth_header = malloc(7 + strlen(encoded) + 1);
    if (auth_header == NULL) {
        pav_error_set(err, "internal", "out of memory", 0);
        goto done;
    }
    snprintf(auth_header, 7 + strlen(encoded) + 1, "Basic %s", encoded);
    free(encoded);
    encoded = NULL;

    for (redirects = 0; redirects < max_redirects; redirects++) {
        w.done = 0;
        w.status_code = 0;
        w.location[0] = '\0';
        free(w.body);
        w.body = NULL;
        w.body_len = 0;

        memset(&req, 0, sizeof(req));
        req.method = "GET";
        snprintf(full_url, sizeof(full_url), "%s", current_url);
        req.url = full_url;
        req.ca_file = ca_file;
        req.on_done = account_test_done;
        req.user = &w;
        req.headers = json_object();
        if (req.headers == NULL) {
            pav_error_set(err, "internal", "out of memory", 0);
            goto done;
        }
        json_object_set_new(req.headers, "Authorization", json_string(auth_header));

        rc = pav_net_request(&req);
        if (rc != 0) {
            pav_error_set(err, "internal", "cannot queue request", 0);
            goto done;
        }

        {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 30;
            pthread_mutex_lock(&w.mu);
            while (!w.done) {
                int r = pthread_cond_timedwait(&w.cv, &w.mu, &ts);
                if (r == ETIMEDOUT) break;
            }
            pthread_mutex_unlock(&w.mu);
        }

        if (!w.done) {
            pav_error_set(err, "network", "account.test timed out", 1);
            goto done;
        }

        if (w.status_code == 302 || w.status_code == 301 ||
            w.status_code == 307 || w.status_code == 308) {
            if (req.resp_headers != NULL) {
                json_decref(req.resp_headers);
                req.resp_headers = NULL;
            }
            free(w.body);
            w.body = NULL;
            w.body_len = 0;

            if (w.location[0] == '\0') {
                char msg[256];
                snprintf(msg, sizeof(msg), "redirect %d without Location", w.status_code);
                pav_error_set(err, "server", msg, 0);
                goto done;
            }

            if (strncmp(w.location, "https://", 8) != 0) {
                pav_error_set(err, "server", "redirect target is not https", 0);
                goto done;
            }
            strncpy(current_url, w.location, sizeof(current_url) - 1);
            current_url[sizeof(current_url) - 1] = '\0';
            continue;
        }
        break;
    }

    rc = -1;
    if (!w.done) {
        pav_error_set(err, "network", "account.test timed out", 1);
        goto done;
    }
    if (w.status_code != 200) {
        if (w.status_code == 401) {
            pav_error_set(err, "auth", "credentials rejected", 0);
        } else if (w.status_code == 0) {
            pav_error_set(err, "network", "request failed", 1);
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg), "server returned %d", w.status_code);
            pav_error_set(err, "server", msg, 0);
        }
        goto done;
    }

    rc = pav_jmap_parse_session(w.body, w.body_len, &session, errbuf, sizeof(errbuf));
    if (rc != 0) {
        pav_error_set(err, "internal", "failed to parse session", 0);
        goto done;
    }

    {
        json_t *caps_arr = json_array();
        if (caps_arr == NULL) {
            pav_error_set(err, "internal", "out of memory", 0);
            goto done;
        }
        if (session.has_mail) json_array_append_new(caps_arr, json_string("mail"));
        if (session.has_submission) json_array_append_new(caps_arr, json_string("submission"));
        if (session.has_contacts) json_array_append_new(caps_arr, json_string("contacts"));
        if (session.has_calendars) json_array_append_new(caps_arr, json_string("calendars"));
        if (session.has_sieve) json_array_append_new(caps_arr, json_string("sieve"));
        if (session.has_files) json_array_append_new(caps_arr, json_string("files"));
        if (session.has_websocket) json_array_append_new(caps_arr, json_string("websocket"));

        *data = json_object();
        if (*data == NULL) {
            json_decref(caps_arr);
            pav_error_set(err, "internal", "out of memory", 0);
            goto done;
        }
        json_object_set_new(*data, "ok", json_true());
        json_object_set_new(*data, "capabilities", caps_arr);
        json_object_set_new(*data, "has_mail", json_boolean(session.has_mail));
        json_object_set_new(*data, "has_submission", json_boolean(session.has_submission));
        json_object_set_new(*data, "has_contacts", json_boolean(session.has_contacts));
        json_object_set_new(*data, "has_calendars", json_boolean(session.has_calendars));
        json_object_set_new(*data, "has_sieve", json_boolean(session.has_sieve));
        json_object_set_new(*data, "has_files", json_boolean(session.has_files));
        json_object_set_new(*data, "has_websocket", json_boolean(session.has_websocket));
        if (session.accounts != NULL) {
            json_object_set_new(*data, "accounts", json_incref(session.accounts));
        } else {
            json_object_set_new(*data, "accounts", json_array());
        }
        if (session.api_url[0] != '\0') {
            json_object_set_new(*data, "api_url", json_string(session.api_url));
        }
        if (session.session_state[0] != '\0') {
            json_object_set_new(*data, "session_state", json_string(session.session_state));
        }
        rc = 0;
    }

done:
    if (started) {
        pav_net_stop();
    }
        free(w.body);

    if (req.headers != NULL) {
        json_decref(req.headers);
        req.headers = NULL;
    }
    pav_jmap_session_free(&session);
    free(auth_header);
    free(encoded);
    pthread_mutex_destroy(&w.mu);
    pthread_cond_destroy(&w.cv);
    return rc;
}

int pav_account_test(json_t *args, json_t **data, PavError *err) {
    const char *session_url = NULL;
    const char *username = NULL;
    const char *auth = NULL;
    const char *secret = NULL;
    const char *email = NULL;
    json_t *value;
    const char *key;
    char resolved_url[2048];
    const char *ca_file = NULL;
    const char *pav_ca = getenv("PAV_FIXTURE_CA");
    int rc;

    json_object_foreach(args, key, value) {
        if (strcmp(key, "sessionUrl") == 0) {
            if (!json_is_string(value)) {
                pav_error_set(err, "bad_args", "sessionUrl must be a string", 0);
                return -1;
            }
            session_url = json_string_value(value);
        } else if (strcmp(key, "username") == 0) {
            if (!json_is_string(value)) {
                pav_error_set(err, "bad_args", "username must be a string", 0);
                return -1;
            }
            username = json_string_value(value);
        } else if (strcmp(key, "email") == 0) {
            if (!json_is_string(value)) {
                pav_error_set(err, "bad_args", "email must be a string", 0);
                return -1;
            }
            email = json_string_value(value);
        } else if (strcmp(key, "auth") == 0) {
            if (!json_is_string(value)) {
                pav_error_set(err, "bad_args", "auth must be a string", 0);
                return -1;
            }
            auth = json_string_value(value);
        } else if (strcmp(key, "secret") == 0) {
            if (!json_is_string(value)) {
                pav_error_set(err, "bad_args", "secret must be a string", 0);
                return -1;
            }
            secret = json_string_value(value);
        } else if (strcmp(key, "caFile") == 0) {
            if (!json_is_string(value)) {
                pav_error_set(err, "bad_args", "caFile must be a string", 0);
                return -1;
            }
            ca_file = json_string_value(value);
        } else {
            char buf[128];
            snprintf(buf, sizeof(buf), "unknown argument %s", key);
            pav_error_set(err, "bad_args", buf, 0);
            return -1;
        }
    }

    if (session_url == NULL && email == NULL) {
        pav_error_set(err, "bad_args", "sessionUrl or email is required", 0);
        return -1;
    }
    if (username == NULL || username[0] == '\0') {
        pav_error_set(err, "bad_args", "username is required", 0);
        return -1;
    }
    if (auth != NULL && strcmp(auth, "basic") != 0) {
        pav_error_set(err, "bad_args", "only auth method 'basic' is supported", 0);
        return -1;
    }
    if (secret == NULL || secret[0] == '\0') {
        pav_error_set(err, "bad_args", "secret is required", 0);
        return -1;
    }

    rc = session_url_for(email, session_url, resolved_url, sizeof(resolved_url));
    if (rc != 0) {
        pav_error_set(err, "bad_args", "sessionUrl must be https:// or email is invalid", 0);
        return -1;
    }

    if (ca_file == NULL && pav_ca != NULL && pav_ca[0] != '\0') {
        ca_file = pav_ca;
    }

    rc = do_account_test(resolved_url, username, secret, ca_file, data, err);
    if (rc != 0) {
        return -1;
    }
    return 0;
}

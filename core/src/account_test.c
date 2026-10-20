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
#include "pav_activity.h"
#include "pavillion.h"

#include <jansson.h>

#include <errno.h>
#include <strings.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>
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
    char content_type[128];
} TestWait;

/* Length of "scheme://authority" in url, or 0 when url has no scheme. */
static size_t url_origin_len(const char *url) {
    const char *p = strstr(url, "://");
    if (p == NULL) return 0;
    p += 3;
    while (*p != '\0' && *p != '/' && *p != '?' && *p != '#') p++;
    return (size_t)(p - url);
}

/* The host[:port] of url into out ("" when there is none). */
void pav_account_test_host(const char *url, char *out, size_t len) {
    const char *start = url != NULL ? strstr(url, "://") : NULL;
    size_t n;

    if (len == 0) return;
    out[0] = '\0';
    if (start == NULL) return;
    start += 3;
    n = url_origin_len(url) - (size_t)(start - url);
    if (memchr(start, '@', n) != NULL) {               /* never log userinfo */
        const char *at = memchr(start, '@', n);
        n -= (size_t)(at + 1 - start);
        start = at + 1;
    }
    if (n >= len) n = len - 1;
    memcpy(out, start, n);
    out[n] = '\0';
}

/* Resolve a Location value against the request URL: absolute, "//host/path",
   "/path", or a bare relative path. 0 on success. */
static int resolve_location(const char *base, const char *loc, char *out, size_t len) {
    size_t origin = url_origin_len(base);
    int n;

    if (strstr(loc, "://") != NULL) {
        n = snprintf(out, len, "%s", loc);
    } else if (origin == 0) {
        return -1;
    } else if (loc[0] == '/' && loc[1] == '/') {
        const char *colon = strchr(base, ':');
        n = snprintf(out, len, "%.*s:%s", (int)(colon - base), base, loc);
    } else if (loc[0] == '/') {
        n = snprintf(out, len, "%.*s%s", (int)origin, base, loc);
    } else {
        const char *path = base + origin;
        const char *end = path;
        const char *slash = NULL;
        while (*end != '\0' && *end != '?' && *end != '#') {
            if (*end == '/') slash = end;
            end++;
        }
        if (slash == NULL) {
            n = snprintf(out, len, "%.*s/%s", (int)origin, base, loc);
        } else {
            n = snprintf(out, len, "%.*s%s", (int)(slash + 1 - base), base, loc);
        }
    }
    return (n < 0 || (size_t)n >= len) ? -1 : 0;
}

/* One request's state, shared by the waiter and the net thread (API Lock,
   2026-10-19). Freed by whichever lets go last, so a reply that comes after
   the waiter gave up lands in live memory and is dropped. */
typedef struct ReqCtx {
    TestWait w;     /* mu and cv guard everything here */
    PavReq req;
    char url[2048];
    int refs;
    int abandoned;
} ReqCtx;

static int g_wait_ms = 30000;

void pav_account_set_wait_ms(int ms) {
    g_wait_ms = ms > 0 ? ms : 30000;
}

static void req_ctx_release(ReqCtx *c) {
    int last;

    pthread_mutex_lock(&c->w.mu);
    last = --c->refs == 0;
    pthread_mutex_unlock(&c->w.mu);
    if (!last) return;
    if (c->req.headers != NULL) {
        pav_secret_wipe_json(c->req.headers, "Authorization");
        json_decref(c->req.headers);
    }
    free(c->w.body);
    pthread_mutex_destroy(&c->w.mu);
    pthread_cond_destroy(&c->w.cv);
    free(c);
}

static void account_test_done(PavReq *req, void *user) {
    ReqCtx *c = (ReqCtx *)user;
    TestWait *w = &c->w;
    int abandoned;

    pthread_mutex_lock(&w->mu);
    abandoned = c->abandoned;
    if (!abandoned) {
        w->done = 1;
        w->status_code = req->status_code;
        if (req->body_out != NULL) {
            w->body = strdup(req->body_out);
            w->body_len = req->body_out_len;
        }
        if (req->resp_headers != NULL) {
            const char *hkey;
            json_t *hval;

            json_object_foreach(req->resp_headers, hkey, hval) {
                if (!json_is_string(hval)) continue;
                if (strcasecmp(hkey, "Location") == 0) {
                    snprintf(w->location, sizeof(w->location), "%s", json_string_value(hval));
                } else if (strcasecmp(hkey, "Content-Type") == 0) {
                    snprintf(w->content_type, sizeof(w->content_type), "%s", json_string_value(hval));
                }
            }
        }
        pthread_cond_broadcast(&w->cv);
    }
    pthread_mutex_unlock(&w->mu);
    if (abandoned) {
        char host[256];
        pav_account_test_host(c->url, host, sizeof(host));
        pav_log("account", PAV_DEBUG, 2, "late reply dropped (%d from %s)", req->status_code, host);
    } else {
        pav_log("account", PAV_DEBUG, 1, "test response %d", req->status_code);
    }

    if (req->resp_headers != NULL) {
        json_decref(req->resp_headers);
        req->resp_headers = NULL;
    }
    pav_net_free(req->body_out);
    req->body_out = NULL;
    req_ctx_release(c);
}


/* "scheme://host:port", lowercased, with the scheme's default port filled in. */
static void origin_key(const char *url, char *out, size_t len) {
    char hp[256];
    const char *sep = strstr(url, "://");
    size_t slen = sep != NULL ? (size_t)(sep - url) : 0;
    const char *colon;
    const char *port;
    size_t hlen, i;
    int n;

    pav_account_test_host(url, hp, sizeof(hp));
    colon = hp[0] == '[' ? strstr(hp, "]:") : strrchr(hp, ':');
    if (colon != NULL && hp[0] == '[') colon++;
    hlen = colon != NULL ? (size_t)(colon - hp) : strlen(hp);
    if (colon != NULL && colon[1] != '\0') {
        port = colon + 1;
    } else if (slen == 5 && strncasecmp(url, "https", 5) == 0) {
        port = "443";
    } else if (slen == 4 && strncasecmp(url, "http", 4) == 0) {
        port = "80";
    } else {
        port = "";
    }
    n = snprintf(out, len, "%.*s://%.*s:%s", (int)slen, url, (int)hlen, hp, port);
    if (n < 0) {
        out[0] = '\0';
        return;
    }
    for (i = 0; out[i] != '\0'; i++) {
        if (out[i] >= 'A' && out[i] <= 'Z') out[i] = (char)(out[i] - 'A' + 'a');
    }
}

static int looks_like_web_page(const TestWait *w) {
    const char *p = w->body;

    if (strstr(w->content_type, "html") != NULL || strstr(w->content_type, "HTML") != NULL) {
        return 1;
    }
    if (p == NULL) return 0;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return *p == '<';
}

/* GET the session at initial_url, following up to 3 https redirects. The
   Authorization header (NULL: none) is dropped once a redirect changes scheme,
   host, or port. final_url gets the URL that answered; *auth_sent (may be NULL)
   is 1 when that answer was fetched with Authorization. Net must be started. */
static int fetch_session(const char *initial_url, const char *auth_header, const char *ca_file,
                         int single_attempt, int *auth_sent,
                         char *final_url, size_t final_len, PavJmapSession *session,
                         PavError *err) {
    char current_url[2048];
    char errbuf[512];
    TestWait w;
    int rc;
    int ret = -1;
    int redirects;
    int send_auth = auth_header != NULL;
    const int max_requests = 4; /* the first request and at most 3 redirects */

    memset(&w, 0, sizeof(w));
    snprintf(current_url, sizeof(current_url), "%s", initial_url);

    for (redirects = 0; redirects < max_requests; redirects++) {
        ReqCtx *c;
        int got;

        free(w.body);
        memset(&w, 0, sizeof(w));

        c = calloc(1, sizeof(*c));
        if (c == NULL) {
            pav_error_set(err, "internal", "out of memory", 0);
            goto done;
        }
        pthread_mutex_init(&c->w.mu, NULL);
        pthread_cond_init(&c->w.cv, NULL);
        c->refs = 1;
        snprintf(c->url, sizeof(c->url), "%s", current_url);
        c->req.method = "GET";
        c->req.url = c->url;
        c->req.ca_file = ca_file;
        c->req.on_done = account_test_done;
        c->req.user = c;
        if (single_attempt) c->req.retry_count = PAV_NET_RETRY_MAX - 1;
        c->req.headers = json_object();
        if (c->req.headers == NULL) {
            req_ctx_release(c);
            pav_error_set(err, "internal", "out of memory", 0);
            goto done;
        }
        if (send_auth) {
            json_object_set_new(c->req.headers, "Authorization", json_string(auth_header));
        }

        c->refs = 2; /* this waiter and the net thread */
        rc = pav_net_request(&c->req);
        if (rc != 0) {
            c->refs = 1;
            req_ctx_release(c);
            pav_error_set(err, "internal", "cannot queue request", 0);
            goto done;
        }

        {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += g_wait_ms / 1000;
            ts.tv_nsec += (long)(g_wait_ms % 1000) * 1000000L;
            if (ts.tv_nsec >= 1000000000L) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000L;
            }
            pthread_mutex_lock(&c->w.mu);
            while (!c->w.done) {
                int r = pthread_cond_timedwait(&c->w.cv, &c->w.mu, &ts);
                if (r == ETIMEDOUT) break;
            }
            got = c->w.done;
            if (got) {
                /* Take the reply; the context keeps nothing the waiter needs. */
                w.done = 1;
                w.status_code = c->w.status_code;
                w.body = c->w.body;
                w.body_len = c->w.body_len;
                memcpy(w.location, c->w.location, sizeof(w.location));
                memcpy(w.content_type, c->w.content_type, sizeof(w.content_type));
                c->w.body = NULL;
            } else {
                /* Give up: the net thread may still answer; it drops the reply. */
                c->abandoned = 1;
                c->req.cancelled = 1;
            }
            pthread_mutex_unlock(&c->w.mu);
        }
        req_ctx_release(c);

        if (!got) {
            pav_error_set(err, "network", "the session request timed out", 1);
            goto done;

        }

        if (w.status_code == 302 || w.status_code == 301 ||
            w.status_code == 307 || w.status_code == 308) {
            char next[2048];
            char from_key[300];
            char to_key[300];

            if (redirects == max_requests - 1) {
                pav_error_set(err, "server", "too many redirects", 0);
                goto done;
            }
            if (w.location[0] == '\0') {
                char msg[256];
                snprintf(msg, sizeof(msg), "redirect %d without Location", w.status_code);
                pav_error_set(err, "server", msg, 0);
                goto done;
            }
            if (resolve_location(current_url, w.location, next, sizeof(next)) != 0) {
                pav_error_set(err, "server", "redirect target is invalid", 0);
                goto done;
            }
            if (strncmp(next, "https://", 8) != 0) {
                pav_error_set(err, "server", "redirect target is not https", 0);
                goto done;
            }
            origin_key(current_url, from_key, sizeof(from_key));
            origin_key(next, to_key, sizeof(to_key));
            if (send_auth && strcmp(from_key, to_key) != 0) {
                char from_host[256];
                char to_host[256];
                pav_account_test_host(current_url, from_host, sizeof(from_host));
                pav_account_test_host(next, to_host, sizeof(to_host));
                pav_log("account", PAV_DEBUG, 2,
                        "redirect leaves %s for %s: Authorization dropped", from_host, to_host);
                send_auth = 0;
            }
            pav_log("account", PAV_DEBUG, 2, "test redirect %d to %s", w.status_code, next);
            memcpy(current_url, next, sizeof(current_url));
            continue;
        }
        break;
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
    if (looks_like_web_page(&w)) {
        pav_error_set(err, "server",
                      "the server returned a web page, not a JMAP session (check the URL)", 0);
        goto done;
    }
    if (pav_jmap_parse_session(w.body, w.body_len, session, errbuf, sizeof(errbuf)) != 0) {
        pav_error_set(err, "internal", "failed to parse session", 0);
        goto done;
    }
    snprintf(final_url, final_len, "%s", current_url);
    if (auth_sent != NULL) {
        *auth_sent = send_auth;
    }
    ret = 0;

done:
    free(w.body);
    return ret;
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
        pav_log("account", PAV_DEBUG, 0, "session URL from the address domain (well-known)");
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
        pav_secret_wipe(raw, raw_len);
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
    pav_secret_wipe(raw, raw_len);
    free(raw);
    return encoded;
}

static int do_account_test(const char *initial_url, const char *username,
                           const char *secret_val, const char *ca_file,
                           json_t **data, PavError *err) {
    char final_url[2048];
    PavJmapSession session;
    char *encoded = NULL;
    int auth_sent = 0;
    char *auth_header = NULL;
    int started = 0;
    int ret = -1;

    memset(&session, 0, sizeof(session));

    if (pav_net_start() != 0) {
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

    if (fetch_session(initial_url, auth_header, ca_file, 0, &auth_sent, final_url, sizeof(final_url),
                      &session, err) != 0) {
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
        json_object_set_new(*data, "sessionUrl", json_string(final_url));
        json_object_set_new(*data, "authSent", json_boolean(auth_sent));
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
        ret = 0;
    }

done:
    if (started) {
        pav_net_stop();
    }
    pav_jmap_session_free(&session);
    pav_secret_free(auth_header);
    pav_secret_free(encoded);
    if (ret != 0 && err->code[0] == '\0') {
        pav_error_set(err, "internal", "account.test failed", 0);
    }
    return ret;
}

int pav_account_test(json_t *args, json_t **data, PavError *err) {
    const char *session_url = NULL;
    const char *username = NULL;
    const char *auth = NULL;
    const char *secret = NULL;
    const char *email = NULL;
    json_t *value;
    const char *account_id = NULL;
    char *saved = NULL;
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
        } else if (strcmp(key, "id") == 0) {
            if (!json_is_string(value)) {
                pav_error_set(err, "bad_args", "id must be a string", 0);
                return -1;
            }
            account_id = json_string_value(value);
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
    if ((secret == NULL || secret[0] == '\0') && (account_id == NULL || account_id[0] == '\0')) {
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

    {
        char host[256];
        char title[300];
        int64_t act;

        pav_account_test_host(resolved_url, host, sizeof(host));
        snprintf(title, sizeof(title), "Test %s", host[0] != '\0' ? host : "account");
        pav_log("account", PAV_DEBUG, 2, "test %s as %s", host, username);
        act = pav_activity_begin("account.test", NULL, title, 0, 0);
        rc = 0;
        /* The saved-secret lookup can take a second or more (Stronghold
           unlock), so it runs inside the activity the user sees. */
        if (secret == NULL || secret[0] == '\0') {
            rc = pav_account_saved_secret(account_id, &saved, err);
            secret = saved;
            if (rc == 0) {
                pav_log("account", PAV_DEBUG, 1, "test uses the saved secret of %s", account_id);
            }
        }
        if (rc == 0) {
            rc = do_account_test(resolved_url, username, secret, ca_file, data, err);
        }
        /* K-Q10: a verified test of a saved account is stored (core thread). */
        if (rc == 0 && account_id != NULL && account_id[0] != '\0' && *data != NULL &&
            json_is_true(json_object_get(*data, "authSent"))) {
            char checked[32];
            int64_t now = pav_platform()->now_ms != NULL ? pav_platform()->now_ms() : -1;
            time_t secs = (time_t)(now / 1000);
            struct tm tm;
            json_t *caps = pav_account_caps_clean(json_object_get(*data, "capabilities"));

            if (now >= 0 && gmtime_r(&secs, &tm) != NULL && caps != NULL) {
                size_t len = strftime(checked, sizeof(checked), "%Y-%m-%dT%H:%M:%S", &tm);
                snprintf(checked + len, sizeof(checked) - len, ".%03dZ", (int)(now % 1000));
                if (pav_core_post_capabilities(account_id, caps, checked) != 0) {
                    pav_log("account", PAV_DEBUG, 1, "capabilities of %s not queued", account_id);
                }
            } else {
                json_decref(caps);
            }
        }
        if (act > 0) {
            pav_activity_end(act, rc == 0 ? PAV_ACT_DONE : PAV_ACT_FAILED, rc == 0 ? NULL : err->message);
        }
    }
    pav_secret_free(saved);
    if (rc != 0) {
        return -1;
    }
    return 0;
}

static int g_discover_port; /* test seam: well-known port, 0 = 443 */

void pav_account_discover_set_port(int port) {
    g_discover_port = port;
}

/* One discovery candidate: no credentials, final response must carry apiUrl. */
static int discover_try(const char *url, const char *ca_file, char *final_url,
                        size_t final_len, PavError *err) {
    PavJmapSession session;
    int rc;

    memset(&session, 0, sizeof(session));
    rc = fetch_session(url, NULL, ca_file, 1, NULL, final_url, final_len, &session, err);
    if (rc == 0 && session.api_url[0] == '\0') {
        pav_error_set(err, "server", "the JMAP session has no apiUrl", 0);
        rc = -1;
    }
    pav_jmap_session_free(&session);
    return rc;
}

static int valid_domain(const char *d) {
    size_t i, n = strlen(d);

    if (n == 0 || n > 253 || d[0] == '.' || d[0] == '-' || d[n - 1] == '.') return 0;
    for (i = 0; i < n; i++) {
        char c = d[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.')) {
            return 0;
        }
    }
    return 1;
}

static int do_discover(const char *domain, const char *ca_file, json_t **data, PavError *err) {
    const PavPlatform *plat = pav_platform();
    char name[300];
    char target[256];
    char url[600];
    char final_url[2048];
    char host[256];
    const char *via = NULL;
    int port = 0;
    int rc;
    PavError last;

    memset(&last, 0, sizeof(last));
    snprintf(name, sizeof(name), "_jmap._tcp.%s", domain);
    rc = (plat != NULL && plat->srv_lookup != NULL)
             ? plat->srv_lookup(name, target, sizeof(target), &port) : 1;
    if (rc == 0) {
        size_t tl = strlen(target);
        if (tl > 0 && target[tl - 1] == '.') target[tl - 1] = '\0';
    }
    if (rc == 0 && valid_domain(target) && port > 0 && port < 65536) {
        if (port == 443) {
            snprintf(url, sizeof(url), "https://%s/.well-known/jmap", target);
        } else {
            snprintf(url, sizeof(url), "https://%s:%d/.well-known/jmap", target, port);
        }
        pav_log("account", PAV_DEBUG, 2, "discover SRV %s gives %s", name, url);
        if (discover_try(url, ca_file, final_url, sizeof(final_url), &last) == 0) {
            via = "srv";
        } else {
            pav_log("account", PAV_DEBUG, 2, "discover SRV candidate failed (%s): %s",
                    last.code, last.message);
        }
    } else {
        pav_log("account", PAV_DEBUG, 2, "discover SRV %s: %s", name,
                rc < 0 ? "lookup failed" : "no usable record");
    }

    if (via == NULL) {
        if (g_discover_port > 0) {
            snprintf(url, sizeof(url), "https://%s:%d/.well-known/jmap", domain, g_discover_port);
        } else {
            snprintf(url, sizeof(url), "https://%s/.well-known/jmap", domain);
        }
        memset(&last, 0, sizeof(last));
        if (discover_try(url, ca_file, final_url, sizeof(final_url), &last) == 0) {
            via = "well-known";
        }
    }

    if (via == NULL) {
        char msg[600];
        snprintf(msg, sizeof(msg), "no JMAP session found for %.253s (%.300s)", domain,
                 last.message[0] != '\0' ? last.message : "no answer");
        pav_error_set(err, "not_found", msg, 0);
        return -1;
    }

    *data = json_object();
    if (*data == NULL) {
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    json_object_set_new(*data, "sessionUrl", json_string(final_url));
    json_object_set_new(*data, "via", json_string(via));
    pav_account_test_host(final_url, host, sizeof(host));
    pav_log("account", PAV_INFO, 3, "discover %s: %s via %s", domain, host, via);
    return 0;
}

int pav_account_discover(json_t *args, json_t **data, PavError *err) {
    const char *email = NULL;
    const char *ca_file = NULL;
    const char *pav_ca = getenv("PAV_FIXTURE_CA");
    const char *key;
    const char *at;
    json_t *value;
    char domain[256];
    char title[300];
    size_t i;
    int64_t act;
    int rc;

    json_object_foreach(args, key, value) {
        if (strcmp(key, "email") == 0 && json_is_string(value)) {
            email = json_string_value(value);
        } else if (strcmp(key, "caFile") == 0 && json_is_string(value)) {
            ca_file = json_string_value(value);
        } else {
            char buf[128];
            snprintf(buf, sizeof(buf), "bad argument %s", key);
            pav_error_set(err, "bad_args", buf, 0);
            return -1;
        }
    }
    at = email != NULL ? strrchr(email, '@') : NULL;
    if (at == NULL || at == email || strlen(at + 1) >= sizeof(domain)) {
        pav_error_set(err, "bad_args", "email must look like name@domain", 0);
        return -1;
    }
    for (i = 0; at[1 + i] != '\0'; i++) {
        char c = at[1 + i];
        domain[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    domain[i] = '\0';
    if (!valid_domain(domain)) {
        pav_error_set(err, "bad_args", "email must look like name@domain", 0);
        return -1;
    }
    if ((ca_file == NULL || ca_file[0] == '\0') && pav_ca != NULL && pav_ca[0] != '\0') {
        ca_file = pav_ca;
    }
    if (ca_file != NULL && ca_file[0] == '\0') {
        ca_file = NULL;
    }

    if (pav_net_start() != 0) {
        pav_error_set(err, "internal", "cannot start net", 0);
        return -1;
    }
    snprintf(title, sizeof(title), "Configure %s", domain);
    act = pav_activity_begin("account.discover", NULL, title, 0, 0);
    rc = do_discover(domain, ca_file, data, err);
    if (act > 0) {
        pav_activity_end(act, rc == 0 ? PAV_ACT_DONE : PAV_ACT_FAILED, rc == 0 ? NULL : err->message);
    }
    pav_net_stop();
    return rc == 0 ? 0 : -1;
}

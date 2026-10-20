#define _POSIX_C_SOURCE 200809L

#include "pav_jmap.h"
#include "pavillion.h"

#include <curl/curl.h>

#include <jansson.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int jmap_cap_index(PavJmapSession *s, const char *name) {
    int i;

    for (i = 0; i < s->cap_count; i++) {
        if (strcmp(s->caps[i].name, name) == 0) {
            return i;
        }
    }
    if (s->cap_count >= (int)(sizeof(s->caps) / sizeof(s->caps[0]))) {
        return -1;
    }
    s->caps[s->cap_count].name = name;
    s->caps[s->cap_count].present = 0;
    s->caps[s->cap_count].limits = NULL;
    return s->cap_count++;
}

static void map_capability(const char *urn, PavJmapSession *s) {
    int idx = jmap_cap_index(s, urn);

    if (idx < 0) return;

    s->caps[idx].present = 1;
    if (strcmp(urn, "urn:ietf:params:jmap:mail") == 0) {
        s->has_mail = 1;
    } else if (strcmp(urn, "urn:ietf:params:jmap:submission") == 0) {
        s->has_submission = 1;
    } else if (strcmp(urn, "urn:ietf:params:jmap:contacts") == 0 ||
               strcmp(urn, "urn:ietf:params:jmap:contracts") == 0) {
        s->has_contacts = 1;
    } else if (strcmp(urn, "urn:ietf:params:jmap:calendars") == 0) {
        s->has_calendars = 1;
    } else if (strcmp(urn, "urn:ietf:params:jmap:sieve") == 0) {
        s->has_sieve = 1;
    } else if (strcmp(urn, "urn:ietf:params:jmap:filenode") == 0 || /* JMAP File Storage draft (Stalwart) */
               strcmp(urn, "urn:ietf:params:jmap:files") == 0 ||
               strcmp(urn, "urn:apache:jmap:files") == 0 ||
               strcmp(urn, "urn:ietf:params:jmap:files:0") == 0) {
        s->has_files = 1;
    } else if (strcmp(urn, "urn:ietf:params:jmap:websocket") == 0) {
        s->has_websocket = 1;
    }
}

static void copy_url(char *dst, size_t dst_len, json_t *obj, const char *key) {
    const char *val;

    if (obj == NULL || !json_is_object(obj)) return;

    val = json_string_value(json_object_get(obj, key));
    if (val == NULL || val[0] == '\0') return;

    strncpy(dst, val, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

int pav_jmap_parse_session(const char *body, size_t len, PavJmapSession *out,
                           char *err, size_t err_len) {
    json_t *root = NULL;
    json_error_t jerr;
    json_t *caps_obj;
    json_t *accounts;
    json_t *primary;
    json_t *accts;
    const char *key;
    json_t *value;

    (void)len;
    memset(out, 0, sizeof(*out));

    root = json_loads(body, 0, &jerr);
    if (root == NULL) {
        snprintf(err, err_len, "invalid session JSON: %s", jerr.text);
        pav_log("jmap", PAV_WARN, 1, "invalid session JSON at line %d", jerr.line);
        return -1;
    }

    copy_url(out->api_url, sizeof(out->api_url), root, "apiUrl");
    copy_url(out->download_url, sizeof(out->download_url), root, "downloadUrl");
    copy_url(out->upload_url, sizeof(out->upload_url), root, "uploadUrl");
    copy_url(out->event_source_url, sizeof(out->event_source_url), root,
             "eventSourceUrl");

    {
        const char *state = json_string_value(json_object_get(root, "state"));
        if (state != NULL && state[0] != '\0') {
            strncpy(out->session_state, state, sizeof(out->session_state) - 1);
            out->session_state[sizeof(out->session_state) - 1] = '\0';
        }
    }

    caps_obj = json_object_get(root, "capabilities");
    if (caps_obj != NULL && json_is_object(caps_obj)) {
        json_object_foreach(caps_obj, key, value) {
            (void)value;
            map_capability(key, out);
        }
    }

    primary = json_object_get(root, "primaryAccounts");
    if (primary != NULL && json_is_object(primary)) {
        json_t *arr = json_object();
        if (arr != NULL) {
            json_object_foreach(primary, key, value) {
                if (json_is_string(value)) {
                    json_object_set(arr, key, value);
                }
            }
            char *text = json_dumps(arr, JSON_COMPACT);
            if (text != NULL) {
                strncpy(out->primary_accounts, text,
                        sizeof(out->primary_accounts) - 1);
                out->primary_accounts[sizeof(out->primary_accounts) - 1] = '\0';
                free(text);
            }
            json_decref(arr);
        }
    }

    accounts = json_object_get(root, "accounts");
    if (accounts != NULL && json_is_object(accounts)) {
        accts = json_array();
        if (accts != NULL) {
            json_object_foreach(accounts, key, value) {
                json_t *entry = json_object();
                if (entry == NULL) continue;
                if (json_is_object(value)) {
                    json_object_set_new(entry, "id",
                                        json_string(json_string_value(json_object_get(value, "id"))));
                    json_object_set_new(entry, "name",
                                        json_string(json_string_value(json_object_get(value, "name"))));
                    json_object_set_new(entry, "isPersonal",
                                        json_is_true(json_object_get(value, "isPersonal")) ? json_true() : json_false());
                    json_object_set_new(entry, "username",
                                        json_string(json_string_value(json_object_get(value, "username"))));
                    json_object_set_new(entry, "from",
                                        json_string(json_string_value(json_object_get(value, "from"))));
                    {
                        json_t *caps = json_object_get(value, "capabilities");
                        if (json_is_object(caps)) {
                            json_object_set_new(entry, "capabilities", json_copy(caps));
                        }
                    }
                }
                json_array_append_new(accts, entry);
            }
            out->accounts = accts;
        }
    }

    json_decref(root);
    pav_log("jmap", PAV_DEBUG, 2, "session parsed: %d capabilities, %d accounts", out->cap_count, out->accounts != NULL ? (int)json_array_size(out->accounts) : 0);
    return 0;
}

void pav_jmap_session_free(PavJmapSession *s) {
    int i;

    if (s == NULL) return;
    if (s->accounts != NULL) {
        json_decref(s->accounts);
    }
    for (i = 0; i < s->cap_count; i++) {
        if (s->caps[i].limits != NULL) {
            json_decref(s->caps[i].limits);
        }
    }
    memset(s, 0, sizeof(*s));
}

int pav_jmap_build_request(json_t *method_calls, char **out) {
    json_t *root;
    char *text;

    if (method_calls == NULL || !json_is_array(method_calls) || out == NULL) {
        return -1;
    }
    root = json_object();
    if (root == NULL) return -1;

    if (json_object_set_new(root, "using",
                            json_pack("[{s}]", "urn:ietf:params:jmap:core")) != 0) {
        json_decref(root);
        return -1;
    }
    if (json_object_set(root, "methodCalls", method_calls) != 0) {
        json_decref(root);
        return -1;
    }
    if (json_object_set_new(root, "RequestId", json_string("pav-m2")) != 0) {
        json_decref(root);
        return -1;
    }

    text = json_dumps(root, JSON_COMPACT);
    json_decref(root);
    if (text == NULL) return -1;
    *out = text;
    return 0;
}

static void jmap_post_done(PavReq *req, void *user) {
    PavJmapCtx *ctx = (PavJmapCtx *)user;

    pav_log("jmap", PAV_DEBUG, 2, "response %d (%lld bytes)", req->status_code, (long long)req->body_out_len);
    ctx->on_done(ctx, req->status_code, req->body_out, req->body_out_len,
                 req->resp_headers, ctx->user);

    if (req->resp_headers != NULL) {
        json_decref(req->resp_headers);
        req->resp_headers = NULL;
    }
    pav_net_free(req->body_out);
    free(ctx->request_json);
    ctx->request_json = NULL;
}

/* The request keeps ctx->session_url, which the caller owns for the life of
   ctx (until on_done). It used to point at a stack buffer of this function,
   which was gone by the time the network thread read it. */
int pav_jmap_post(PavJmapCtx *ctx) {
    if (ctx == NULL || ctx->on_done == NULL || ctx->session_url == NULL ||
        ctx->session_url[0] == '\0') {
        return -1;
    }

    if (ctx->request_json == NULL) {
        return -1;
    }

    memset(&ctx->req, 0, sizeof(ctx->req));
    ctx->req.method = "POST";
    ctx->req.url = ctx->session_url;
    ctx->req.body = ctx->request_json;
    ctx->req.body_len = strlen(ctx->request_json);
    ctx->req.ca_file = ctx->ca_file;
    ctx->req.keyring_user = ctx->keyring_user;
    ctx->req.username = ctx->username;
    ctx->req.auth_method = ctx->auth_method;
    ctx->req.on_done = jmap_post_done;
    ctx->req.user = ctx;

    pav_log("jmap", PAV_DEBUG, 1, "post %zu bytes", ctx->req.body_len);
    return pav_net_request(&ctx->req);
}

int pav_jmap_parse_response(const char *body, size_t len, PavJmapResp *out,
                            char *err, size_t err_len) {
    json_t *root;
    json_error_t jerr;
    json_t *method_responses;
    const char *type;
    json_t *first;
    size_t i;
    size_t n;

    (void)len;
    memset(out, 0, sizeof(*out));

    root = json_loads(body, 0, &jerr);
    if (root == NULL) {
        snprintf(err, err_len, "invalid JMAP response: %s", jerr.text);
        pav_log("jmap", PAV_WARN, 1, "invalid response JSON at line %d", jerr.line);
        return -1;
    }

    {
        json_t *state_json = json_object_get(root, "sessionState");
        const char *s = json_string_value(state_json);
        if (s != NULL && s[0] != '\0') {
            strncpy(out->session_state, s, sizeof(out->session_state) - 1);
            out->session_state[sizeof(out->session_state) - 1] = '\0';
        }
    }

    method_responses = json_object_get(root, "methodResponses");
    if (!json_is_array(method_responses)) {
        snprintf(err, err_len, "missing methodResponses array");
        pav_log("jmap", PAV_WARN, 0, "response has no methodResponses");
        json_decref(root);
        return -1;
    }

    out->method_responses = json_array();
    if (out->method_responses == NULL) {
        json_decref(root);
        return -1;
    }

    n = json_array_size(method_responses);
    for (i = 0; i < n; i++) {
        json_t *entry = json_array_get(method_responses, i);

        if (json_is_array(entry)) {
            json_array_append(out->method_responses, entry);
        }
    }

    if (n > 0) {
        first = json_array_get(method_responses, 0);
        if (json_is_array(first) && json_array_size(first) >= 2) {
            type = json_string_value(json_array_get(first, 0));
            if (type != NULL && strcmp(type, "error") == 0) {
                json_t *emsg = json_array_get(first, 1);
                const char *code = json_is_object(emsg) ? json_string_value(json_object_get(emsg, "code")) : NULL;
                if (json_is_object(emsg)) {
                    const char *desc = json_string_value(json_object_get(emsg, "description"));
                    snprintf(out->error, sizeof(out->error), "%s: %s",
                             code != NULL ? code : "unknown",
                             desc != NULL ? desc : "");
                } else {
                    snprintf(out->error, sizeof(out->error), "JMAP error response");
                }
                out->is_success = 0;
                pav_log("jmap", PAV_WARN, 1, "method error: %s", code != NULL ? code : "unknown");
            } else {
                out->is_success = 1;
            }
        } else {
            out->is_success = 1;
        }
    } else {
        out->is_success = 1;
    }

    json_decref(root);
    return 0;
}

#ifndef PAV_JMAP_H
#define PAV_JMAP_H

#include <jansson.h>

#include "pav_net.h"

typedef struct PavJmapCap {
    const char *name;
    int present;
    json_t *limits;
} PavJmapCap;

typedef struct PavJmapSession {
    char api_url[2048];
    char download_url[2048];
    char upload_url[2048];
    char event_source_url[2048];
    char primary_accounts[512];
    char session_state[128];
    PavJmapCap caps[8];
    int cap_count;
    int has_mail;
    int has_submission;
    int has_contacts;
    int has_calendars;
    int has_sieve;
    int has_files;
    int has_websocket;
    json_t *accounts;
} PavJmapSession;

void pav_jmap_session_free(PavJmapSession *s);

typedef struct PavJmapResp {
    json_t *method_responses;
    char session_state[128];
    int is_success;
    char error[256];
} PavJmapResp;

int pav_jmap_build_request(json_t *method_calls, char **out);

typedef struct PavJmapCtx {
    void (*on_done)(struct PavJmapCtx *ctx, int status, const char *body,
                    size_t body_len, const json_t *resp_headers, void *user);
    void *user;
    PavReq req;
    char *request_json;
    char *session_url;
    const char *ca_file;
    const char *keyring_user;
    const char *username;
    const char *auth_method;
} PavJmapCtx;

int pav_jmap_post(PavJmapCtx *ctx);

int pav_jmap_parse_session(const char *body, size_t len, PavJmapSession *out,
                           char *err, size_t err_len);

int pav_jmap_parse_response(const char *body, size_t len, PavJmapResp *out,
                            char *err, size_t err_len);

#endif

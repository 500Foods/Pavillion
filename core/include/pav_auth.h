#ifndef PAV_AUTH_H
#define PAV_AUTH_H

#include <jansson.h>

#include <stddef.h>

typedef struct PavAccount {
    const char *username;
    const char *auth;
} PavAccount;

typedef struct PavReq {
    /* Input: request target. */
    const char *method;
    const char *url;
    /* Input: auth context (set before pav_net_request). The net thread reads
       the secret for keyring_user, builds a PavAccount, and calls
       PavAuth.apply to add the Authorization header. */
    const char *account_id;
    const char *keyring_user;
    const char *username;
    const char *auth_method;
    const char *ca_file;     /* optional pinned CA path for TLS */
    /* Input: headers (auth added by the net thread), body or file paths. */
    json_t *headers;
    const char *body;
    size_t body_len;
    const char *download_path;   /* NULL to capture in body_out */
    const char *upload_path;     /* NULL for body or no upload */
    /* Output, filled by the net thread on completion. */
    int status_code;
    /* 0 when auth was applied or not required. PAV_SECRET_LOCKED, MISSING,
       or FAILED when the header could not be built. Stays set when
       status_code is forced to 0 on that path. */
    int auth_rc;
    json_t *resp_headers;   /* caller reads Retry-After, Content-Type; net frees */
    char *body_out;         /* net-allocated; caller frees with pav_net_free */
    size_t body_out_len;
     void (*on_done)(struct PavReq *req, void *user);
    void *user;
    int cancelled;          /* set by the caller to abort */
    int retry_count;        /* internal: current retry attempt */
} PavReq;

typedef struct PavSecret {
    const char *val;
    size_t len;
} PavSecret;

typedef struct PavAuth {
    const char *method;
    int (*apply)(const PavAccount *account, PavReq *req, PavSecret *secret);
    int (*on_unauthorized)(const PavAccount *account);
} PavAuth;

const PavAuth *pav_auth_for(const PavAccount *account, char *err, size_t err_len);

#endif

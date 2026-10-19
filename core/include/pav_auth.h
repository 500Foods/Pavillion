#ifndef PAV_AUTH_H
#define PAV_AUTH_H

#include <jansson.h>

#include <stddef.h>

typedef struct PavAccount {
    const char *username;
    const char *auth;
} PavAccount;

typedef struct PavReq {
    json_t *headers;
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

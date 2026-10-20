#ifndef ACCOUNT_H
#define ACCOUNT_H

#include "dispatch.h"

#include <stddef.h>

int pav_account_list(json_t *args, json_t **data, PavError *err);
int pav_account_add(json_t *args, json_t **data, PavError *err);
int pav_account_update(json_t *args, json_t **data, PavError *err);
int pav_account_remove(json_t *args, json_t **data, PavError *err);
int pav_account_set_secret(json_t *args, json_t **data, PavError *err);
int pav_account_test(json_t *args, json_t **data, PavError *err);
/* The saved secret of account id (free with pav_secret_free). 0, or -1 with err. */
int pav_account_saved_secret(const char *id, char **out, PavError *err);
/* host[:port] of a URL for log lines, without userinfo; "" when none. */
void pav_account_test_host(const char *url, char *out, size_t len);
/* account.discover (K-Q5). */
int pav_account_discover(json_t *args, json_t **data, PavError *err);
/* Test seam: the well-known fallback uses this port (0: 443). */
void pav_account_discover_set_port(int port);

#endif

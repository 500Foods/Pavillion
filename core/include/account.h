#ifndef ACCOUNT_H
#define ACCOUNT_H

#include "dispatch.h"

#include <stddef.h>

int pav_account_list(json_t *args, json_t **data, PavError *err);
int pav_account_add(json_t *args, json_t **data, PavError *err);
int pav_account_update(json_t *args, json_t **data, PavError *err);
int pav_account_remove(json_t *args, json_t **data, PavError *err);
int pav_account_set_secret(json_t *args, json_t **data, PavError *err);

#endif

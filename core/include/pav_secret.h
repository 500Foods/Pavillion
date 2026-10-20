#ifndef PAV_SECRET_H
#define PAV_SECRET_H

#include "pav_platform.h"

/* Account secret through the installed platform. Zeroes its copy after use.
   *out is malloc'd on success. Free with pav_secret_free. Never logs the value. */
int pav_secret_get(const char *service, const char *user, char **out);
int pav_secret_set(const char *service, const char *user, const char *label, const char *val);
int pav_secret_delete(const char *service, const char *user);
void pav_secret_free(char *value);

/* K Lock (no caching, zeroize after use): wipe n bytes with volatile stores. */
void pav_secret_wipe(void *p, size_t n);
/* Wipe the string under key in a jansson object (a typed password in the
   arguments, an Authorization header) before it is freed. */
struct json_t;
void pav_secret_wipe_json(struct json_t *obj, const char *key);

#endif

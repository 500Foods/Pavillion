#ifndef PAV_SECRET_H
#define PAV_SECRET_H

#include "pav_platform.h"

/* Account secret through the installed platform. Zeroes its copy after use.
   *out is malloc'd on success. Free with pav_secret_free. Never logs the value. */
int pav_secret_get(const char *service, const char *user, char **out);
int pav_secret_set(const char *service, const char *user, const char *label, const char *val);
int pav_secret_delete(const char *service, const char *user);
void pav_secret_free(char *value);

#endif

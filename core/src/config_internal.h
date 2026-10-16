#ifndef CONFIG_INTERNAL_H
#define CONFIG_INTERNAL_H

#include <jansson.h>

#include <stddef.h>

int config_join(char *dst, size_t dst_len, const char *dir, const char *name);
int config_resolve(char *dst, size_t dst_len, const char *workdir, const char *path);

/* 1 when node contains password, token, secret, or refresh_token.
   *name receives that key. */
int config_forbidden_key(const json_t *node, const char **name);

#endif

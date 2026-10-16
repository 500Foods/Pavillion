#ifndef CONFIG_INTERNAL_H
#define CONFIG_INTERNAL_H

#include <stddef.h>

int config_join(char *dst, size_t dst_len, const char *dir, const char *name);
int config_resolve(char *dst, size_t dst_len, const char *workdir, const char *path);

#endif

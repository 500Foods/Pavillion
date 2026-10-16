#ifndef PLATFORM_INTERNAL_H
#define PLATFORM_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

int pav_posix_mkdir_private(const char *path);
int pav_posix_write_private(const char *path, const void *buf, size_t n);
/* Writes path.tmp, deletes it, and returns -1. path itself is left as it was. */
int pav_posix_write_abort(const char *path, const void *buf, size_t n);
int64_t pav_posix_now_ms(void);

#endif

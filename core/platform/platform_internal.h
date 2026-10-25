#ifndef PLATFORM_INTERNAL_H
#define PLATFORM_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

typedef struct PavMemoryInfo PavMemoryInfo;

int pav_posix_mkdir_private(const char *path);
int pav_posix_write_private(const char *path, const void *buf, size_t n);
/* Writes path.tmp, deletes it, and returns -1. path itself is left as it was. */
int pav_posix_write_abort(const char *path, const void *buf, size_t n);
int64_t pav_posix_now_ms(void);
/* 0 and *out is f_bavail times the fragment size, or the Windows equivalent.
   -1 when path is missing or the volume cannot be read. */
int pav_posix_volume_free(const char *path, int64_t *out);

/* Returns process RSS in kilobytes, or -1. Uses getrusage on POSIX,
   GetProcessMemoryInfo on Windows. */
int pav_posix_usage_kb(void);
/* Fills info with current RSS, peak RSS, and child-process summary. */
int pav_posix_memory_info(PavMemoryInfo *out);

/* SRV lookup (K-Q5): posix/srv_resolv.c or windows/srv_dnsapi.c. 0 found,
   1 no record, -1 lookup failed. */
int pav_os_srv_lookup(const char *name, char *target, size_t len, int *port);

#endif

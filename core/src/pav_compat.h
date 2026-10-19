#ifndef PAV_COMPAT_H
#define PAV_COMPAT_H

/* MinGW (MSYS2 UCRT64, G-Q4): the few POSIX calls the core, the fake
   platform, and the Unity tests use that the Windows C runtime lacks.
   Empty on every other platform. File modes are a no-op on Windows (G Lock). */
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static inline char *pav_compat_mkdtemp(char *templ) {
    if (_mktemp_s(templ, strlen(templ) + 1) != 0 || _mkdir(templ) != 0) {
        return NULL;
    }
    return templ;
}

static inline int pav_compat_setenv(const char *name, const char *value, int overwrite) {
    if (!overwrite && getenv(name) != NULL) {
        return 0;
    }
    return _putenv_s(name, value) == 0 ? 0 : -1;
}

#define mkdtemp(t) pav_compat_mkdtemp(t)
#define setenv(n, v, o) pav_compat_setenv((n), (v), (o))
#define unsetenv(n) (_putenv_s((n), "") == 0 ? 0 : -1)
#define lstat stat
#define mkdir(p, m) _mkdir(p)
#define fchmod(fd, m) ((void)(fd), (void)(m), 0)
#define fsync(fd) _commit(fd)
#endif

/* Binary file I/O everywhere: Windows text mode would rewrite \n as \r\n. */
#include <fcntl.h>
#ifndef O_BINARY
#define O_BINARY 0
#endif
#endif

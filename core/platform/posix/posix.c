#define _POSIX_C_SOURCE 200809L

#include "platform_internal.h"
#include "pav_platform.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const PavPlatform *g_platform;

void pav_platform_install(const PavPlatform *platform) {
    if (g_platform == NULL && platform != NULL) {
        g_platform = platform;
    }
}

const PavPlatform *pav_platform(void) {
    return g_platform;
}

int pav_path_join(char *out, size_t len, const char *a, const char *b) {
    size_t na;
    size_t nb;
    int sep;

    if (out == NULL || len == 0 || a == NULL || b == NULL) {
        return -1;
    }
    na = strlen(a);
    nb = strlen(b);
    sep = (na > 0 && a[na - 1] != '/') ? 1 : 0;
    if (na + (size_t)sep + nb + 1 > len) {
        return -1;
    }
    memcpy(out, a, na);
    if (sep) {
        out[na] = '/';
    }
    memcpy(out + na + (size_t)sep, b, nb + 1);
    return 0;
}

int pav_path_is_absolute(const char *path) {
    return path != NULL && path[0] == '/';
}

int pav_path_parent(char *out, size_t len, const char *path) {
    const char *slash;
    size_t n;

    if (out == NULL || path == NULL) {
        return -1;
    }
    slash = strrchr(path, '/');
    if (slash == NULL) {
        return -1;
    }
    if (slash == path) {
        if (len < 2) {
            return -1;
        }
        out[0] = '/';
        out[1] = '\0';
        return 0;
    }
    n = (size_t)(slash - path);
    if (n >= len) {
        return -1;
    }
    memcpy(out, path, n);
    out[n] = '\0';
    return 0;
}

int pav_exists(const char *path) {
    struct stat st;

    return path != NULL && stat(path, &st) == 0;
}

int pav_is_dir(const char *path) {
    struct stat st;

    return path != NULL && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int pav_file_private(const char *path) {
    if (path == NULL || chmod(path, 0600) != 0) {
        return -1;
    }
    return 0;
}

int pav_dir_each(const char *path, int (*visit)(const char *name, void *user), void *user) {
    DIR *dir;
    struct dirent *ent;

    if (path == NULL || visit == NULL) {
        return -1;
    }
    dir = opendir(path);
    if (dir == NULL) {
        return -1;
    }
    while ((ent = readdir(dir)) != NULL) {
        int rc;

        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        rc = visit(ent->d_name, user);
        if (rc != 0) {
            closedir(dir);
            return rc;
        }
    }
    closedir(dir);
    return 0;
}

int64_t pav_posix_now_ms(void) {
    struct timespec ts;

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0 || ts.tv_sec < 0) {
        return -1;
    }
    return (int64_t)ts.tv_sec * 1000 + (int64_t)(ts.tv_nsec / 1000000L);
}

int64_t pav_now_ms(void) {
    const PavPlatform *platform = pav_platform();

    if (platform != NULL && platform->now_ms != NULL) {
        return platform->now_ms();
    }
    return pav_posix_now_ms();
}

int pav_format_utc(char *out, size_t len, int64_t ms) {
    time_t sec;
    struct tm tm;
    int msec;
    int n;

    if (out == NULL || ms < 0) {
        return -1;
    }
    sec = (time_t)(ms / 1000);
    msec = (int)(ms % 1000);
    if (gmtime_r(&sec, &tm) == NULL) {
        return -1;
    }
    n = snprintf(out, len, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1,
                 tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, msec);
    if (n < 0 || (size_t)n >= len) {
        return -1;
    }
    return 0;
}

static int ensure_one(const char *path) {
    struct stat st;

    if (path == NULL || path[0] == '\0') {
        return -1;
    }
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }
    if (mkdir(path, 0700) != 0) {
        if (errno == EEXIST && stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            return 0;
        }
        return -1;
    }
    /* umask would otherwise widen 0700. An existing directory keeps its mode. */
    if (chmod(path, 0700) != 0) {
        return -1;
    }
    return 0;
}

int pav_posix_mkdir_private(const char *path) {
    char buf[4096];
    size_t len;
    size_t i;

    if (path == NULL) {
        return -1;
    }
    len = strlen(path);
    if (len == 0 || len >= sizeof(buf)) {
        return -1;
    }
    memcpy(buf, path, len + 1);
    for (i = 1; i < len; i++) {
        if (buf[i] != '/') {
            continue;
        }
        buf[i] = '\0';
        if (ensure_one(buf) != 0) {
            return -1;
        }
        buf[i] = '/';
    }
    return ensure_one(buf);
}

static int write_temp(const char *path, const void *buf, size_t n, int abort_rename) {
    char tmp[4608];
    size_t path_len;
    size_t off = 0;
    int fd;

    if (path == NULL || (n > 0 && buf == NULL)) {
        return -1;
    }
    path_len = strlen(path);
    if (path_len + 5 > sizeof(tmp)) {
        return -1;
    }
    memcpy(tmp, path, path_len);
    memcpy(tmp + path_len, ".tmp", 5);
    fd = open(tmp, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (fd < 0) {
        return -1;
    }
    if (fchmod(fd, 0600) != 0) {
        close(fd);
        unlink(tmp);
        return -1;
    }
    while (off < n) {
        const char *bytes = buf;
        ssize_t wrote = write(fd, bytes + off, n - off);

        if (wrote < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(fd);
            unlink(tmp);
            return -1;
        }
        if (wrote == 0) {
            close(fd);
            unlink(tmp);
            return -1;
        }
        off += (size_t)wrote;
    }
    if (fsync(fd) != 0) {
        close(fd);
        unlink(tmp);
        return -1;
    }
    if (close(fd) != 0) {
        unlink(tmp);
        return -1;
    }
    if (abort_rename) {
        unlink(tmp);
        return -1;
    }
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

int pav_posix_write_private(const char *path, const void *buf, size_t n) {
    return write_temp(path, buf, n, 0);
}

int pav_posix_write_abort(const char *path, const void *buf, size_t n) {
    return write_temp(path, buf, n, 1);
}

int pav_thread_create(void **out, void *(*fn)(void *), void *arg) {
    pthread_t *thread;
    pthread_attr_t attr;
    int rc;

    if (out == NULL || fn == NULL) {
        return -1;
    }
    thread = malloc(sizeof(*thread));
    if (thread == NULL) {
        return -1;
    }
    if (pthread_attr_init(&attr) != 0) {
        free(thread);
        return -1;
    }
    /* C-CORE Lock: every thread this helper creates uses a 512 KB stack. */
    if (pthread_attr_setstacksize(&attr, 512u * 1024u) != 0) {
        pthread_attr_destroy(&attr);
        free(thread);
        return -1;
    }
    rc = pthread_create(thread, &attr, fn, arg);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        free(thread);
        return -1;
    }
    *out = thread;
    return 0;
}

int pav_thread_join(void *thread) {
    pthread_t *value = thread;
    int rc;

    if (value == NULL) {
        return -1;
    }
    rc = pthread_join(*value, NULL);
    free(value);
    return rc == 0 ? 0 : -1;
}

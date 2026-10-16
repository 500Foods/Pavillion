#define _POSIX_C_SOURCE 200809L

#include "pav_config.h"
#include "pav_cfg.h"
#include "config_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char k_default_index[] =
    "{\n"
    "  \"kind\": \"pavillion-index\",\n"
    "  \"schema\": 1,\n"
    "  \"active\": \"default.json\",\n"
    "  \"profile_dir\": \"profiles\",\n"
    "  \"log_level\": \"info\"\n"
    "}\n";

static const char k_default_profile[] =
    "{\n"
    "  \"kind\": \"pavillion-profile\",\n"
    "  \"schema\": 1,\n"
    "  \"id\": \"default\",\n"
    "  \"name\": \"Default\",\n"
    "  \"storage\": {\n"
    "    \"engine\": \"sqlite\",\n"
    "    \"path\": \"default.sqlite\"\n"
    "  },\n"
    "  \"logs\": \"logs/default.log\",\n"
    "  \"blobs\": \"blobs/default\",\n"
    "  \"cache\": \"cache/default\",\n"
    "  \"accounts\": [],\n"
    "  \"preferences\": {\n"
    "    \"timezone\": \"America/Vancouver\",\n"
    "    \"week_start\": \"monday\",\n"
    "    \"time_format\": \"24h\",\n"
    "    \"theme\": \"system\",\n"
    "    \"density\": \"comfortable\",\n"
    "    \"zoom\": {\n"
    "      \"shell\": 1,\n"
    "      \"navButtons\": 1,\n"
    "      \"navBody\": 1,\n"
    "      \"mailList\": 1,\n"
    "      \"mailDetail\": 1\n"
    "    },\n"
    "    \"window\": {\n"
    "      \"maximized\": false,\n"
    "      \"width\": 1200,\n"
    "      \"height\": 800,\n"
    "      \"x\": null,\n"
    "      \"y\": null,\n"
    "      \"chrome\": \"system\"\n"
    "    },\n"
    "    \"ui\": {\n"
    "      \"nav\": { \"width\": 320, \"min\": 200, \"max\": 500 },\n"
    "      \"mail\": { \"split\": 0.5, \"min\": 0.25, \"max\": 0.75 },\n"
    "      \"contacts\": { \"split\": 0.5, \"min\": 0.25, \"max\": 0.75 },\n"
    "      \"files\": { \"split\": 0.5, \"min\": 0.25, \"max\": 0.75 },\n"
    "      \"fontawesome_kit\": \"45801522ef\"\n"
    "    }\n"
    "  }\n"
    "}\n";

int config_join(char *dst, size_t dst_len, const char *dir, const char *name) {
    int n = snprintf(dst, dst_len, "%s/%s", dir, name);
    if (n < 0 || (size_t)n >= dst_len) {
        return -1;
    }
    return 0;
}

int config_resolve(char *dst, size_t dst_len, const char *workdir, const char *path) {
    if (path[0] == '/') {
        if (strlen(path) >= dst_len) {
            return -1;
        }
        memcpy(dst, path, strlen(path) + 1);
        return 0;
    }
    return config_join(dst, dst_len, workdir, path);
}

static int write_new_file(const char *path, const char *text, char *err, size_t err_len) {
    size_t len = strlen(text);
    size_t off = 0;
    int fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600);

    if (fd < 0) {
        cfg_err(err, err_len, "cannot create %s", path);
        return -1;
    }
    if (fchmod(fd, 0600) != 0) {
        close(fd);
        unlink(path);
        cfg_err(err, err_len, "cannot set mode on %s", path);
        return -1;
    }
    while (off < len) {
        ssize_t wrote = write(fd, text + off, len - off);
        if (wrote < 0) {
            close(fd);
            unlink(path);
            cfg_err(err, err_len, "cannot write %s", path);
            return -1;
        }
        off += (size_t)wrote;
    }
    if (close(fd) != 0) {
        unlink(path);
        cfg_err(err, err_len, "cannot write %s", path);
        return -1;
    }
    return 0;
}

static int write_replacing(const char *path, const char *text, char *err, size_t err_len) {
    char tmp[4104];
    size_t path_len;
    size_t len;
    size_t off;
    int fd;
    int need_nl;

    path_len = strlen(path);
    if (path_len + 5 > sizeof(tmp)) {
        cfg_err(err, err_len, "path is too long");
        return -1;
    }
    memcpy(tmp, path, path_len);
    memcpy(tmp + path_len, ".tmp", 5);
    fd = open(tmp, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (fd < 0) {
        cfg_err(err, err_len, "cannot write %s", path);
        return -1;
    }
    if (fchmod(fd, 0600) != 0) {
        close(fd);
        unlink(tmp);
        cfg_err(err, err_len, "cannot set mode on %s", path);
        return -1;
    }
    len = strlen(text);
    off = 0;
    while (off < len) {
        ssize_t wrote = write(fd, text + off, len - off);
        if (wrote < 0) {
            close(fd);
            unlink(tmp);
            cfg_err(err, err_len, "cannot write %s", path);
            return -1;
        }
        off += (size_t)wrote;
    }
    need_nl = len == 0 || text[len - 1] != '\n';
    if (need_nl && write(fd, "\n", 1) != 1) {
        close(fd);
        unlink(tmp);
        cfg_err(err, err_len, "cannot write %s", path);
        return -1;
    }
    if (fsync(fd) != 0 || close(fd) != 0) {
        unlink(tmp);
        cfg_err(err, err_len, "cannot write %s", path);
        return -1;
    }
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        cfg_err(err, err_len, "cannot write %s", path);
        return -1;
    }
    return 0;
}

int pav_profile_store(const char *path, const PavProfile *profile, char *err, size_t err_len) {
    char *text = NULL;

    if (path == NULL || path[0] == '\0') {
        cfg_err(err, err_len, "missing profile");
        return -1;
    }
    if (pav_profile_rewrite(profile, &text) != 0) {
        cfg_err(err, err_len, "cannot write profile");
        return -1;
    }
    if (write_replacing(path, text, err, err_len) != 0) {
        free(text);
        return -1;
    }
    free(text);
    return 0;
}

int pav_config_ensure(const char *workdir, char *err, size_t err_len) {
    char index_path[4096];
    char dir_path[4096];
    char profile_path[4096];
    struct stat st;

    if (workdir == NULL || workdir[0] == '\0') {
        cfg_err(err, err_len, "missing workdir");
        return -1;
    }
    if (config_join(index_path, sizeof(index_path), workdir, "Pavillion.json") != 0 ||
        config_join(dir_path, sizeof(dir_path), workdir, "profiles") != 0 ||
        config_join(profile_path, sizeof(profile_path), dir_path, "default.json") != 0) {
        cfg_err(err, err_len, "path is too long");
        return -1;
    }
    if (stat(index_path, &st) == 0) {
        return 0;
    }
    if (mkdir(dir_path, 0700) != 0 && errno != EEXIST) {
        cfg_err(err, err_len, "cannot create %s", dir_path);
        return -1;
    }
    if (write_new_file(profile_path, k_default_profile, err, err_len) != 0) {
        return -1;
    }
    if (write_new_file(index_path, k_default_index, err, err_len) != 0) {
        unlink(profile_path);
        return -1;
    }
    return 0;
}

static int ensure_dir(const char *path, mode_t mode) {
    struct stat st;

    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }
    if (mkdir(path, mode) != 0) {
        return -1;
    }
    if (chmod(path, mode) != 0) {
        return -1;
    }
    return 0;
}

static int mkdir_p(const char *path, mode_t mode) {
    char buf[4096];
    size_t len;
    size_t i;

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
        if (ensure_dir(buf, mode) != 0) {
            return -1;
        }
        buf[i] = '/';
    }
    return ensure_dir(buf, mode);
}

static int parent_of(char *dst, size_t dst_len, const char *path) {
    const char *slash = strrchr(path, '/');
    size_t len;

    if (slash == NULL) {
        return -1;
    }
    if (slash == path) {
        if (dst_len < 2) {
            return -1;
        }
        dst[0] = '/';
        dst[1] = '\0';
        return 0;
    }
    len = (size_t)(slash - path);
    if (len >= dst_len) {
        return -1;
    }
    memcpy(dst, path, len);
    dst[len] = '\0';
    return 0;
}

static int create_log_file(const char *path, char *err, size_t err_len) {
    char parent[4096];
    int fd;

    if (parent_of(parent, sizeof(parent), path) != 0 || mkdir_p(parent, 0700) != 0) {
        cfg_err(err, err_len, "cannot create %s", path);
        return -1;
    }
    fd = open(path, O_CREAT | O_WRONLY, 0600);
    if (fd < 0) {
        cfg_err(err, err_len, "cannot create %s", path);
        return -1;
    }
    if (fchmod(fd, 0600) != 0) {
        close(fd);
        cfg_err(err, err_len, "cannot create %s", path);
        return -1;
    }
    if (close(fd) != 0) {
        cfg_err(err, err_len, "cannot create %s", path);
        return -1;
    }
    return 0;
}

int pav_config_prepare(const char *workdir, PavProfile *profile, char *err, size_t err_len) {
    if (workdir == NULL || workdir[0] == '\0' || profile == NULL) {
        cfg_err(err, err_len, "missing workdir");
        return -1;
    }
    if (config_resolve(profile->resolved_logs, sizeof(profile->resolved_logs), workdir, profile->logs) != 0 ||
        config_resolve(profile->resolved_blobs, sizeof(profile->resolved_blobs), workdir, profile->blobs) !=
            0 ||
        config_resolve(profile->resolved_cache, sizeof(profile->resolved_cache), workdir, profile->cache) !=
            0 ||
        config_resolve(profile->resolved_storage, sizeof(profile->resolved_storage), workdir,
                       profile->storage_path) != 0) {
        cfg_err(err, err_len, "path is too long");
        return -1;
    }
    if (create_log_file(profile->resolved_logs, err, err_len) != 0) {
        return -1;
    }
    if (mkdir_p(profile->resolved_blobs, 0700) != 0) {
        cfg_err(err, err_len, "cannot create %s", profile->resolved_blobs);
        return -1;
    }
    if (mkdir_p(profile->resolved_cache, 0700) != 0) {
        cfg_err(err, err_len, "cannot create %s", profile->resolved_cache);
        return -1;
    }
    return 0;
}

#include "pav_config.h"
#include "pav_cfg.h"
#include "pav_platform.h"
#include "config_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char k_default_index[] =
    "{\n"
    "  \"kind\": \"pavillion-index\",\n"
    "  \"schema\": 1,\n"
    "  \"active\": \"default.json\",\n"
    "  \"profile_dir\": \"profiles\",\n"
    "  \"log_level\": \"info\",\n"
    "  \"next_account_number\": 1\n"
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
    "  \"logs\": \"default.log.db\",\n"
    "  \"blobs\": \"blobs/default\",\n"
    "  \"cache\": \"cache/default\",\n"
    "  \"accounts\": [],\n"
    "  \"preferences\": {\n"
    "    \"timezone\": \"America/Vancouver\",\n"
    "    \"week_start\": \"monday\",\n"
    "    \"time_format\": \"24h\",\n"
    "    \"theme\": \"system\",\n"
    "    \"density\": \"comfortable\",\n"
    "    \"window\": {\n"
    "      \"maximized\": false,\n"
    "      \"width\": 1200,\n"
    "      \"height\": 800,\n"
    "      \"x\": null,\n"
    "      \"y\": null,\n"
    "      \"chrome\": \"system\"\n"
    "    },\n"
    "    \"ui\": {\n"
    "      \"zoom\": {\n"
    "        \"shell\": 1,\n"
    "        \"navButtons\": 1,\n"
    "        \"navBody\": 1,\n"
    "        \"mailList\": 1,\n"
    "        \"mailDetail\": 1\n"
    "      },\n"
    "      \"nav\": { \"width\": 320, \"min\": 200, \"max\": 500 },\n"
    "      \"mail\": { \"split\": 0.5, \"min\": 0.25, \"max\": 0.75 },\n"
    "      \"contacts\": { \"split\": 0.5, \"min\": 0.25, \"max\": 0.75 },\n"
    "      \"files\": { \"split\": 0.5, \"min\": 0.25, \"max\": 0.75 },\n"
    "      \"fontawesome_kit\": \"45801522ef\"\n"
    "    }\n"
    "  }\n"
    "}\n";

int config_join(char *dst, size_t dst_len, const char *dir, const char *name) {
    return pav_path_join(dst, dst_len, dir, name);
}

int config_resolve(char *dst, size_t dst_len, const char *workdir, const char *path) {
    if (pav_path_is_absolute(path)) {
        if (strlen(path) >= dst_len) {
            return -1;
        }
        memcpy(dst, path, strlen(path) + 1);
        return 0;
    }
    return pav_path_join(dst, dst_len, workdir, path);
}

static const PavPlatform *need_platform(void) {
    const PavPlatform *platform = pav_platform();

    if (platform == NULL || platform->mkdir_private == NULL || platform->write_private == NULL) {
        return NULL;
    }
    return platform;
}

int pav_profile_store(const char *path, const PavProfile *profile, char *err, size_t err_len) {
    const PavPlatform *platform = need_platform();
    char *text = NULL;
    size_t len;

    if (path == NULL || path[0] == '\0') {
        cfg_err(err, err_len, "missing profile");
        return -1;
    }
    if (platform == NULL) {
        cfg_err(err, err_len, "cannot write %s", path);
        return -1;
    }
    if (pav_profile_rewrite(profile, &text) != 0 || text == NULL) {
        cfg_err(err, err_len, "cannot write profile");
        return -1;
    }
    len = strlen(text);
    if (len == 0 || text[len - 1] != '\n') {
        char *grown = realloc(text, len + 2);

        if (grown == NULL) {
            free(text);
            cfg_err(err, err_len, "cannot write profile");
            return -1;
        }
        text = grown;
        text[len] = '\n';
        text[len + 1] = '\0';
        len++;
    }
    if (platform->write_private(path, text, len) != 0) {
        free(text);
        cfg_err(err, err_len, "cannot write %s", path);
        return -1;
    }
    free(text);
    return 0;
}

static int write_new_file(const char *path, const char *text, char *err, size_t err_len) {
    const PavPlatform *platform = need_platform();

    if (platform == NULL || platform->write_private(path, text, strlen(text)) != 0) {
        cfg_err(err, err_len, "cannot create %s", path);
        return -1;
    }
    return 0;
}

int pav_config_ensure(const char *workdir, char *err, size_t err_len) {
    const PavPlatform *platform = need_platform();
    char index_path[4096];
    char dir_path[4096];
    char profile_path[4096];

    if (workdir == NULL || workdir[0] == '\0') {
        cfg_err(err, err_len, "missing workdir");
        return -1;
    }
    if (platform == NULL) {
        cfg_err(err, err_len, "cannot create profile");
        return -1;
    }
    if (config_join(index_path, sizeof(index_path), workdir, "Pavillion.json") != 0 ||
        config_join(dir_path, sizeof(dir_path), workdir, "profiles") != 0 ||
        config_join(profile_path, sizeof(profile_path), dir_path, "default.json") != 0) {
        cfg_err(err, err_len, "path is too long");
        return -1;
    }
    if (pav_exists(index_path)) {
        return 0;
    }
    if (platform->mkdir_private(dir_path) != 0) {
        cfg_err(err, err_len, "cannot create %s", dir_path);
        return -1;
    }
    /* A profile left behind without an index is not overwritten. */
    if (pav_exists(profile_path)) {
        cfg_err(err, err_len, "cannot create %s", profile_path);
        return -1;
    }
    if (write_new_file(profile_path, k_default_profile, err, err_len) != 0) {
        return -1;
    }
    if (write_new_file(index_path, k_default_index, err, err_len) != 0) {
        remove(profile_path);
        return -1;
    }
    return 0;
}

int pav_config_prepare(const char *workdir, PavProfile *profile, char *err, size_t err_len) {
    const PavPlatform *platform = need_platform();

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
    if (platform == NULL || platform->mkdir_private(profile->resolved_blobs) != 0) {
        cfg_err(err, err_len, "cannot create %s", profile->resolved_blobs);
        return -1;
    }
    if (platform->mkdir_private(profile->resolved_cache) != 0) {
        cfg_err(err, err_len, "cannot create %s", profile->resolved_cache);
        return -1;
    }
    return 0;
}

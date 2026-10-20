#define _GNU_SOURCE

#include "pav_platform.h"

#include <libsecret/secret.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <unistd.h>

static const SecretSchema g_schema = {
    "com.500nodes.pavillion.Snapshot",
    SECRET_SCHEMA_NONE,
     {{"service", SECRET_SCHEMA_ATTRIBUTE_STRING},
     {"user", SECRET_SCHEMA_ATTRIBUTE_STRING},
     {NULL, 0}},
    0,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
};

extern int pav_stronghold_set(const char *path, const unsigned char *password, size_t password_len,
                              const char *user, const char *label, const char *value);
extern int pav_stronghold_get(const char *path, const unsigned char *password, size_t password_len,
                              const char *user, char **out);
extern int pav_stronghold_delete(const char *path, const unsigned char *password, size_t password_len,
                                 const char *user);
extern int pav_stronghold_take_resaved(void);

static int profile_of(const char *user, char *out, size_t len) {
    const char *slash = strchr(user, '/');
    size_t n;

    if (slash == NULL || slash == user) {
        return -1;
    }
    n = (size_t)(slash - user);
    if (n + 1 > len) {
        return -1;
    }
    memcpy(out, user, n);
    out[n] = '\0';
    return 0;
}

static int snapshot_user(const char *user, char *out, size_t len) {
    char profile[128];

    if (profile_of(user, profile, sizeof(profile)) != 0) {
        return -1;
    }
    return pav_path_join(out, len, profile, "stronghold");
}

static int snapshot_path(const char *user, char *out, size_t len) {
    char profile[128];
    char dir[4096];
    char data[4096];

    if (profile_of(user, profile, sizeof(profile)) != 0) {
        return -1;
    }
    if (pav_platform()->dir(PAV_DIR_DATA, data, sizeof(data)) != 0) {
        return -1;
    }
    if (pav_path_join(dir, sizeof(dir), data, profile) != 0) {
        return -1;
    }
    if (pav_platform()->mkdir_private(dir) != 0) {
        return -1;
    }
    return pav_path_join(out, len, dir, "secrets.stronghold");
}

static int hex_decode(const char *hex, unsigned char *out, size_t out_len) {
    size_t n = strlen(hex);
    size_t i;

    if (n != out_len * 2) {
        return -1;
    }
    for (i = 0; i < out_len; i++) {
        unsigned int byte;
        if (sscanf(hex + (i * 2), "%2x", &byte) != 1) {
            return -1;
        }
        out[i] = (unsigned char)byte;
    }
    return 0;
}

static void hex_encode(const unsigned char *in, size_t n, char *out) {
    static const char digits[] = "0123456789abcdef";
    size_t i;

    for (i = 0; i < n; i++) {
        out[i * 2] = digits[in[i] >> 4];
        out[i * 2 + 1] = digits[in[i] & 0x0f];
    }
    out[n * 2] = '\0';
}

static int keyring_locked(const GError *error) {
    if (error == NULL) {
        return 0;
    }
    return g_error_matches(error, SECRET_ERROR, SECRET_ERROR_IS_LOCKED) ||
           (error->message != NULL && strstr(error->message, "locked") != NULL);
}

static int snapshot_password(const char *user, unsigned char *out, size_t out_len) {
    char snap_user[160];
    GError *error = NULL;
    gchar *stored;
    unsigned char raw[32];
    char hex[65];
    ssize_t got;

    if (out_len != sizeof(raw)) {
        return PAV_SECRET_FAILED;
    }
    if (snapshot_user(user, snap_user, sizeof(snap_user)) != 0) {
        return PAV_SECRET_FAILED;
    }
    stored = secret_password_lookup_sync(&g_schema, NULL, &error, "service", "pavillion", "user", snap_user, NULL);
    if (error != NULL) {
        int locked = keyring_locked(error);
        g_error_free(error);
        return locked ? PAV_SECRET_LOCKED : PAV_SECRET_FAILED;
    }
    if (stored != NULL) {
        int decoded = hex_decode(stored, out, out_len);
        secret_password_free(stored);
        return decoded == 0 ? 0 : PAV_SECRET_FAILED;
    }
    got = getrandom(raw, sizeof(raw), 0);
    if (got != (ssize_t)sizeof(raw)) {
        explicit_bzero(raw, sizeof(raw));
        return PAV_SECRET_FAILED;
    }
    hex_encode(raw, sizeof(raw), hex);
    if (!secret_password_store_sync(&g_schema, SECRET_COLLECTION_DEFAULT, "Pavillion snapshot", hex, NULL, &error,
                                    "service", "pavillion", "user", snap_user, NULL)) {
        int locked = keyring_locked(error);
        if (error != NULL) {
            g_error_free(error);
        }
        explicit_bzero(raw, sizeof(raw));
        explicit_bzero(hex, sizeof(hex));
        return locked ? PAV_SECRET_LOCKED : PAV_SECRET_FAILED;
    }
    memcpy(out, raw, sizeof(raw));
    explicit_bzero(raw, sizeof(raw));
    explicit_bzero(hex, sizeof(hex));
    return 0;
}

static int linux_secret_get(const char *service, const char *user, char **out) {
    unsigned char password[32];
    char path[4096];
    int code;

    if (out != NULL) {
        *out = NULL;
    }
    if (service == NULL || user == NULL || out == NULL || strcmp(service, "pavillion") != 0) {
        return PAV_SECRET_FAILED;
    }
    code = snapshot_password(user, password, sizeof(password));
    if (code != 0) {
        return code;
    }
    if (snapshot_path(user, path, sizeof(path)) != 0) {
        explicit_bzero(password, sizeof(password));
        return PAV_SECRET_FAILED;
    }
    code = pav_stronghold_get(path, password, sizeof(password), user, out);
    explicit_bzero(password, sizeof(password));
    return code;
}

static int linux_secret_set(const char *service, const char *user, const char *label, const char *val) {
    unsigned char password[32];
    char path[4096];
    int code;

    if (service == NULL || user == NULL || label == NULL || val == NULL || strcmp(service, "pavillion") != 0) {
        return PAV_SECRET_FAILED;
    }
    code = snapshot_password(user, password, sizeof(password));
    if (code != 0) {
        return code;
    }
    if (snapshot_path(user, path, sizeof(path)) != 0) {
        explicit_bzero(password, sizeof(password));
        return PAV_SECRET_FAILED;
    }
    code = pav_stronghold_set(path, password, sizeof(password), user, label, val);
    explicit_bzero(password, sizeof(password));
    if (code == 0) {
        (void)pav_file_private(path);
    }
    return code;
}

static int linux_secret_delete(const char *service, const char *user) {
    unsigned char password[32];
    char path[4096];
    int code;

    if (service == NULL || user == NULL || strcmp(service, "pavillion") != 0) {
        return PAV_SECRET_FAILED;
    }
    code = snapshot_password(user, password, sizeof(password));
    if (code != 0) {
        return code;
    }
    if (snapshot_path(user, path, sizeof(path)) != 0) {
        explicit_bzero(password, sizeof(password));
        return PAV_SECRET_FAILED;
    }
    code = pav_stronghold_delete(path, password, sizeof(password), user);
    explicit_bzero(password, sizeof(password));
    return code;
}

void pav_linux_secret_bind(PavPlatform *platform) {
    platform->secret_get = linux_secret_get;
    platform->secret_set = linux_secret_set;
    platform->secret_delete = linux_secret_delete;
    platform->secret_resaved = pav_stronghold_take_resaved;
}

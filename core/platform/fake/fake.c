#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include "platform_internal.h"
#include "pav_platform.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pav_compat.h"

#define SECRET_CAP 32

typedef struct FakeSecret {
    char *service;
    char *user;
    char *label;
    char *value;
} FakeSecret;

static FakeSecret g_secrets[SECRET_CAP];
static int g_locked;
static char g_root[64];
static int g_now_set;
static int64_t g_now_ms;
static int g_abort_write;
static void (*g_net)(void *);
static void *g_net_user;
static void (*g_resume)(void *);
static void *g_resume_user;

static void secret_clear(FakeSecret *slot) {
    free(slot->service);
    free(slot->user);
    free(slot->label);
    free(slot->value);
    memset(slot, 0, sizeof(*slot));
}

static FakeSecret *secret_find(const char *service, const char *user, int *free_slot) {
    int i;

    if (free_slot != NULL) {
        *free_slot = -1;
    }
    for (i = 0; i < SECRET_CAP; i++) {
        if (g_secrets[i].service == NULL) {
            if (free_slot != NULL && *free_slot < 0) {
                *free_slot = i;
            }
            continue;
        }
        if (strcmp(g_secrets[i].service, service) == 0 && strcmp(g_secrets[i].user, user) == 0) {
            return &g_secrets[i];
        }
    }
    return NULL;
}

static int fake_secret_get(const char *service, const char *user, char **out) {
    FakeSecret *slot;

    if (service == NULL || user == NULL || out == NULL) {
        return PAV_SECRET_FAILED;
    }
    *out = NULL;
    if (g_locked) {
        return PAV_SECRET_LOCKED;
    }
    slot = secret_find(service, user, NULL);
    if (slot == NULL) {
        return PAV_SECRET_MISSING;
    }
    *out = strdup(slot->value);
    return *out == NULL ? PAV_SECRET_FAILED : 0;
}

static int fake_secret_set(const char *service, const char *user, const char *label, const char *val) {
    FakeSecret *slot;
    int free_slot = -1;
    char *service_copy;
    char *user_copy;
    char *label_copy;
    char *value_copy;

    if (service == NULL || user == NULL || label == NULL || val == NULL) {
        return PAV_SECRET_FAILED;
    }
    if (g_locked) {
        return PAV_SECRET_LOCKED;
    }
    slot = secret_find(service, user, &free_slot);
    value_copy = strdup(val);
    label_copy = strdup(label);
    if (value_copy == NULL || label_copy == NULL) {
        free(value_copy);
        free(label_copy);
        return PAV_SECRET_FAILED;
    }
    if (slot != NULL) {
        free(slot->label);
        free(slot->value);
        slot->label = label_copy;
        slot->value = value_copy;
        return 0;
    }
    if (free_slot < 0) {
        free(label_copy);
        free(value_copy);
        return PAV_SECRET_FAILED;
    }
    service_copy = strdup(service);
    user_copy = strdup(user);
    if (service_copy == NULL || user_copy == NULL) {
        free(service_copy);
        free(user_copy);
        free(label_copy);
        free(value_copy);
        return PAV_SECRET_FAILED;
    }
    g_secrets[free_slot].service = service_copy;
    g_secrets[free_slot].user = user_copy;
    g_secrets[free_slot].label = label_copy;
    g_secrets[free_slot].value = value_copy;
    return 0;
}

static int fake_secret_delete(const char *service, const char *user) {
    FakeSecret *slot;

    if (service == NULL || user == NULL) {
        return PAV_SECRET_FAILED;
    }
    if (g_locked) {
        return PAV_SECRET_LOCKED;
    }
    slot = secret_find(service, user, NULL);
    if (slot == NULL) {
        return PAV_SECRET_MISSING;
    }
    secret_clear(slot);
    return 0;
}

static int fake_root(void) {
    char templ[] = "/tmp/pav-fake-XXXXXX";

    if (g_root[0] != '\0') {
        return 0;
    }
    if (mkdtemp(templ) == NULL) {
        return -1;
    }
    memcpy(g_root, templ, strlen(templ) + 1);
    return 0;
}

static const char *dir_name(int which) {
    switch (which) {
    case PAV_DIR_DATA:
        return "data";
    case PAV_DIR_CONFIG:
        return "config";
    case PAV_DIR_CACHE:
        return "cache";
    case PAV_DIR_LOG:
        return "log";
    case PAV_DIR_RUNTIME:
        return "runtime";
    default:
        return NULL;
    }
}

static int fake_dir(int which, char *out, size_t len) {
    const char *name = dir_name(which);

    if (name == NULL || out == NULL) {
        return -1;
    }
    if (fake_root() != 0) {
        return -1;
    }
    if (pav_path_join(out, len, g_root, name) != 0) {
        return -1;
    }
    return pav_posix_mkdir_private(out);
}

static int64_t fake_now(void) {
    if (g_now_set) {
        return g_now_ms;
    }
    return pav_posix_now_ms();
}

static int fake_write(const char *path, const void *buf, size_t n) {
    if (g_abort_write) {
        g_abort_write = 0;
        return pav_posix_write_abort(path, buf, n);
    }
    return pav_posix_write_private(path, buf, n);
}

static void fake_on_network(void (*cb)(void *), void *user) {
    g_net = cb;
    g_net_user = user;
    if (g_net == NULL) {
        g_net_user = NULL;
    }
}

static void fake_on_resume(void (*cb)(void *), void *user) {
    g_resume = cb;
    g_resume_user = user;
    if (g_resume == NULL) {
        g_resume_user = NULL;
    }
}

void pav_platform_fake_set_now_ms(int64_t ms) {
    g_now_ms = ms;
    g_now_set = 1;
}

void pav_platform_fake_reset(void) {
    g_now_set = 0;
    g_now_ms = 0;
    g_abort_write = 0;
    g_locked = 0;
}

void pav_platform_fake_set_locked(int locked) {
    g_locked = locked ? 1 : 0;
}

void pav_platform_fake_abort_next_write(void) {
    g_abort_write = 1;
}

const PavPlatform *pav_platform_fake(void) {
    static PavPlatform platform;
    static int ready;

    if (!ready) {
        platform.name = "fake";
        platform.dir = fake_dir;
        platform.mkdir_private = pav_posix_mkdir_private;
        platform.write_private = fake_write;
        platform.secret_get = fake_secret_get;
        platform.secret_set = fake_secret_set;
        platform.secret_delete = fake_secret_delete;
        platform.now_ms = fake_now;
        platform.on_network_change = fake_on_network;
        platform.on_resume = fake_on_resume;
        ready = 1;
    }
    return &platform;
}

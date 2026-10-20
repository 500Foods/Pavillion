#define _POSIX_C_SOURCE 200809L

#include "platform_internal.h"
#include "pav_platform.h"

#include <stdlib.h>
#include <string.h>

#ifdef PAV_HAVE_LIBSECRET
void pav_linux_secret_bind(PavPlatform *platform);
#else
static int secret_unavailable(const char *service, const char *user, char **out) {
    (void)service;
    (void)user;
    if (out != NULL) {
        *out = NULL;
    }
    return PAV_SECRET_FAILED;
}

static int secret_unavailable_set(const char *service, const char *user, const char *label,
                                  const char *val) {
    (void)service;
    (void)user;
    (void)label;
    (void)val;
    return PAV_SECRET_FAILED;
}

static int secret_unavailable_delete(const char *service, const char *user) {
    (void)service;
    (void)user;
    return PAV_SECRET_FAILED;
}
#endif

static void (*g_net)(void *);
static void *g_net_user;
static void (*g_resume)(void *);
static void *g_resume_user;

static void linux_on_network(void (*cb)(void *), void *user) {
    g_net = cb;
    g_net_user = user;
    if (g_net == NULL) {
        g_net_user = NULL;
    }
}

static void linux_on_resume(void (*cb)(void *), void *user) {
    g_resume = cb;
    g_resume_user = user;
    if (g_resume == NULL) {
        g_resume_user = NULL;
    }
}

static int xdg_dir(const char *env_name, const char *home_suffix, char *out, size_t len) {
    const char *env = getenv(env_name);
    char base[4096];

    if (env != NULL && env[0] != '\0') {
        if (strlen(env) >= sizeof(base)) {
            return -1;
        }
        memcpy(base, env, strlen(env) + 1);
    } else if (home_suffix == NULL) {
        return -1;
    } else {
        const char *home = getenv("HOME");

        if (home == NULL || home[0] == '\0') {
            return -1;
        }
        if (pav_path_join(base, sizeof(base), home, home_suffix) != 0) {
            return -1;
        }
    }
    return pav_path_join(out, len, base, "pavillion");
}

static int linux_dir(int which, char *out, size_t len) {
    switch (which) {
    case PAV_DIR_DATA:
        return xdg_dir("XDG_DATA_HOME", ".local/share", out, len);
    case PAV_DIR_CONFIG:
        return xdg_dir("XDG_CONFIG_HOME", ".config", out, len);
    case PAV_DIR_CACHE:
        return xdg_dir("XDG_CACHE_HOME", ".cache", out, len);
    case PAV_DIR_LOG:
        return xdg_dir("XDG_STATE_HOME", ".local/state", out, len);
    case PAV_DIR_RUNTIME:
        return xdg_dir("XDG_RUNTIME_DIR", NULL, out, len);
    default:
        return -1;
    }
}

static const PavPlatform *linux_platform(void) {
    static PavPlatform platform;
    static int ready;

    if (!ready) {
        platform.name = "linux";
        platform.dir = linux_dir;
        platform.mkdir_private = pav_posix_mkdir_private;
        platform.write_private = pav_posix_write_private;
#ifdef PAV_HAVE_LIBSECRET
        pav_linux_secret_bind(&platform);
#else
        platform.secret_get = secret_unavailable;
        platform.secret_set = secret_unavailable_set;
        platform.secret_delete = secret_unavailable_delete;
#endif
        platform.now_ms = pav_posix_now_ms;
        platform.on_network_change = linux_on_network;
        platform.on_resume = linux_on_resume;
        platform.srv_lookup = pav_os_srv_lookup;
        ready = 1;
    }
    return &platform;
}

void pav_platform_install_host(void) {
    pav_platform_install(linux_platform());
}

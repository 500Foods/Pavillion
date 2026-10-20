#ifndef PAV_PLATFORM_H
#define PAV_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

/* dir() returns the pavillion directory for that kind. LOG is XDG state.
   RUNTIME has no home fallback: it fails when XDG_RUNTIME_DIR is unset. */
#define PAV_DIR_DATA 1
#define PAV_DIR_CONFIG 2
#define PAV_DIR_CACHE 3
#define PAV_DIR_LOG 4
#define PAV_DIR_RUNTIME 5

/* secret_get/set/delete. 0 is stored or deleted. */
#define PAV_SECRET_MISSING 1
#define PAV_SECRET_LOCKED 2
#define PAV_SECRET_FAILED 3

typedef struct PavPlatform {
    const char *name; /* "linux" or "fake" */
    int (*dir)(int which, char *out, size_t len);
    int (*mkdir_private)(const char *path); /* 0700, including missing parents */
    int (*write_private)(const char *path, const void *buf, size_t n); /* 0600, atomic rename */
    int (*secret_get)(const char *service, const char *user, char **out);
    int (*secret_set)(const char *service, const char *user, const char *label, const char *val);
    int (*secret_delete)(const char *service, const char *user);
    /* 1 once after a secret_get re-saved an older snapshot at the fast work
       factor (K Lock), else 0. May be NULL. */
    int (*secret_resaved)(void);
    int64_t (*now_ms)(void); /* UTC unix milliseconds, or -1 */
    void (*on_network_change)(void (*cb)(void *), void *user);
    void (*on_resume)(void (*cb)(void *), void *user);
    /* SRV lookup for name: the lowest-priority, highest-weight target (no
       trailing dot) and port. 0 found, 1 no record, -1 lookup failed. */
    int (*srv_lookup)(const char *name, char *target, size_t len, int *port);
} PavPlatform;

/* The first install wins. pavillion_start will not replace a test's fake. */
void pav_platform_install(const PavPlatform *platform);
const PavPlatform *pav_platform(void);

/* Linux table, or the fake where the core was built without it. */
void pav_platform_install_host(void);

const PavPlatform *pav_platform_fake(void);
void pav_platform_fake_set_now_ms(int64_t ms);
void pav_platform_fake_reset(void);
/* The next write_private builds the temp file, deletes it, and fails. */
void pav_platform_fake_abort_next_write(void);
/* Non-zero: secret_get/set/delete return PAV_SECRET_LOCKED and do not touch the store. */
void pav_platform_fake_set_locked(int locked);
/* The fake srv_lookup answers name with target:port. NULL target: no record.
   One entry; pav_platform_fake_reset clears it. */
void pav_platform_fake_set_srv(const char *name, const char *target, int port);

int pav_path_join(char *out, size_t len, const char *a, const char *b);
int pav_path_is_absolute(const char *path);
int pav_path_parent(char *out, size_t len, const char *path);

int pav_exists(const char *path);
int pav_is_dir(const char *path);
/* Mode 0600 on an existing file. Returns 0, or -1. */
int pav_file_private(const char *path);

/* Calls visit for each name except "." and "..". Returns 0, or -1 when the
   directory cannot be opened. A non-zero visit result stops the walk. */
int pav_dir_each(const char *path, int (*visit)(const char *name, void *user), void *user);

/* Installed clock, or the real clock when no platform is installed yet. */
int64_t pav_now_ms(void);
/* Writes YYYY-MM-DDThh:mm:ss.sssZ. Returns 0, or -1 when ms is negative. */
int pav_format_utc(char *out, size_t len, int64_t ms);

int pav_thread_create(void **out, void *(*fn)(void *), void *arg);
int pav_thread_join(void *thread);

#endif

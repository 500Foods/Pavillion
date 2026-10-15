#ifndef PAV_CONFIG_H
#define PAV_CONFIG_H

#include <stddef.h>

typedef struct PavIndex {
    int schema;
    char kind[32];
    char active[256];
    char profile_dir[256];
    char log_level[16];
    void *doc;
} PavIndex;

typedef struct PavProfile {
    int schema;
    char kind[32];
    char id[64];
    char name[256];
    char engine[32];
    char storage_path[256];
    char logs[256];
    char blobs[256];
    char cache[256];
    char timezone[64];
    char week_start[16];
    char time_format[8];
    char theme[16];
    char density[16];
    double zoom_shell;
    int window_maximized;
    int window_width;
    int window_height;
    int window_x_set;
    int window_y_set;
    int window_x;
    int window_y;
    char chrome[16];
    size_t account_count;
    char resolved_logs[4096];
    char resolved_blobs[4096];
    char resolved_cache[4096];
    char resolved_storage[4096];
    void *doc;
} PavProfile;

/* On success, *out is allocated and the caller frees it. Returns 0, or -1. */
int pav_index_parse(const char *text, PavIndex **out, char *err, size_t err_len);
int pav_profile_parse(const char *text, PavProfile **out, char *err, size_t err_len);

/* Dumps the in-memory object. Caller frees *text. Returns 0, or -1. */
int pav_index_rewrite(const PavIndex *index, char **text);
int pav_profile_rewrite(const PavProfile *profile, char **text);

void pav_index_free(PavIndex *index);
void pav_profile_free(PavProfile *profile);

/* When Pavillion.json is missing, create it and profiles/default.json, mode 0600.
   When the index exists, leave the profile file alone. Returns 0, or -1. */
int pav_config_ensure(const char *workdir, char *err, size_t err_len);

typedef struct PavConfigLoad {
    char state[32]; /* "ready" or "error" */
    char error[512];
    PavProfile *profile; /* set only when state is ready */
} PavConfigLoad;

/* Reads the index and the active profile. Does not create files.
   Returns 0 when the index was read, including when state is error.
   Returns -1 when the index itself cannot be read. */
int pav_config_load(const char *workdir, PavConfigLoad **out, char *err, size_t err_len);
void pav_config_load_free(PavConfigLoad *load);

/* Resolves logs, blobs, cache, and storage.path. Creates the log file mode 0600
   and the blobs and cache directories. blobs is 0700. Does not create the
   SQLite file. Returns 0, or -1 and err names the path that failed. */
int pav_config_prepare(const char *workdir, PavProfile *profile, char *err, size_t err_len);

/* sqlite is accepted. A missing engine is sqlite. Any other engine is an
   error whose text contains that engine name. Does not call a database. */
int pav_profile_check_engine(const PavProfile *profile, char *err, size_t err_len);

#endif

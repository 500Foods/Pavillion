#ifndef PAV_CONFIG_H
#define PAV_CONFIG_H

#include "pavillion.h"

#include <stddef.h>

/* Internal window snapshot. Not part of the FFI. G-8 removed it from pavillion.h. */
typedef struct PavShellPrefs {
    int window_maximized;
    int window_width;
    int window_height;
    int window_x_set;
    int window_y_set;
    int window_x;
    int window_y;
    char chrome[16];
    int log_max_mb;    /* preferences.log_max_mb, 1 to 100, default 10 */
    int logs_migrated; /* logs named an old .log file and now names <id>.log.db */
} PavShellPrefs;

#define PAV_PREFS_WINDOW 2u

typedef struct PavIndex {
    int schema;
    char kind[32];
    char active[256];
    char profile_dir[256];
    char log_level[16];
    int next_account_number;
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
    int window_maximized;
    int window_width;
    int window_height;
    int window_x_set;
    int window_y_set;
    int window_x;
    int window_y;
    char chrome[16];
    int log_max_mb;    /* preferences.log_max_mb, 1 to 100, default 10 */
    int logs_migrated; /* logs named an old .log file and now names <id>.log.db */
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
    char profile_path[4096]; /* set when profile is set */
    char log_level[16]; /* index log_level, copied before the index is freed */
    int next_account_number; /* index next_account_number, copied before the index is freed */
    PavProfile *profile; /* set only when state is ready */
} PavConfigLoad;

/* Reads the index and the active profile. Does not create files.
   Returns 0 when the index was read, including when state is error.
   Returns -1 when the index itself cannot be read. */
int pav_config_load(const char *workdir, PavConfigLoad **out, char *err, size_t err_len);
void pav_config_load_free(PavConfigLoad *load);

/* Resolves logs, blobs, cache, and storage.path. Creates nothing for logs
   (the log thread creates log.db, H-4). Creates the blobs and cache
   directories; blobs is 0700. Does not create the SQLite file. Returns 0, or -1 and err names the path that failed. */
int pav_config_prepare(const char *workdir, PavProfile *profile, char *err, size_t err_len);

/* sqlite is accepted. A missing engine is sqlite. Any other engine is an
   error whose text contains that engine name. Does not call a database. */
int pav_profile_check_engine(const PavProfile *profile, char *err, size_t err_len);

/* Moves preferences.zoom, and a layout key that still sits beside
   preferences.ui, into that object, then deletes the old key. Creates
   preferences.ui when it is missing. Does not invent keys inside the blob.
   *changed is 1 when the document changed. Schema stays 1. Returns 0, or -1. */
int pav_profile_migrate_ui(PavProfile *profile, int *changed, char *err, size_t err_len);

/* Reads the window. Returns 0, or -1. */
int pav_profile_read_shell(const PavProfile *profile, PavShellPrefs *out, char *err, size_t err_len);

/* Writes the window when mask includes PAV_PREFS_WINDOW. Does not write the
   file and does not touch preferences.ui. Returns 0, or -1. */
int pav_profile_write_shell(PavProfile *profile, const PavShellPrefs *in, unsigned mask,
                            char *err, size_t err_len);

/* Reads preferences.ui as a JSON object string (caller frees). A missing
   object is "{}". Returns 0, or -1. */
int pav_profile_read_ui(const PavProfile *profile, char **out, char *err, size_t err_len);

/* Replaces preferences.ui with json. json is an object, at most
   PAV_UI_JSON_MAX bytes, with no forbidden key. Does not write the file.
   Returns 0, or -1. */
int pav_profile_write_ui(PavProfile *profile, const char *json, char *err, size_t err_len);

/* Reads preferences.ui.icons into out as a JSON object string (caller frees).
   When icons is missing, writes an empty object and sets *added. Returns 0, or -1. */
int pav_profile_read_icons(const PavProfile *profile, char **out, char *err, size_t err_len);

/* Writes the contents of `json` (a JSON object mapping icon keys to FA class
   strings) into preferences.ui.icons. Does not write the file. Returns 0, or -1. */
int pav_profile_write_icons(PavProfile *profile, const char *json, char *err, size_t err_len);

/* Writes preferences.ui.fontawesome_kit. Does not write the file. Returns 0, or -1. */
int pav_profile_write_kit(PavProfile *profile, const char *kit, char *err, size_t err_len);

/* Reads preferences.ui.fontawesome_kit into out (default "45801522ef" when missing).
   Returns 0, or -1. */
int pav_profile_read_kit(const PavProfile *profile, char *out, size_t out_len, char *err, size_t err_len);

/* Rewrites the profile over path, mode 0600. Returns 0, or -1. */
int pav_profile_store(const char *path, const PavProfile *profile, char *err, size_t err_len);

#endif

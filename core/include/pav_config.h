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

#endif

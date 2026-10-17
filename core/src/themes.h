#ifndef THEMES_H
#define THEMES_H

#include "dispatch.h"

#include <stddef.h>

int pav_themes_list(json_t *args, json_t **data, PavError *err);
int pav_themes_ensure(json_t *args, json_t **data, PavError *err);
int pav_themes_folder_set(json_t *args, json_t **data, PavError *err);

/* Reads one image or font beside the themes folder. *body is malloc'd.
   *type points at a static string. Returns 0, or -1 when the URL is refused
   or the file cannot be read. */
int pav_theme_read_asset(const char *url, void **body, size_t *len, const char **type);

#endif

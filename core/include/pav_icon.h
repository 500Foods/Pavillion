#ifndef PAV_ICON_H
#define PAV_ICON_H

#include "dispatch.h"

#include <jansson.h>

#include <stddef.h>
#include <stdint.h>

/* 0 when preferences.ui.iconLibraries is absent or valid. */
int pav_icon_libraries_check(const json_t *ui, char *err, size_t err_len);

/* SHA-256 hex, 64 chars plus NUL. */
int pav_icon_sha256(const void *data, size_t n, char out[65]);

int pav_cmd_asset_put(json_t *args, json_t **data, PavError *err);
int pav_cmd_asset_import(json_t *args, json_t **data, PavError *err);
int pav_cmd_asset_read(json_t *args, json_t **data, PavError *err);
int pav_cmd_asset_list(json_t *args, json_t **data, PavError *err);
int pav_cmd_asset_delete(json_t *args, json_t **data, PavError *err);
int pav_cmd_asset_fetch(json_t *args, json_t **data, PavError *err);
int pav_cmd_iconlib_refresh(json_t *args, json_t **data, PavError *err);
int pav_cmd_iconlib_search(json_t *args, json_t **data, PavError *err);

/* Store a metadata document already in memory. The writer must be running.
   *count receives the number of names stored. */
int pav_iconlib_ingest(const char *library_id, const char *body, int *count, char *err, size_t err_len);

/* pav_protocol helpers. *body is malloc'd for an asset and must be freed.
   A system icon's *body points at static bytes and must not be freed. */
int pav_icon_read_asset(const char *url, void **body, size_t *len, const char **type);
int pav_icon_read_system(const char *url, const void **body, size_t *len, const char **type);

#endif

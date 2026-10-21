#ifndef PAV_BLOB_H
#define PAV_BLOB_H

#include <stddef.h>
#include <stdint.h>

/* Andrew, 2026-10-21. Megabytes. 0 is not a profile value; tests may set it. */
#define PAV_CACHE_BODIES_MB 2048
#define PAV_CACHE_BLOBS_MB 4096
#define PAV_CACHE_MB_MIN 1
#define PAV_CACHE_MB_MAX (1024 * 1024)

/* Resolved blobs directory for the open profile. */
int pav_blob_bind(const char *root);
void pav_blob_unbind(void);

/* blobs/<profile>/<encoded ra>/<hh>/<encoded id>. hh is the first two
   characters of the base64url blob id. Both ids are encoded so a slash
   or ".." stays one path segment. Returns 0, or -1. */
int pav_blob_path(const char *ra, const char *blob_id, char *out, size_t len);
int pav_blob_write(const char *ra, const char *blob_id, const void *buf, size_t n);
int pav_blob_exists(const char *ra, const char *blob_id);
int pav_blob_remove(const char *ra, const char *blob_id);

/* In-memory limits used by pav_cache_evict. The profile is the stored copy. */
void pav_cache_set_limits(int bodies_mb, int blobs_mb);

/* Deletes oldest unprotected body files and blobs until each budget fits.
   Drafts, flagged mail, and outbox rows stay. An activity is cache.evict
   only when a budget is already over. Returns 0, or -1. */
int pav_cache_evict(void);

/* $XDG_RUNTIME_DIR/pavillion/open, mode 0700. Remove deletes the tree. */
int pav_runtime_open_prepare(void);
void pav_runtime_open_remove(void);

#endif

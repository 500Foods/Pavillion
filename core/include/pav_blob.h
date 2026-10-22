#ifndef PAV_BLOB_H
#define PAV_BLOB_H

#include <jansson.h>
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
/* Copies the bound blob directory. 0 when one is set, or -1. */
int pav_blob_root(char *out, size_t len);

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

/* Bytes of body files and blob files on disk, grouped by profile account.
   A missing file counts as 0. The same blob file counts once. Caller frees
   with pav_cache_local_usage_free. Returns 0, or -1. */
typedef struct PavCacheUse {
    char *account_id;
    int64_t bodies;
    int64_t blobs;
} PavCacheUse;

int pav_cache_local_usage(PavCacheUse **out, size_t *n);
void pav_cache_local_usage_free(PavCacheUse *rows, size_t n);

/* 1 when a Quota/get response has an octets quota, and *used is that
   value. A quota whose types include Mail wins over any other octets quota. */
int pav_quota_pick_octets(const char *body, int64_t *used);

/* Compact Quota/get request for one JMAP account id. Caller frees. */
char *pav_quota_request(const char *jmap_id);

/* One server store on a cache.usage read. has_* is 0 when that figure
   was not in the response. count_from_query is set when a query total
   supplied the count, so a later quota count does not replace it. */
typedef struct PavStoreFigure {
    int has_count;
    int64_t count;
    int count_from_query;
    int has_bytes;
    int64_t bytes;
    /* Mail only. Decoded text plus HTML, decoded attachments, and how
       many attachment parts that sum counted. */
    int has_split;
    int64_t body_bytes;
    int64_t part_bytes;
    int64_t part_count;
} PavStoreFigure;

/* Figures from one JMAP response. A mixed octets quota is server_bytes.
   Page fields describe the last Email or FileNode query/get pair. */
typedef struct PavUsageParse {
    PavStoreFigure mail;
    PavStoreFigure contacts;
    PavStoreFigure calendars;
    PavStoreFigure files;
    int has_server_bytes;
    int64_t server_bytes;
    int page_ok;
    int page_error;
    int page_has_total;
    int64_t page_total;
    int page_ids;
    int64_t page_bytes;
    int page_sized;
    int page_unsized;
    int page_not_found;
    int page_list;
} PavUsageParse;

/* 0 when body is a JSON object. -1 when body or out is unusable. */
int pav_usage_parse(const char *body, PavUsageParse *out);

/* Ids are JMAP account ids. A null or empty id omits that call.
   files_urn is the files capability to advertise; null uses filenode.
   Caller frees. NULL when nothing would be asked. */
typedef struct PavUsageAsk {
    const char *quota_id;
    const char *mail_id;
    const char *contacts_id;
    const char *calendars_id;
    const char *files_id;
    const char *files_urn;
} PavUsageAsk;

char *pav_usage_quick_request(const PavUsageAsk *ask);

/* store is "mail" or "files". position is the query window. Caller frees. */
char *pav_usage_measure_request(const char *jmap_id, const char *store,
                                const char *files_urn, int position);

/* One Check sizes request may carry several query windows. ok is 0 when a
   window is incomplete, repeats an id, or is short before the last window. */
typedef struct PavUsageBatch {
    int ok;
    int windows;
    int ids;
    int64_t bytes;
    /* Mail only. Decoded text plus HTML, decoded attachments, and the
       attachment part count. */
    int64_t body_bytes;
    int64_t attachment_bytes;
    int64_t attachment_count;
    int has_total;
    int64_t total;
    int last_ids;
    char first_id[256];
} PavUsageBatch;

/* 0 when body is a JSON object. -1 when body, store, or out is unusable.
   store is "mail" or "files". */
int pav_usage_parse_batch(const char *body, const char *store, PavUsageBatch *out);

/* positions has count entries (1..8). page is the query limit. Caller frees. */
char *pav_usage_measure_batch(const char *jmap_id, const char *store, const char *files_urn,
                              int page, const int *positions, int count);

/* 1 when name is cache.usage and args is exactly {"cancel":true}. reply is
   the envelope, or NULL when it could not be built. 0 leaves the call queued. */
int pav_cache_usage_preempt(const char *name, json_t *args, char **reply);

/* $XDG_RUNTIME_DIR/pavillion/open, mode 0700. Remove deletes the tree. */
int pav_runtime_open_prepare(void);
void pav_runtime_open_remove(void);

#endif

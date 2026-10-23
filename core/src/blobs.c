#define _POSIX_C_SOURCE 200809L

#include "pav_blob.h"

#include "pav_activity.h"
#include "pav_db.h"
#include "pav_fts.h"
#include "pav_platform.h"
#include "pavillion.h"

#include <sqlite3.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pav_compat.h"

static char g_root[4096];
static int g_bodies_mb = PAV_CACHE_BODIES_MB;
static int g_blobs_mb = PAV_CACHE_BLOBS_MB;

static const char k_b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static int b64url(const char *in, char *out, size_t out_len) {
    const unsigned char *src;
    size_t n;
    size_t i = 0;
    size_t o = 0;

    if (in == NULL || out == NULL) {
        return -1;
    }
    n = strlen(in);
    if (n == 0 || n > 1024) {
        return -1;
    }
    src = (const unsigned char *)in;
    if (((n + 2) / 3) * 4 + 1 > out_len) {
        return -1;
    }
    while (i < n) {
        unsigned int value = (unsigned int)src[i++] << 16;
        int remain = 1;

        if (i < n) {
            value |= (unsigned int)src[i++] << 8;
            remain = 2;
        }
        if (i < n) {
            value |= (unsigned int)src[i++];
            remain = 3;
        }
        out[o++] = k_b64[(value >> 18) & 63u];
        out[o++] = k_b64[(value >> 12) & 63u];
        if (remain >= 2) {
            out[o++] = k_b64[(value >> 6) & 63u];
        }
        if (remain == 3) {
            out[o++] = k_b64[value & 63u];
        }
    }
    out[o] = '\0';
    if (o < 2) {
        return -1;
    }
    return (int)o;
}

int pav_blob_bind(const char *root) {
    size_t n;

    if (root == NULL || root[0] == '\0') {
        return -1;
    }
    n = strlen(root);
    if (n >= sizeof(g_root)) {
        return -1;
    }
    memcpy(g_root, root, n + 1);
    return 0;
}

void pav_blob_unbind(void) {
    g_root[0] = '\0';
}

int pav_blob_root(char *out, size_t len) {
    size_t n;

    if (out == NULL || len == 0 || g_root[0] == '\0') {
        return -1;
    }
    n = strlen(g_root);
    if (n >= len) {
        return -1;
    }
    memcpy(out, g_root, n + 1);
    return 0;
}

int pav_blob_path(const char *ra, const char *blob_id, char *out, size_t len) {
    char enc_ra[1400];
    char enc_id[1400];
    char shard[3];
    char dir_ra[4096];
    char dir_shard[4096];
    int n_ra;
    int n_id;

    if (g_root[0] == '\0' || out == NULL) {
        return -1;
    }
    n_ra = b64url(ra, enc_ra, sizeof(enc_ra));
    n_id = b64url(blob_id, enc_id, sizeof(enc_id));
    if (n_ra < 2 || n_id < 2) {
        return -1;
    }
    shard[0] = enc_id[0];
    shard[1] = enc_id[1];
    shard[2] = '\0';
    if (pav_path_join(dir_ra, sizeof(dir_ra), g_root, enc_ra) != 0 ||
        pav_path_join(dir_shard, sizeof(dir_shard), dir_ra, shard) != 0 ||
        pav_path_join(out, len, dir_shard, enc_id) != 0) {
        return -1;
    }
    return 0;
}

int pav_blob_write(const char *ra, const char *blob_id, const void *buf, size_t n) {
    const PavPlatform *platform = pav_platform();
    char path[4096];
    char parent[4096];

    if (n > 0 && buf == NULL) {
        return -1;
    }
    if (platform == NULL || platform->mkdir_private == NULL || platform->write_private == NULL) {
        return -1;
    }
    if (pav_blob_path(ra, blob_id, path, sizeof(path)) != 0 ||
        pav_path_parent(parent, sizeof(parent), path) != 0) {
        return -1;
    }
    if (platform->mkdir_private(parent) != 0) {
        return -1;
    }
    return platform->write_private(path, buf, n);
}

int pav_blob_exists(const char *ra, const char *blob_id) {
    char path[4096];

    if (pav_blob_path(ra, blob_id, path, sizeof(path)) != 0) {
        return 0;
    }
    return pav_exists(path) && !pav_is_dir(path);
}

int pav_blob_remove(const char *ra, const char *blob_id) {
    char path[4096];

    if (pav_blob_path(ra, blob_id, path, sizeof(path)) != 0) {
        return -1;
    }
    if (!pav_exists(path)) {
        return 0;
    }
    return remove(path) == 0 ? 0 : -1;
}

void pav_cache_set_limits(int bodies_mb, int blobs_mb) {
    g_bodies_mb = bodies_mb < 0 ? 0 : bodies_mb;
    g_blobs_mb = blobs_mb < 0 ? 0 : blobs_mb;
}

static int64_t budget_bytes(int mb) {
    if (mb <= 0) {
        return 0;
    }
    return (int64_t)mb * 1024 * 1024;
}

static int64_t file_bytes(const char *path) {
    int64_t n;

    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    n = pav_file_size(path);
    return n > 0 ? n : 0;
}

static char *dup_text(const unsigned char *text) {
    if (text == NULL || text[0] == '\0') {
        return NULL;
    }
    return strdup((const char *)text);
}

typedef struct BodyCand {
    int64_t rowid;
    int64_t opened;
    int protect;
    int64_t bytes;
    char *paths[3];
} BodyCand;

typedef struct BlobCand {
    int64_t rowid;
    int64_t opened;
    int protect;
    int64_t bytes;
    int attachment;
    char *ra;
    char *blob_id;
    char *part_id;
} BlobCand;

static int cmp_body(const void *left, const void *right) {
    const BodyCand *a = left;
    const BodyCand *b = right;

    if (a->opened != b->opened) {
        return a->opened < b->opened ? -1 : 1;
    }
    if (a->rowid != b->rowid) {
        return a->rowid < b->rowid ? -1 : 1;
    }
    return 0;
}

static int cmp_blob(const void *left, const void *right) {
    const BlobCand *a = left;
    const BlobCand *b = right;

    if (a->opened != b->opened) {
        return a->opened < b->opened ? -1 : 1;
    }
    if (a->rowid != b->rowid) {
        return a->rowid < b->rowid ? -1 : 1;
    }
    return 0;
}

static void free_body_paths(BodyCand *row) {
    int p;

    for (p = 0; p < 3; p++) {
        free(row->paths[p]);
        row->paths[p] = NULL;
    }
}

static void free_bodies(BodyCand *rows, size_t n) {
    size_t i;

    if (rows == NULL) {
        return;
    }
    for (i = 0; i < n; i++) {
        free_body_paths(&rows[i]);
    }
    free(rows);
}

static void free_blobs(BlobCand *rows, size_t n) {
    size_t i;

    for (i = 0; i < n; i++) {
        free(rows[i].ra);
        free(rows[i].blob_id);
        free(rows[i].part_id);
    }
    free(rows);
}

static int grow_ptr(void **items, size_t *cap, size_t need, size_t width) {
    size_t next;
    void *grown;

    if (need <= *cap) {
        return 0;
    }
    next = *cap == 0 ? 8 : *cap;
    while (next < need) {
        if (next > (size_t)-1 / 2) {
            return -1;
        }
        next *= 2;
    }
    grown = realloc(*items, next * width);
    if (grown == NULL) {
        return -1;
    }
    *items = grown;
    *cap = next;
    return 0;
}

static const char k_body_sql[] =
    "SELECT e.rowid, COALESCE(e.last_opened, 0), "
    "CASE WHEN COALESCE(e.draft, 0) != 0 OR COALESCE(e.flagged, 0) != 0 OR EXISTS ("
    "  SELECT 1 FROM outbox o WHERE o.ra_id = e.ra_id AND o.draft_email_id = e.id"
    ") THEN 1 ELSE 0 END, "
    "b.text_plain_path, b.html_raw_path, b.html_sanitized_path "
    "FROM email e LEFT JOIN body_cache b ON b.email_rowid = e.rowid";

static int load_bodies(BodyCand **out, size_t *out_n, int64_t *total) {
    static const char k_key[] = "cache.body_list";
    PavDbRead *read = NULL;
    sqlite3_stmt *stmt = NULL;
    BodyCand *rows = NULL;
    size_t n = 0;
    size_t cap = 0;
    int rc;

    *out = NULL;
    *out_n = 0;
    *total = 0;
    if (pav_db_read_begin(&read) != 0) {
        return -1;
    }
    if (pav_db_prepare(pav_db_read_handle(read), k_key, k_body_sql, &stmt) != 0) {
        pav_db_read_end(read);
        return -1;
    }
    while ((rc = pav_db_step(stmt)) == SQLITE_ROW) {
        BodyCand row;
        int p;

        memset(&row, 0, sizeof(row));
        row.rowid = sqlite3_column_int64(stmt, 0);
        row.opened = sqlite3_column_int64(stmt, 1);
        row.protect = sqlite3_column_int(stmt, 2);
        for (p = 0; p < 3; p++) {
            row.paths[p] = dup_text(sqlite3_column_text(stmt, 3 + p));
            row.bytes += file_bytes(row.paths[p]);
        }
        if (grow_ptr((void **)&rows, &cap, n + 1, sizeof(*rows)) != 0) {
            free_body_paths(&row);
            free_bodies(rows, n);
            pav_db_read_end(read);
            return -1;
        }
        rows[n++] = row;
        *total += row.bytes;
    }
    pav_db_read_end(read);
    if (rc != SQLITE_DONE) {
        free_bodies(rows, n);
        return -1;
    }
    *out = rows;
    *out_n = n;
    return 0;
}

static const char k_email_blob_sql[] =
    "SELECT e.rowid, COALESCE(e.last_opened, 0), "
    "CASE WHEN COALESCE(e.draft, 0) != 0 OR COALESCE(e.flagged, 0) != 0 OR EXISTS ("
    "  SELECT 1 FROM outbox o WHERE o.ra_id = e.ra_id AND o.draft_email_id = e.id"
    ") THEN 1 ELSE 0 END, e.ra_id, e.blob_id "
    "FROM email e WHERE e.blob_id IS NOT NULL AND e.blob_id != ''";

static const char k_part_blob_sql[] =
    "SELECT e.rowid, COALESCE(e.last_opened, 0), "
    "CASE WHEN COALESCE(e.draft, 0) != 0 OR COALESCE(e.flagged, 0) != 0 OR EXISTS ("
    "  SELECT 1 FROM outbox o WHERE o.ra_id = e.ra_id AND o.draft_email_id = e.id"
    ") THEN 1 ELSE 0 END, e.ra_id, a.blob_id, a.part_id "
    "FROM attachment a JOIN email e ON e.rowid = a.email_rowid "
    "WHERE a.blob_id IS NOT NULL AND a.blob_id != ''";

static int push_blob(BlobCand **rows, size_t *n, size_t *cap, BlobCand row, int64_t *total) {
    if (row.ra == NULL || row.blob_id == NULL) {
        free(row.ra);
        free(row.blob_id);
        free(row.part_id);
        return 0;
    }
    if (grow_ptr((void **)rows, cap, *n + 1, sizeof(**rows)) != 0) {
        free(row.ra);
        free(row.blob_id);
        free(row.part_id);
        return -1;
    }
    (*rows)[(*n)++] = row;
    *total += row.bytes;
    return 0;
}

static int load_blobs(BlobCand **out, size_t *out_n, int64_t *total) {
    static const char k_email_key[] = "cache.email_blobs";
    static const char k_part_key[] = "cache.part_blobs";
    PavDbRead *read = NULL;
    sqlite3_stmt *stmt = NULL;
    BlobCand *rows = NULL;
    size_t n = 0;
    size_t cap = 0;
    int rc;

    *out = NULL;
    *out_n = 0;
    *total = 0;
    if (g_root[0] == '\0' || pav_db_read_begin(&read) != 0) {
        return -1;
    }
    if (pav_db_prepare(pav_db_read_handle(read), k_email_key, k_email_blob_sql, &stmt) != 0) {
        pav_db_read_end(read);
        return -1;
    }
    while ((rc = pav_db_step(stmt)) == SQLITE_ROW) {
        BlobCand row;
        char path[4096];

        memset(&row, 0, sizeof(row));
        row.rowid = sqlite3_column_int64(stmt, 0);
        row.opened = sqlite3_column_int64(stmt, 1);
        row.protect = sqlite3_column_int(stmt, 2);
        row.ra = dup_text(sqlite3_column_text(stmt, 3));
        row.blob_id = dup_text(sqlite3_column_text(stmt, 4));
        if (row.ra != NULL && row.blob_id != NULL &&
            pav_blob_path(row.ra, row.blob_id, path, sizeof(path)) == 0) {
            row.bytes = file_bytes(path);
        }
        if (push_blob(&rows, &n, &cap, row, total) != 0) {
            free_blobs(rows, n);
            pav_db_read_end(read);
            return -1;
        }
    }
    if (rc != SQLITE_DONE) {
        free_blobs(rows, n);
        pav_db_read_end(read);
        return -1;
    }
    if (pav_db_prepare(pav_db_read_handle(read), k_part_key, k_part_blob_sql, &stmt) != 0) {
        free_blobs(rows, n);
        pav_db_read_end(read);
        return -1;
    }
    while ((rc = pav_db_step(stmt)) == SQLITE_ROW) {
        BlobCand row;
        char path[4096];

        memset(&row, 0, sizeof(row));
        row.attachment = 1;
        row.rowid = sqlite3_column_int64(stmt, 0);
        row.opened = sqlite3_column_int64(stmt, 1);
        row.protect = sqlite3_column_int(stmt, 2);
        row.ra = dup_text(sqlite3_column_text(stmt, 3));
        row.blob_id = dup_text(sqlite3_column_text(stmt, 4));
        row.part_id = dup_text(sqlite3_column_text(stmt, 5));
        if (row.ra != NULL && row.blob_id != NULL &&
            pav_blob_path(row.ra, row.blob_id, path, sizeof(path)) == 0) {
            row.bytes = file_bytes(path);
        }
        if (push_blob(&rows, &n, &cap, row, total) != 0) {
            free_blobs(rows, n);
            pav_db_read_end(read);
            return -1;
        }
    }
    pav_db_read_end(read);
    if (rc != SQLITE_DONE) {
        free_blobs(rows, n);
        return -1;
    }
    *out = rows;
    *out_n = n;
    return 0;
}

typedef struct BodyUpdate {
    int64_t *ids;
    int n;
} BodyUpdate;

static const char k_clear_paths[] =
    "UPDATE body_cache SET text_plain_path = NULL, html_raw_path = NULL, "
    "html_sanitized_path = NULL WHERE email_rowid = ?";

static const char k_mark_evicted[] = "UPDATE email SET body_state = 'evicted' WHERE rowid = ?";

static int clear_body_job(void *ctx, sqlite3 *db) {
    static const char k_paths[] = "cache.clear_body_paths";
    static const char k_state[] = "cache.mark_evicted";
    BodyUpdate *job = ctx;
    sqlite3_stmt *paths = NULL;
    sqlite3_stmt *state = NULL;
    int i;

    if (pav_db_prepare(db, k_paths, k_clear_paths, &paths) != 0 ||
        pav_db_prepare(db, k_state, k_mark_evicted, &state) != 0) {
        return -1;
    }
    for (i = 0; i < job->n; i++) {
        if (pav_db_bind_int64(paths, 1, job->ids[i]) != 0 || pav_db_step(paths) != SQLITE_DONE) {
            return -1;
        }
        sqlite3_reset(paths);
        sqlite3_clear_bindings(paths);
        if (pav_db_bind_int64(state, 1, job->ids[i]) != 0 || pav_db_step(state) != SQLITE_DONE) {
            return -1;
        }
        sqlite3_reset(state);
        sqlite3_clear_bindings(state);
    }
    return 0;
}

typedef struct BlobUpdate {
    BlobCand *rows;
    int n;
} BlobUpdate;

static const char k_clear_local[] =
    "UPDATE attachment SET local_path = NULL WHERE email_rowid = ? AND part_id = ?";

static int clear_blob_job(void *ctx, sqlite3 *db) {
    static const char k_key[] = "cache.clear_local_path";
    BlobUpdate *job = ctx;
    sqlite3_stmt *stmt = NULL;
    int i;

    if (pav_db_prepare(db, k_key, k_clear_local, &stmt) != 0) {
        return -1;
    }
    for (i = 0; i < job->n; i++) {
        if (!job->rows[i].attachment || job->rows[i].part_id == NULL) {
            continue;
        }
        if (pav_db_bind_int64(stmt, 1, job->rows[i].rowid) != 0 ||
            pav_db_bind_text(stmt, 2, job->rows[i].part_id) != 0 || pav_db_step(stmt) != SQLITE_DONE) {
            return -1;
        }
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }
    return 0;
}

static int evict_bodies(int64_t limit, int *removed) {
    BodyCand *rows = NULL;
    size_t n = 0;
    int64_t total = 0;
    int64_t *ids = NULL;
    size_t *pick = NULL;
    int kept = 0;
    size_t i;
    int j;
    int rc = 0;

    if (load_bodies(&rows, &n, &total) != 0) {
        return -1;
    }
    if (total <= limit || n == 0) {
        free_bodies(rows, n);
        return 0;
    }
    qsort(rows, n, sizeof(*rows), cmp_body);
    ids = calloc(n, sizeof(*ids));
    pick = calloc(n, sizeof(*pick));
    if (ids == NULL || pick == NULL) {
        free(ids);
        free(pick);
        free_bodies(rows, n);
        return -1;
    }
    for (i = 0; i < n && total > limit; i++) {
        if (rows[i].protect || rows[i].bytes <= 0) {
            continue;
        }
        pick[kept] = i;
        ids[kept] = rows[i].rowid;
        total -= rows[i].bytes;
        kept++;
    }
    if (kept == 0) {
        free(ids);
        free(pick);
        free_bodies(rows, n);
        return 0;
    }
    /* The index delete has to read the body files, so it runs before unlink. */
    if (pav_fts_clear_bodies(ids, kept) != 0) {
        free(ids);
        free(pick);
        free_bodies(rows, n);
        return -1;
    }
    for (j = 0; j < kept; j++) {
        BodyCand *row = &rows[pick[j]];
        int p;
        int failed = 0;

        for (p = 0; p < 3; p++) {
            if (row->paths[p] != NULL && remove(row->paths[p]) != 0 && errno != ENOENT) {
                failed = 1;
            }
        }
        if (failed) {
            rc = -1;
            break;
        }
        *removed += 1;
    }
    {
        BodyUpdate job;

        job.ids = ids;
        job.n = kept;
        if (pav_db_call(clear_body_job, &job) != 0) {
            rc = -1;
        }
    }
    free(ids);
    free(pick);
    free_bodies(rows, n);
    return rc;
}

static int evict_blobs(int64_t limit, int *removed) {
    BlobCand *rows = NULL;
    size_t n = 0;
    int64_t total = 0;
    BlobCand *gone = NULL;
    int kept = 0;
    size_t i;
    int rc = 0;

    if (g_root[0] == '\0') {
        return 0;
    }
    if (load_blobs(&rows, &n, &total) != 0) {
        return -1;
    }
    if (total <= limit || n == 0) {
        free_blobs(rows, n);
        return 0;
    }
    qsort(rows, n, sizeof(*rows), cmp_blob);
    gone = calloc(n, sizeof(*gone));
    if (gone == NULL) {
        free_blobs(rows, n);
        return -1;
    }
    for (i = 0; i < n && total > limit; i++) {
        if (rows[i].protect || rows[i].bytes <= 0) {
            continue;
        }
        if (pav_blob_remove(rows[i].ra, rows[i].blob_id) != 0) {
            rc = -1;
            break;
        }
        total -= rows[i].bytes;
        gone[kept++] = rows[i];
        *removed += 1;
    }
    if (rc == 0 && kept > 0) {
        BlobUpdate job;

        job.rows = gone;
        job.n = kept;
        if (pav_db_call(clear_blob_job, &job) != 0) {
            rc = -1;
        }
    }
    free(gone);
    free_blobs(rows, n);
    return rc;
}

int pav_cache_evict(void) {
    int64_t body_total = 0;
    int64_t blob_total = 0;
    BodyCand *bodies = NULL;
    BlobCand *blobs = NULL;
    size_t body_n = 0;
    size_t blob_n = 0;
    int64_t body_limit;
    int64_t blob_limit;
    int64_t id = 0;
    int removed = 0;
    int rc = 0;

    if (!pav_db_writer_running()) {
        return -1;
    }
    body_limit = budget_bytes(g_bodies_mb);
    blob_limit = budget_bytes(g_blobs_mb);
    if (load_bodies(&bodies, &body_n, &body_total) != 0) {
        return -1;
    }
    free_bodies(bodies, body_n);
    if (g_root[0] != '\0') {
        if (load_blobs(&blobs, &blob_n, &blob_total) != 0) {
            return -1;
        }
        free_blobs(blobs, blob_n);
    }
    if (body_total <= body_limit && blob_total <= blob_limit) {
        return 0;
    }
    id = pav_activity_begin("cache.evict", NULL, "Evict cache", 0, 0);
    if (body_total > body_limit && evict_bodies(body_limit, &removed) != 0) {
        rc = -1;
    }
    if (rc == 0 && blob_total > blob_limit && evict_blobs(blob_limit, &removed) != 0) {
        rc = -1;
    }
    if (id > 0) {
        pav_activity_end(id, rc == 0 ? PAV_ACT_DONE : PAV_ACT_FAILED, rc == 0 ? NULL : "cache eviction failed");
    }
    if (rc == 0) {
        pav_log("store", PAV_INFO, 1, "cache evict removed %d", removed);
    } else {
        pav_log("store", PAV_ERROR, 0, "cache eviction failed");
    }
    return rc;
}

typedef struct NameList {
    char **names;
    size_t n;
    size_t cap;
} NameList;

static int collect_name(const char *name, void *user) {
    NameList *list = user;
    char *copy;
    char **grown;
    size_t cap;

    if (list->n == list->cap) {
        cap = list->cap == 0 ? 8 : list->cap * 2;
        grown = realloc(list->names, cap * sizeof(*grown));
        if (grown == NULL) {
            return -1;
        }
        list->names = grown;
        list->cap = cap;
    }
    copy = strdup(name);
    if (copy == NULL) {
        return -1;
    }
    list->names[list->n++] = copy;
    return 0;
}

static int rm_tree(const char *path) {
    NameList list;
    size_t i;
    int rc = 0;

    if (path == NULL || path[0] == '\0' || !pav_exists(path)) {
        return 0;
    }
    if (!pav_is_dir(path)) {
        return remove(path) == 0 || errno == ENOENT ? 0 : -1;
    }
    memset(&list, 0, sizeof(list));
    if (pav_dir_each(path, collect_name, &list) != 0) {
        rc = -1;
    }
    for (i = 0; i < list.n; i++) {
        char child[4096];

        if (rc == 0 && pav_path_join(child, sizeof(child), path, list.names[i]) == 0) {
            if (rm_tree(child) != 0) {
                rc = -1;
            }
        } else {
            rc = -1;
        }
        free(list.names[i]);
    }
    free(list.names);
    if (rc != 0) {
        return -1;
    }
    if (rmdir(path) != 0 && errno != ENOENT) {
        return -1;
    }
    return 0;
}

int pav_runtime_open_prepare(void) {
    const PavPlatform *platform = pav_platform();
    char root[4096];
    char open_dir[4096];

    if (platform == NULL || platform->dir == NULL || platform->mkdir_private == NULL) {
        return -1;
    }
    if (platform->dir(PAV_DIR_RUNTIME, root, sizeof(root)) != 0) {
        return -1;
    }
    if (pav_path_join(open_dir, sizeof(open_dir), root, "open") != 0) {
        return -1;
    }
    return platform->mkdir_private(open_dir);
}

static int use_add(PavCacheUse **rows, size_t *n, size_t *cap, const char *id, int64_t bodies, int64_t blobs) {
    PavCacheUse *row;
    size_t i;

    if (id == NULL || id[0] == '\0') {
        return 0;
    }
    for (i = 0; i < *n; i++) {
        if (strcmp((*rows)[i].account_id, id) == 0) {
            (*rows)[i].bodies += bodies;
            (*rows)[i].blobs += blobs;
            return 0;
        }
    }
    if (grow_ptr((void **)rows, cap, *n + 1, sizeof(**rows)) != 0) {
        return -1;
    }
    row = &(*rows)[*n];
    row->account_id = strdup(id);
    if (row->account_id == NULL) {
        return -1;
    }
    row->bodies = bodies;
    row->blobs = blobs;
    (*n)++;
    return 0;
}

static const char k_usage_bodies[] =
    "SELECT ra.account_id, b.text_plain_path, b.html_raw_path, b.html_sanitized_path "
    "FROM body_cache b "
    "JOIN email e ON e.rowid = b.email_rowid "
    "JOIN remote_account ra ON ra.id = e.ra_id";

static const char k_usage_blobs[] =
    "SELECT account_id, ra_id, blob_id FROM ("
    " SELECT ra.account_id AS account_id, e.ra_id AS ra_id, e.blob_id AS blob_id"
    " FROM email e JOIN remote_account ra ON ra.id = e.ra_id"
    " WHERE e.blob_id IS NOT NULL AND e.blob_id != ''"
    " UNION"
    " SELECT ra.account_id, e.ra_id, a.blob_id"
    " FROM attachment a"
    " JOIN email e ON e.rowid = a.email_rowid"
    " JOIN remote_account ra ON ra.id = e.ra_id"
    " WHERE a.blob_id IS NOT NULL AND a.blob_id != ''"
    ")";

void pav_cache_local_usage_free(PavCacheUse *rows, size_t n) {
    size_t i;

    if (rows == NULL) {
        return;
    }
    for (i = 0; i < n; i++) {
        free(rows[i].account_id);
    }
    free(rows);
}

int pav_cache_local_usage(PavCacheUse **out, size_t *n) {
    static const char k_body_key[] = "cache.usage_bodies";
    static const char k_blob_key[] = "cache.usage_blobs";
    PavDbRead *read = NULL;
    sqlite3_stmt *stmt = NULL;
    PavCacheUse *rows = NULL;
    size_t count = 0;
    size_t cap = 0;
    int rc;

    if (out == NULL || n == NULL) {
        return -1;
    }
    *out = NULL;
    *n = 0;
    if (pav_db_read_begin(&read) != 0) {
        return -1;
    }
    if (pav_db_prepare(pav_db_read_handle(read), k_body_key, k_usage_bodies, &stmt) != 0) {
        pav_db_read_end(read);
        return -1;
    }
    while ((rc = pav_db_step(stmt)) == SQLITE_ROW) {
        const char *id = (const char *)sqlite3_column_text(stmt, 0);
        int64_t bytes = 0;
        int p;

        for (p = 0; p < 3; p++) {
            bytes += file_bytes((const char *)sqlite3_column_text(stmt, 1 + p));
        }
        if (use_add(&rows, &count, &cap, id, bytes, 0) != 0) {
            pav_cache_local_usage_free(rows, count);
            pav_db_read_end(read);
            return -1;
        }
    }
    if (rc != SQLITE_DONE) {
        pav_cache_local_usage_free(rows, count);
        pav_db_read_end(read);
        return -1;
    }
    if (pav_db_prepare(pav_db_read_handle(read), k_blob_key, k_usage_blobs, &stmt) != 0) {
        pav_cache_local_usage_free(rows, count);
        pav_db_read_end(read);
        return -1;
    }
    while ((rc = pav_db_step(stmt)) == SQLITE_ROW) {
        const char *id = (const char *)sqlite3_column_text(stmt, 0);
        const char *ra = (const char *)sqlite3_column_text(stmt, 1);
        const char *blob = (const char *)sqlite3_column_text(stmt, 2);
        char path[4096];
        int64_t bytes = 0;

        if (ra != NULL && blob != NULL && pav_blob_path(ra, blob, path, sizeof(path)) == 0) {
            bytes = file_bytes(path);
        }
        if (use_add(&rows, &count, &cap, id, 0, bytes) != 0) {
            pav_cache_local_usage_free(rows, count);
            pav_db_read_end(read);
            return -1;
        }
    }
    pav_db_read_end(read);
    if (rc != SQLITE_DONE) {
        pav_cache_local_usage_free(rows, count);
        return -1;
    }
    *out = rows;
    *n = count;
    return 0;
}

void pav_runtime_open_remove(void) {
    const PavPlatform *platform = pav_platform();
    char root[4096];
    char open_dir[4096];

    if (platform == NULL || platform->dir == NULL) {
        return;
    }
    if (platform->dir(PAV_DIR_RUNTIME, root, sizeof(root)) != 0) {
        return;
    }
    if (pav_path_join(open_dir, sizeof(open_dir), root, "open") != 0) {
        return;
    }
    (void)rm_tree(open_dir);
}

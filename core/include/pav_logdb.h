#ifndef PAV_LOGDB_H
#define PAV_LOGDB_H

#include <sqlite3.h>

#include <stddef.h>
#include <stdint.h>

/* log.db (H-4 Lock). Only the log thread calls these; nothing else opens
   log.db. None of them log. */

typedef struct PavLogRow {
    int64_t seq;
    int64_t ts;
    int level;
    const char *subsystem;
    const char *text;
} PavLogRow;

#include "pavillion.h"

/* PavLogStats is in pavillion.h; cap_mb is left 0 here. */

/* Opens or creates path (0600, WAL, auto_vacuum=INCREMENTAL), runs the log
   migrations, and starts a session row (source "run"). *max_seq is the
   largest seq stored, 0 when empty. A log.db newer than the binary fails. */
int pav_logdb_open(const char *path, sqlite3 **out, int64_t *session, int64_t *max_seq, char *err,
                   size_t err_len);
/* Writes rows in one transaction with a prepared insert. */
int pav_logdb_insert(sqlite3 *db, int64_t session, const PavLogRow *rows, size_t count);
/* bytes is (page_count - freelist_count) * page_size. */
int pav_logdb_stats(sqlite3 *db, PavLogStats *out);
/* Deletes the oldest seq first in chunks of 1,000 rows until bytes <= cap,
   drops sessions with no lines (the current one stays), then
   incremental_vacuum. out is the stats after. */
int pav_logdb_trim(sqlite3 *db, int64_t cap_bytes, int64_t keep_session, PavLogStats *out);
/* Deletes nothing: estimates the stats after a trim to cap_bytes. */
int pav_logdb_trim_preview(sqlite3 *db, int64_t cap_bytes, PavLogStats *out);
/* Checkpoints the WAL and closes. */
void pav_logdb_close(sqlite3 *db);

#endif

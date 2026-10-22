#include "pav_fts.h"

#include <stddef.h>

static int step_done(sqlite3_stmt *stmt) {
    int rc = sqlite3_step(stmt);

    return rc == SQLITE_DONE || rc == SQLITE_OK;
}

static const char *or_empty(const char *text) {
    return text != NULL ? text : "";
}

int pav_fts_index_headers(sqlite3 *db, int64_t rowid, const char *subject, const char *from_text,
                          const char *to_text, int replace) {
    sqlite3_stmt *stmt = NULL;

    if (db == NULL || rowid <= 0) {
        return -1;
    }
    if (replace) {
        /* A missing rowid is an error from FTS5. The insert below is the one
           that has to succeed. */
        if (sqlite3_prepare_v2(db, "INSERT INTO email_fts(email_fts, rowid) VALUES('delete', ?)", -1, &stmt, NULL) !=
            SQLITE_OK) {
            return -1;
        }
        sqlite3_bind_int64(stmt, 1, (sqlite3_int64)rowid);
        (void)sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        stmt = NULL;
    }
    if (sqlite3_prepare_v2(db,
                           "INSERT INTO email_fts(rowid, subject, from_text, to_text, body) VALUES(?, ?, ?, ?, '')",
                           -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    if (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)rowid) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 2, or_empty(subject), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 3, or_empty(from_text), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 4, or_empty(to_text), -1, SQLITE_TRANSIENT) != SQLITE_OK || !step_done(stmt)) {
        sqlite3_finalize(stmt);
        return -1;
    }
    sqlite3_finalize(stmt);
    return 0;
}

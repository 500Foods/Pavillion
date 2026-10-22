#ifndef PAV_FTS_H
#define PAV_FTS_H

#include <sqlite3.h>

#include <stdint.h>

/* Header-only email_fts row. The body column stays empty. Q-1 fills the body
   by deleting this row and inserting it again. replace deletes an existing
   rowid first. Called on the store writer connection. Returns 0, or -1. */
int pav_fts_index_headers(sqlite3 *db, int64_t rowid, const char *subject, const char *from_text,
                          const char *to_text, int replace);

#endif

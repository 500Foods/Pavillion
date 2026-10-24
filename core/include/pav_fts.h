#ifndef PAV_FTS_H
#define PAV_FTS_H

#include <sqlite3.h>
#include <jansson.h>

#include <stdint.h>

/* Body column cap. Plain text wins. HTML is the sanitizer tree walk. */
#define PAV_FTS_BODY_CAP (256u * 1024u)

/* Header-only email_fts row. The body column stays empty. replace deletes an
   empty-body row whose header text is the text passed here, then inserts.
   A header change has to delete the indexed text first. Called on the store
   writer connection. Returns 0, or -1. */
int pav_fts_index_headers(sqlite3 *db, int64_t rowid, const char *subject, const char *from_text,
                          const char *to_text, int replace);

/* Contentless FTS5 keeps no copy of the text. A delete has to repeat the
   strings that were inserted, including the body. */
int pav_fts_delete(sqlite3 *db, int64_t rowid, const char *subject, const char *from_text,
                   const char *to_text, const char *body);

/* Insert one row. The body may be empty. Fails when the rowid is already indexed. */
int pav_fts_insert(sqlite3 *db, int64_t rowid, const char *subject, const char *from_text,
                   const char *to_text, const char *body);

/* Names and addresses, "Name <email>" joined by ", ". NULL when both lists
   are empty. The same bytes are what sync stores in the index. Caller frees. */
char *pav_fts_addresses(const json_t *first_list, const json_t *second_list);

typedef struct PavFtsDoc {
    char *subject;
    char *from_text;
    char *to_text;
    char *body;
} PavFtsDoc;

void pav_fts_doc_free(PavFtsDoc *doc);

/* Headers from the email row, body from the plain-text file or the HTML
   walk. Every string is malloc'd and may be empty. 0, or -1. */
int pav_fts_capture(sqlite3 *db, int64_t rowid, PavFtsDoc *doc);

/* Headers only. body is an empty string. Does not read body files. */
int pav_fts_load_headers(sqlite3 *db, int64_t rowid, PavFtsDoc *doc);

/* Read the indexed body for these paths. Plain text when the file has bytes,
   otherwise the HTML text walk. *out is malloc'd. 0, or -1. */
int pav_fts_body_text(const char *text_path, const char *html_path, char **out);

/* Body text the index holds. Empty unless body_state is fetched, in which
   case it is the plain text or the HTML walk of the files still on disk.
   *out is malloc'd. 0, or -1 when the email row is missing. */
int pav_fts_indexed_body(sqlite3 *db, int64_t rowid, char **out);

/* pav_fts_indexed_body on a read-pool connection. */
int pav_fts_indexed_body_read(int64_t rowid, char **out);

/* Delete the indexed row using the text that was stored. 0 when the row is
   already absent. A wrong body is rolled back and the delete is retried with
   an empty body, which is how a header-only row was stored. Returns -1 when
   both miss. */
int pav_fts_remove(sqlite3 *db, int64_t rowid);

/* Delete the indexed row and insert it again with new_body. old_body is the
   body currently in the index. Headers come from the email row. An absent
   index row is inserted. */
int pav_fts_set_body(sqlite3 *db, int64_t rowid, const char *old_body, const char *new_body);

/* Drop these bodies back to empty while the body files still exist.
   One write job. 0, or -1. */
int pav_fts_clear_bodies(const int64_t *ids, int n);

/* One row. Runs its own write job. */
int pav_fts_clear_body_row(int64_t rowid);

/* Delete every index row. Contentless tables have no other rebuild. */
int pav_fts_delete_all(sqlite3 *db);

#endif

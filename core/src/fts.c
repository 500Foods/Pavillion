#include "pav_fts.h"

#include "pav_db.h"
#include "pav_sanitize.h"
#include "pavillion.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int step_done(sqlite3_stmt *stmt) {
    int rc = sqlite3_step(stmt);

    return rc == SQLITE_DONE || rc == SQLITE_OK;
}

static const char *or_empty(const char *text) {
    return text != NULL ? text : "";
}

static int body_empty(const char *body) {
    return body == NULL || body[0] == '\0';
}

static int insert_doc(sqlite3 *db, int64_t rowid, const char *subject, const char *from_text, const char *to_text,
                      const char *body) {
    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2(db,
                           "INSERT INTO email_fts(rowid, subject, from_text, to_text, body) VALUES(?, ?, ?, ?, ?)",
                           -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    if (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)rowid) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 2, or_empty(subject), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 3, or_empty(from_text), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 4, or_empty(to_text), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 5, body, -1, SQLITE_TRANSIENT) != SQLITE_OK || !step_done(stmt)) {
        sqlite3_finalize(stmt);
        return -1;
    }
    sqlite3_finalize(stmt);
    return 0;
}

static int delete_header(sqlite3 *db, int64_t rowid, const char *subject, const char *from_text,
                         const char *to_text) {
    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2(db,
                           "INSERT INTO email_fts(email_fts, rowid, subject, from_text, to_text, body) "
                           "VALUES('delete', ?, ?, ?, ?, '')",
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

static int delete_doc(sqlite3 *db, int64_t rowid, const char *subject, const char *from_text, const char *to_text,
                      const char *body) {
    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2(db,
                           "INSERT INTO email_fts(email_fts, rowid, subject, from_text, to_text, body) "
                           "VALUES('delete', ?, ?, ?, ?, ?)",
                           -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    if (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)rowid) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 2, or_empty(subject), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 3, or_empty(from_text), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 4, or_empty(to_text), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 5, body, -1, SQLITE_TRANSIENT) != SQLITE_OK || !step_done(stmt)) {
        sqlite3_finalize(stmt);
        return -1;
    }
    sqlite3_finalize(stmt);
    return 0;
}

int pav_fts_delete(sqlite3 *db, int64_t rowid, const char *subject, const char *from_text, const char *to_text,
                   const char *body) {
    if (db == NULL || rowid <= 0) {
        return -1;
    }
    if (body_empty(body)) {
        return delete_header(db, rowid, subject, from_text, to_text);
    }
    return delete_doc(db, rowid, subject, from_text, to_text, body);
}

int pav_fts_index_headers(sqlite3 *db, int64_t rowid, const char *subject, const char *from_text,
                          const char *to_text, int replace) {
    sqlite3_stmt *stmt = NULL;

    if (db == NULL || rowid <= 0) {
        return -1;
    }
    /* Body is the SQL literal ''. A contentless delete has to repeat it. */
    if (replace && pav_fts_delete(db, rowid, subject, from_text, to_text, "") != 0) {
        return -1;
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

int pav_fts_insert(sqlite3 *db, int64_t rowid, const char *subject, const char *from_text, const char *to_text,
                   const char *body) {
    if (db == NULL || rowid <= 0) {
        return -1;
    }
    if (body_empty(body)) {
        return pav_fts_index_headers(db, rowid, subject, from_text, to_text, 0);
    }
    return insert_doc(db, rowid, subject, from_text, to_text, body);
}

int pav_fts_delete_all(sqlite3 *db) {
    char *err = NULL;
    int rc;

    if (db == NULL) {
        return -1;
    }
    rc = sqlite3_exec(db, "INSERT INTO email_fts(email_fts) VALUES('delete-all')", NULL, NULL, &err);
    sqlite3_free(err);
    return rc == SQLITE_OK ? 0 : -1;
}

static void append_addresses(char *dst, size_t n, int *first, const json_t *arr) {
    size_t i;
    size_t count;

    if (!json_is_array(arr) || n == 0) {
        return;
    }
    count = json_array_size(arr);
    for (i = 0; i < count; i++) {
        json_t *item = json_array_get(arr, i);
        const char *name;
        const char *email;
        char piece[640];
        int wrote;
        size_t have;

        if (!json_is_object(item)) {
            continue;
        }
        name = json_string_value(json_object_get(item, "name"));
        email = json_string_value(json_object_get(item, "email"));
        if ((name == NULL || name[0] == '\0') && (email == NULL || email[0] == '\0')) {
            continue;
        }
        if (name != NULL && name[0] != '\0' && email != NULL && email[0] != '\0') {
            wrote = snprintf(piece, sizeof(piece), "%s <%s>", name, email);
        } else if (email != NULL && email[0] != '\0') {
            wrote = snprintf(piece, sizeof(piece), "%s", email);
        } else {
            wrote = snprintf(piece, sizeof(piece), "%s", name != NULL ? name : "");
        }
        if (wrote < 0) {
            continue;
        }
        have = strlen(dst);
        if (!*first) {
            if (have + 2 >= n) {
                return;
            }
            memcpy(dst + have, ", ", 2);
            have += 2;
            dst[have] = '\0';
        }
        if (have + (size_t)wrote >= n) {
            return;
        }
        memcpy(dst + have, piece, (size_t)wrote);
        dst[have + (size_t)wrote] = '\0';
        *first = 0;
    }
}

char *pav_fts_addresses(const json_t *first_list, const json_t *second_list) {
    char buf[4096];
    size_t n;
    char *out;
    int first = 1;

    buf[0] = '\0';
    append_addresses(buf, sizeof(buf), &first, first_list);
    append_addresses(buf, sizeof(buf), &first, second_list);
    if (buf[0] == '\0') {
        return NULL;
    }
    n = strlen(buf);
    out = malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, buf, n + 1);
    return out;
}

void pav_fts_doc_free(PavFtsDoc *doc) {
    if (doc == NULL) {
        return;
    }
    free(doc->subject);
    free(doc->from_text);
    free(doc->to_text);
    free(doc->body);
    memset(doc, 0, sizeof(*doc));
}

static char *dup_text(const char *text) {
    size_t n;
    char *out;

    if (text == NULL) {
        text = "";
    }
    n = strlen(text);
    out = malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, text, n + 1);
    return out;
}

static int read_limit(const char *path, size_t cap, char **out, size_t *out_n) {
    FILE *f;
    char *buf;
    size_t n;

    *out = NULL;
    *out_n = 0;
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    buf = malloc(cap + 1);
    if (buf == NULL) {
        fclose(f);
        return -1;
    }
    n = fread(buf, 1, cap, f);
    if (ferror(f)) {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);
    if (n == 0) {
        free(buf);
        return 0;
    }
    while (n > 0 && ((unsigned char)buf[n - 1] & 0xc0) == 0x80) {
        n--;
    }
    if (n > 0 && ((unsigned char)buf[n - 1] & 0xc0) == 0xc0) {
        n--;
    }
    buf[n] = '\0';
    *out = buf;
    *out_n = n;
    return 0;
}

static int has_visible(const char *text, size_t n) {
    size_t i;

    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)text[i];

        if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '\f' && c != '\v') {
            return 1;
        }
    }
    return 0;
}

int pav_fts_body_text(const char *text_path, const char *html_path, char **out) {
    char *plain = NULL;
    char *html = NULL;
    char *text = NULL;
    size_t n = 0;
    int rc;

    if (out == NULL) {
        return -1;
    }
    *out = NULL;
    if (read_limit(text_path, (size_t)PAV_FTS_BODY_CAP, &plain, &n) != 0) {
        return -1;
    }
    if (plain != NULL && has_visible(plain, n)) {
        *out = plain;
        return 0;
    }
    free(plain);
    /* The indexed text is capped. The HTML source can be larger than that. */
    if (read_limit(html_path, 2u * 1024u * 1024u, &html, &n) != 0) {
        return -1;
    }
    if (html == NULL || n == 0) {
        free(html);
        *out = dup_text("");
        return *out != NULL ? 0 : -1;
    }
    rc = pav_html_text(html, n, (size_t)PAV_FTS_BODY_CAP, &text);
    free(html);
    if (rc != 0) {
        return -1;
    }
    *out = text;
    return 0;
}

static int take_col(sqlite3_stmt *stmt, int col, char **out) {
    const char *raw = (const char *)sqlite3_column_text(stmt, col);

    *out = dup_text(raw != NULL ? raw : "");
    return *out != NULL ? 0 : -1;
}

static int load_doc(sqlite3 *db, int64_t rowid, PavFtsDoc *doc, int with_body) {
    sqlite3_stmt *stmt = NULL;
    json_t *from = NULL;
    json_t *to = NULL;
    json_t *cc = NULL;
    char *from_json = NULL;
    char *to_json = NULL;
    char *cc_json = NULL;
    char *text_path = NULL;
    char *html_path = NULL;
    int rc;

    memset(doc, 0, sizeof(*doc));
    if (db == NULL || rowid <= 0) {
        return -1;
    }
    if (sqlite3_prepare_v2(db,
                           "SELECT e.subject, e.from_json, e.to_json, e.cc_json, "
                           "b.text_plain_path, b.html_raw_path "
                           "FROM email e LEFT JOIN body_cache b ON b.email_rowid = e.rowid "
                           "WHERE e.rowid = ?",
                           -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    if (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)rowid) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return -1;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return -1;
    }
    if (take_col(stmt, 0, &doc->subject) != 0 || take_col(stmt, 1, &from_json) != 0 ||
        take_col(stmt, 2, &to_json) != 0 || take_col(stmt, 3, &cc_json) != 0 ||
        take_col(stmt, 4, &text_path) != 0 || take_col(stmt, 5, &html_path) != 0) {
        sqlite3_finalize(stmt);
        pav_fts_doc_free(doc);
        free(from_json);
        free(to_json);
        free(cc_json);
        free(text_path);
        free(html_path);
        return -1;
    }
    sqlite3_finalize(stmt);
    if (from_json[0] != '\0') {
        from = json_loads(from_json, 0, NULL);
    }
    if (to_json[0] != '\0') {
        to = json_loads(to_json, 0, NULL);
    }
    if (cc_json[0] != '\0') {
        cc = json_loads(cc_json, 0, NULL);
    }
    free(from_json);
    free(to_json);
    free(cc_json);
    doc->from_text = pav_fts_addresses(from, NULL);
    doc->to_text = pav_fts_addresses(to, cc);
    json_decref(from);
    json_decref(to);
    json_decref(cc);
    if (doc->from_text == NULL) {
        doc->from_text = dup_text("");
    }
    if (doc->to_text == NULL) {
        doc->to_text = dup_text("");
    }
    if (doc->from_text == NULL || doc->to_text == NULL) {
        free(text_path);
        free(html_path);
        pav_fts_doc_free(doc);
        return -1;
    }
    if (!with_body) {
        doc->body = dup_text("");
        free(text_path);
        free(html_path);
        return doc->body != NULL ? 0 : -1;
    }
    if (pav_fts_body_text(text_path[0] != '\0' ? text_path : NULL, html_path[0] != '\0' ? html_path : NULL,
                          &doc->body) != 0) {
        free(text_path);
        free(html_path);
        pav_fts_doc_free(doc);
        return -1;
    }
    free(text_path);
    free(html_path);
    return 0;
}

int pav_fts_capture(sqlite3 *db, int64_t rowid, PavFtsDoc *doc) {
    return load_doc(db, rowid, doc, 1);
}

int pav_fts_load_headers(sqlite3 *db, int64_t rowid, PavFtsDoc *doc) {
    return load_doc(db, rowid, doc, 0);
}

static int fts_present(sqlite3 *db, int64_t rowid, int *present) {
    sqlite3_stmt *stmt = NULL;
    int rc;

    *present = 0;
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM email_fts WHERE rowid = ? LIMIT 1", -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    if (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)rowid) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return -1;
    }
    rc = sqlite3_step(stmt);
    *present = rc == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW || rc == SQLITE_DONE ? 0 : -1;
}

int pav_fts_indexed_body(sqlite3 *db, int64_t rowid, char **out) {
    sqlite3_stmt *stmt = NULL;
    const char *state;
    const char *text_path;
    const char *html_path;
    int fetched = 0;
    int rc;

    if (out == NULL) {
        return -1;
    }
    *out = NULL;
    if (db == NULL || rowid <= 0) {
        return -1;
    }
    if (sqlite3_prepare_v2(db,
                           "SELECT e.body_state, b.text_plain_path, b.html_raw_path "
                           "FROM email e LEFT JOIN body_cache b ON b.email_rowid = e.rowid "
                           "WHERE e.rowid = ?",
                           -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    if (sqlite3_bind_int64(stmt, 1, (sqlite3_int64)rowid) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return -1;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return -1;
    }
    state = (const char *)sqlite3_column_text(stmt, 0);
    text_path = (const char *)sqlite3_column_text(stmt, 1);
    html_path = (const char *)sqlite3_column_text(stmt, 2);
    fetched = state != NULL && strcmp(state, "fetched") == 0;
    if (!fetched) {
        sqlite3_finalize(stmt);
        *out = dup_text("");
        return *out != NULL ? 0 : -1;
    }
    rc = pav_fts_body_text(text_path != NULL && text_path[0] != '\0' ? text_path : NULL,
                           html_path != NULL && html_path[0] != '\0' ? html_path : NULL, out);
    sqlite3_finalize(stmt);
    return rc;
}

int pav_fts_indexed_body_read(int64_t rowid, char **out) {
    PavDbRead *read = NULL;
    int rc;

    if (out == NULL) {
        return -1;
    }
    *out = NULL;
    if (pav_db_read_begin(&read) != 0) {
        return -1;
    }
    rc = pav_fts_indexed_body(pav_db_read_handle(read), rowid, out);
    pav_db_read_end(read);
    return rc;
}

static int exec_sql(sqlite3 *db, const char *sql) {
    char *err = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err);

    sqlite3_free(err);
    return rc == SQLITE_OK ? 0 : -1;
}

/* Contentless FTS5 deletes only when the values match the indexed row.
   SQLite 3.51 turns a mismatch into SQLITE_CORRUPT_VTAB and aborts the
   statement, which fails the caller's whole transaction. The savepoint
   undoes that. A header-only row was inserted with the SQL literal '',
   so a failed guess retries that before giving up. */
static int delete_row(sqlite3 *db, int64_t rowid, const char *subject, const char *from_text, const char *to_text,
                      const char *body) {
    int present = 0;
    int rc;

    if (exec_sql(db, "SAVEPOINT pav_fts_del") != 0) {
        return -1;
    }
    rc = pav_fts_delete(db, rowid, subject, from_text, to_text, body);
    if (rc == 0 && fts_present(db, rowid, &present) != 0) {
        exec_sql(db, "ROLLBACK TO pav_fts_del");
        exec_sql(db, "RELEASE pav_fts_del");
        return -1;
    }
    if (rc != 0 || present) {
        if (exec_sql(db, "ROLLBACK TO pav_fts_del") != 0 || body_empty(body)) {
            exec_sql(db, "RELEASE pav_fts_del");
            return -1;
        }
        rc = pav_fts_delete(db, rowid, subject, from_text, to_text, "");
        present = 1;
        if (rc == 0 && fts_present(db, rowid, &present) != 0) {
            exec_sql(db, "ROLLBACK TO pav_fts_del");
            exec_sql(db, "RELEASE pav_fts_del");
            return -1;
        }
        if (rc != 0 || present) {
            exec_sql(db, "ROLLBACK TO pav_fts_del");
            exec_sql(db, "RELEASE pav_fts_del");
            return -1;
        }
        pav_log("store", PAV_DEBUG, 1, "fts delete retried for row %lld", (long long)rowid);
    }
    return exec_sql(db, "RELEASE pav_fts_del");
}

int pav_fts_remove(sqlite3 *db, int64_t rowid) {
    PavFtsDoc doc;
    char *body = NULL;
    int present = 0;
    int rc;

    if (fts_present(db, rowid, &present) != 0) {
        return -1;
    }
    if (!present) {
        return 0;
    }
    if (pav_fts_load_headers(db, rowid, &doc) != 0 || pav_fts_indexed_body(db, rowid, &body) != 0) {
        pav_fts_doc_free(&doc);
        free(body);
        return -1;
    }
    rc = delete_row(db, rowid, doc.subject, doc.from_text, doc.to_text, body);
    pav_fts_doc_free(&doc);
    free(body);
    return rc;
}

int pav_fts_set_body(sqlite3 *db, int64_t rowid, const char *old_body, const char *new_body) {
    PavFtsDoc doc;
    int present = 0;

    if (pav_fts_load_headers(db, rowid, &doc) != 0 || fts_present(db, rowid, &present) != 0) {
        pav_fts_doc_free(&doc);
        return -1;
    }
    if (present) {
        if (delete_row(db, rowid, doc.subject, doc.from_text, doc.to_text, old_body) != 0) {
            pav_fts_doc_free(&doc);
            return -1;
        }
    }
    if (pav_fts_insert(db, rowid, doc.subject, doc.from_text, doc.to_text, new_body) != 0) {
        pav_fts_doc_free(&doc);
        return -1;
    }
    pav_fts_doc_free(&doc);
    return 0;
}

/* Headers from SQL. guess is the body we believe is indexed. delete_row
   retries an empty body when that guess does not match. */
static int clear_one(sqlite3 *db, int64_t rowid) {
    PavFtsDoc doc;
    char *guess = NULL;
    int present = 0;
    int rc;

    if (fts_present(db, rowid, &present) != 0) {
        return -1;
    }
    if (!present) {
        return 0;
    }
    if (pav_fts_load_headers(db, rowid, &doc) != 0 || pav_fts_indexed_body(db, rowid, &guess) != 0) {
        pav_fts_doc_free(&doc);
        free(guess);
        return -1;
    }
    rc = delete_row(db, rowid, doc.subject, doc.from_text, doc.to_text, guess);
    if (rc == 0) {
        rc = pav_fts_insert(db, rowid, doc.subject, doc.from_text, doc.to_text, "");
    }
    pav_fts_doc_free(&doc);
    free(guess);
    return rc;
}

typedef struct ClearMany {
    const int64_t *ids;
    int n;
} ClearMany;

static int clear_many_job(void *ctx, sqlite3 *db) {
    ClearMany *job = ctx;
    int i;

    for (i = 0; i < job->n; i++) {
        if (clear_one(db, job->ids[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

int pav_fts_clear_bodies(const int64_t *ids, int n) {
    ClearMany job;

    if (n < 0 || (n > 0 && ids == NULL)) {
        return -1;
    }
    if (n == 0) {
        return 0;
    }
    job.ids = ids;
    job.n = n;
    return pav_db_call(clear_many_job, &job);
}

int pav_fts_clear_body_row(int64_t rowid) {
    if (rowid <= 0) {
        return -1;
    }
    return pav_fts_clear_bodies(&rowid, 1);
}

/* N-3. One reply for a mailbox, a flat thread, or a unified name.
   Newest first, capped by preferences.cache.listRows. The page keeps this
   reply. It does not ask for another page while scrolling. A keywords
   object that contains $deleted is left out, and total counts what remains.
   The time column is an instant. The page formats it. */

#include "dispatch.h"
#include "pav_blob.h"
#include "pav_db.h"
#include "pav_platform.h"
#include "pav_search.h"
#include "pavillion.h"

#include <jansson.h>
#include <sqlite3.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The command envelope is {"ok":true,"data":...}. Leave room under 2 MB. */
#define PAV_LIST_REPLY_MAX ((2u * 1024u * 1024u) - 128u)

enum {
    VIEW_MAILBOX = 0,
    VIEW_SEARCH = 1,
    VIEW_UNIFIED = 2,
    VIEW_THREADED = 3
};

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

/* First address name, else its email, else "". */
static char *address_label(const char *raw) {
    json_error_t jerr;
    json_t *arr;
    json_t *first;
    const char *name;
    const char *email;
    const char *pick;
    char *out;

    if (raw == NULL || raw[0] == '\0') {
        return dup_text("");
    }
    arr = json_loads(raw, 0, &jerr);
    if (!json_is_array(arr) || json_array_size(arr) == 0) {
        json_decref(arr);
        return dup_text("");
    }
    first = json_array_get(arr, 0);
    name = json_string_value(json_object_get(first, "name"));
    email = json_string_value(json_object_get(first, "email"));
    if (name != NULL && name[0] != '\0') {
        pick = name;
    } else if (email != NULL && email[0] != '\0') {
        pick = email;
    } else {
        pick = "";
    }
    out = dup_text(pick);
    json_decref(arr);
    return out;
}

/* Compact JSON string length, including the quotes json_dumps will add. */
static size_t json_string_bytes(const char *text) {
    const unsigned char *p;
    size_t n = 2;

    if (text == NULL) {
        return 2;
    }
    for (p = (const unsigned char *)text; *p != '\0'; p++) {
        if (*p == '"' || *p == '\\') {
            n += 2;
        } else if (*p < 0x20) {
            n += 6;
        } else {
            n += 1;
        }
    }
    return n;
}

static size_t dec_digits(int64_t n) {
    size_t d = 1;

    if (n < 0) {
        d++;
        n = -n;
    }
    while (n >= 10) {
        n /= 10;
        d++;
    }
    return d;
}

static const char k_row_skel[] =
    "{\"id\":,\"from\":,\"to\":,\"subject\":,\"preview\":,\"receivedAt\":,\"seen\":,\"flagged\":,"
    "\"answered\":,\"forwarded\":,\"hasAttachment\":,\"accountId\":},";

static size_t row_bytes(const char *id, const char *from, const char *to, const char *subject,
                        const char *preview, int64_t received_at, int seen, int flagged, int answered,
                        int forwarded, int has_attachment, const char *account_id) {
    return (sizeof(k_row_skel) - 1) + json_string_bytes(id) + json_string_bytes(from) + json_string_bytes(to) +
           json_string_bytes(subject) + json_string_bytes(preview) + json_string_bytes(account_id) +
           dec_digits(received_at) + (seen ? 4 : 5) + (flagged ? 4 : 5) + (answered ? 4 : 5) +
           (forwarded ? 4 : 5) + (has_attachment ? 4 : 5);
}

static int copy_col(char *dst, size_t n, sqlite3_stmt *stmt, int col) {
    const char *text = (const char *)sqlite3_column_text(stmt, col);
    size_t len;

    if (n == 0) {
        return -1;
    }
    if (text == NULL) {
        dst[0] = '\0';
        return 0;
    }
    len = strlen(text);
    if (len >= n) {
        return -1;
    }
    memcpy(dst, text, len + 1);
    return 0;
}

static const char *col_text(sqlite3_stmt *stmt, int col) {
    const char *text = (const char *)sqlite3_column_text(stmt, col);

    return text != NULL ? text : "";
}

int pav_list_append_row(json_t *messages, size_t *used, size_t max_bytes, sqlite3_stmt *stmt) {
    const char *id = col_text(stmt, 0);
    const char *subject = col_text(stmt, 1);
    const char *from_json = col_text(stmt, 2);
    const char *to_json = col_text(stmt, 3);
    const char *preview = col_text(stmt, 4);
    int64_t received_at = sqlite3_column_int64(stmt, 5);
    int seen = sqlite3_column_int(stmt, 6) != 0;
    int flagged = sqlite3_column_int(stmt, 7) != 0;
    int answered = sqlite3_column_int(stmt, 8) != 0;
    int forwarded = sqlite3_column_int(stmt, 9) != 0;
    int has_attachment = sqlite3_column_int(stmt, 10) != 0;
    const char *account_id = col_text(stmt, 11);
    char *from;
    char *to;
    json_t *row;
    size_t next;

    if (messages == NULL || used == NULL || stmt == NULL || id[0] == '\0') {
        return -1;
    }
    from = address_label(from_json);
    to = address_label(to_json);
    if (from == NULL || to == NULL) {
        free(from);
        free(to);
        return -1;
    }
    next = row_bytes(id, from, to, subject, preview, received_at, seen, flagged, answered, forwarded,
                     has_attachment, account_id);
    if (json_array_size(messages) > 0 && *used + next > max_bytes) {
        free(from);
        free(to);
        return 0;
    }
    row = json_object();
    if (row == NULL || json_object_set_new(row, "id", json_string(id)) != 0 ||
        json_object_set_new(row, "from", json_string(from)) != 0 ||
        json_object_set_new(row, "to", json_string(to)) != 0 ||
        json_object_set_new(row, "subject", json_string(subject)) != 0 ||
        json_object_set_new(row, "preview", json_string(preview)) != 0 ||
        json_object_set_new(row, "receivedAt", json_integer(received_at)) != 0 ||
        json_object_set_new(row, "seen", seen ? json_true() : json_false()) != 0 ||
        json_object_set_new(row, "flagged", flagged ? json_true() : json_false()) != 0 ||
        json_object_set_new(row, "answered", answered ? json_true() : json_false()) != 0 ||
        json_object_set_new(row, "forwarded", forwarded ? json_true() : json_false()) != 0 ||
        json_object_set_new(row, "hasAttachment", has_attachment ? json_true() : json_false()) != 0 ||
        json_object_set_new(row, "accountId", json_string(account_id)) != 0 ||
        json_array_append_new(messages, row) != 0) {
        json_decref(row);
        free(from);
        free(to);
        return -1;
    }
    free(from);
    free(to);
    *used += next;
    return 1;
}

static int unified_name_ok(const char *name) {
    return name != NULL &&
           (strcmp(name, "inbox") == 0 || strcmp(name, "unread") == 0 || strcmp(name, "flagged") == 0 ||
            strcmp(name, "drafts") == 0 || strcmp(name, "sent") == 0);
}

static int take_view(json_t *args, int *kind, const char **id, const char **query, const char **name,
                     PavError *err) {
    const char *key;
    json_t *value;
    json_t *view = NULL;
    int saw_view = 0;
    int saw_kind = 0;

    *kind = VIEW_MAILBOX;
    *id = NULL;
    *query = NULL;
    *name = NULL;
    if (!json_is_object(args)) {
        pav_error_set(err, "bad_args", "arguments are not an object", 0);
        return -1;
    }
    json_object_foreach(args, key, value) {
        if (strcmp(key, "view") != 0) {
            char buf[128];

            snprintf(buf, sizeof(buf), "unknown argument %s", key);
            pav_error_set(err, "bad_args", buf, 0);
            return -1;
        }
        saw_view = 1;
        view = value;
    }
    if (!saw_view || !json_is_object(view)) {
        pav_error_set(err, "bad_args", "view must be an object", 0);
        return -1;
    }
    json_object_foreach(view, key, value) {
        if (strcmp(key, "kind") == 0) {
            const char *text = json_string_value(value);

            if (text != NULL && strcmp(text, "search") == 0) {
                *kind = VIEW_SEARCH;
            } else if (text != NULL && strcmp(text, "mailbox") == 0) {
                *kind = VIEW_MAILBOX;
            } else if (text != NULL && strcmp(text, "unified") == 0) {
                *kind = VIEW_UNIFIED;
            } else if (text != NULL && strcmp(text, "threaded") == 0) {
                *kind = VIEW_THREADED;
            } else {
                pav_error_set(err, "bad_args", "view.kind must be mailbox, search, unified, or threaded", 0);
                return -1;
            }
            saw_kind = 1;
        } else if (strcmp(key, "id") == 0) {
            const char *text = json_string_value(value);

            if (text == NULL || text[0] == '\0') {
                pav_error_set(err, "bad_args", "view.id must be a string", 0);
                return -1;
            }
            if (strlen(text) >= 640) {
                pav_error_set(err, "bad_args", "view.id is too long", 0);
                return -1;
            }
            *id = text;
        } else if (strcmp(key, "query") == 0) {
            const char *text = json_string_value(value);

            if (text == NULL) {
                pav_error_set(err, "bad_args", "view.query must be a string", 0);
                return -1;
            }
            *query = text;
        } else if (strcmp(key, "name") == 0) {
            const char *text = json_string_value(value);

            if (text == NULL || text[0] == '\0') {
                pav_error_set(err, "bad_args", "view.name must be a string", 0);
                return -1;
            }
            *name = text;
        } else {
            char buf[128];

            snprintf(buf, sizeof(buf), "unknown argument view.%s", key);
            pav_error_set(err, "bad_args", buf, 0);
            return -1;
        }
    }
    if (!saw_kind) {
        pav_error_set(err, "bad_args", "view.kind must be mailbox, search, unified, or threaded", 0);
        return -1;
    }
    if (*kind == VIEW_SEARCH) {
        if (*id != NULL) {
            pav_error_set(err, "bad_args", "unknown argument view.id", 0);
            return -1;
        }
        if (*name != NULL) {
            pav_error_set(err, "bad_args", "unknown argument view.name", 0);
            return -1;
        }
        if (*query == NULL) {
            pav_error_set(err, "bad_args", "view.query must be a string", 0);
            return -1;
        }
        return 0;
    }
    if (*kind == VIEW_UNIFIED) {
        if (*id != NULL) {
            pav_error_set(err, "bad_args", "unknown argument view.id", 0);
            return -1;
        }
        if (*query != NULL) {
            pav_error_set(err, "bad_args", "unknown argument view.query", 0);
            return -1;
        }
        if (*name == NULL) {
            pav_error_set(err, "bad_args", "view.name must be a string", 0);
            return -1;
        }
        if (!unified_name_ok(*name)) {
            pav_error_set(err, "bad_args", "view.name must be inbox, unread, flagged, drafts, or sent", 0);
            return -1;
        }
        return 0;
    }
    if (*query != NULL) {
        pav_error_set(err, "bad_args", "unknown argument view.query", 0);
        return -1;
    }
    if (*name != NULL) {
        pav_error_set(err, "bad_args", "unknown argument view.name", 0);
        return -1;
    }
    if (*id == NULL) {
        pav_error_set(err, "bad_args", "view.id must be a string", 0);
        return -1;
    }
    return 0;
}

static int list_rows_cap(void) {
    PavConfigLoad *load = pav_core_load();
    int rows;

    if (load == NULL || load->profile == NULL) {
        return PAV_LIST_ROWS_DEFAULT;
    }
    rows = load->profile->list_rows;
    if (rows < PAV_LIST_ROWS_MIN || rows > PAV_LIST_ROWS_MAX) {
        return PAV_LIST_ROWS_DEFAULT;
    }
    return rows;
}

static int profile_ready(PavError *err) {
    PavConfigLoad *load = pav_core_load();

    if (load == NULL || load->profile == NULL || strcmp(load->state, "ready") != 0) {
        pav_error_set(err, "not_ready", "profile is not ready", 0);
        return -1;
    }
    return 0;
}

static int begin_reply(json_t **root_out, json_t **messages_out, const char *account_id, int64_t total,
                       size_t *used, PavError *err) {
    json_t *root;
    json_t *messages;

    messages = json_array();
    root = json_object();
    if (messages == NULL || root == NULL ||
        json_object_set_new(root, "accountId", account_id != NULL ? json_string(account_id) : json_null()) != 0 ||
        json_object_set_new(root, "messages", messages) != 0 ||
        json_object_set_new(root, "total", json_integer(total)) != 0) {
        json_decref(messages);
        json_decref(root);
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    *root_out = root;
    *messages_out = json_object_get(root, "messages");
    *used = 48 + (account_id != NULL ? json_string_bytes(account_id) : 4) + 24;
    return 0;
}

static int finish_reply(json_t *root, json_t *messages, json_t **data, PavError *err) {
    int guard = 0;

    while (guard < 8) {
        char *dump = json_dumps(root, JSON_COMPACT);
        size_t n = dump != NULL ? strlen(dump) : 0;

        free(dump);
        if (dump == NULL) {
            json_decref(root);
            pav_error_set(err, "internal", "out of memory", 0);
            return -1;
        }
        if (n <= PAV_LIST_REPLY_MAX) {
            *data = root;
            return 0;
        }
        if (json_array_size(messages) == 0) {
            json_decref(root);
            pav_error_set(err, "too_large", "message list is too large", 0);
            return -1;
        }
        json_array_remove(messages, json_array_size(messages) - 1);
        guard++;
    }
    json_decref(root);
    pav_error_set(err, "too_large", "message list is too large", 0);
    return -1;
}

static int step_rows(sqlite3_stmt *stmt, json_t *messages, size_t *used, PavError *err) {
    int rc;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        int added = pav_list_append_row(messages, used, PAV_LIST_REPLY_MAX, stmt);

        if (added < 0) {
            pav_error_set(err, "internal", "cannot read messages", 0);
            return -1;
        }
        if (added == 0) {
            return 0;
        }
    }
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        pav_error_set(err, "internal", "cannot read messages", 0);
        return -1;
    }
    return 0;
}

#define LIST_COLS_EM \
    "e.id, IFNULL(e.subject, ''), IFNULL(e.from_json, ''), IFNULL(e.to_json, ''), " \
    "IFNULL(e.preview, ''), IFNULL(em.received_at, 0), IFNULL(e.seen, 0), IFNULL(e.flagged, 0), " \
    "IFNULL(e.answered, 0), IFNULL(e.forwarded, 0), IFNULL(e.has_attachment, 0), IFNULL(r.account_id, '')"

#define LIST_COLS_EMAIL \
    "e.id, IFNULL(e.subject, ''), IFNULL(e.from_json, ''), IFNULL(e.to_json, ''), " \
    "IFNULL(e.preview, ''), IFNULL(e.received_at, 0), IFNULL(e.seen, 0), IFNULL(e.flagged, 0), " \
    "IFNULL(e.answered, 0), IFNULL(e.forwarded, 0), IFNULL(e.has_attachment, 0), IFNULL(r.account_id, '')"

static int find_mailbox(sqlite3 *db, const char *node_id, char *account_id, size_t account_n, char *ra_id,
                        size_t ra_n, char *mailbox_id, size_t mailbox_n, PavError *err) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    int found = 0;

    rc = sqlite3_prepare_v2(db,
                            "SELECT r.account_id, m.ra_id, m.id FROM mailbox m "
                            "JOIN remote_account r ON r.id = m.ra_id "
                            "WHERE ('mbx:' || m.ra_id || ':' || m.id) = ?1 LIMIT 1",
                            -1, &stmt, NULL);
    if (rc != SQLITE_OK || pav_db_bind_text(stmt, 1, node_id) != 0) {
        sqlite3_finalize(stmt);
        pav_error_set(err, "internal", "cannot read messages", 0);
        return -1;
    }
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        found = 1;
        if (copy_col(account_id, account_n, stmt, 0) != 0 || copy_col(ra_id, ra_n, stmt, 1) != 0 ||
            copy_col(mailbox_id, mailbox_n, stmt, 2) != 0 || account_id[0] == '\0' || ra_id[0] == '\0' ||
            mailbox_id[0] == '\0') {
            sqlite3_finalize(stmt);
            pav_error_set(err, "internal", "cannot read messages", 0);
            return -1;
        }
    } else if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        pav_error_set(err, "internal", "cannot read messages", 0);
        return -1;
    }
    sqlite3_finalize(stmt);
    if (!found) {
        pav_error_set(err, "not_found", "mailbox is not in the store", 0);
        return -1;
    }
    return 0;
}

static int list_mailbox(sqlite3 *db, const char *node_id, int cap, json_t **data, PavError *err) {
    char account_id[64];
    char ra_id[256];
    char mailbox_id[128];
    sqlite3_stmt *stmt = NULL;
    json_t *root = NULL;
    json_t *messages = NULL;
    int64_t total = 0;
    size_t used = 0;
    int rc;

    if (find_mailbox(db, node_id, account_id, sizeof(account_id), ra_id, sizeof(ra_id), mailbox_id,
                     sizeof(mailbox_id), err) != 0) {
        return -1;
    }
    rc = sqlite3_prepare_v2(db,
                            "SELECT COUNT(*) FROM email_mailbox em JOIN email e ON e.rowid = em.email_rowid "
                            "WHERE em.ra_id = ?1 AND em.mailbox_id = ?2 AND " PAV_SQL_NOT_DELETED,
                            -1, &stmt, NULL);
    if (rc != SQLITE_OK || pav_db_bind_text(stmt, 1, ra_id) != 0 || pav_db_bind_text(stmt, 2, mailbox_id) != 0) {
        sqlite3_finalize(stmt);
        pav_error_set(err, "internal", "cannot read messages", 0);
        return -1;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        pav_error_set(err, "internal", "cannot read messages", 0);
        return -1;
    }
    total = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    stmt = NULL;
    if (total < 0 || begin_reply(&root, &messages, account_id, total, &used, err) != 0) {
        json_decref(root);
        if (total < 0) {
            pav_error_set(err, "internal", "cannot read messages", 0);
        }
        return -1;
    }
    rc = sqlite3_prepare_v2(db,
                            "SELECT " LIST_COLS_EM " FROM email_mailbox em "
                            "JOIN email e ON e.rowid = em.email_rowid "
                            "JOIN remote_account r ON r.id = em.ra_id "
                            "WHERE em.ra_id = ?1 AND em.mailbox_id = ?2 AND " PAV_SQL_NOT_DELETED " "
                            "ORDER BY em.received_at DESC, em.email_rowid DESC LIMIT ?3",
                            -1, &stmt, NULL);
    if (rc != SQLITE_OK || pav_db_bind_text(stmt, 1, ra_id) != 0 || pav_db_bind_text(stmt, 2, mailbox_id) != 0 ||
        pav_db_bind_int(stmt, 3, cap) != 0 || step_rows(stmt, messages, &used, err) != 0) {
        sqlite3_finalize(stmt);
        json_decref(root);
        if (err != NULL && err->code[0] == '\0') {
            pav_error_set(err, "internal", "cannot read messages", 0);
        }
        return -1;
    }
    sqlite3_finalize(stmt);
    return finish_reply(root, messages, data, err);
}

static int list_unified(sqlite3 *db, const char *name, int cap, json_t **data, PavError *err) {
    sqlite3_stmt *count_stmt = NULL;
    sqlite3_stmt *stmt = NULL;
    json_t *root = NULL;
    json_t *messages = NULL;
    int64_t total = 0;
    size_t used = 0;
    int role = strcmp(name, "inbox") == 0 || strcmp(name, "drafts") == 0 || strcmp(name, "sent") == 0;
    int rc;
    const char *count_sql;
    const char *list_sql;

    if (role) {
        count_sql = "SELECT COUNT(*) FROM email_mailbox em JOIN email e ON e.rowid = em.email_rowid "
                    "JOIN mailbox m ON m.ra_id = em.ra_id AND m.id = em.mailbox_id "
                    "WHERE m.role = ?1 AND " PAV_SQL_NOT_DELETED;
        list_sql = "SELECT " LIST_COLS_EM " FROM email_mailbox em "
                   "JOIN email e ON e.rowid = em.email_rowid "
                   "JOIN mailbox m ON m.ra_id = em.ra_id AND m.id = em.mailbox_id "
                   "JOIN remote_account r ON r.id = em.ra_id "
                   "WHERE m.role = ?1 AND " PAV_SQL_NOT_DELETED " "
                   "ORDER BY em.received_at DESC, em.email_rowid DESC LIMIT ?2";
    } else if (strcmp(name, "unread") == 0) {
        count_sql = "SELECT COUNT(*) FROM email e JOIN remote_account r ON r.id = e.ra_id "
                    "WHERE IFNULL(e.seen, 0) = 0 AND " PAV_SQL_NOT_DELETED;
        list_sql = "SELECT " LIST_COLS_EMAIL " FROM email e JOIN remote_account r ON r.id = e.ra_id "
                   "WHERE IFNULL(e.seen, 0) = 0 AND " PAV_SQL_NOT_DELETED " "
                   "ORDER BY e.received_at DESC, e.rowid DESC LIMIT ?1";
    } else {
        count_sql = "SELECT COUNT(*) FROM email e JOIN remote_account r ON r.id = e.ra_id "
                    "WHERE IFNULL(e.flagged, 0) = 1 AND " PAV_SQL_NOT_DELETED;
        list_sql = "SELECT " LIST_COLS_EMAIL " FROM email e JOIN remote_account r ON r.id = e.ra_id "
                   "WHERE IFNULL(e.flagged, 0) = 1 AND " PAV_SQL_NOT_DELETED " "
                   "ORDER BY e.received_at DESC, e.rowid DESC LIMIT ?1";
    }
    rc = sqlite3_prepare_v2(db, count_sql, -1, &count_stmt, NULL);
    if (rc != SQLITE_OK || (role && pav_db_bind_text(count_stmt, 1, name) != 0)) {
        sqlite3_finalize(count_stmt);
        pav_error_set(err, "internal", "cannot read messages", 0);
        return -1;
    }
    rc = sqlite3_step(count_stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(count_stmt);
        pav_error_set(err, "internal", "cannot read messages", 0);
        return -1;
    }
    total = sqlite3_column_int64(count_stmt, 0);
    sqlite3_finalize(count_stmt);
    if (total < 0 || begin_reply(&root, &messages, NULL, total, &used, err) != 0) {
        json_decref(root);
        if (total < 0) {
            pav_error_set(err, "internal", "cannot read messages", 0);
        }
        return -1;
    }
    rc = sqlite3_prepare_v2(db, list_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK || (role && pav_db_bind_text(stmt, 1, name) != 0) ||
        pav_db_bind_int(stmt, role ? 2 : 1, cap) != 0 || step_rows(stmt, messages, &used, err) != 0) {
        sqlite3_finalize(stmt);
        json_decref(root);
        if (err != NULL && err->code[0] == '\0') {
            pav_error_set(err, "internal", "cannot read messages", 0);
        }
        return -1;
    }
    sqlite3_finalize(stmt);
    return finish_reply(root, messages, data, err);
}

int pav_cmd_message_list(json_t *args, json_t **data, PavError *err) {
    const char *node_id = NULL;
    const char *search_query = NULL;
    const char *name = NULL;
    int kind = VIEW_MAILBOX;
    PavDbRead *read = NULL;
    int cap;
    int rc;

    if (data == NULL) {
        pav_error_set(err, "internal", "missing data", 0);
        return -1;
    }
    *data = NULL;
    if (take_view(args, &kind, &node_id, &search_query, &name, err) != 0) {
        return -1;
    }
    if (kind == VIEW_SEARCH) {
        return pav_cmd_message_search(search_query, data, err);
    }
    if (profile_ready(err) != 0) {
        return -1;
    }
    cap = list_rows_cap();
    if (pav_db_read_begin(&read) != 0) {
        pav_error_set(err, "internal", "cannot read messages", 0);
        return -1;
    }
    if (kind == VIEW_UNIFIED) {
        rc = list_unified(pav_db_read_handle(read), name, cap, data, err);
    } else {
        rc = list_mailbox(pav_db_read_handle(read), node_id, cap, data, err);
    }
    pav_db_read_end(read);
    return rc;
}

#ifndef NDEBUG
static unsigned g_seed_seq;

struct SeedCtx {
    char ra_id[256];
    char mailbox_id[128];
    char account_id[64];
    unsigned seq;
    int count;
    int64_t now;
};

static int seed_job(void *v, sqlite3 *db) {
    struct SeedCtx *ctx = v;
    sqlite3_stmt *email = NULL;
    sqlite3_stmt *member = NULL;
    int i;

    if (sqlite3_prepare_v2(db,
                           "INSERT INTO email (ra_id, id, subject, from_json, to_json, preview, received_at, "
                           "seen, flagged, answered, forwarded, draft, has_attachment) "
                           "VALUES (?1, ?2, ?3, ?4, '[]', 'seed', ?5, 0, 0, 0, 0, 0, 0)",
                           -1, &email, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(db,
                           "INSERT INTO email_mailbox (email_rowid, ra_id, mailbox_id, received_at) "
                           "VALUES (?1, ?2, ?3, ?4)",
                           -1, &member, NULL)) {
        sqlite3_finalize(email);
        sqlite3_finalize(member);
        return -1;
    }
    for (i = 0; i < ctx->count; i++) {
        char id[96];
        char subject[32];
        int wrote;
        sqlite3_int64 rowid;

        wrote = snprintf(id, sizeof(id), "seed-%lld-%u-%d", (long long)ctx->now, ctx->seq, i);
        if (wrote < 0 || (size_t)wrote >= sizeof(id) || snprintf(subject, sizeof(subject), "Seed %d", i + 1) < 0) {
            sqlite3_finalize(email);
            sqlite3_finalize(member);
            return -1;
        }
        sqlite3_reset(email);
        sqlite3_clear_bindings(email);
        if (pav_db_bind_text(email, 1, ctx->ra_id) != 0 || pav_db_bind_text(email, 2, id) != 0 ||
            pav_db_bind_text(email, 3, subject) != 0 ||
            pav_db_bind_text(email, 4, "[{\"name\":\"Seed\",\"email\":\"seed@pavillion.test\"}]") != 0 ||
            pav_db_bind_int64(email, 5, ctx->now - (int64_t)i) != 0 || sqlite3_step(email) != SQLITE_DONE) {
            sqlite3_finalize(email);
            sqlite3_finalize(member);
            return -1;
        }
        rowid = sqlite3_last_insert_rowid(db);
        sqlite3_reset(member);
        sqlite3_clear_bindings(member);
        if (pav_db_bind_int64(member, 1, (int64_t)rowid) != 0 || pav_db_bind_text(member, 2, ctx->ra_id) != 0 ||
            pav_db_bind_text(member, 3, ctx->mailbox_id) != 0 ||
            pav_db_bind_int64(member, 4, ctx->now - (int64_t)i) != 0 || sqlite3_step(member) != SQLITE_DONE) {
            sqlite3_finalize(email);
            sqlite3_finalize(member);
            return -1;
        }
    }
    sqlite3_finalize(email);
    sqlite3_finalize(member);
    return 0;
}

static void post_seeded(const char *account_id) {
    json_t *obj = json_object();
    char *text;

    if (obj == NULL || json_object_set_new(obj, "accountId", json_string(account_id != NULL ? account_id : "")) != 0 ||
        json_object_set_new(obj, "created", json_array()) != 0 ||
        json_object_set_new(obj, "updated", json_array()) != 0 ||
        json_object_set_new(obj, "destroyed", json_array()) != 0) {
        json_decref(obj);
        return;
    }
    text = json_dumps(obj, JSON_COMPACT);
    json_decref(obj);
    if (text == NULL) {
        return;
    }
    pav_event_post("mail.messages_changed", text);
    free(text);
}

static int take_seed(json_t *args, const char **mailbox, json_int_t *count, PavError *err) {
    const char *key;
    json_t *value;
    int saw_box = 0;
    int saw_count = 0;

    *mailbox = NULL;
    *count = 0;
    if (!json_is_object(args)) {
        pav_error_set(err, "bad_args", "arguments are not an object", 0);
        return -1;
    }
    json_object_foreach(args, key, value) {
        if (strcmp(key, "mailboxId") == 0) {
            const char *text = json_string_value(value);

            if (text == NULL || text[0] == '\0') {
                pav_error_set(err, "bad_args", "mailboxId must be a string", 0);
                return -1;
            }
            if (strlen(text) >= 640) {
                pav_error_set(err, "bad_args", "mailboxId is too long", 0);
                return -1;
            }
            *mailbox = text;
            saw_box = 1;
        } else if (strcmp(key, "count") == 0) {
            json_int_t n = json_integer_value(value);

            if (!json_is_integer(value) || n < 1) {
                pav_error_set(err, "bad_args", "count must be an integer", 0);
                return -1;
            }
            *count = n;
            saw_count = 1;
        } else {
            char buf[128];

            snprintf(buf, sizeof(buf), "unknown argument %s", key);
            pav_error_set(err, "bad_args", buf, 0);
            return -1;
        }
    }
    if (!saw_box) {
        pav_error_set(err, "bad_args", "mailboxId must be a string", 0);
        return -1;
    }
    if (!saw_count) {
        pav_error_set(err, "bad_args", "count must be an integer", 0);
        return -1;
    }
    return 0;
}

int pav_cmd_seed_messages(json_t *args, json_t **data, PavError *err) {
    const char *mailbox = NULL;
    json_int_t raw = 0;
    struct SeedCtx ctx;
    PavDbRead *read = NULL;
    int cap;
    int64_t now;
    json_t *root;

    if (data == NULL) {
        pav_error_set(err, "internal", "missing data", 0);
        return -1;
    }
    *data = NULL;
    if (take_seed(args, &mailbox, &raw, err) != 0) {
        return -1;
    }
    if (profile_ready(err) != 0) {
        return -1;
    }
    cap = list_rows_cap();
    if (raw > cap) {
        raw = cap;
    }
    memset(&ctx, 0, sizeof(ctx));
    ctx.count = (int)raw;
    ctx.seq = ++g_seed_seq;
    now = pav_now_ms();
    ctx.now = now > 0 ? now : 0;
    if (pav_db_read_begin(&read) != 0) {
        pav_error_set(err, "internal", "cannot seed messages", 0);
        return -1;
    }
    if (find_mailbox(pav_db_read_handle(read), mailbox, ctx.account_id, sizeof(ctx.account_id), ctx.ra_id,
                     sizeof(ctx.ra_id), ctx.mailbox_id, sizeof(ctx.mailbox_id), err) != 0) {
        pav_db_read_end(read);
        if (strcmp(err->code, "not_found") != 0) {
            pav_error_set(err, "internal", "cannot seed messages", 0);
        }
        return -1;
    }
    pav_db_read_end(read);
    if (pav_db_call(seed_job, &ctx) != 0) {
        pav_error_set(err, "internal", "cannot seed messages", 0);
        return -1;
    }
    root = json_object();
    if (root == NULL || json_object_set_new(root, "inserted", json_integer(ctx.count)) != 0 ||
        json_object_set_new(root, "mailboxId", json_string(mailbox)) != 0) {
        json_decref(root);
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    post_seeded(ctx.account_id);
    pav_log("mail", PAV_INFO, 1, "seeded %d", ctx.count);
    *data = root;
    return 0;
}
#endif

/* N-3a. One folder, newest first, capped by preferences.cache.listRows.
   The page keeps this reply. It does not ask for another page while scrolling. */

#include "dispatch.h"
#include "pav_blob.h"
#include "pav_db.h"

#include <jansson.h>
#include <sqlite3.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The command envelope is {"ok":true,"data":...}. Leave room under 2 MB. */
#define PAV_LIST_REPLY_MAX ((2u * 1024u * 1024u) - 128u)

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
static char *from_label(const char *raw) {
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

static size_t row_bytes(const char *id, const char *from, const char *subject) {
    /* {"id":,"from":,"subject":}, */
    return 24 + json_string_bytes(id) + json_string_bytes(from) + json_string_bytes(subject);
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

static int take_view(json_t *args, const char **id, PavError *err) {
    const char *key;
    json_t *value;
    json_t *view = NULL;
    int saw_view = 0;
    int saw_kind = 0;

    *id = NULL;
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
            if (!json_is_string(value) || strcmp(json_string_value(value), "mailbox") != 0) {
                pav_error_set(err, "bad_args", "view.kind must be mailbox", 0);
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
        } else {
            char buf[128];

            snprintf(buf, sizeof(buf), "unknown argument view.%s", key);
            pav_error_set(err, "bad_args", buf, 0);
            return -1;
        }
    }
    if (!saw_kind) {
        pav_error_set(err, "bad_args", "view.kind must be mailbox", 0);
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

static void fail_read(PavDbRead *read, json_t *root, PavError *err, const char *message) {
    if (read != NULL) {
        pav_db_read_end(read);
    }
    json_decref(root);
    pav_error_set(err, "internal", message, 0);
}

int pav_cmd_message_list(json_t *args, json_t **data, PavError *err) {
    const char *node_id = NULL;
    PavDbRead *read = NULL;
    sqlite3 *db;
    sqlite3_stmt *stmt = NULL;
    json_t *root = NULL;
    json_t *messages = NULL;
    char account_id[64];
    char ra_id[256];
    char mailbox_id[128];
    int64_t total = 0;
    int cap;
    int rc;
    size_t used;
    int found = 0;

    if (data == NULL) {
        pav_error_set(err, "internal", "missing data", 0);
        return -1;
    }
    *data = NULL;
    if (take_view(args, &node_id, err) != 0) {
        return -1;
    }
    {
        PavConfigLoad *load = pav_core_load();

        if (load == NULL || load->profile == NULL || strcmp(load->state, "ready") != 0) {
            pav_error_set(err, "not_ready", "profile is not ready", 0);
            return -1;
        }
    }
    cap = list_rows_cap();
    if (pav_db_read_begin(&read) != 0) {
        pav_error_set(err, "internal", "cannot read messages", 0);
        return -1;
    }
    db = pav_db_read_handle(read);
    rc = sqlite3_prepare_v2(db,
                            "SELECT r.account_id, m.ra_id, m.id FROM mailbox m "
                            "JOIN remote_account r ON r.id = m.ra_id "
                            "WHERE ('mbx:' || m.ra_id || ':' || m.id) = ?1 LIMIT 1",
                            -1, &stmt, NULL);
    if (rc != SQLITE_OK || pav_db_bind_text(stmt, 1, node_id) != 0) {
        sqlite3_finalize(stmt);
        fail_read(read, NULL, err, "cannot read messages");
        return -1;
    }
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        found = 1;
        if (copy_col(account_id, sizeof(account_id), stmt, 0) != 0 ||
            copy_col(ra_id, sizeof(ra_id), stmt, 1) != 0 ||
            copy_col(mailbox_id, sizeof(mailbox_id), stmt, 2) != 0 || account_id[0] == '\0' ||
            ra_id[0] == '\0' || mailbox_id[0] == '\0') {
            sqlite3_finalize(stmt);
            fail_read(read, NULL, err, "cannot read messages");
            return -1;
        }
    } else if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        fail_read(read, NULL, err, "cannot read messages");
        return -1;
    }
    sqlite3_finalize(stmt);
    stmt = NULL;
    if (!found) {
        pav_db_read_end(read);
        pav_error_set(err, "not_found", "mailbox is not in the store", 0);
        return -1;
    }

    rc = sqlite3_prepare_v2(db,
                            "SELECT COUNT(*) FROM email_mailbox WHERE ra_id = ?1 AND mailbox_id = ?2",
                            -1, &stmt, NULL);
    if (rc != SQLITE_OK || pav_db_bind_text(stmt, 1, ra_id) != 0 || pav_db_bind_text(stmt, 2, mailbox_id) != 0) {
        sqlite3_finalize(stmt);
        fail_read(read, NULL, err, "cannot read messages");
        return -1;
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        fail_read(read, NULL, err, "cannot read messages");
        return -1;
    }
    total = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    stmt = NULL;
    if (total < 0) {
        fail_read(read, NULL, err, "cannot read messages");
        return -1;
    }

    messages = json_array();
    root = json_object();
    if (messages == NULL || root == NULL ||
        json_object_set_new(root, "accountId", json_string(account_id)) != 0 ||
        json_object_set_new(root, "messages", messages) != 0 ||
        json_object_set_new(root, "total", json_integer(total)) != 0) {
        json_decref(messages);
        fail_read(read, root, err, "out of memory");
        return -1;
    }
    /* json_object_set_new stole messages. A later failure frees root only. */
    messages = json_object_get(root, "messages");
    used = 48 + json_string_bytes(account_id) + 24;

    rc = sqlite3_prepare_v2(db,
                            "SELECT e.id, IFNULL(e.subject, ''), IFNULL(e.from_json, '') "
                            "FROM email_mailbox em JOIN email e ON e.rowid = em.email_rowid "
                            "WHERE em.ra_id = ?1 AND em.mailbox_id = ?2 "
                            "ORDER BY em.received_at DESC, em.email_rowid DESC LIMIT ?3",
                            -1, &stmt, NULL);
    if (rc != SQLITE_OK || pav_db_bind_text(stmt, 1, ra_id) != 0 || pav_db_bind_text(stmt, 2, mailbox_id) != 0 ||
        pav_db_bind_int(stmt, 3, cap) != 0) {
        sqlite3_finalize(stmt);
        fail_read(read, root, err, "cannot read messages");
        return -1;
    }
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *id = (const char *)sqlite3_column_text(stmt, 0);
        const char *subject = (const char *)sqlite3_column_text(stmt, 1);
        const char *from_json = (const char *)sqlite3_column_text(stmt, 2);
        char *from;
        json_t *row;
        size_t next;

        if (id == NULL || id[0] == '\0') {
            sqlite3_finalize(stmt);
            fail_read(read, root, err, "cannot read messages");
            return -1;
        }
        if (subject == NULL) {
            subject = "";
        }
        from = from_label(from_json);
        if (from == NULL) {
            sqlite3_finalize(stmt);
            fail_read(read, root, err, "out of memory");
            return -1;
        }
        next = row_bytes(id, from, subject);
        if (json_array_size(messages) > 0 && used + next > PAV_LIST_REPLY_MAX) {
            free(from);
            break;
        }
        row = json_object();
        if (row == NULL || json_object_set_new(row, "id", json_string(id)) != 0 ||
            json_object_set_new(row, "from", json_string(from)) != 0 ||
            json_object_set_new(row, "subject", json_string(subject)) != 0 ||
            json_array_append_new(messages, row) != 0) {
            json_decref(row);
            free(from);
            sqlite3_finalize(stmt);
            fail_read(read, root, err, "out of memory");
            return -1;
        }
        free(from);
        used += next;
    }
    sqlite3_finalize(stmt);
    pav_db_read_end(read);
    read = NULL;
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        json_decref(root);
        pav_error_set(err, "internal", "cannot read messages", 0);
        return -1;
    }

    /* The estimate counts escapes. One dump confirms the reply still fits. */
    {
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
                break;
            }
            if (json_array_size(messages) == 0) {
                json_decref(root);
                pav_error_set(err, "too_large", "message list is too large", 0);
                return -1;
            }
            json_array_remove(messages, json_array_size(messages) - 1);
            guard++;
        }
    }

    *data = root;
    return 0;
}

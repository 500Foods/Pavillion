/* Q-1. Local search.run, the message.list search view, and index.rebuild. */

#define _POSIX_C_SOURCE 200809L

#include "pav_search.h"

#include "pav_activity.h"
#include "pav_blob.h"
#include "pav_db.h"
#include "pav_fts.h"
#include "pav_platform.h"
#include "pavillion.h"

#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SEARCH_LIMIT_MAX 1000
#define SEARCH_ACCOUNT_MAX 64
#define SEARCH_ID_MAX 256
#define SEARCH_NODE_MAX 640
#define REBUILD_BATCH 200
#define LIST_REPLY_MAX ((2u * 1024u * 1024u) - 128u)

enum { BIND_TEXT = 1, BIND_I64 = 2, BIND_INT = 3 };

typedef struct Bind {
    int kind;
    const char *text;
    int64_t i64;
    int i;
} Bind;

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static void *g_thread;
static int g_stop;
static int g_done;
static int64_t g_act;

static int ready_or_err(PavError *err) {
    PavConfigLoad *load = pav_core_load();

    if (load == NULL || load->profile == NULL || strcmp(load->state, "ready") != 0) {
        pav_error_set(err, "not_ready", "profile is not ready", 0);
        return -1;
    }
    return 0;
}

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

static int sql_add(char *buf, size_t cap, size_t *n, const char *text) {
    size_t add = strlen(text);

    if (*n + add + 1 > cap) {
        return -1;
    }
    memcpy(buf + *n, text, add + 1);
    *n += add;
    return 0;
}

static int split_day(const char *text, int *y, int *m, int *d) {
    int i;

    if (text == NULL || strlen(text) != 10 || text[4] != '-' || text[7] != '-') {
        return -1;
    }
    for (i = 0; i < 10; i++) {
        if (i == 4 || i == 7) {
            continue;
        }
        if (!isdigit((unsigned char)text[i])) {
            return -1;
        }
    }
    *y = (text[0] - '0') * 1000 + (text[1] - '0') * 100 + (text[2] - '0') * 10 + (text[3] - '0');
    *m = (text[5] - '0') * 10 + (text[6] - '0');
    *d = (text[8] - '0') * 10 + (text[9] - '0');
    return 0;
}

static int push_bind_text(Bind *binds, int *n, int cap, const char *text) {
    if (*n >= cap) {
        return -1;
    }
    binds[*n].kind = BIND_TEXT;
    binds[*n].text = text != NULL ? text : "";
    (*n)++;
    return 0;
}

static int push_bind_i64(Bind *binds, int *n, int cap, int64_t value) {
    if (*n >= cap) {
        return -1;
    }
    binds[*n].kind = BIND_I64;
    binds[*n].i64 = value;
    (*n)++;
    return 0;
}

static int add_flag(char *sql, size_t cap, size_t *n, const char *column, int value) {
    char piece[64];

    if (value < 0) {
        return 0;
    }
    if (value == 2) {
        return sql_add(sql, cap, n, " AND 0");
    }
    if (value != 0 && value != 1) {
        return -1;
    }
    snprintf(piece, sizeof(piece), " AND e.%s = %d", column, value);
    return sql_add(sql, cap, n, piece);
}

static int add_filters(char *sql, size_t cap, size_t *n, Bind *binds, int *bn, int bcap, const PavQuery *query,
                       const PavSearchIn *in, const char *zone) {
    const char *before;
    const char *after;
    const char *mailbox;
    int i;

    if (add_flag(sql, cap, n, "seen", pav_query_seen(query)) != 0 ||
        add_flag(sql, cap, n, "flagged", pav_query_flagged(query)) != 0 ||
        add_flag(sql, cap, n, "answered", pav_query_answered(query)) != 0 ||
        add_flag(sql, cap, n, "has_attachment", pav_query_attachment(query)) != 0) {
        return -1;
    }
    before = pav_query_before_day(query);
    after = pav_query_after_day(query);
    if (before != NULL) {
        int y;
        int m;
        int d;
        int64_t ms = 0;

        if (split_day(before, &y, &m, &d) != 0 || pav_local_day_utc_ms(zone, y, m, d, &ms) != 0) {
            return -2;
        }
        if (sql_add(sql, cap, n, " AND e.received_at < ?") != 0 || push_bind_i64(binds, bn, bcap, ms) != 0) {
            return -1;
        }
    }
    if (after != NULL) {
        int y;
        int m;
        int d;
        int64_t ms = 0;

        if (split_day(after, &y, &m, &d) != 0 || pav_local_day_utc_ms(zone, y, m, d, &ms) != 0) {
            return -2;
        }
        if (sql_add(sql, cap, n, " AND e.received_at >= ?") != 0 || push_bind_i64(binds, bn, bcap, ms) != 0) {
            return -1;
        }
    }
    mailbox = pav_query_mailbox(query);
    if (mailbox != NULL) {
        if (sql_add(sql, cap, n,
                    " AND EXISTS (SELECT 1 FROM email_mailbox em "
                    "JOIN mailbox m ON m.ra_id = em.ra_id AND m.id = em.mailbox_id "
                    "WHERE em.email_rowid = e.rowid AND (m.name = ? OR m.role = ?))") != 0 ||
            push_bind_text(binds, bn, bcap, mailbox) != 0 || push_bind_text(binds, bn, bcap, mailbox) != 0) {
            return -1;
        }
    }
    if (in->mailbox_node != NULL) {
        if (sql_add(sql, cap, n,
                    " AND EXISTS (SELECT 1 FROM email_mailbox em WHERE em.email_rowid = e.rowid "
                    "AND ('mbx:' || em.ra_id || ':' || em.mailbox_id) = ?)") != 0 ||
            push_bind_text(binds, bn, bcap, in->mailbox_node) != 0) {
            return -1;
        }
    }
    if (in->have_accounts) {
        if (in->ra_n <= 0 || in->ra_ids == NULL) {
            if (sql_add(sql, cap, n, " AND 0") != 0) {
                return -1;
            }
        } else {
            if (sql_add(sql, cap, n, " AND r.account_id IN (") != 0) {
                return -1;
            }
            for (i = 0; i < in->ra_n; i++) {
                if ((i > 0 && sql_add(sql, cap, n, ",") != 0) || sql_add(sql, cap, n, "?") != 0 ||
                    push_bind_text(binds, bn, bcap, in->ra_ids[i]) != 0) {
                    return -1;
                }
            }
            if (sql_add(sql, cap, n, ")") != 0) {
                return -1;
            }
        }
    }
    if (in->account_token != NULL) {
        if (sql_add(sql, cap, n, " AND (r.account_id = ? OR IFNULL(r.name, '') = ?)") != 0 ||
            push_bind_text(binds, bn, bcap, in->account_token) != 0 ||
            push_bind_text(binds, bn, bcap, in->account_token) != 0) {
            return -1;
        }
    }
    return 0;
}

static int bind_all(sqlite3_stmt *stmt, const Bind *binds, int n) {
    int i;

    for (i = 0; i < n; i++) {
        int rc;

        if (binds[i].kind == BIND_TEXT) {
            rc = sqlite3_bind_text(stmt, i + 1, binds[i].text, -1, SQLITE_TRANSIENT);
        } else if (binds[i].kind == BIND_I64) {
            rc = sqlite3_bind_int64(stmt, i + 1, (sqlite3_int64)binds[i].i64);
        } else {
            rc = sqlite3_bind_int(stmt, i + 1, binds[i].i);
        }
        if (rc != SQLITE_OK) {
            return -1;
        }
    }
    return 0;
}

static int query_has_fts(const PavQuery *query) {
    int groups;
    int g;

    groups = pav_query_groups(query);
    for (g = 0; g < groups; g++) {
        int terms = pav_query_terms(query, g);
        int i;

        for (i = 0; i < terms; i++) {
            int kind = 0;
            int neg = 0;
            const char *text = NULL;

            if (pav_query_term(query, g, i, &kind, &neg, &text) != 0) {
                continue;
            }
            if (kind == PAV_Q_WORD || kind == PAV_Q_PHRASE || kind == PAV_Q_FROM || kind == PAV_Q_TO ||
                kind == PAV_Q_CC || kind == PAV_Q_SUBJECT) {
                return 1;
            }
        }
    }
    return 0;
}

static int fill_page(sqlite3_stmt *stmt, json_t *messages, size_t *used, int limit) {
    int rc;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        int added;

        if (json_array_size(messages) >= (size_t)limit) {
            break;
        }
        added = pav_list_append_row(messages, used, LIST_REPLY_MAX, stmt);
        if (added < 0) {
            return -1;
        }
        if (added == 0) {
            break;
        }
    }
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        return -1;
    }
    return 0;
}

static int trim_reply(json_t *root, json_t *messages, PavError *err) {
    int guard = 0;

    while (guard < 8) {
        char *dump = json_dumps(root, JSON_COMPACT);
        size_t n = dump != NULL ? strlen(dump) : 0;

        free(dump);
        if (dump == NULL) {
            pav_error_set(err, "internal", "out of memory", 0);
            return -1;
        }
        if (n <= LIST_REPLY_MAX) {
            return 0;
        }
        if (json_array_size(messages) == 0) {
            pav_error_set(err, "too_large", "message list is too large", 0);
            return -1;
        }
        json_array_remove(messages, json_array_size(messages) - 1);
        guard++;
    }
    pav_error_set(err, "too_large", "message list is too large", 0);
    return -1;
}

int pav_search_exec(sqlite3 *db, const PavQuery *query, const PavSearchIn *in, json_t **data, PavError *err) {
    char filter[4096];
    char sql[8192];
    Bind filters[80];
    Bind binds[82];
    int filter_n = 0;
    int bind_n = 0;
    size_t filter_len = 0;
    char *match = NULL;
    char *only_not = NULL;
    const char *from;
    const char *where;
    const char *order;
    sqlite3_stmt *count_stmt = NULL;
    sqlite3_stmt *stmt = NULL;
    json_t *root = NULL;
    json_t *messages = NULL;
    int64_t total = 0;
    int limit;
    int wrote;
    int fr;
    int rc;
    size_t used;
    const char *account_id;

    if (data == NULL) {
        pav_error_set(err, "internal", "missing data", 0);
        return -1;
    }
    *data = NULL;
    if (db == NULL || query == NULL || in == NULL) {
        pav_error_set(err, "internal", "cannot search", 0);
        return -1;
    }
    if (pav_query_fts(query, &match, &only_not) != 0) {
        free(match);
        free(only_not);
        pav_error_set(err, "internal", "cannot search", 0);
        return -1;
    }
    if (match == NULL && only_not == NULL && query_has_fts(query)) {
        pav_error_set(err, "bad_args", "query combines OR and exclusion", 0);
        return -1;
    }
    filter[0] = '\0';
    fr = add_filters(filter, sizeof(filter), &filter_len, filters, &filter_n, (int)(sizeof(filters) / sizeof(filters[0])),
                     query, in, in->zone);
    if (fr == -2) {
        free(match);
        free(only_not);
        pav_error_set(err, "bad_args", "unknown timezone", 0);
        return -1;
    }
    if (fr != 0) {
        free(match);
        free(only_not);
        pav_error_set(err, "internal", "cannot search", 0);
        return -1;
    }
    {
        const char *hidden = " AND " PAV_SQL_NOT_DELETED;
        size_t hidden_n = strlen(hidden);

        if (filter_len + hidden_n >= sizeof(filter)) {
            free(match);
            free(only_not);
            pav_error_set(err, "internal", "cannot search", 0);
            return -1;
        }
        memcpy(filter + filter_len, hidden, hidden_n + 1);
        filter_len += hidden_n;
    }
    limit = in->limit < 1 ? 100 : in->limit;
    if (match != NULL) {
        from = "FROM email_fts JOIN email e ON e.rowid = email_fts.rowid "
               "JOIN remote_account r ON r.id = e.ra_id ";
        where = "WHERE email_fts MATCH ?";
        order = "ORDER BY bm25(email_fts), e.received_at DESC, e.rowid DESC ";
        binds[0].kind = BIND_TEXT;
        binds[0].text = match;
        bind_n = 1;
    } else if (only_not != NULL) {
        from = "FROM email e JOIN remote_account r ON r.id = e.ra_id ";
        where = "WHERE NOT EXISTS (SELECT 1 FROM email_fts WHERE email_fts.rowid = e.rowid AND email_fts MATCH ?)";
        order = "ORDER BY e.received_at DESC, e.rowid DESC ";
        binds[0].kind = BIND_TEXT;
        binds[0].text = only_not;
        bind_n = 1;
    } else {
        from = "FROM email e JOIN remote_account r ON r.id = e.ra_id ";
        where = "WHERE 1";
        order = "ORDER BY e.received_at DESC, e.rowid DESC ";
        bind_n = 0;
    }
    if (filter_n > 0) {
        memcpy(binds + bind_n, filters, (size_t)filter_n * sizeof(filters[0]));
    }
    bind_n += filter_n;
    wrote = snprintf(sql, sizeof(sql), "SELECT COUNT(*) %s%s%s", from, where, filter);
    if (wrote < 0 || (size_t)wrote >= sizeof(sql) || sqlite3_prepare_v2(db, sql, -1, &count_stmt, NULL) != SQLITE_OK ||
        bind_all(count_stmt, binds, bind_n) != 0) {
        sqlite3_finalize(count_stmt);
        free(match);
        free(only_not);
        pav_error_set(err, "internal", "cannot search", 0);
        return -1;
    }
    rc = sqlite3_step(count_stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(count_stmt);
        free(match);
        free(only_not);
        pav_error_set(err, "internal", "cannot search", 0);
        return -1;
    }
    total = sqlite3_column_int64(count_stmt, 0);
    sqlite3_finalize(count_stmt);
    count_stmt = NULL;
    if (bind_n >= (int)(sizeof(binds) / sizeof(binds[0]))) {
        free(match);
        free(only_not);
        pav_error_set(err, "internal", "cannot search", 0);
        return -1;
    }
    binds[bind_n].kind = BIND_INT;
    binds[bind_n].i = limit;
    bind_n++;
    wrote = snprintf(sql, sizeof(sql),
                     "SELECT e.id, IFNULL(e.subject, ''), IFNULL(e.from_json, ''), IFNULL(e.to_json, ''), "
                     "IFNULL(e.preview, ''), IFNULL(e.received_at, 0), IFNULL(e.seen, 0), IFNULL(e.flagged, 0), "
                     "IFNULL(e.answered, 0), IFNULL(e.forwarded, 0), IFNULL(e.has_attachment, 0), "
                     "IFNULL(r.account_id, '') %s%s%s %sLIMIT ?",
                     from, where, filter, order);
    if (wrote < 0 || (size_t)wrote >= sizeof(sql) || sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK ||
        bind_all(stmt, binds, bind_n) != 0) {
        sqlite3_finalize(stmt);
        free(match);
        free(only_not);
        pav_error_set(err, "internal", "cannot search", 0);
        return -1;
    }
    messages = json_array();
    root = json_object();
    account_id = in->account_id != NULL && in->account_id[0] != '\0' ? in->account_id : NULL;
    if (messages == NULL || root == NULL ||
        json_object_set_new(root, "accountId", account_id != NULL ? json_string(account_id) : json_null()) != 0 ||
        json_object_set_new(root, "messages", messages) != 0 ||
        json_object_set_new(root, "total", json_integer(total)) != 0) {
        json_decref(messages);
        json_decref(root);
        sqlite3_finalize(stmt);
        free(match);
        free(only_not);
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    messages = json_object_get(root, "messages");
    used = 48 + (account_id != NULL ? json_string_bytes(account_id) : 4) + 24;
    if (fill_page(stmt, messages, &used, limit) != 0) {
        json_decref(root);
        sqlite3_finalize(stmt);
        free(match);
        free(only_not);
        pav_error_set(err, "internal", "cannot search", 0);
        return -1;
    }
    sqlite3_finalize(stmt);
    free(match);
    free(only_not);
    if (trim_reply(root, messages, err) != 0) {
        json_decref(root);
        return -1;
    }
    *data = root;
    return 0;
}

static int all_digits(const char *text) {
    const unsigned char *p;

    if (text == NULL || text[0] == '\0') {
        return 0;
    }
    for (p = (const unsigned char *)text; *p != '\0'; p++) {
        if (!isdigit(*p)) {
            return 0;
        }
    }
    return 1;
}

static int number_eq(const char *stored, const char *token) {
    if (!all_digits(stored) || !all_digits(token)) {
        return 0;
    }
    while (*stored == '0') {
        stored++;
    }
    while (*token == '0') {
        token++;
    }
    return strcmp(stored, token) == 0;
}

/* 1 when a profile account matches, 0 when none does, -1 when the id does not fit. */
static int profile_account(const char *token, char *id_out, size_t id_n) {
    PavConfigLoad *load = pav_core_load();
    json_t *accounts;
    const char *id_hit = NULL;
    const char *num_hit = NULL;
    const char *name_hit = NULL;
    const char *pick;
    size_t i;
    size_t n;

    if (token == NULL || token[0] == '\0') {
        return 0;
    }
    if (load == NULL || load->profile == NULL || load->profile->doc == NULL) {
        return 0;
    }
    accounts = json_object_get((json_t *)load->profile->doc, "accounts");
    if (!json_is_array(accounts)) {
        return 0;
    }
    n = json_array_size(accounts);
    for (i = 0; i < n; i++) {
        json_t *acct = json_array_get(accounts, i);
        const char *id = json_string_value(json_object_get(acct, "id"));
        const char *number = json_string_value(json_object_get(acct, "number"));
        const char *name = json_string_value(json_object_get(acct, "name"));

        if (id == NULL || id[0] == '\0') {
            continue;
        }
        if (strcmp(id, token) == 0) {
            id_hit = id;
            break;
        }
        if (num_hit == NULL && number != NULL && number_eq(number, token)) {
            num_hit = id;
        }
        if (name_hit == NULL && name != NULL && strcmp(name, token) == 0) {
            name_hit = id;
        }
    }
    pick = id_hit != NULL ? id_hit : (num_hit != NULL ? num_hit : name_hit);
    if (pick == NULL) {
        return 0;
    }
    if (strlen(pick) >= id_n) {
        return -1;
    }
    memcpy(id_out, pick, strlen(pick) + 1);
    return 1;
}

static int account_known(sqlite3 *db, const char *id) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    int found;

    if (sqlite3_prepare_v2(db,
                           "SELECT 1 FROM account_state WHERE account_id = ?1 "
                           "UNION ALL SELECT 1 FROM remote_account WHERE account_id = ?1 LIMIT 1",
                           -1, &stmt, NULL) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return -1;
    }
    rc = sqlite3_step(stmt);
    found = rc == SQLITE_ROW;
    sqlite3_finalize(stmt);
    if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
        return -1;
    }
    return found ? 1 : 0;
}

static int mailbox_known(sqlite3 *db, const char *node) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    int found;

    if (sqlite3_prepare_v2(db, "SELECT 1 FROM mailbox WHERE ('mbx:' || ra_id || ':' || id) = ?1 LIMIT 1", -1, &stmt,
                           NULL) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 1, node, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return -1;
    }
    rc = sqlite3_step(stmt);
    found = rc == SQLITE_ROW;
    sqlite3_finalize(stmt);
    if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
        return -1;
    }
    return found ? 1 : 0;
}

static int scope_contains(const char **ids, int n, const char *id) {
    int i;

    for (i = 0; i < n; i++) {
        if (strcmp(ids[i], id) == 0) {
            return 1;
        }
    }
    return 0;
}

static const char *profile_zone(char *buf, size_t n) {
    PavConfigLoad *load = pav_core_load();

    if (load == NULL || load->profile == NULL || load->profile->timezone[0] == '\0') {
        return NULL;
    }
    snprintf(buf, n, "%s", load->profile->timezone);
    return buf;
}

static int take_scope_accounts(json_t *scope, const char **ids, int *n, PavError *err) {
    json_t *arr = json_object_get(scope, "accountIds");
    size_t i;
    size_t count;

    *n = 0;
    if (arr == NULL) {
        return 0;
    }
    if (!json_is_array(arr)) {
        pav_error_set(err, "bad_args", "scope.accountIds must be an array", 0);
        return -1;
    }
    count = json_array_size(arr);
    if (count == 0) {
        pav_error_set(err, "bad_args", "scope.accountIds is empty", 0);
        return -1;
    }
    if (count > SEARCH_ACCOUNT_MAX) {
        pav_error_set(err, "bad_args", "scope.accountIds is too long", 0);
        return -1;
    }
    for (i = 0; i < count; i++) {
        const char *text = json_string_value(json_array_get(arr, i));

        if (text == NULL || text[0] == '\0') {
            pav_error_set(err, "bad_args", "scope.accountIds must be strings", 0);
            return -1;
        }
        if (strlen(text) >= SEARCH_ID_MAX) {
            pav_error_set(err, "bad_args", "scope.accountIds is too long", 0);
            return -1;
        }
        ids[i] = text;
    }
    *n = (int)count;
    return 1;
}

static int apply_account_query(const PavQuery *query, const char **scope_ids, int scope_n, int have_scope,
                               char *pin, size_t pin_n, const char **use, int *use_n, PavSearchIn *in, PavError *err) {
    const char *token = pav_query_account(query);
    int matched = 0;

    in->ra_ids = NULL;
    in->ra_n = 0;
    in->have_accounts = 0;
    in->account_token = NULL;
    in->account_id = NULL;
    *use_n = 0;
    if (token != NULL) {
        matched = profile_account(token, pin, pin_n);
        if (matched < 0) {
            pav_error_set(err, "bad_args", "account is too long", 0);
            return -1;
        }
    }
    if (matched == 1) {
        in->have_accounts = 1;
        in->account_id = pin;
        if (have_scope && !scope_contains(scope_ids, scope_n, pin)) {
            in->ra_n = 0;
            return 0;
        }
        use[0] = pin;
        *use_n = 1;
        in->ra_ids = use;
        in->ra_n = 1;
        return 0;
    }
    if (token != NULL) {
        in->account_token = token;
    }
    if (have_scope) {
        int i;

        in->have_accounts = 1;
        for (i = 0; i < scope_n; i++) {
            use[i] = scope_ids[i];
        }
        *use_n = scope_n;
        in->ra_ids = use;
        in->ra_n = scope_n;
        if (scope_n == 1) {
            in->account_id = scope_ids[0];
        }
    }
    return 0;
}

int pav_cmd_message_search(const char *query, json_t **data, PavError *err) {
    PavDbRead *read = NULL;
    PavSearchIn in;
    char zone_buf[64];
    char pin[SEARCH_ID_MAX];
    const char *use[SEARCH_ACCOUNT_MAX];
    int use_n = 0;
    PavQuery *parsed = NULL;
    char msg[128];
    int rc;

    if (data == NULL) {
        pav_error_set(err, "internal", "missing data", 0);
        return -1;
    }
    *data = NULL;
    if (ready_or_err(err) != 0) {
        return -1;
    }
    msg[0] = '\0';
    if (pav_query_parse(query, &parsed, msg, sizeof(msg)) != 0) {
        pav_error_set(err, "bad_args", msg[0] != '\0' ? msg : "query is empty", 0);
        return -1;
    }
    memset(&in, 0, sizeof(in));
    if (apply_account_query(parsed, NULL, 0, 0, pin, sizeof(pin), use, &use_n, &in, err) != 0) {
        pav_query_free(parsed);
        return -1;
    }
    /* The search view has no scope, so the reply is not pinned to one account. */
    in.account_id = NULL;
    in.zone = profile_zone(zone_buf, sizeof(zone_buf));
    in.limit = list_rows_cap();
    if (pav_db_read_begin(&read) != 0) {
        pav_query_free(parsed);
        pav_error_set(err, "internal", "cannot search", 0);
        return -1;
    }
    rc = pav_search_exec(pav_db_read_handle(read), parsed, &in, data, err);
    pav_db_read_end(read);
    pav_query_free(parsed);
    return rc;
}

int pav_cmd_search_run(json_t *args, json_t **data, PavError *err) {
    const char *key;
    json_t *value;
    const char *query = NULL;
    json_t *scope = NULL;
    int saw_query = 0;
    int saw_server = 0;
    int limit = 100;
    int have_scope_accounts = 0;
    const char *scope_ids[SEARCH_ACCOUNT_MAX];
    int scope_n = 0;
    const char *mailbox = NULL;
    char pin[SEARCH_ID_MAX];
    const char *use[SEARCH_ACCOUNT_MAX];
    int use_n = 0;
    char zone_buf[64];
    PavSearchIn in;
    PavQuery *parsed = NULL;
    char msg[128];
    PavDbRead *read = NULL;
    int rc;
    int i;

    if (data == NULL) {
        pav_error_set(err, "internal", "missing data", 0);
        return -1;
    }
    *data = NULL;
    if (!json_is_object(args)) {
        pav_error_set(err, "bad_args", "arguments are not an object", 0);
        return -1;
    }
    json_object_foreach(args, key, value) {
        if (strcmp(key, "query") == 0) {
            const char *text = json_string_value(value);

            if (text == NULL) {
                pav_error_set(err, "bad_args", "query must be a string", 0);
                return -1;
            }
            if (text[0] == '\0') {
                pav_error_set(err, "bad_args", "query is empty", 0);
                return -1;
            }
            if (strlen(text) > PAV_QUERY_MAX) {
                pav_error_set(err, "bad_args", "query is too long", 0);
                return -1;
            }
            query = text;
            saw_query = 1;
        } else if (strcmp(key, "server") == 0) {
            if (!json_is_boolean(value)) {
                pav_error_set(err, "bad_args", "server must be a boolean", 0);
                return -1;
            }
            if (json_is_true(value)) {
                pav_error_set(err, "bad_args", "server search is not available", 0);
                return -1;
            }
            saw_server = 1;
        } else if (strcmp(key, "limit") == 0) {
            json_int_t n;

            if (!json_is_integer(value)) {
                pav_error_set(err, "bad_args", "limit must be an integer", 0);
                return -1;
            }
            n = json_integer_value(value);
            if (n < 1 || n > SEARCH_LIMIT_MAX) {
                pav_error_set(err, "bad_args", "limit is out of range", 0);
                return -1;
            }
            limit = (int)n;
        } else if (strcmp(key, "scope") == 0) {
            if (!json_is_object(value)) {
                pav_error_set(err, "bad_args", "scope must be an object", 0);
                return -1;
            }
            scope = value;
        } else {
            char buf[128];

            snprintf(buf, sizeof(buf), "unknown argument %s", key);
            pav_error_set(err, "bad_args", buf, 0);
            return -1;
        }
    }
    if (!saw_query) {
        pav_error_set(err, "bad_args", "query is required", 0);
        return -1;
    }
    if (!saw_server) {
        pav_error_set(err, "bad_args", "server is required", 0);
        return -1;
    }
    if (ready_or_err(err) != 0) {
        return -1;
    }
    if (scope != NULL) {
        const char *skey;
        json_t *sval;

        json_object_foreach(scope, skey, sval) {
            if (strcmp(skey, "accountIds") != 0 && strcmp(skey, "mailboxId") != 0) {
                char buf[128];

                snprintf(buf, sizeof(buf), "unknown argument scope.%s", skey);
                pav_error_set(err, "bad_args", buf, 0);
                return -1;
            }
        }
        if (take_scope_accounts(scope, scope_ids, &scope_n, err) < 0) {
            return -1;
        }
        have_scope_accounts = scope_n > 0;
        mailbox = json_string_value(json_object_get(scope, "mailboxId"));
        if (json_object_get(scope, "mailboxId") != NULL) {
            if (mailbox == NULL || mailbox[0] == '\0') {
                pav_error_set(err, "bad_args", "scope.mailboxId must be a string", 0);
                return -1;
            }
            if (strlen(mailbox) >= SEARCH_NODE_MAX) {
                pav_error_set(err, "bad_args", "scope.mailboxId is too long", 0);
                return -1;
            }
        }
    }
    msg[0] = '\0';
    if (pav_query_parse(query, &parsed, msg, sizeof(msg)) != 0) {
        pav_error_set(err, "bad_args", msg[0] != '\0' ? msg : "query is empty", 0);
        return -1;
    }
    memset(&in, 0, sizeof(in));
    if (apply_account_query(parsed, scope_ids, scope_n, have_scope_accounts, pin, sizeof(pin), use, &use_n, &in,
                            err) != 0) {
        pav_query_free(parsed);
        return -1;
    }
    in.mailbox_node = mailbox;
    in.zone = profile_zone(zone_buf, sizeof(zone_buf));
    in.limit = limit;
    if (pav_db_read_begin(&read) != 0) {
        pav_query_free(parsed);
        pav_error_set(err, "internal", "cannot search", 0);
        return -1;
    }
    if (mailbox != NULL) {
        int known = mailbox_known(pav_db_read_handle(read), mailbox);

        if (known < 0) {
            pav_db_read_end(read);
            pav_query_free(parsed);
            pav_error_set(err, "internal", "cannot search", 0);
            return -1;
        }
        if (known == 0) {
            pav_db_read_end(read);
            pav_query_free(parsed);
            pav_error_set(err, "not_found", "mailbox is not in the store", 0);
            return -1;
        }
    }
    for (i = 0; i < scope_n; i++) {
        int known = account_known(pav_db_read_handle(read), scope_ids[i]);

        if (known < 0) {
            pav_db_read_end(read);
            pav_query_free(parsed);
            pav_error_set(err, "internal", "cannot search", 0);
            return -1;
        }
        if (known == 0) {
            pav_db_read_end(read);
            pav_query_free(parsed);
            pav_error_set(err, "not_found", "account is not in the store", 0);
            return -1;
        }
    }
    rc = pav_search_exec(pav_db_read_handle(read), parsed, &in, data, err);
    pav_db_read_end(read);
    pav_query_free(parsed);
    return rc;
}

typedef struct RebuildJob {
    int64_t act;
} RebuildJob;

static int rebuild_stopping(void) {
    int stop;

    pthread_mutex_lock(&g_mu);
    stop = g_stop;
    pthread_mutex_unlock(&g_mu);
    return stop;
}

static void rebuild_mark_done(void) {
    pthread_mutex_lock(&g_mu);
    g_done = 1;
    pthread_mutex_unlock(&g_mu);
}

static int load_rowids(int64_t **out, int *out_n) {
    PavDbRead *read = NULL;
    sqlite3_stmt *stmt = NULL;
    int64_t *ids = NULL;
    int n = 0;
    int cap = 0;
    int rc;

    *out = NULL;
    *out_n = 0;
    if (pav_db_read_begin(&read) != 0) {
        return -1;
    }
    if (sqlite3_prepare_v2(pav_db_read_handle(read), "SELECT rowid FROM email ORDER BY rowid", -1, &stmt, NULL) !=
        SQLITE_OK) {
        pav_db_read_end(read);
        return -1;
    }
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (n == cap) {
            int next = cap == 0 ? 256 : cap * 2;
            int64_t *grown;

            if (next < cap) {
                free(ids);
                sqlite3_finalize(stmt);
                pav_db_read_end(read);
                return -1;
            }
            grown = realloc(ids, (size_t)next * sizeof(*ids));
            if (grown == NULL) {
                free(ids);
                sqlite3_finalize(stmt);
                pav_db_read_end(read);
                return -1;
            }
            ids = grown;
            cap = next;
        }
        ids[n++] = (int64_t)sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    pav_db_read_end(read);
    if (rc != SQLITE_DONE) {
        free(ids);
        return -1;
    }
    *out = ids;
    *out_n = n;
    return 0;
}

static int delete_all_job(void *ctx, sqlite3 *db) {
    (void)ctx;
    return pav_fts_delete_all(db);
}

typedef struct RebuildBatch {
    const int64_t *ids;
    int begin;
    int end;
} RebuildBatch;

static int reinsert_job(void *ctx, sqlite3 *db) {
    RebuildBatch *batch = ctx;
    int i;

    for (i = batch->begin; i < batch->end; i++) {
        PavFtsDoc doc;
        char *body = NULL;
        int rc;

        memset(&doc, 0, sizeof(doc));
        if (pav_fts_load_headers(db, batch->ids[i], &doc) != 0 ||
            pav_fts_indexed_body(db, batch->ids[i], &body) != 0) {
            pav_fts_doc_free(&doc);
            free(body);
            return -1;
        }
        rc = pav_fts_insert(db, batch->ids[i], doc.subject, doc.from_text, doc.to_text, body);
        pav_fts_doc_free(&doc);
        free(body);
        if (rc != 0) {
            return -1;
        }
    }
    return 0;
}

static void *rebuild_main(void *arg) {
    RebuildJob *job = arg;
    int64_t act = job->act;
    int64_t *ids = NULL;
    int n = 0;
    int done = 0;

    free(job);
    if (load_rowids(&ids, &n) != 0) {
        pav_log("search", PAV_WARN, 0, "index rebuild failed");
        pav_activity_end(act, PAV_ACT_FAILED, "cannot read messages");
        rebuild_mark_done();
        return NULL;
    }
    if (pav_db_call(delete_all_job, NULL) != 0) {
        free(ids);
        pav_log("search", PAV_WARN, 0, "index rebuild failed");
        pav_activity_end(act, PAV_ACT_FAILED, "cannot clear the index");
        rebuild_mark_done();
        return NULL;
    }
    pav_log("search", PAV_INFO, 1, "index rebuild started, %d messages", n);
    pav_activity_progress(act, 0, n, "messages", NULL);
    if (n == 0) {
        pav_activity_end(act, PAV_ACT_DONE, NULL);
        pav_log("search", PAV_INFO, 1, "index rebuild finished, %d messages", n);
        rebuild_mark_done();
        return NULL;
    }
    while (done < n) {
        RebuildBatch batch;
        int end;

        if (rebuild_stopping() || pav_activity_cancelled(act)) {
            free(ids);
            pav_log("search", PAV_WARN, 0, "index rebuild cancelled");
            pav_activity_end(act, PAV_ACT_CANCELLED, NULL);
            rebuild_mark_done();
            return NULL;
        }
        end = done + REBUILD_BATCH;
        if (end > n) {
            end = n;
        }
        batch.ids = ids;
        batch.begin = done;
        batch.end = end;
        if (pav_db_call(reinsert_job, &batch) != 0) {
            free(ids);
            pav_log("search", PAV_WARN, 0, "index rebuild failed");
            pav_activity_end(act, PAV_ACT_FAILED, "cannot write the index");
            rebuild_mark_done();
            return NULL;
        }
        done = end;
        pav_activity_progress(act, done, n, "messages", NULL);
    }
    free(ids);
    pav_activity_end(act, PAV_ACT_DONE, NULL);
    pav_log("search", PAV_INFO, 1, "index rebuild finished, %d messages", n);
    rebuild_mark_done();
    return NULL;
}

static void format_act(char *dst, size_t n, int64_t id) {
    snprintf(dst, n, "act-%lld", (long long)id);
}

static int reply_activity(int64_t id, json_t **data, PavError *err) {
    char text[64];
    json_t *root;

    format_act(text, sizeof(text), id);
    root = json_object();
    if (root == NULL || json_object_set_new(root, "activityId", json_string(text)) != 0) {
        json_decref(root);
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    *data = root;
    return 0;
}

static void join_finished(void) {
    void *thread = NULL;

    pthread_mutex_lock(&g_mu);
    if (g_thread != NULL && g_done) {
        thread = g_thread;
        g_thread = NULL;
        g_done = 0;
    }
    pthread_mutex_unlock(&g_mu);
    if (thread != NULL) {
        pav_thread_join(thread);
    }
}

int pav_cmd_index_rebuild(json_t *args, json_t **data, PavError *err) {
    const char *key;
    json_t *value;
    RebuildJob *job;
    int64_t act;
    int64_t running;

    if (data == NULL) {
        pav_error_set(err, "internal", "missing data", 0);
        return -1;
    }
    *data = NULL;
    if (!json_is_object(args)) {
        pav_error_set(err, "bad_args", "arguments are not an object", 0);
        return -1;
    }
    json_object_foreach(args, key, value) {
        char buf[128];

        snprintf(buf, sizeof(buf), "unknown argument %s", key);
        pav_error_set(err, "bad_args", buf, 0);
        return -1;
    }
    if (ready_or_err(err) != 0) {
        return -1;
    }
    join_finished();
    pthread_mutex_lock(&g_mu);
    if (g_thread != NULL && !g_done) {
        running = g_act;
        pthread_mutex_unlock(&g_mu);
        return reply_activity(running, data, err);
    }
    pthread_mutex_unlock(&g_mu);
    act = pav_activity_begin("index.rebuild", NULL, "Rebuild", 1, 0);
    if (act <= 0) {
        pav_error_set(err, "internal", "cannot start rebuild", 0);
        return -1;
    }
    job = malloc(sizeof(*job));
    if (job == NULL) {
        pav_activity_end(act, PAV_ACT_FAILED, "out of memory");
        pav_error_set(err, "internal", "cannot start rebuild", 0);
        return -1;
    }
    job->act = act;
    pthread_mutex_lock(&g_mu);
    g_stop = 0;
    g_done = 0;
    g_act = act;
    if (pav_thread_create(&g_thread, rebuild_main, job) != 0) {
        g_thread = NULL;
        g_act = 0;
        pthread_mutex_unlock(&g_mu);
        free(job);
        pav_activity_end(act, PAV_ACT_FAILED, "cannot start rebuild");
        pav_log("search", PAV_WARN, 0, "index rebuild failed");
        pav_error_set(err, "internal", "cannot start rebuild", 0);
        return -1;
    }
    pthread_mutex_unlock(&g_mu);
    return reply_activity(act, data, err);
}

void pav_search_shutdown(void) {
    void *thread;

    pthread_mutex_lock(&g_mu);
    g_stop = 1;
    thread = g_thread;
    pthread_mutex_unlock(&g_mu);
    if (thread != NULL) {
        pav_thread_join(thread);
    }
    pthread_mutex_lock(&g_mu);
    g_thread = NULL;
    g_done = 0;
    g_stop = 0;
    g_act = 0;
    pthread_mutex_unlock(&g_mu);
}

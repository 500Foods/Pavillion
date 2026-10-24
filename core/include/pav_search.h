#ifndef PAV_SEARCH_H
#define PAV_SEARCH_H

#include "dispatch.h"
#include "pav_query.h"

#include <sqlite3.h>

/* Join a running index rebuild, if there is one. Called before the store closes. */
void pav_search_shutdown(void);

int pav_cmd_search_run(json_t *args, json_t **data, PavError *err);
int pav_cmd_index_rebuild(json_t *args, json_t **data, PavError *err);

/* message.list {view:{kind:"search", query}}. */
int pav_cmd_message_search(const char *query, json_t **data, PavError *err);

/* Direct search for tests. ra_ids NULL does not filter accounts. ra_n 0
   matches nothing. mailbox_node is mbx:<ra>:<id> or NULL. zone NULL uses
   America/Vancouver. limit < 1 uses 100. account_id NULL becomes JSON null. */
typedef struct PavSearchIn {
    const char **ra_ids;
    int ra_n;
    int have_accounts;
    const char *mailbox_node;
    const char *zone;
    int limit;
    const char *account_id;
    /* Set when account: did not match a profile account. Rows stay when the
       remote account id or its name equals this token. */
    const char *account_token;
} PavSearchIn;

int pav_search_exec(sqlite3 *db, const PavQuery *query, const PavSearchIn *in, json_t **data, PavError *err);

/* A keywords object that contains $deleted. NULL and invalid JSON stay. */
#define PAV_SQL_NOT_DELETED "json_type(e.keywords, '$.\"$deleted\"') IS NULL"

/* One message.list / search.run row. The statement's columns, in order:
   id, subject, from_json, to_json, preview, received_at, seen, flagged,
   answered, forwarded, has_attachment, account_id.
   Returns 1 appended, 0 when this row would pass max_bytes (not appended;
   the first row is still appended), -1 on error. */
int pav_list_append_row(json_t *messages, size_t *used, size_t max_bytes, sqlite3_stmt *stmt);

#endif

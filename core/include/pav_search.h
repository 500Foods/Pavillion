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

#endif

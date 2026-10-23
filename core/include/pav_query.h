#ifndef PAV_QUERY_H
#define PAV_QUERY_H

#include <jansson.h>

#include <stddef.h>
#include <stdint.h>

/* Q-1. One AST for the local index and the JMAP filter. */

#define PAV_QUERY_MAX 2000

typedef struct PavQuery PavQuery;

/* 0 on success. A NULL or empty query, a query past PAV_QUERY_MAX, or a
   broken date is -1 and err explains it. err may be NULL. */
int pav_query_parse(const char *text, PavQuery **out, char *err, size_t err_len);

void pav_query_free(PavQuery *query);

/* FTS5 MATCH text. *match is NULL when the query has no positive full-text
   term. *only_not is NULL, or the MATCH that a NOT EXISTS should apply when
   every full-text term is negated. Either pointer may be NULL to skip it.
   Caller frees the strings. 0, or -1. */
int pav_query_fts(const PavQuery *query, char **match, char **only_not);

/* JMAP Email/query filter. account: is not part of it. zone is an IANA name.
   0, or -1. */
int pav_query_jmap(const PavQuery *query, const char *zone, json_t **out, char *err, size_t err_len);

/* UTC milliseconds of local midnight at the start of year-month-day.
   before: is received_at strictly before this instant. after: is received_at
   at or after it. 0, or -1. */
int pav_local_day_utc_ms(const char *zone, int year, int month, int day, int64_t *utc_ms);

/* SQL pieces the search command binds. Dates are resolved in zone.
   Pointers are NULL when that filter is absent. The ints are -1 when absent,
   0 or 1 when set, and 2 when both polarities were requested (is:read and
   is:unread, or a flag and its negation). A 2 matches nothing. Seen,
   flagged, answered, and attachment use that same scale. */
int pav_query_seen(const PavQuery *query);
int pav_query_flagged(const PavQuery *query);
int pav_query_answered(const PavQuery *query);
int pav_query_attachment(const PavQuery *query);
const char *pav_query_before_day(const PavQuery *query);
const char *pav_query_after_day(const PavQuery *query);
const char *pav_query_mailbox(const PavQuery *query);
const char *pav_query_account(const PavQuery *query);

typedef enum PavQKind {
    PAV_Q_WORD = 1,
    PAV_Q_PHRASE,
    PAV_Q_FROM,
    PAV_Q_TO,
    PAV_Q_CC,
    PAV_Q_SUBJECT
} PavQKind;

int pav_query_groups(const PavQuery *query);
int pav_query_terms(const PavQuery *query, int group);
int pav_query_term(const PavQuery *query, int group, int index, int *kind, int *neg, const char **text);

#endif

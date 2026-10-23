#ifndef PAV_BODY_H
#define PAV_BODY_H

#include <stddef.h>
#include <stdint.h>

/* N-1. message.get is declared in dispatch.h. These are the test seams. */

/* Email/get JSON for one id. Caller frees. NULL on failure. */
char *pav_body_email_get_request(const char *jmap_account, const char *email_id);

/* Parse one Email/get list object (the email, not the method response).
   0 stored complete, 1 headers only because a used body value is missing or
   truncated (body_state unchanged; the fetch path downloads), -1 on failure. */
int pav_body_apply_jmap(int64_t email_rowid, const char *ra, const char *email_json);

/* Core thread. A prefetch continuation posted from the net thread.
   Kind 5 asks the core thread to start the next prefetch. The net thread
   must not call pav_net_request while it still holds the server mutex. */
#define PAV_BODY_CORE_KIND 4
#define PAV_BODY_KICK_KIND 5
int pav_body_take_core(void *result);

/* Parse raw MIME and store it the same way. */
int pav_body_store_bytes(int64_t email_rowid, const char *ra, const void *bytes, size_t len);

/* Unfetched and evicted rows for one profile account. mode is none, recent,
   or all. recent keeps received_at within 30 days of now_ms. Writes at most
   limit rowids. 0, or -1. */
int pav_body_select(const char *account_id, const char *mode, int64_t now_ms, int limit,
                    int64_t *out, int *out_n);

/* After a sync reaches idle. At most two fetches in flight for that account. */
void pav_body_prefetch(const char *account_id);

/* activity.cancel for a body.fetch. Other ids are ignored. */
void pav_body_cancel(int64_t activity_id);

#endif

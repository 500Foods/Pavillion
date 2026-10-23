#ifndef PAV_SYNC_H
#define PAV_SYNC_H

#include <jansson.h>

#include <stdint.h>
#include <time.h>

/* Start supervisors for the profile's enabled JMAP accounts. Core thread. */
int pav_sync_boot(json_t *profile_doc, const char *profile_id);

/* Cancel in-flight session requests and drop supervisors. Does not join the
   net threads and does not touch the store. Core thread, before the store
   closes. */
void pav_sync_shutdown(void);

/* Core thread, after the profile change is stored. */
void pav_sync_note_account(json_t *account);
void pav_sync_note_removed(const char *account_id);
void pav_sync_note_secret(const char *account_id);

/* NULL syncs every enabled account. An unknown or disabled id does nothing.
   Returns 0, or -1 when the request cannot be queued. */
int pav_sync_now(const char *account_id);

/* 1 when a poll is armed. *out is an absolute CLOCK_REALTIME deadline. */
int pav_sync_deadline(struct timespec *out);

/* Runs due retries. No-op when the deadline has not passed. */
void pav_sync_on_wake(void);

/* Core thread. Takes ownership of a completion posted from the net thread. */
void pav_sync_on_result(void *result);
void pav_sync_result_free(void *result);

/* Test seams. poll_ms below 1000 is raised to 1000. */
void pav_sync_set_poll_ms(int64_t ms);
int pav_sync_attempt_count(const char *account_id);

/* N-1. A user body fetch holds the next email page. hold 1 increments.
   hold 0 decrements and, at zero, resumes a paused walk on the core thread.
   An account with no supervisor is a no-op. */
void pav_sync_set_body_hold(const char *account_id, int hold);

/* 1 when the account is idle, nothing is in flight, and no body fetch holds
   the walk. No supervisor is 0. */
int pav_sync_is_idle(const char *account_id);

#endif

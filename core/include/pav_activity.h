#ifndef PAV_ACTIVITY_H
#define PAV_ACTIVITY_H

#include <stdint.h>

/* End states for pav_activity_end. */
#define PAV_ACT_DONE 1
#define PAV_ACT_FAILED 2
#define PAV_ACT_CANCELLED 3

/* Returns a positive id, or 0 when the kind is not in the initial set, the
   title is empty or too long, the parent is not 0 and not an activity, or
   256 activities are already running or queued. parent 0 means no parent.
   Callable from any thread. */
int64_t pav_activity_begin(const char *kind, const char *account_id, const char *title,
                           int cancellable, int64_t parent);

/* total below 0 means the total is unknown and the ETA stays null. NULL unit
   or detail leaves that field as it was. */
void pav_activity_progress(int64_t id, int64_t done, int64_t total, const char *unit,
                           const char *detail);

/* Ignores an unknown id and an activity that has already finished. */
void pav_activity_end(int64_t id, int state, const char *error);

/* 1 when cancel was requested and the activity has not finished. */
int pav_activity_cancelled(int64_t id);

/* Loads the activity table from the open SQLite connection. Does not set
   PRAGMA user_version and does not close db. NULL fails. A second attach
   replaces the in-memory rows with the table. */
int pav_activity_attach(void *db);

/* Cancels running work, joins demo threads, persists finished rows, and
   drops the SQLite statements. Does not close the connection. */
void pav_activity_shutdown(void);

#endif

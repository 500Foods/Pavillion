#ifndef PAVILLION_H
#define PAVILLION_H

#include <stddef.h>

typedef struct PavillionStatus {
    char profile_name[256];
    char profile_id[64];
    char state[32]; /* "ready" or "error" */
    char error[512];
} PavillionStatus;

/* A-1 stub. workdir and err are ignored. Returns 0. */
int pavillion_start(const char *workdir, char *err, size_t err_len);

/* Safe to call more than once. Does nothing in A-1. */
void pavillion_stop(void);

/* Fills out with state "ready" and empty name, id, and error. Returns 0,
   or -1 when out is NULL. */
int pavillion_status(PavillionStatus *out);

/* A line numerically above the current level is dropped. Default level is info. */
#define PAV_ERROR 0
#define PAV_WARN  1
#define PAV_INFO  2
#define PAV_DEBUG 3

/* num_args is the number of format specifiers in fmt. A mismatch is logged
   at error and fmt is not formatted. */
void pav_log(const char *subsystem, int level, int num_args, const char *fmt, ...);

/* Test-only. Truncates path and writes later lines there. NULL closes that
   file. Returns 0, or -1 when path cannot be opened. */
int pav_log_set_output(const char *path);

/* The log thread writes queued lines. A second start while it runs returns -1.
   Shutdown drains the queue and joins. A second shutdown does nothing. */
int pav_log_thread_start(void);
void pav_log_thread_shutdown(void);

#endif

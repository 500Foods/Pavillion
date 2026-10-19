#ifndef PAVILLION_H
#define PAVILLION_H

#include <stddef.h>
#include <stdint.h>

/* Incompatible-change epoch. pavillion_api_version() returns this number.
   build.rs copies it into the shell. Adding a command or an optional field
   does not bump it. The line is a bare decimal integer so the shell and the
   C compiler cannot disagree about its value. */
#define PAVILLION_API_VERSION 3

/* preferences.ui as serialized JSON text, not counting the terminating NUL. */
#define PAV_UI_JSON_MAX 65536

/* Returns PAVILLION_API_VERSION. Safe before pavillion_start. The shell
   calls this symbol; it does not substitute the macro. */
int pavillion_api_version(void);

/* Starts the core thread and returns after it has published state "ready"
   or "error". Returns 0, or -1 when the core is already running, the path
   does not fit, or the thread cannot be created. */
int pavillion_start(const char *workdir, char *err, size_t err_len);

/* The working directory passed to pavillion_start. Empty before start. */
const char *pav_core_workdir(void);

/* Pushes stop and joins the core thread. A second call does nothing. */
void pavillion_stop(void);

/* Test-only. 1 while the core thread is inside its loop, otherwise 0. */
int pav_core_thread_alive(void);

/* A line numerically above the current level is dropped. Default level is info. */
#define PAV_ERROR 0
#define PAV_WARN  1
#define PAV_INFO  2
#define PAV_DEBUG 3
#define PAV_TRACE 4

/* num_args is the number of format specifiers in fmt. A mismatch is logged
   at error and fmt is not formatted. */
void pav_log(const char *subsystem, int level, int num_args, const char *fmt, ...);

/* Runs one command on the core thread. args_json is a JSON object.
   *result_json is {"ok":true,"data":...} or
   {"ok":false,"error":{"code":"...","message":"...","retryable":false}}.
   Returns 0 when a reply was produced, or -1 when it could not be.
   Free *result_json with pavillion_free. */
int pavillion_call(const char *cmd, const char *args_json, char **result_json);

/* Frees a buffer the core handed out (a call reply, or a later body). */
void pavillion_free(void *p);

/* One callback for every event. Called on the event thread, not the core
   thread. The host only queues. json is UTF-8 JSON text. */
typedef void (*pav_event_cb)(const char *name, const char *json, void *user);
void pav_on_event(pav_event_cb cb, void *user);

/* Queues an event. With no callback, or before the event thread is running,
   the event is dropped and counted and this returns 0. A full queue increments
   the same counter and returns -1. json NULL is "{}". */
int pav_event_post(const char *name, const char *json);

/* Events dropped because nobody was listening, or because the queue was full. */
unsigned long long pav_event_dropped(void);

/* Registered commands, in strcmp order. pav_command_name returns NULL
   when index is out of range. */
int pav_command_count(void);
const char *pav_command_name(int index);

/* URI scheme handlers. The host forwards the request unparsed. Returns 0
   when *out was filled, including a 4xx. Free headers_json and body with
   pavillion_free. */
typedef struct PavBytes {
    int status;
    char *headers_json;
    void *body;
    size_t body_len;
} PavBytes;
int pav_protocol(const char *req_json, PavBytes *out);

/* Unknown subcommand. Prints "pavillion: no subcommands yet" and returns 1.
   --help and --version stay in the shell and do not call this. */
int pav_cli_main(int argc, char **argv);

/* Test-only. 1 when the caller is the running core thread. */
int pav_on_core_thread(void);

/* Test-only text sink. Truncates path and writes later lines there instead
   of log.db. NULL closes it. Returns 0, or -1 when path cannot be opened. */
int pav_log_set_output(const char *path);

/* A workdir exists: stderr drops to WARN/ERROR and lines are kept for
   log.db until pav_log_attach_profile. NULL forgets them. Creates nothing. */
int pav_log_set_workdir(const char *workdir);

/* The log thread opens log.db at path (H-4 Lock), writes the lines kept since
   the workdir once, renumbered to continue the stored seq. Needs the log
   thread. Does nothing when a test sink is set. Returns 0, or -1. */
int pav_log_attach_profile(const char *path);

typedef struct PavLogStats {
    int64_t bytes;
    int64_t records;
    int64_t oldest_seq;
    int64_t oldest_ts;
    int cap_mb;
} PavLogStats;

/* preferences.log_max_mb, 1 to 100 (default 10). The cap trimmed to at
   shutdown and by pav_log_trim(0). */
int pav_log_set_cap_mb(int mb);
/* Each runs on the log thread and needs log.db open. Returns 0, or -1. */
int pav_log_stats(PavLogStats *out);
/* Estimate after a trim to mb (1 to 100). Deletes nothing. */
int pav_log_trim_preview(int mb, PavLogStats *out);
/* Trims to mb and makes it the cap; mb 0 trims to the saved cap. */
int pav_log_trim(int mb, PavLogStats *out);

/* Blocks until lines queued before this call are in log.db (or the test
   sink) and the ring. */
void pav_log_flush(void);

/* The log thread writes queued lines. A second start while it runs returns -1.
   Shutdown drains the queue and joins. A second shutdown does nothing.
   pavillion_start starts it when it is not already running. */
int pav_log_thread_start(void);
void pav_log_thread_shutdown(void);

#endif

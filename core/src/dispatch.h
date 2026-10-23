#ifndef DISPATCH_H
#define DISPATCH_H

#include "pav_config.h"
#include <jansson.h>

/* Filled by a handler. retryable is kept only for "server", and forced on
   for "network". Every other code stores 0. */
typedef struct PavError {
    char code[32];
    char message[512];
    int retryable;
} PavError;

/* One queued pavillion_call. The caller allocates it and frees it after
   done is set. The core thread owns args until it decrefs them. */
typedef struct PavCall {
    char name[64];
    json_t *args;
    int done;
    char *reply;
} PavCall;

void pav_error_set(PavError *err, const char *code, const char *message, int retryable);

/* malloc'd envelope. pavillion_free releases it. NULL means allocation failed. */
char *pav_reply_error(const char *code, const char *message, int retryable);
char *pav_reply_ok(json_t *data);

/* Runs the handler for name. args is an object and is not freed here.
   The returned text is the envelope. */
char *pav_dispatch_reply(const char *name, json_t *args);

/* Called on the core thread only. These do not touch the command queue. */
PavConfigLoad *pav_core_load(void);
int pav_core_status_data(json_t **data, PavError *err);
int pav_core_prefs_get_data(json_t **data, PavError *err);
int pav_core_prefs_set(json_t *args, json_t **data, PavError *err);
int pav_core_ui_get_data(json_t **data, PavError *err);
int pav_core_ui_set(json_t *args, json_t **data, PavError *err);
/* Writes preferences.log_max_mb (1 to 100) to the profile. Returns 0, or -1. */
int pav_core_set_log_max_mb(int mb, PavError *err);

int pav_ui_log(const char *subsystem, int level, const char *message);

/* K-Q10: from the worker, hand a verified account.test result to the core
   thread, which stores it before any later command runs. Takes ownership of
   caps. 0 queued, -1 not (core stopped, queue full). */
int pav_core_post_capabilities(const char *id, json_t *caps, const char *checked);

/* M-3a: a reduced sync completion. The core thread frees it with
   pav_sync_result_free. 0 queued, -1 not (caller frees). */
int pav_core_post_sync(void *result);

int pav_cmd_identity_list(json_t *args, json_t **data, PavError *err);
int pav_cmd_mailbox_list(json_t *args, json_t **data, PavError *err);
int pav_cmd_message_list(json_t *args, json_t **data, PavError *err);

int pav_activity_cmd_list(json_t *args, json_t **data, PavError *err);
int pav_activity_cmd_cancel(json_t *args, json_t **data, PavError *err);
int pav_activity_cmd_dismiss(json_t *args, json_t **data, PavError *err);
#ifndef NDEBUG
int pav_activity_cmd_demo(json_t *args, json_t **data, PavError *err);
#endif

/* 1 when the dispatcher runs this command off the core thread. */
int pav_command_is_worker(const char *name);

int pav_event_thread_start(void);
void pav_event_thread_stop(void);

/* H-4 test hook: clears the log.client 50-per-second window. */
void pav_log_client_rate_reset(void);
#endif

#ifndef WINDOW_STATE_H
#define WINDOW_STATE_H

#include "dispatch.h"

#include <jansson.h>
#include <time.h>

/* Core-thread only. The command handlers and the core loop share this state. */
int pav_cmd_window_initial(json_t *args, json_t **data, PavError *err);
int pav_cmd_window_report(json_t *args, json_t **data, PavError *err);

/* 1 when a debounced write is waiting. *out is the absolute CLOCK_REALTIME deadline. */
int pav_window_deadline(struct timespec *out);

/* Writes the pending rectangle when the deadline has passed. */
void pav_window_fire(void);

/* Writes the pending rectangle now. pavillion_stop uses this so a quit
   inside the debounce window is not lost. */
void pav_window_flush(void);

void pav_window_reset(void);

#endif

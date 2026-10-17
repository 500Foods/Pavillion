#ifndef LOGRING_H
#define LOGRING_H

#include <jansson.h>

#include <stdint.h>

/* Last 10,000 formatted lines. The log thread pushes the bytes it writes. */
#define PAV_LOGRING_CAP 10000

/* Copies line. The oldest line is dropped at capacity. Returns 0, or -1. */
int pav_logring_push(uint64_t seq, const char *line);

/* *out is {"lines":[{seq, ts, level, subsystem, text}, ...]}.
   Lines with seq <= since_seq are skipped. min_level is PAV_ERROR..PAV_DEBUG
   and keeps lines at that severity or worse. subsystems NULL keeps every
   subsystem; an array keeps only those names. limit is clamped to 1..CAP
   and keeps the newest matches, oldest first. Returns 0, or -1. */
int pav_logring_read(uint64_t since_seq, int limit, int min_level, const json_t *subsystems,
                     json_t **out);

/* One line object, or NULL when line is not a log line. Caller decrefs. */
json_t *pav_logring_line_json(uint64_t seq, const char *line);

void pav_logring_reset(void);

#endif

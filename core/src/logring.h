#ifndef LOGRING_H
#define LOGRING_H

#include <jansson.h>

#include <stdint.h>

/* Last 10,000 formatted lines. The log thread pushes the bytes it writes. */
#define PAV_LOGRING_CAP 10000

/* Copies line. The oldest line is dropped at capacity. Returns 0, or -1. */
int pav_logring_push(uint64_t seq, const char *line);

/* *out is {"lines":[{seq, ts, level, subsystem, text}, ...]}.
   Lines with seq <= since_seq are skipped. min_level is PAV_ERROR..PAV_TRACE
   and keeps lines at that severity or worse. subsystems NULL keeps every
   subsystem; an array keeps only those names. limit is clamped to 1..CAP
   and keeps the newest matches, oldest first. Returns 0, or -1. */
int pav_logring_read(uint64_t since_seq, int limit, int min_level, const json_t *subsystems,
                     json_t **out);

/* One line object, or NULL when line is not a log line. Caller decrefs. */
json_t *pav_logring_line_json(uint64_t seq, const char *line);

void pav_logring_reset(void);

/* Adds offset to every seq >= from_seq. log.db attach uses it so lines
   logged before attach continue the stored seq (H-4 Lock). */
void pav_logring_renumber(uint64_t from_seq, uint64_t offset);

/* Copies matching file lines, including each trailing newline, into *out.
   levels is ERROR|WARN|INFO|DEBUG|TRACE; an empty array keeps nothing.
   subsystems NULL keeps every subsystem; an empty array keeps nothing.
   text NULL or "" keeps every line. Otherwise the line is kept when the
   file line, without its trailing newline, contains text. The match is
   literal and ASCII case-insensitive. *lines is the count. A result over
   32 MB returns -2. Returns 0, -1 on allocation failure, or -2. */
int pav_logring_export(const json_t *levels, const json_t *subsystems, const char *text,
                       char **out, size_t *out_len, int *lines);

#endif

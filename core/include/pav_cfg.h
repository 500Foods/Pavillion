#ifndef PAV_CFG_H
#define PAV_CFG_H

#include <jansson.h>

#include <stddef.h>

void cfg_err(char *err, size_t err_len, const char *fmt, ...);

/* Borrowed. Does not create objects. NULL when any intermediate is missing
   or not an object, or when the last key is missing. The last value may be
   any JSON type. One segment, not a dotted path, is a single key. */
json_t *cfg_at(json_t *root, const char *path);

/* Borrowed. One key, not a dotted path. create inserts {} only when the key
   is absent. A present value that is not an object is left alone and this
   returns NULL. */
json_t *cfg_obj(json_t *obj, const char *key, int create);

/* Missing key: copy def, or fail when def is NULL.
   A present value that is not a string fails, even when def is set. */
int cfg_str(json_t *obj, const char *key, const char *def, char *out, size_t len,
            char *err, size_t err_len);

/* Missing key: use def. A present non-number fails. The result, including
   def, is clamped to [min, max]. step > 0 snaps to the nearest step from
   min. step <= 0 does not snap. */
int cfg_num_range(json_t *obj, const char *key, double def, double min, double max,
                  double step, double *out, char *err, size_t err_len);

/* Like cfg_num_range. Only a JSON integer is a number. A missing key uses def. */
int cfg_int_range(json_t *obj, const char *key, int def, int min, int max, int step,
                  int *out, char *err, size_t err_len);

/* Missing key: *out = def. A present non-boolean fails. */
int cfg_bool(json_t *obj, const char *key, int def, int *out, char *err, size_t err_len);

/* allowed is NULL-terminated. Missing key: copy def, or fail when def is NULL.
   A present string that is not in the list fails. A present non-string fails. */
int cfg_enum(json_t *obj, const char *key, const char *const *allowed, const char *def,
             char *out, size_t len, char *err, size_t err_len);

int cfg_set_str(json_t *obj, const char *key, const char *value, char *err, size_t err_len);
int cfg_set_num(json_t *obj, const char *key, double value, char *err, size_t err_len);
int cfg_set_int(json_t *obj, const char *key, int value, char *err, size_t err_len);
int cfg_set_bool(json_t *obj, const char *key, int value, char *err, size_t err_len);
int cfg_set_null(json_t *obj, const char *key, char *err, size_t err_len);

/* NaN fails and writes nothing. *written, when non-NULL, receives the clamped
   value on success. */
int cfg_set_num_range(json_t *obj, const char *key, double value, double min, double max,
                      double step, double *written, char *err, size_t err_len);
int cfg_set_int_range(json_t *obj, const char *key, int value, int min, int max, int step,
                      int *written, char *err, size_t err_len);
int cfg_set_enum(json_t *obj, const char *key, const char *value, const char *const *allowed,
                 char *err, size_t err_len);

#endif

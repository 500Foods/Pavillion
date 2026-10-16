#include "pav_cfg.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void cfg_err(char *err, size_t err_len, const char *fmt, ...) {
    va_list ap;

    if (err == NULL || err_len == 0 || fmt == NULL) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(err, err_len, fmt, ap);
    va_end(ap);
}

static const char *key_text(const char *key) {
    return key != NULL ? key : "";
}

static int copy_text(char *dst, size_t dst_len, const char *text, const char *key, char *err,
                     size_t err_len) {
    size_t n;

    if (dst == NULL || text == NULL) {
        cfg_err(err, err_len, "missing %s", key_text(key));
        return -1;
    }
    n = strlen(text);
    if (n >= dst_len) {
        cfg_err(err, err_len, "%s is too long", key_text(key));
        return -1;
    }
    memcpy(dst, text, n + 1);
    return 0;
}

static int read_number(const json_t *value, double *out) {
    if (json_is_integer(value)) {
        *out = (double)json_integer_value(value);
        return 0;
    }
    if (json_is_real(value)) {
        *out = json_real_value(value);
        return 0;
    }
    return -1;
}

static double snap_double(double value, double min, double max, double step) {
    double n;
    double nearest;

    if (value < min) {
        value = min;
    }
    if (value > max) {
        value = max;
    }
    if (!(step > 0.0) || value != value) {
        return value;
    }
    n = (value - min) / step;
    if (n >= 0.0) {
        nearest = (double)(long long)(n + 0.5);
    } else {
        nearest = (double)(long long)(n - 0.5);
    }
    value = min + nearest * step;
    if (value < min) {
        value = min;
    }
    if (value > max) {
        value = max;
    }
    return value;
}

static int snap_int(int value, int min, int max, int step) {
    long long span;
    long long steps;
    long long next;

    if (value < min) {
        value = min;
    }
    if (value > max) {
        value = max;
    }
    if (step <= 0) {
        return value;
    }
    span = (long long)value - (long long)min;
    if (span >= 0) {
        steps = (span + (long long)step / 2) / step;
    } else {
        steps = (span - (long long)step / 2) / step;
    }
    next = (long long)min + steps * (long long)step;
    if (next > max) {
        next = max;
    }
    if (next < min) {
        next = min;
    }
    return (int)next;
}

static int set_new(json_t *obj, const char *key, json_t *value, char *err, size_t err_len) {
    if (!json_is_object(obj) || key == NULL) {
        json_decref(value);
        cfg_err(err, err_len, "out of memory");
        return -1;
    }
    if (value == NULL || json_object_set_new(obj, key, value) != 0) {
        cfg_err(err, err_len, "out of memory");
        return -1;
    }
    return 0;
}

static int enum_has(const char *value, const char *const *allowed) {
    size_t i;

    if (value == NULL || allowed == NULL) {
        return 0;
    }
    for (i = 0; allowed[i] != NULL; i++) {
        if (strcmp(value, allowed[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static void enum_error(const char *key, const char *const *allowed, char *err, size_t err_len) {
    char list[256];
    size_t used = 0;
    size_t i;

    list[0] = '\0';
    if (allowed != NULL) {
        for (i = 0; allowed[i] != NULL; i++) {
            size_t n = strlen(allowed[i]);
            const char *sep = i == 0 ? "" : ", ";
            size_t sepn = strlen(sep);

            if (used + sepn + n >= sizeof(list)) {
                break;
            }
            memcpy(list + used, sep, sepn);
            used += sepn;
            memcpy(list + used, allowed[i], n);
            used += n;
            list[used] = '\0';
        }
    }
    cfg_err(err, err_len, "%s must be one of: %s", key_text(key), list);
}

json_t *cfg_at(json_t *root, const char *path) {
    json_t *cur = root;
    const char *p;

    if (cur == NULL || path == NULL || path[0] == '\0') {
        return NULL;
    }
    p = path;
    while (*p != '\0') {
        char key[128];
        size_t n = 0;
        json_t *next;

        if (!json_is_object(cur)) {
            return NULL;
        }
        while (p[n] != '\0' && p[n] != '.') {
            if (n + 1 >= sizeof(key)) {
                return NULL;
            }
            key[n] = p[n];
            n++;
        }
        if (n == 0) {
            return NULL;
        }
        key[n] = '\0';
        next = json_object_get(cur, key);
        if (next == NULL) {
            return NULL;
        }
        p += n;
        if (*p == '.') {
            p++;
            if (*p == '\0') {
                return NULL;
            }
        }
        cur = next;
    }
    return cur;
}

json_t *cfg_obj(json_t *obj, const char *key, int create) {
    json_t *cur;

    if (!json_is_object(obj) || key == NULL || key[0] == '\0') {
        return NULL;
    }
    cur = json_object_get(obj, key);
    if (json_is_object(cur)) {
        return cur;
    }
    if (cur != NULL || !create) {
        return NULL;
    }
    cur = json_object();
    if (cur == NULL) {
        return NULL;
    }
    if (json_object_set_new(obj, key, cur) != 0) {
        return NULL;
    }
    return cur;
}

int cfg_str(json_t *obj, const char *key, const char *def, char *out, size_t len, char *err,
            size_t err_len) {
    json_t *value;

    if (key == NULL) {
        cfg_err(err, err_len, "missing");
        return -1;
    }
    if (!json_is_object(obj)) {
        cfg_err(err, err_len, "missing %s", key);
        return -1;
    }
    value = json_object_get(obj, key);
    if (value == NULL) {
        if (def == NULL) {
            cfg_err(err, err_len, "missing %s", key);
            return -1;
        }
        return copy_text(out, len, def, key, err, err_len);
    }
    if (!json_is_string(value)) {
        cfg_err(err, err_len, "missing %s", key);
        return -1;
    }
    return copy_text(out, len, json_string_value(value), key, err, err_len);
}

int cfg_num_range(json_t *obj, const char *key, double def, double min, double max, double step,
                  double *out, char *err, size_t err_len) {
    json_t *value;
    double parsed;

    if (out == NULL || key == NULL) {
        cfg_err(err, err_len, "missing %s", key_text(key));
        return -1;
    }
    if (!json_is_object(obj)) {
        cfg_err(err, err_len, "missing %s", key);
        return -1;
    }
    value = json_object_get(obj, key);
    if (value == NULL) {
        *out = snap_double(def, min, max, step);
        return 0;
    }
    if (read_number(value, &parsed) != 0) {
        cfg_err(err, err_len, "missing %s", key);
        return -1;
    }
    *out = snap_double(parsed, min, max, step);
    return 0;
}

int cfg_int_range(json_t *obj, const char *key, int def, int min, int max, int step, int *out,
                  char *err, size_t err_len) {
    json_t *value;

    if (out == NULL || key == NULL) {
        cfg_err(err, err_len, "missing %s", key_text(key));
        return -1;
    }
    if (!json_is_object(obj)) {
        cfg_err(err, err_len, "missing %s", key);
        return -1;
    }
    value = json_object_get(obj, key);
    if (value == NULL) {
        *out = snap_int(def, min, max, step);
        return 0;
    }
    if (!json_is_integer(value)) {
        cfg_err(err, err_len, "missing %s", key);
        return -1;
    }
    *out = snap_int((int)json_integer_value(value), min, max, step);
    return 0;
}

int cfg_bool(json_t *obj, const char *key, int def, int *out, char *err, size_t err_len) {
    json_t *value;

    if (out == NULL || key == NULL) {
        cfg_err(err, err_len, "missing %s", key_text(key));
        return -1;
    }
    if (!json_is_object(obj)) {
        cfg_err(err, err_len, "missing %s", key);
        return -1;
    }
    value = json_object_get(obj, key);
    if (value == NULL) {
        *out = def;
        return 0;
    }
    if (!json_is_boolean(value)) {
        cfg_err(err, err_len, "missing %s", key);
        return -1;
    }
    *out = json_is_true(value) ? 1 : 0;
    return 0;
}

int cfg_enum(json_t *obj, const char *key, const char *const *allowed, const char *def, char *out,
             size_t len, char *err, size_t err_len) {
    json_t *value;
    const char *text;

    if (key == NULL) {
        cfg_err(err, err_len, "missing");
        return -1;
    }
    if (!json_is_object(obj)) {
        cfg_err(err, err_len, "missing %s", key);
        return -1;
    }
    value = json_object_get(obj, key);
    if (value == NULL) {
        if (def == NULL) {
            cfg_err(err, err_len, "missing %s", key);
            return -1;
        }
        return copy_text(out, len, def, key, err, err_len);
    }
    if (!json_is_string(value)) {
        cfg_err(err, err_len, "missing %s", key);
        return -1;
    }
    text = json_string_value(value);
    if (!enum_has(text, allowed)) {
        enum_error(key, allowed, err, err_len);
        return -1;
    }
    return copy_text(out, len, text, key, err, err_len);
}

int cfg_set_str(json_t *obj, const char *key, const char *value, char *err, size_t err_len) {
    if (value == NULL) {
        cfg_err(err, err_len, "missing %s", key_text(key));
        return -1;
    }
    return set_new(obj, key, json_string(value), err, err_len);
}

int cfg_set_num(json_t *obj, const char *key, double value, char *err, size_t err_len) {
    if (value != value) {
        cfg_err(err, err_len, "missing %s", key_text(key));
        return -1;
    }
    return set_new(obj, key, json_real(value), err, err_len);
}

int cfg_set_int(json_t *obj, const char *key, int value, char *err, size_t err_len) {
    return set_new(obj, key, json_integer(value), err, err_len);
}

int cfg_set_bool(json_t *obj, const char *key, int value, char *err, size_t err_len) {
    return set_new(obj, key, json_boolean(value ? 1 : 0), err, err_len);
}

int cfg_set_null(json_t *obj, const char *key, char *err, size_t err_len) {
    return set_new(obj, key, json_null(), err, err_len);
}

int cfg_set_num_range(json_t *obj, const char *key, double value, double min, double max,
                      double step, double *written, char *err, size_t err_len) {
    double snapped;

    if (value != value) {
        cfg_err(err, err_len, "missing %s", key_text(key));
        return -1;
    }
    snapped = snap_double(value, min, max, step);
    if (cfg_set_num(obj, key, snapped, err, err_len) != 0) {
        return -1;
    }
    if (written != NULL) {
        *written = snapped;
    }
    return 0;
}

int cfg_set_int_range(json_t *obj, const char *key, int value, int min, int max, int step,
                      int *written, char *err, size_t err_len) {
    int snapped = snap_int(value, min, max, step);

    if (cfg_set_int(obj, key, snapped, err, err_len) != 0) {
        return -1;
    }
    if (written != NULL) {
        *written = snapped;
    }
    return 0;
}

int cfg_set_enum(json_t *obj, const char *key, const char *value, const char *const *allowed,
                 char *err, size_t err_len) {
    if (!enum_has(value, allowed)) {
        enum_error(key, allowed, err, err_len);
        return -1;
    }
    return cfg_set_str(obj, key, value, err, err_len);
}

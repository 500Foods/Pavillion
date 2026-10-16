#define _POSIX_C_SOURCE 200809L

#include "window_state.h"

#include "pavillion.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WINDOW_DEBOUNCE_NS (500L * 1000L * 1000L)
#define MONITOR_MAX 16

typedef struct Monitor {
    int x;
    int y;
    int w;
    int h;
} Monitor;

static int g_armed;
static int g_pending;
static int g_have_rect;
static int g_maximized;
static int g_w;
static int g_h;
static int g_x_set;
static int g_y_set;
static int g_x;
static int g_y;
static struct timespec g_deadline;

static int take_int(json_t *value, int *out) {
    json_int_t n;

    if (!json_is_integer(value)) {
        return -1;
    }
    n = json_integer_value(value);
    if (n < -32000 || n > 32000) {
        return -1;
    }
    *out = (int)n;
    return 0;
}

static int clamp_range(int value, int lo, int hi) {
    if (value < lo) {
        return lo;
    }
    if (value > hi) {
        return hi;
    }
    return value;
}

static int pick_monitor(const Monitor *monitors, int count, int x, int y, int w, int h) {
    int cx = x + w / 2;
    int cy = y + h / 2;
    int i;
    int best = 0;
    long best_dist = 0;

    for (i = 0; i < count; i++) {
        if (cx >= monitors[i].x && cx < monitors[i].x + monitors[i].w && cy >= monitors[i].y &&
            cy < monitors[i].y + monitors[i].h) {
            return i;
        }
    }
    for (i = 0; i < count; i++) {
        long dx = (long)cx - (monitors[i].x + monitors[i].w / 2);
        long dy = (long)cy - (monitors[i].y + monitors[i].h / 2);
        long dist = dx * dx + dy * dy;

        if (i == 0 || dist < best_dist) {
            best = i;
            best_dist = dist;
        }
    }
    return best;
}

static void clamp_to_monitors(int *x, int *y, int *w, int *h, const Monitor *monitors, int count) {
    int mw;
    int mh;
    const Monitor *monitor;

    *w = clamp_range(*w, 200, 10000);
    *h = clamp_range(*h, 200, 10000);
    if (count <= 0) {
        *x = clamp_range(*x, -32000, 32000);
        *y = clamp_range(*y, -32000, 32000);
        return;
    }
    monitor = &monitors[pick_monitor(monitors, count, *x, *y, *w, *h)];
    mw = monitor->w > 0 ? monitor->w : 200;
    mh = monitor->h > 0 ? monitor->h : 200;
    if (*w > mw) {
        *w = mw;
    }
    if (*h > mh) {
        *h = mh;
    }
    if (*x < monitor->x) {
        *x = monitor->x;
    }
    if (*y < monitor->y) {
        *y = monitor->y;
    }
    if (*x + *w > monitor->x + monitor->w) {
        *x = monitor->x + monitor->w - *w;
    }
    if (*y + *h > monitor->y + monitor->h) {
        *y = monitor->y + monitor->h - *h;
    }
}

static int read_monitors(json_t *value, Monitor *out, int *count, PavError *err) {
    size_t i;
    size_t n;

    *count = 0;
    if (value == NULL) {
        return 0;
    }
    if (!json_is_array(value)) {
        pav_error_set(err, "bad_args", "monitors must be an array", 0);
        return -1;
    }
    n = json_array_size(value);
    if (n > MONITOR_MAX) {
        pav_error_set(err, "bad_args", "too many monitors", 0);
        return -1;
    }
    for (i = 0; i < n; i++) {
        json_t *item = json_array_get(value, i);
        Monitor monitor;

        if (!json_is_object(item) || take_int(json_object_get(item, "x"), &monitor.x) != 0 ||
            take_int(json_object_get(item, "y"), &monitor.y) != 0 ||
            take_int(json_object_get(item, "w"), &monitor.w) != 0 ||
            take_int(json_object_get(item, "h"), &monitor.h) != 0 || monitor.w <= 0 || monitor.h <= 0) {
            pav_error_set(err, "bad_args", "monitor must be {x,y,w,h}", 0);
            return -1;
        }
        out[i] = monitor;
    }
    *count = (int)n;
    return 0;
}

static int reject_unknown(json_t *args, const char *allowed[], PavError *err) {
    const char *key;
    void *value;
    char buf[128];

    json_object_foreach(args, key, value) {
        int i;
        int known = 0;

        (void)value;
        for (i = 0; allowed[i] != NULL; i++) {
            if (strcmp(key, allowed[i]) == 0) {
                known = 1;
                break;
            }
        }
        if (!known) {
            snprintf(buf, sizeof(buf), "unknown argument %s", key);
            pav_error_set(err, "bad_args", buf, 0);
            return -1;
        }
    }
    return 0;
}

static void arm_deadline(void) {
    clock_gettime(CLOCK_REALTIME, &g_deadline);
    g_deadline.tv_nsec += WINDOW_DEBOUNCE_NS;
    if (g_deadline.tv_nsec >= 1000000000L) {
        g_deadline.tv_sec += 1;
        g_deadline.tv_nsec -= 1000000000L;
    }
    g_armed = 1;
    g_pending = 1;
}

static void remember_saved(void) {
    json_t *data = NULL;
    PavError err;
    json_t *width;
    json_t *height;
    json_t *x;
    json_t *y;
    json_t *maximized;

    memset(&err, 0, sizeof(err));
    if (pav_core_prefs_get_data(&data, &err) != 0 || !json_is_object(data)) {
        json_decref(data);
        g_w = 1200;
        g_h = 800;
        g_x_set = 0;
        g_y_set = 0;
        g_maximized = 0;
        g_have_rect = 1;
        return;
    }
    width = json_object_get(data, "windowWidth");
    height = json_object_get(data, "windowHeight");
    x = json_object_get(data, "windowX");
    y = json_object_get(data, "windowY");
    maximized = json_object_get(data, "windowMaximized");
    g_w = json_is_integer(width) ? (int)json_integer_value(width) : 1200;
    g_h = json_is_integer(height) ? (int)json_integer_value(height) : 800;
    g_x_set = json_is_integer(x);
    g_y_set = json_is_integer(y);
    g_x = g_x_set ? (int)json_integer_value(x) : 0;
    g_y = g_y_set ? (int)json_integer_value(y) : 0;
    g_maximized = json_is_true(maximized);
    g_have_rect = 1;
    json_decref(data);
}

static int write_pending(void) {
    json_t *args;
    json_t *data = NULL;
    PavError err;

    if (!g_pending || !g_have_rect) {
        g_armed = 0;
        g_pending = 0;
        return 0;
    }
    args = json_object();
    if (args == NULL) {
        return -1;
    }
    memset(&err, 0, sizeof(err));
    if (json_object_set_new(args, "windowMaximized", json_boolean(g_maximized)) != 0 ||
        json_object_set_new(args, "windowWidth", json_integer(g_w)) != 0 ||
        json_object_set_new(args, "windowHeight", json_integer(g_h)) != 0 ||
        json_object_set_new(args, "windowX", g_x_set ? json_integer(g_x) : json_null()) != 0 ||
        json_object_set_new(args, "windowY", g_y_set ? json_integer(g_y) : json_null()) != 0) {
        json_decref(args);
        return -1;
    }
    if (pav_core_prefs_set(args, &data, &err) != 0) {
        json_decref(args);
        json_decref(data);
        return -1;
    }
    json_decref(args);
    json_decref(data);
    g_armed = 0;
    g_pending = 0;
    return 0;
}

void pav_window_reset(void) {
    memset(&g_deadline, 0, sizeof(g_deadline));
    g_armed = 0;
    g_pending = 0;
    g_have_rect = 0;
    g_maximized = 0;
    g_w = 0;
    g_h = 0;
    g_x_set = 0;
    g_y_set = 0;
    g_x = 0;
    g_y = 0;
}

int pav_window_deadline(struct timespec *out) {
    if (!g_armed || out == NULL) {
        return 0;
    }
    *out = g_deadline;
    return 1;
}

void pav_window_fire(void) {
    struct timespec now;

    if (!g_armed) {
        return;
    }
    clock_gettime(CLOCK_REALTIME, &now);
    if (now.tv_sec < g_deadline.tv_sec ||
        (now.tv_sec == g_deadline.tv_sec && now.tv_nsec < g_deadline.tv_nsec)) {
        return;
    }
    pav_window_flush();
}

void pav_window_flush(void) {
    write_pending();
}

int pav_cmd_window_initial(json_t *args, json_t **data, PavError *err) {
    static const char *allowed[] = {"monitors", NULL};
    json_t *saved = NULL;
    Monitor monitors[MONITOR_MAX];
    int count = 0;
    int x = 0;
    int y = 0;
    int w;
    int h;
    int x_set;
    int y_set;
    int maximized;
    json_t *obj;

    if (reject_unknown(args, allowed, err) != 0 ||
        read_monitors(json_object_get(args, "monitors"), monitors, &count, err) != 0) {
        return -1;
    }
    if (pav_core_prefs_get_data(&saved, err) != 0) {
        return -1;
    }
    w = json_is_integer(json_object_get(saved, "windowWidth"))
            ? (int)json_integer_value(json_object_get(saved, "windowWidth"))
            : 1200;
    h = json_is_integer(json_object_get(saved, "windowHeight"))
            ? (int)json_integer_value(json_object_get(saved, "windowHeight"))
            : 800;
    x_set = json_is_integer(json_object_get(saved, "windowX"));
    y_set = json_is_integer(json_object_get(saved, "windowY"));
    if (x_set) {
        x = (int)json_integer_value(json_object_get(saved, "windowX"));
    }
    if (y_set) {
        y = (int)json_integer_value(json_object_get(saved, "windowY"));
    }
    maximized = json_is_true(json_object_get(saved, "windowMaximized"));
    json_decref(saved);
    if (count > 0) {
        int cx = x_set ? x : 0;
        int cy = y_set ? y : 0;

        clamp_to_monitors(&cx, &cy, &w, &h, monitors, count);
        if (x_set) {
            x = cx;
        }
        if (y_set) {
            y = cy;
        }
    }
    obj = json_object();
    if (obj == NULL || json_object_set_new(obj, "x", x_set ? json_integer(x) : json_null()) != 0 ||
        json_object_set_new(obj, "y", y_set ? json_integer(y) : json_null()) != 0 ||
        json_object_set_new(obj, "w", json_integer(w)) != 0 ||
        json_object_set_new(obj, "h", json_integer(h)) != 0 ||
        json_object_set_new(obj, "maximized", json_boolean(maximized)) != 0) {
        json_decref(obj);
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    *data = obj;
    return 0;
}

int pav_cmd_window_report(json_t *args, json_t **data, PavError *err) {
    static const char *allowed[] = {"x", "y", "w", "h", "maximized", "monitors", NULL};
    Monitor monitors[MONITOR_MAX];
    int count = 0;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    int maximized;
    json_t *flag;

    if (reject_unknown(args, allowed, err) != 0) {
        return -1;
    }
    if (take_int(json_object_get(args, "w"), &w) != 0 || take_int(json_object_get(args, "h"), &h) != 0) {
        pav_error_set(err, "bad_args", "w and h must be integers", 0);
        return -1;
    }
    flag = json_object_get(args, "maximized");
    if (!json_is_boolean(flag)) {
        pav_error_set(err, "bad_args", "maximized must be a boolean", 0);
        return -1;
    }
    maximized = json_is_true(flag);
    if (json_object_get(args, "x") != NULL && take_int(json_object_get(args, "x"), &x) != 0) {
        pav_error_set(err, "bad_args", "x must be an integer", 0);
        return -1;
    }
    if (json_object_get(args, "y") != NULL && take_int(json_object_get(args, "y"), &y) != 0) {
        pav_error_set(err, "bad_args", "y must be an integer", 0);
        return -1;
    }
    if (read_monitors(json_object_get(args, "monitors"), monitors, &count, err) != 0) {
        return -1;
    }
    clamp_to_monitors(&x, &y, &w, &h, monitors, count);
    if (maximized) {
        if (!g_have_rect) {
            remember_saved();
        }
        g_maximized = 1;
    } else {
        g_w = w;
        g_h = h;
        g_x = x;
        g_y = y;
        g_x_set = json_object_get(args, "x") != NULL;
        g_y_set = json_object_get(args, "y") != NULL;
        g_maximized = 0;
        g_have_rect = 1;
    }
    arm_deadline();
    *data = json_object();
    if (*data == NULL) {
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    return 0;
}

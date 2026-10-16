#include "pav_config.h"
#include "pav_cfg.h"

#include <jansson.h>
#include <math.h>
#include <string.h>

static json_t *make_nav(void) {
    json_t *obj = json_object();

    if (obj == NULL) {
        return NULL;
    }
    if (cfg_set_int(obj, "width", 320, NULL, 0) != 0 || cfg_set_int(obj, "min", 200, NULL, 0) != 0 ||
        cfg_set_int(obj, "max", 500, NULL, 0) != 0) {
        json_decref(obj);
        return NULL;
    }
    return obj;
}

static json_t *make_split(void) {
    json_t *obj = json_object();

    if (obj == NULL) {
        return NULL;
    }
    if (cfg_set_num(obj, "split", 0.5, NULL, 0) != 0 || cfg_set_num(obj, "min", 0.25, NULL, 0) != 0 ||
        cfg_set_num(obj, "max", 0.75, NULL, 0) != 0) {
        json_decref(obj);
        return NULL;
    }
    return obj;
}

static int ensure_num(json_t *obj, const char *key, double fallback, int as_int, int *added, char *err,
                      size_t err_len) {
    double ignored;
    int rc;

    if (json_object_get(obj, key) == NULL) {
        rc = as_int ? cfg_set_int(obj, key, (int)fallback, err, err_len)
                    : cfg_set_num(obj, key, fallback, err, err_len);
        if (rc == 0) {
            *added = 1;
        }
        return rc;
    }
    return cfg_num_range(obj, key, fallback, -INFINITY, INFINITY, 0, &ignored, err, err_len);
}

static int ensure_pane(json_t *ui, const char *name, int pixels, int *added, char *err, size_t err_len) {
    json_t *pane;
    int missing = json_object_get(ui, name) == NULL;

    if (missing) {
        pane = pixels ? make_nav() : make_split();
        if (pane == NULL || json_object_set_new(ui, name, pane) != 0) {
            cfg_err(err, err_len, "out of memory");
            return -1;
        }
        *added = 1;
        return 0;
    }
    pane = cfg_obj(ui, name, 0);
    if (pane == NULL) {
        cfg_err(err, err_len, "preferences.ui is not an object");
        return -1;
    }
    if (pixels) {
        if (ensure_num(pane, "width", 320, 1, added, err, err_len) != 0 ||
            ensure_num(pane, "min", 200, 1, added, err, err_len) != 0 ||
            ensure_num(pane, "max", 500, 1, added, err, err_len) != 0) {
            return -1;
        }
        return 0;
    }
    if (ensure_num(pane, "split", 0.5, 0, added, err, err_len) != 0 ||
        ensure_num(pane, "min", 0.25, 0, added, err, err_len) != 0 ||
        ensure_num(pane, "max", 0.75, 0, added, err, err_len) != 0) {
        return -1;
    }
    return 0;
}

int pav_profile_ensure_ui(PavProfile *profile, int *added, char *err, size_t err_len) {
    json_t *prefs;
    json_t *ui;
    int missing;

    if (added != NULL) {
        *added = 0;
    }
    if (profile == NULL || profile->doc == NULL || added == NULL) {
        cfg_err(err, err_len, "missing profile");
        return -1;
    }
    prefs = cfg_obj(profile->doc, "preferences", 0);
    if (prefs == NULL) {
        cfg_err(err, err_len, "missing profile fields");
        return -1;
    }
    missing = json_object_get(prefs, "ui") == NULL;
    ui = cfg_obj(prefs, "ui", 1);
    if (ui == NULL) {
        cfg_err(err, err_len, missing ? "out of memory" : "preferences.ui is not an object");
        return -1;
    }
    if (missing) {
        *added = 1;
    }
    if (ensure_pane(ui, "nav", 1, added, err, err_len) != 0 ||
        ensure_pane(ui, "mail", 0, added, err, err_len) != 0 ||
        ensure_pane(ui, "contacts", 0, added, err, err_len) != 0 ||
        ensure_pane(ui, "files", 0, added, err, err_len) != 0) {
        return -1;
    }
    return 0;
}

int pav_profile_ensure_zoom(PavProfile *profile, int *added, char *err, size_t err_len) {
    json_t *prefs;
    json_t *zoom;
    int missing;

    if (added != NULL) {
        *added = 0;
    }
    if (profile == NULL || profile->doc == NULL || added == NULL) {
        cfg_err(err, err_len, "missing profile");
        return -1;
    }
    prefs = cfg_obj(profile->doc, "preferences", 0);
    if (prefs == NULL) {
        cfg_err(err, err_len, "missing profile fields");
        return -1;
    }
    missing = json_object_get(prefs, "zoom") == NULL;
    zoom = cfg_obj(prefs, "zoom", 1);
    if (zoom == NULL) {
        cfg_err(err, err_len, missing ? "out of memory" : "preferences.zoom is not an object");
        return -1;
    }
    if (missing) {
        *added = 1;
    }
    if (ensure_num(zoom, "shell", 1, 0, added, err, err_len) != 0 ||
        ensure_num(zoom, "navButtons", 1, 0, added, err, err_len) != 0 ||
        ensure_num(zoom, "navBody", 1, 0, added, err, err_len) != 0 ||
        ensure_num(zoom, "mailList", 1, 0, added, err, err_len) != 0 ||
        ensure_num(zoom, "mailDetail", 1, 0, added, err, err_len) != 0) {
        return -1;
    }
    return 0;
}

static int prefs_objects(const PavProfile *profile, json_t **prefs, json_t **zoom, json_t **window,
                         json_t **ui, char *err, size_t err_len) {
    if (profile == NULL || profile->doc == NULL) {
        cfg_err(err, err_len, "missing profile");
        return -1;
    }
    *prefs = cfg_obj(profile->doc, "preferences", 0);
    if (*prefs == NULL) {
        cfg_err(err, err_len, "missing profile fields");
        return -1;
    }
    if (zoom != NULL) {
        *zoom = cfg_obj(*prefs, "zoom", 0);
        if (*zoom == NULL) {
            cfg_err(err, err_len, "missing profile fields");
            return -1;
        }
    }
    if (window != NULL) {
        *window = cfg_obj(*prefs, "window", 0);
        if (*window == NULL) {
            cfg_err(err, err_len, "missing profile fields");
            return -1;
        }
    }
    if (ui != NULL) {
        *ui = cfg_obj(*prefs, "ui", 0);
        if (*ui == NULL) {
            cfg_err(err, err_len, "missing profile fields");
            return -1;
        }
    }
    return 0;
}

static int read_limits(json_t *pane, double *lo, double *hi, int pixels, char *err, size_t err_len) {
    if (json_object_get(pane, "min") == NULL || json_object_get(pane, "max") == NULL ||
        cfg_num_range(pane, "min", 0, -INFINITY, INFINITY, 0, lo, NULL, 0) != 0 ||
        cfg_num_range(pane, "max", 0, -INFINITY, INFINITY, 0, hi, NULL, 0) != 0) {
        cfg_err(err, err_len, "missing limits");
        return -1;
    }
    if (*lo != *lo || *hi != *hi || *lo > *hi) {
        cfg_err(err, err_len, "invalid limits");
        return -1;
    }
    if (pixels) {
        if (*lo < 0.0 || *hi > 10000.0) {
            cfg_err(err, err_len, "invalid limits");
            return -1;
        }
        return 0;
    }
    if (*lo < 0.0 || *hi > 1.0) {
        cfg_err(err, err_len, "invalid limits");
        return -1;
    }
    return 0;
}

static int whole_int(double value, int *out) {
    if (value < 0.0 || value > 1000000.0) {
        return -1;
    }
    *out = (int)(value + 0.5);
    return 0;
}

static int read_px(json_t *ui, const char *name, int *width, int *min, int *max, char *err,
                   size_t err_len) {
    json_t *pane = cfg_obj(ui, name, 0);
    double lo;
    double hi;
    double value;
    int lo_i;
    int hi_i;
    int value_i;

    if (pane == NULL || read_limits(pane, &lo, &hi, 1, err, err_len) != 0 ||
        json_object_get(pane, "width") == NULL ||
        cfg_num_range(pane, "width", 0, -INFINITY, INFINITY, 0, &value, NULL, 0) != 0 ||
        whole_int(lo, &lo_i) != 0 || whole_int(hi, &hi_i) != 0 || whole_int(value, &value_i) != 0) {
        if (err != NULL && err_len > 0 && err[0] == '\0') {
            cfg_err(err, err_len, "missing %s", name);
        }
        return -1;
    }
    *min = lo_i;
    *max = hi_i;
    if (value_i < lo_i) {
        value_i = lo_i;
    }
    if (value_i > hi_i) {
        value_i = hi_i;
    }
    *width = value_i;
    return 0;
}

static int read_split(json_t *ui, const char *name, double *split, double *min, double *max, char *err,
                      size_t err_len) {
    json_t *pane = cfg_obj(ui, name, 0);
    double value;

    if (pane == NULL || read_limits(pane, min, max, 0, err, err_len) != 0 ||
        json_object_get(pane, "split") == NULL ||
        cfg_num_range(pane, "split", 0, -INFINITY, INFINITY, 0, &value, NULL, 0) != 0) {
        if (err != NULL && err_len > 0 && err[0] == '\0') {
            cfg_err(err, err_len, "missing %s", name);
        }
        return -1;
    }
    if (value < *min) {
        value = *min;
    }
    if (value > *max) {
        value = *max;
    }
    *split = value;
    return 0;
}

int pav_profile_read_shell(const PavProfile *profile, PavShellPrefs *out, char *err, size_t err_len) {
    json_t *prefs;
    json_t *zoom;
    json_t *window;
    json_t *ui;

    if (out == NULL) {
        cfg_err(err, err_len, "missing prefs");
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (prefs_objects(profile, &prefs, &zoom, &window, &ui, err, err_len) != 0) {
        return -1;
    }
    out->zoom_shell = profile->zoom_shell;
    if (out->zoom_shell < 0.05) {
        out->zoom_shell = 0.05;
    }
    if (out->zoom_shell > 5.0) {
        out->zoom_shell = 5.0;
    }
    if (cfg_num_range(zoom, "navButtons", 1.0, 0.05, 5.0, 0, &out->zoom_nav_buttons, err, err_len) != 0 ||
        cfg_num_range(zoom, "navBody", 1.0, 0.05, 5.0, 0, &out->zoom_nav_body, err, err_len) != 0 ||
        cfg_num_range(zoom, "mailList", 1.0, 0.05, 5.0, 0, &out->zoom_mail_list, err, err_len) != 0 ||
        cfg_num_range(zoom, "mailDetail", 1.0, 0.05, 5.0, 0, &out->zoom_mail_detail, err, err_len) != 0) {
        return -1;
    }
    out->window_maximized = profile->window_maximized ? 1 : 0;
    out->window_width = profile->window_width;
    out->window_height = profile->window_height;
    out->window_x_set = profile->window_x_set;
    out->window_y_set = profile->window_y_set;
    out->window_x = profile->window_x;
    out->window_y = profile->window_y;
    memcpy(out->chrome, profile->chrome, sizeof(out->chrome));
    if (read_px(ui, "nav", &out->nav_width, &out->nav_min, &out->nav_max, err, err_len) != 0 ||
        read_split(ui, "mail", &out->mail_split, &out->mail_min, &out->mail_max, err, err_len) != 0 ||
        read_split(ui, "contacts", &out->contacts_split, &out->contacts_min, &out->contacts_max, err,
                   err_len) != 0 ||
        read_split(ui, "files", &out->files_split, &out->files_min, &out->files_max, err, err_len) != 0) {
        return -1;
    }
    return 0;
}

static int write_px(json_t *ui, const char *name, int width, char *err, size_t err_len) {
    json_t *pane = cfg_obj(ui, name, 0);
    double lo;
    double hi;
    int lo_i;
    int hi_i;
    int clamped;

    if (pane == NULL || read_limits(pane, &lo, &hi, 1, err, err_len) != 0 || whole_int(lo, &lo_i) != 0 ||
        whole_int(hi, &hi_i) != 0) {
        if (err != NULL && err_len > 0 && err[0] == '\0') {
            cfg_err(err, err_len, "missing %s", name);
        }
        return -1;
    }
    clamped = width;
    if (clamped < lo_i) {
        clamped = lo_i;
    }
    if (clamped > hi_i) {
        clamped = hi_i;
    }
    return cfg_set_int(pane, "width", clamped, err, err_len);
}

static int write_split(json_t *ui, const char *name, double split, char *err, size_t err_len) {
    json_t *pane = cfg_obj(ui, name, 0);
    double lo;
    double hi;

    if (pane == NULL || read_limits(pane, &lo, &hi, 0, err, err_len) != 0) {
        if (err != NULL && err_len > 0 && err[0] == '\0') {
            cfg_err(err, err_len, "missing %s", name);
        }
        return -1;
    }
    if (split != split) {
        cfg_err(err, err_len, "missing %s", name);
        return -1;
    }
    if (split < lo) {
        split = lo;
    }
    if (split > hi) {
        split = hi;
    }
    return cfg_set_num(pane, "split", split, err, err_len);
}

int pav_profile_write_shell(PavProfile *profile, const PavShellPrefs *in, unsigned mask, char *err,
                            size_t err_len) {
    json_t *prefs;
    json_t *zoom;
    json_t *window;
    json_t *ui;
    double zoom_shell;
    static const char *const k_chrome[] = {"system", "hidden", NULL};

    if (in == NULL) {
        cfg_err(err, err_len, "missing prefs");
        return -1;
    }
    if ((mask & (PAV_PREFS_ZOOM | PAV_PREFS_WINDOW | PAV_PREFS_UI)) == 0) {
        cfg_err(err, err_len, "nothing to save");
        return -1;
    }
    if (prefs_objects(profile, &prefs, &zoom, &window, &ui, err, err_len) != 0) {
        return -1;
    }
    if ((mask & PAV_PREFS_ZOOM) != 0) {
        if (cfg_set_num_range(zoom, "shell", in->zoom_shell, 0.05, 5.0, 0, &zoom_shell, err, err_len) != 0) {
            return -1;
        }
        profile->zoom_shell = zoom_shell;
        if (cfg_set_num_range(zoom, "navButtons", in->zoom_nav_buttons, 0.05, 5.0, 0, NULL, err, err_len) !=
                0 ||
            cfg_set_num_range(zoom, "navBody", in->zoom_nav_body, 0.05, 5.0, 0, NULL, err, err_len) != 0 ||
            cfg_set_num_range(zoom, "mailList", in->zoom_mail_list, 0.05, 5.0, 0, NULL, err, err_len) != 0 ||
            cfg_set_num_range(zoom, "mailDetail", in->zoom_mail_detail, 0.05, 5.0, 0, NULL, err, err_len) !=
                0) {
            return -1;
        }
    }
    if ((mask & PAV_PREFS_WINDOW) != 0) {
        if (strcmp(in->chrome, "system") != 0 && strcmp(in->chrome, "hidden") != 0) {
            cfg_err(err, err_len, "chrome must be system or hidden");
            return -1;
        }
        if (in->window_width < 200 || in->window_width > 10000 || in->window_height < 200 ||
            in->window_height > 10000) {
            cfg_err(err, err_len, "window size is out of range");
            return -1;
        }
        if ((in->window_x_set && (in->window_x < -32000 || in->window_x > 32000)) ||
            (in->window_y_set && (in->window_y < -32000 || in->window_y > 32000))) {
            cfg_err(err, err_len, "window position is out of range");
            return -1;
        }
        if (cfg_set_bool(window, "maximized", in->window_maximized, err, err_len) != 0 ||
            cfg_set_int(window, "width", in->window_width, err, err_len) != 0 ||
            cfg_set_int(window, "height", in->window_height, err, err_len) != 0 ||
            (in->window_x_set ? cfg_set_int(window, "x", in->window_x, err, err_len)
                              : cfg_set_null(window, "x", err, err_len)) != 0 ||
            (in->window_y_set ? cfg_set_int(window, "y", in->window_y, err, err_len)
                              : cfg_set_null(window, "y", err, err_len)) != 0 ||
            cfg_set_enum(window, "chrome", in->chrome, k_chrome, err, err_len) != 0) {
            return -1;
        }
        profile->window_maximized = in->window_maximized ? 1 : 0;
        profile->window_width = in->window_width;
        profile->window_height = in->window_height;
        profile->window_x_set = in->window_x_set ? 1 : 0;
        profile->window_y_set = in->window_y_set ? 1 : 0;
        profile->window_x = in->window_x_set ? in->window_x : 0;
        profile->window_y = in->window_y_set ? in->window_y : 0;
        memset(profile->chrome, 0, sizeof(profile->chrome));
        memcpy(profile->chrome, in->chrome, strlen(in->chrome));
    }
    if ((mask & PAV_PREFS_UI) != 0) {
        if (write_px(ui, "nav", in->nav_width, err, err_len) != 0 ||
            write_split(ui, "mail", in->mail_split, err, err_len) != 0 ||
            write_split(ui, "contacts", in->contacts_split, err, err_len) != 0 ||
            write_split(ui, "files", in->files_split, err, err_len) != 0) {
            return -1;
        }
    }
    return 0;
}

int pav_profile_read_icons(const PavProfile *profile, char **out, char *err, size_t err_len) {
    json_t *prefs;
    json_t *ui;
    json_t *icons;
    char *text;
    int missing_ui;
    int missing_icons;

    if (out == NULL) {
        cfg_err(err, err_len, "missing out");
        return -1;
    }
    *out = NULL;
    if (profile == NULL || profile->doc == NULL) {
        cfg_err(err, err_len, "missing profile");
        return -1;
    }
    prefs = cfg_obj(profile->doc, "preferences", 0);
    if (prefs == NULL) {
        cfg_err(err, err_len, "missing profile fields");
        return -1;
    }
    missing_ui = json_object_get(prefs, "ui") == NULL;
    ui = cfg_obj(prefs, "ui", 1);
    if (ui == NULL) {
        cfg_err(err, err_len, missing_ui ? "out of memory" : "preferences.ui is not an object");
        return -1;
    }
    missing_icons = json_object_get(ui, "icons") == NULL;
    icons = cfg_obj(ui, "icons", 1);
    if (icons == NULL) {
        cfg_err(err, err_len, missing_icons ? "out of memory" : "preferences.ui.icons is not an object");
        return -1;
    }
    text = json_dumps(icons, JSON_PRESERVE_ORDER);
    if (text == NULL) {
        cfg_err(err, err_len, "cannot serialize icons");
        return -1;
    }
    *out = text;
    return 0;
}

int pav_profile_write_icons(PavProfile *profile, const char *json, char *err, size_t err_len) {
    json_error_t jerr;
    json_t *root;
    json_t *prefs;
    json_t *ui;
    int missing_ui;

    if (profile == NULL || profile->doc == NULL || json == NULL) {
        cfg_err(err, err_len, "missing arguments");
        return -1;
    }
    root = json_loads(json, JSON_PRESERVE_ORDER, &jerr);
    if (root == NULL) {
        cfg_err(err, err_len, "invalid icons json");
        return -1;
    }
    if (!json_is_object(root)) {
        json_decref(root);
        cfg_err(err, err_len, "icons must be a json object");
        return -1;
    }
    prefs = cfg_obj(profile->doc, "preferences", 0);
    if (prefs == NULL) {
        json_decref(root);
        cfg_err(err, err_len, "missing profile fields");
        return -1;
    }
    missing_ui = json_object_get(prefs, "ui") == NULL;
    ui = cfg_obj(prefs, "ui", 1);
    if (ui == NULL) {
        json_decref(root);
        cfg_err(err, err_len, missing_ui ? "out of memory" : "preferences.ui is not an object");
        return -1;
    }
    if (json_object_set_new(ui, "icons", root) != 0) {
        cfg_err(err, err_len, "out of memory");
        return -1;
    }
    return 0;
}

int pav_profile_read_kit(const PavProfile *profile, char *out, size_t out_len, char *err, size_t err_len) {
    json_t *prefs;
    json_t *ui;
    json_t *value;
    const char *kit;

    if (out == NULL || out_len == 0) {
        cfg_err(err, err_len, "missing out");
        return -1;
    }
    out[0] = '\0';
    if (profile == NULL || profile->doc == NULL) {
        cfg_err(err, err_len, "missing profile");
        return -1;
    }
    if (prefs_objects(profile, &prefs, NULL, NULL, &ui, err, err_len) != 0) {
        return -1;
    }
    value = cfg_at(ui, "fontawesome_kit");
    kit = json_string_value(value);
    if (kit == NULL || kit[0] == '\0') {
        kit = "45801522ef";
    }
    if (strlen(kit) >= out_len) {
        cfg_err(err, err_len, "kit id is too long");
        return -1;
    }
    memcpy(out, kit, strlen(kit) + 1);
    return 0;
}

int pav_profile_write_kit(PavProfile *profile, const char *kit, char *err, size_t err_len) {
    json_t *prefs;
    json_t *ui;

    if (kit == NULL) {
        cfg_err(err, err_len, "missing kit");
        return -1;
    }
    if (profile == NULL || profile->doc == NULL) {
        cfg_err(err, err_len, "missing profile");
        return -1;
    }
    if (prefs_objects(profile, &prefs, NULL, NULL, &ui, err, err_len) != 0) {
        return -1;
    }
    return cfg_set_str(ui, "fontawesome_kit", kit, err, err_len);
}

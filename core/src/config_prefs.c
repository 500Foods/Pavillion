#include "pav_config.h"
#include "pav_cfg.h"
#include "config_internal.h"

#include <jansson.h>
#include <stdlib.h>
#include <string.h>

/* Keys the page owns. A copy that still sits on preferences moves into ui. */
static const char *const k_blob_keys[] = {
    "zoom", "nav", "mail", "contacts", "files", "icons", "fontawesome_kit", "fonts", NULL,
};

/* An old preferences.mail is the split {split,min,max}. N-2 stores
   preferences.mail.cssExtra in that same object. The layout keys move
   under ui. cssExtra stays. */
static int move_mail_layout(json_t *prefs, json_t *ui, int *changed, char *err, size_t err_len) {
    json_t *outside = json_object_get(prefs, "mail");
    json_t *ui_mail;
    static const char *const k_layout[] = {"split", "min", "max", NULL};
    size_t i;
    int layout = 0;

    if (!json_is_object(outside)) {
        return 0;
    }
    for (i = 0; k_layout[i] != NULL; i++) {
        if (json_object_get(outside, k_layout[i]) != NULL) {
            layout = 1;
            break;
        }
    }
    if (!layout) {
        return 0;
    }
    ui_mail = json_object_get(ui, "mail");
    if (ui_mail == NULL) {
        ui_mail = json_object();
        if (ui_mail == NULL || json_object_set_new(ui, "mail", ui_mail) != 0) {
            cfg_err(err, err_len, "out of memory");
            return -1;
        }
        *changed = 1;
    } else if (!json_is_object(ui_mail)) {
        cfg_err(err, err_len, "preferences.ui.mail is not an object");
        return -1;
    }
    for (i = 0; k_layout[i] != NULL; i++) {
        json_t *value = json_object_get(outside, k_layout[i]);

        if (value == NULL) {
            continue;
        }
        if (json_object_get(ui_mail, k_layout[i]) == NULL) {
            json_incref(value);
            if (json_object_set_new(ui_mail, k_layout[i], value) != 0) {
                json_decref(value);
                cfg_err(err, err_len, "out of memory");
                return -1;
            }
        }
        if (json_object_del(outside, k_layout[i]) != 0) {
            cfg_err(err, err_len, "out of memory");
            return -1;
        }
        *changed = 1;
    }
    return 0;
}

static int move_blob_key(json_t *prefs, json_t *ui, const char *key, int *changed, char *err,
                         size_t err_len) {
    json_t *outside = json_object_get(prefs, key);

    if (outside == NULL) {
        return 0;
    }
    if (json_object_get(ui, key) == NULL) {
        if (strcmp(key, "zoom") == 0 && !json_is_object(outside)) {
            cfg_err(err, err_len, "preferences.zoom is not an object");
            return -1;
        }
        json_incref(outside);
        if (json_object_set_new(ui, key, outside) != 0) {
            json_decref(outside);
            cfg_err(err, err_len, "out of memory");
            return -1;
        }
    }
    if (json_object_del(prefs, key) != 0) {
        cfg_err(err, err_len, "out of memory");
        return -1;
    }
    *changed = 1;
    return 0;
}

int pav_profile_migrate_ui(PavProfile *profile, int *changed, char *err, size_t err_len) {
    json_t *prefs;
    json_t *ui;
    int missing;
    size_t i;

    if (changed != NULL) {
        *changed = 0;
    }
    if (profile == NULL || profile->doc == NULL || changed == NULL) {
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
        *changed = 1;
    }
    for (i = 0; k_blob_keys[i] != NULL; i++) {
        if (strcmp(k_blob_keys[i], "mail") == 0) {
            if (move_mail_layout(prefs, ui, changed, err, err_len) != 0) {
                return -1;
            }
            continue;
        }
        if (move_blob_key(prefs, ui, k_blob_keys[i], changed, err, err_len) != 0) {
            return -1;
        }
    }
    return 0;
}

static int prefs_objects(const PavProfile *profile, json_t **prefs, json_t **window, json_t **ui,
                         char *err, size_t err_len) {
    if (profile == NULL || profile->doc == NULL) {
        cfg_err(err, err_len, "missing profile");
        return -1;
    }
    *prefs = cfg_obj(profile->doc, "preferences", 0);
    if (*prefs == NULL) {
        cfg_err(err, err_len, "missing profile fields");
        return -1;
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

int pav_profile_read_shell(const PavProfile *profile, PavShellPrefs *out, char *err, size_t err_len) {
    json_t *prefs;
    json_t *window;

    if (out == NULL) {
        cfg_err(err, err_len, "missing prefs");
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (prefs_objects(profile, &prefs, &window, NULL, err, err_len) != 0) {
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
    return 0;
}

int pav_profile_write_shell(PavProfile *profile, const PavShellPrefs *in, unsigned mask, char *err,
                            size_t err_len) {
    json_t *prefs;
    json_t *window;
    static const char *const k_chrome[] = {"system", "hidden", NULL};

    if (in == NULL) {
        cfg_err(err, err_len, "missing prefs");
        return -1;
    }
    if ((mask & PAV_PREFS_WINDOW) == 0) {
        cfg_err(err, err_len, "nothing to save");
        return -1;
    }
    if (prefs_objects(profile, &prefs, &window, NULL, err, err_len) != 0) {
        return -1;
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
    return 0;
}

int pav_profile_read_ui(const PavProfile *profile, char **out, char *err, size_t err_len) {
    json_t *prefs;
    json_t *ui;
    char *text;

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
    ui = json_object_get(prefs, "ui");
    if (ui == NULL) {
        text = malloc(3);
        if (text == NULL) {
            cfg_err(err, err_len, "out of memory");
            return -1;
        }
        memcpy(text, "{}", 3);
        *out = text;
        return 0;
    }
    if (!json_is_object(ui)) {
        cfg_err(err, err_len, "preferences.ui is not an object");
        return -1;
    }
    text = json_dumps(ui, JSON_PRESERVE_ORDER);
    if (text == NULL) {
        cfg_err(err, err_len, "out of memory");
        return -1;
    }
    *out = text;
    return 0;
}

int pav_profile_write_ui(PavProfile *profile, const char *json, char *err, size_t err_len) {
    json_error_t jerr;
    json_t *root;
    json_t *prefs;
    const char *forbidden = NULL;
    char *compact;

    if (profile == NULL || profile->doc == NULL || json == NULL) {
        cfg_err(err, err_len, "missing arguments");
        return -1;
    }
    if (strlen(json) > PAV_UI_JSON_MAX) {
        cfg_err(err, err_len, "preferences.ui is too large");
        return -1;
    }
    root = json_loads(json, JSON_PRESERVE_ORDER, &jerr);
    if (root == NULL) {
        cfg_err(err, err_len, "invalid json");
        return -1;
    }
    if (!json_is_object(root)) {
        json_decref(root);
        cfg_err(err, err_len, "preferences.ui is not an object");
        return -1;
    }
    if (config_forbidden_key(root, &forbidden)) {
        /* forbidden points into root: format the message before freeing it. */
        cfg_err(err, err_len, "forbidden key %s", forbidden);
        json_decref(root);
        return -1;
    }
    compact = json_dumps(root, JSON_COMPACT | JSON_PRESERVE_ORDER);
    if (compact == NULL) {
        json_decref(root);
        cfg_err(err, err_len, "out of memory");
        return -1;
    }
    if (strlen(compact) > PAV_UI_JSON_MAX) {
        free(compact);
        json_decref(root);
        cfg_err(err, err_len, "preferences.ui is too large");
        return -1;
    }
    free(compact);
    prefs = cfg_obj(profile->doc, "preferences", 0);
    if (prefs == NULL) {
        json_decref(root);
        cfg_err(err, err_len, "missing profile fields");
        return -1;
    }
    if (json_object_set_new(prefs, "ui", root) != 0) {
        cfg_err(err, err_len, "out of memory");
        return -1;
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
    if (prefs_objects(profile, &prefs, NULL, &ui, err, err_len) != 0) {
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
    if (prefs_objects(profile, &prefs, NULL, &ui, err, err_len) != 0) {
        return -1;
    }
    return cfg_set_str(ui, "fontawesome_kit", kit, err, err_len);
}

#define _POSIX_C_SOURCE 200809L

#include "account.h"

#include "dispatch.h"
#include "pav_auth.h"
#include "pav_cfg.h"
#include "pav_config.h"
#include "pav_db.h"
#include "pav_platform.h"
#include "pav_secret.h"
#include "pav_store.h"
#include "pavillion.h"

#include <jansson.h>

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAV_ACCOUNT_ID_MAX 64
/* "%03d" of any int: up to 10 digits, a sign, and the NUL. 001-999 print as
   three digits; a number past 999 keeps all its digits. */
#define PAV_ACCOUNT_NUMBER_LEN 12

static const char *const k_kind_allowed[] = {"jmap", "imap", NULL};
static const char *const k_auth_allowed[] = {"basic", NULL};
static const char *const k_fetch_allowed[] = {"none", "recent", "all", NULL};

static int db_insert_account_state_job(void *ctx, sqlite3 *db);
static int db_delete_account_state_job(void *ctx, sqlite3 *db);
static int db_purge_local_job(void *ctx, sqlite3 *db);

static int cfg_enum_check(const char *val, const char *const *allowed) {
    size_t i;

    if (val == NULL || allowed == NULL) {
        return 0;
    }
    for (i = 0; allowed[i] != NULL; i++) {
        if (strcmp(val, allowed[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int id_is_valid(const char *id) {
    size_t n;
    size_t i;

    if (id == NULL || id[0] == '\0') {
        return 0;
    }
    n = strlen(id);
    if (n > 63) {
        return 0;
    }
    if (!((id[0] >= 'a' && id[0] <= 'z') || (id[0] >= '0' && id[0] <= '9'))) {
        return 0;
    }
    for (i = 1; i < n; i++) {
        char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
            return 0;
        }
    }
    return 1;
}

static void id_from_name(const char *name, char *out, size_t out_len) {
    size_t n = strlen(name);
    size_t j = 0;
    size_t i;

    for (i = 0; i < n && j + 1 < out_len; i++) {
        char c = name[i];

        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            out[j++] = c;
        } else if (j > 0 && out[j - 1] != '-') {
            out[j++] = '-';
        }
    }
    while (j > 0 && out[j - 1] == '-') {
        j--;
    }
    out[j] = '\0';
    if (j == 0 ||
        (!((out[0] >= 'a' && out[0] <= 'z') || (out[0] >= '0' && out[0] <= '9')))) {
        memcpy(out, "a", 2);
    }
}

static int color_is_valid(const char *color) {
    size_t n;
    size_t i;

    if (color == NULL || color[0] != '#') {
        return 0;
    }
    n = strlen(color);
    if (n != 4 && n != 7) {
        return 0;
    }
    for (i = 1; i < n; i++) {
        char c = color[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return 0;
        }
    }
    return 1;
}

/* caFile is an optional absolute path to a PEM CA bundle, under 4096 bytes. */
/* API.md Conventions: absolute per platform (Windows drive and UNC paths too). */
static int ca_file_is_valid(const char *path) {
    return path != NULL && pav_path_is_absolute(path) && strlen(path) < 4096;
}

static int profile_is_ready(PavError *err) {
    PavConfigLoad *load = pav_core_load();

    if (load == NULL || load->profile == NULL || strcmp(load->state, "ready") != 0) {
        pav_error_set(err, "not_ready", "profile is not ready", 0);
        return 0;
    }
    return 1;
}

static int account_has(json_t *accounts, const char *id) {
    size_t i;
    size_t n;

    if (accounts == NULL || !json_is_array(accounts) || id == NULL) {
        return 0;
    }
    n = json_array_size(accounts);
    for (i = 0; i < n; i++) {
        json_t *acct = json_array_get(accounts, i);
        const char *acct_id = json_string_value(json_object_get(acct, "id"));

        if (acct_id != NULL && strcmp(acct_id, id) == 0) {
            return 1;
        }
    }
    return 0;
}

static json_t *account_find(json_t *accounts, const char *id) {
    size_t i;
    size_t n;

    if (accounts == NULL || !json_is_array(accounts) || id == NULL) {
        return NULL;
    }
    n = json_array_size(accounts);
    for (i = 0; i < n; i++) {
        json_t *acct = json_array_get(accounts, i);
        const char *acct_id = json_string_value(json_object_get(acct, "id"));

        if (acct_id != NULL && strcmp(acct_id, id) == 0) {
            return acct;
        }
    }
    return NULL;
}

static int store_account_state(const char *account_id) {
    return pav_db_call(db_insert_account_state_job, (void *)account_id);
}

static int delete_account_state(const char *account_id) {
    return pav_db_call(db_delete_account_state_job, (void *)account_id);
}

static void emit_accounts_changed(json_t *accounts) {
    char *text = json_dumps(accounts, JSON_COMPACT);

    if (text == NULL) {
        return;
    }
    pav_event_post("accounts.changed", text);
    free(text);
}

static int db_insert_account_state_job(void *ctx, sqlite3 *db) {
    const char *id = (const char *)ctx;
    sqlite3_stmt *stmt = NULL;
    int rc;

    rc = pav_db_prepare(db, "account.insert_state",
                        "INSERT INTO account_state (account_id, state) VALUES (?, 'idle')", &stmt);
    if (rc != 0) {
        return -1;
    }
    if (pav_db_bind_text(stmt, 1, id) != 0) {
        return -1;
    }
    rc = pav_db_step(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

static int db_delete_account_state_job(void *ctx, sqlite3 *db) {
    const char *id = (const char *)ctx;
    sqlite3_stmt *stmt = NULL;
    int rc;

    rc = pav_db_prepare(db, "account.delete_state",
                        "DELETE FROM account_state WHERE account_id = ?", &stmt);
    if (rc != 0) {
        return -1;
    }
    if (pav_db_bind_text(stmt, 1, id) != 0) {
        return -1;
    }
    rc = pav_db_step(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

static int db_purge_local_job(void *ctx, sqlite3 *db) {
    const char *id = (const char *)ctx;
    static const char *const k_sql[] = {
        "DELETE FROM sync_state WHERE ra_id IN (SELECT id FROM remote_account WHERE account_id = ?)",
        "DELETE FROM mailbox WHERE ra_id IN (SELECT id FROM remote_account WHERE account_id = ?)",
        "DELETE FROM identity WHERE ra_id IN (SELECT id FROM remote_account WHERE account_id = ?)",
        "DELETE FROM remote_account WHERE account_id = ?",
        NULL,
    };
    int i;

    for (i = 0; k_sql[i] != NULL; i++) {
        sqlite3_stmt *stmt = NULL;
        const char *key = i == 0 ? "account.purge_0" : i == 1 ? "account.purge_1" :
                          i == 2 ? "account.purge_2" : "account.purge_3";
        int rc = pav_db_prepare(db, key, k_sql[i], &stmt);
        if (rc != 0) {
            return -1;
        }
        if (pav_db_bind_text(stmt, 1, id) != 0) {
            return -1;
        }
        rc = pav_db_step(stmt);
        if (rc != SQLITE_DONE) {
            return -1;
        }
    }
    return 0;
}

static int load_index_for_write(PavIndex **out, char *err, size_t err_len) {
    const char *dir = pav_core_workdir();
    char path[4096];
    char *text = NULL;
    char cfgerr[512];
    FILE *f;
    long n;

    if (dir == NULL || dir[0] == '\0') {
        cfg_err(err, err_len, "not running");
        return -1;
    }
    if (pav_path_join(path, sizeof(path), dir, "Pavillion.json") != 0) {
        cfg_err(err, err_len, "path is too long");
        return -1;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        cfg_err(err, err_len, "cannot read index");
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        cfg_err(err, err_len, "cannot read index");
        return -1;
    }
    n = ftell(f);
    if (n < 0 || (size_t)n > 1024 * 1024) {
        fclose(f);
        cfg_err(err, err_len, "index is too large");
        return -1;
    }
    rewind(f);
    text = malloc((size_t)n + 1);
    if (text == NULL) {
        fclose(f);
        cfg_err(err, err_len, "out of memory");
        return -1;
    }
    if (fread(text, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(text);
        cfg_err(err, err_len, "cannot read index");
        return -1;
    }
    fclose(f);
    text[n] = '\0';
    cfgerr[0] = '\0';
    if (pav_index_parse(text, out, cfgerr, sizeof(cfgerr)) != 0) {
        free(text);
        cfg_err(err, err_len, "%s", cfgerr[0] != '\0' ? cfgerr : "cannot read index");
        return -1;
    }
    free(text);
    return 0;
}

static int write_index(PavIndex *index, char *err, size_t err_len) {
    const char *dir = pav_core_workdir();
    char path[4096];
    char *rewritten = NULL;
    const PavPlatform *platform;

    if (pav_path_join(path, sizeof(path), dir, "Pavillion.json") != 0) {
        cfg_err(err, err_len, "path is too long");
        return -1;
    }
    if (pav_index_rewrite(index, &rewritten) != 0 || rewritten == NULL) {
        cfg_err(err, err_len, "out of memory");
        return -1;
    }
    platform = pav_platform();
    if (platform == NULL || platform->write_private == NULL ||
        platform->write_private(path, rewritten, strlen(rewritten)) != 0) {
        free(rewritten);
        cfg_err(err, err_len, "cannot write index");
        return -1;
    }
    free(rewritten);
    return 0;
}

static int set_json_str(json_t *obj, const char *key, const char *val) {
    if (val == NULL) {
        return json_object_set_new(obj, key, json_null()) == 0 ? 0 : -1;
    }
    return json_object_set_new(obj, key, json_string(val)) == 0 ? 0 : -1;
}

/* K-Q6: "" or name@domain.tld-ish, under 256 bytes, no spaces. */
static int email_is_valid(const char *s) {
    const char *at;
    size_t i, n = strlen(s);

    if (n == 0) return 1;
    if (n >= 256) return 0;
    for (i = 0; i < n; i++) {
        if (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n') return 0;
    }
    at = strchr(s, '@');
    return at != NULL && at != s && strchr(at + 1, '@') == NULL && strchr(at + 1, '.') != NULL &&
           at[1] != '.' && s[n - 1] != '.';
}

/* The default email of an account: its username when that is an address. */
static const char *email_default(const char *username) {
    return (username != NULL && username[0] != '\0' && email_is_valid(username)) ? username : "";
}

/* K-Q9: "" (the default envelope) or a Font Awesome HTML snippet under 4096
   bytes. The core only bounds it; the page sanitizes it before drawing. */
static int icon_is_valid(const char *s) {
    size_t i, n = strlen(s);

    if (n == 0) return 1;
    if (n >= 4096 || s[0] != '<') return 0;
    for (i = 0; i < n; i++) {
        char lower[12];
        size_t k;
        for (k = 0; k < sizeof(lower) - 1 && i + k < n; k++) {
            char c = s[i + k];
            lower[k] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
        }
        lower[k] = '\0';
        if (strncmp(lower, "<script", 7) == 0 || strncmp(lower, "javascript:", 11) == 0) return 0;
        if ((s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '/') && (lower[1] == 'o') && lower[2] == 'n') {
            size_t j = i + 3;
            while (j < n && ((s[j] >= 'a' && s[j] <= 'z') || (s[j] >= 'A' && s[j] <= 'Z'))) j++;
            if (j > i + 3 && j < n && s[j] == '=') return 0;
        }
    }
    return 1;
}

int pav_account_migrate_email(json_t *doc) {
    json_t *accounts = json_is_object(doc) ? json_object_get(doc, "accounts") : NULL;
    json_t *acct;
    size_t i;
    int changed = 0;

    if (!json_is_array(accounts)) return 0;
    json_array_foreach(accounts, i, acct) {
        int touched = 0;
        if (!json_is_object(acct)) continue;
        if (!json_is_string(json_object_get(acct, "email"))) {
            /* K-Q6: the username when it is an address, else "". */
            json_t *user = json_object_get(acct, "username");
            json_object_set_new(acct, "email",
                                json_string(email_default(json_is_string(user) ? json_string_value(user) : NULL)));
            touched = 1;
        }
        if (!json_is_string(json_object_get(acct, "icon"))) {
            /* K-Q9: "" is the default envelope. */
            json_object_set_new(acct, "icon", json_string(""));
            touched = 1;
        }
        if (!json_is_array(json_object_get(acct, "capabilities"))) {
            /* K-Q10: never checked. */
            json_object_set_new(acct, "capabilities", json_array());
            touched = 1;
        }
        if (!json_is_string(json_object_get(acct, "capabilitiesChecked")) &&
            !json_is_null(json_object_get(acct, "capabilitiesChecked"))) {
            json_object_set_new(acct, "capabilitiesChecked", json_null());
            touched = 1;
        }
        changed += touched;
    }
    return changed;
}

static int set_json_bool(json_t *obj, const char *key, int val) {
    return json_object_set_new(obj, key, val ? json_true() : json_false()) == 0 ? 0 : -1;
}

static json_t *build_account_json(const char *id, const char *number, const char *name,
                                  const char *kind, const char *session_url,
                                  const char *username, const char *auth, const char *color,
                                  int enabled, int sync_mail, int sync_contacts,
                                  int sync_calendars, const char *fetch_bodies,
                                  const char *ca_file, const char *email,
                                  const char *icon, json_t *caps, const char *checked) {
    json_t *obj = json_object();

    if (obj == NULL) {
        return NULL;
    }
    if (set_json_str(obj, "id", id) != 0) {
        json_decref(obj);
        return NULL;
    }
    if (set_json_str(obj, "number", number) != 0 ||
        set_json_str(obj, "name", name) != 0 ||
        set_json_str(obj, "kind", kind) != 0 ||
        set_json_str(obj, "sessionUrl", session_url) != 0 ||
        set_json_str(obj, "username", username) != 0 ||
        set_json_str(obj, "email", email != NULL ? email : email_default(username)) != 0 ||
        set_json_str(obj, "icon", icon != NULL ? icon : "") != 0 ||
        set_json_str(obj, "auth", auth != NULL ? auth : "basic") != 0) {
        json_decref(obj);
        return NULL;
    }
    if (color != NULL && set_json_str(obj, "color", color) != 0) {
        json_decref(obj);
        return NULL;
    }
    if (set_json_bool(obj, "enabled", enabled) != 0 ||
        set_json_bool(obj, "syncMail", sync_mail) != 0 ||
        set_json_bool(obj, "syncContacts", sync_contacts) != 0 ||
        set_json_bool(obj, "syncCalendars", sync_calendars) != 0) {
        json_decref(obj);
        return NULL;
    }
    if (fetch_bodies != NULL && set_json_str(obj, "fetchBodies", fetch_bodies) != 0) {
        json_decref(obj);
        return NULL;
    }
    if (ca_file != NULL && set_json_str(obj, "caFile", ca_file) != 0) {
        json_decref(obj);
        return NULL;
    }
    /* K-Q10: what a Test before Add found, or never checked. */
    json_object_set_new(obj, "capabilities", caps != NULL ? json_incref(caps) : json_array());
    json_object_set_new(obj, "capabilitiesChecked", checked != NULL ? json_string(checked) : json_null());
    return obj;
}

/* K-Q10: the capability words account.test returns, in that order. */
static const char *const k_cap_words[] = {"mail", "submission", "contacts", "calendars", "sieve", "files", "websocket"};

/* A new array of the known words in raw (others and repeats dropped), in
   k_cap_words order, or NULL when raw is not an array. */
json_t *pav_account_caps_clean(json_t *raw) {
    json_t *out;
    size_t w;

    if (!json_is_array(raw)) return NULL;
    out = json_array();
    if (out == NULL) return NULL;
    for (w = 0; w < sizeof(k_cap_words) / sizeof(k_cap_words[0]); w++) {
        size_t i;
        json_t *v;
        json_array_foreach(raw, i, v) {
            if (json_is_string(v) && strcmp(json_string_value(v), k_cap_words[w]) == 0) {
                json_array_append_new(out, json_string(k_cap_words[w]));
                break;
            }
        }
    }
    return out;
}

/* K-Q10: "YYYY-MM-DDTHH:MM:SSZ" or with ".mmm" before the Z. */
static int checked_is_valid(const char *s) {
    static const char shape[] = "dddd-dd-ddTdd:dd:dd";
    size_t n = strlen(s);
    size_t i;

    if (n != 20 && n != 24) return 0;
    for (i = 0; i < sizeof(shape) - 1; i++) {
        if (shape[i] == 'd' ? !(s[i] >= '0' && s[i] <= '9') : s[i] != shape[i]) return 0;
    }
    if (n == 24) {
        if (s[19] != '.' || s[20] < '0' || s[20] > '9' || s[21] < '0' || s[21] > '9' || s[22] < '0' || s[22] > '9') return 0;
    }
    return s[n - 1] == 'Z';
}

static void caps_text(json_t *caps, char *out, size_t len) {
    size_t i;
    json_t *v;
    size_t w = 0;

    out[0] = '\0';
    json_array_foreach(caps, i, v) {
        int n = snprintf(out + w, len - w, "%s%s", i > 0 ? ", " : "", json_string_value(v));
        if (n < 0 || (size_t)n >= len - w) break;
        w += (size_t)n;
    }
    if (w == 0) snprintf(out, len, "none");
}

/* Core thread only (K-Q10): store a verified test result on account id.
   Takes ownership of caps. 0 stored (or unchanged), -1 on failure. */
int pav_account_store_capabilities(const char *id, json_t *caps, const char *checked) {
    PavConfigLoad *load;
    json_t *accounts;
    json_t *acct;
    json_t *before;
    char errbuf[256];
    char text[160];
    int changed;
    PavError err;

    memset(&err, 0, sizeof(err));
    if (caps == NULL || id == NULL || checked == NULL || !profile_is_ready(&err)) {
        json_decref(caps);
        return -1;
    }
    load = pav_core_load();
    accounts = json_object_get((json_t *)load->profile->doc, "accounts");
    acct = json_is_array(accounts) ? account_find(accounts, id) : NULL;
    if (acct == NULL) {
        json_decref(caps);
        pav_log("account", PAV_DEBUG, 1, "capabilities not stored: %s is gone", id);
        return -1;
    }
    before = json_object_get(acct, "capabilities");
    changed = !json_equal(before, caps);
    caps_text(caps, text, sizeof(text));
    json_object_set_new(acct, "capabilities", caps);
    json_object_set_new(acct, "capabilitiesChecked", json_string(checked));
    errbuf[0] = '\0';
    if (pav_profile_store(load->profile_path, load->profile, errbuf, sizeof(errbuf)) != 0) {
        pav_log("account", PAV_WARN, 1, "cannot store capabilities of %s: %s", id, errbuf);
        return -1;
    }
    if (changed) {
        pav_log("account", PAV_INFO, 2, "capabilities of %s: %s", id, text);
    }
    return 0;
}

int pav_account_list(json_t *args, json_t **data, PavError *err) {
    PavConfigLoad *load;
    json_t *accounts;
    json_t *copy;
    json_t *result;
    const char *key;
    json_t *value;
    char buf[256];

    if (data == NULL) {
        pav_error_set(err, "internal", "missing data", 0);
        return -1;
    }
    *data = NULL;

    json_object_foreach(args, key, value) {
        (void)value;
        snprintf(buf, sizeof(buf), "unknown argument %s", key);
        pav_error_set(err, "bad_args", buf, 0);
        return -1;
    }
    if (!profile_is_ready(err)) {
        return -1;
    }

    load = pav_core_load();
    accounts = json_object_get((json_t *)load->profile->doc, "accounts");
    result = json_object();
    if (result == NULL) {
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    copy = (accounts != NULL && json_is_array(accounts)) ? json_deep_copy(accounts) : json_array();
    if (copy != NULL) {
        json_t *wrap = json_pack("{s:O}", "accounts", copy);
        if (wrap != NULL) {
            pav_account_migrate_email(wrap);
            json_decref(wrap);
        }
    }
    if (copy == NULL) {
        json_decref(result);
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    if (json_object_set_new(result, "accounts", copy) != 0) {
        json_decref(copy);
        json_decref(result);
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    *data = result;
    return 0;
}

int pav_account_add(json_t *args, json_t **data, PavError *err) {
    const char *name = NULL;
    const char *id_in = NULL;
    const char *kind = NULL;
    const char *session_url = NULL;
    const char *username = NULL;
    const char *auth_arg = NULL;
    const char *secret = NULL;
    const char *color = NULL;
    const char *fetch_bodies = NULL;
    const char *ca_file = NULL;
    const char *email = NULL;
    const char *icon = NULL;
    json_t *caps = NULL;
    const char *checked = NULL;
    int sync_mail = 1;
    int sync_contacts = 1;
    int sync_calendars = 1;
    int enabled = 1;
    const char *key;
    json_t *value;
    char id_gen[PAV_ACCOUNT_ID_MAX];
    const char *account_id;
    char number_str[PAV_ACCOUNT_NUMBER_LEN];
    int next_num;
    char errbuf[512];
    char keyring_user[256];
    int keyring_rc;
    int db_rc;
    size_t idx;
    json_t *result;

    if (data == NULL) {
        pav_error_set(err, "internal", "missing data", 0);
        return -1;
    }
    *data = NULL;

    if (!profile_is_ready(err)) {
        return -1;
    }

    json_object_foreach(args, key, value) {
        if (strcmp(key, "name") == 0) {
            if (!json_is_string(value) || json_string_value(value)[0] == '\0' ||
                strlen(json_string_value(value)) >= 256) {
                pav_error_set(err, "bad_args", "name must be a non-empty string under 256 bytes", 0);
                return -1;
            }
            name = json_string_value(value);
        } else if (strcmp(key, "id") == 0) {
            if (!json_is_string(value) || json_string_value(value)[0] == '\0' ||
                strlen(json_string_value(value)) >= PAV_ACCOUNT_ID_MAX) {
                pav_error_set(err, "bad_args", "id must be a non-empty string", 0);
                return -1;
            }
            id_in = json_string_value(value);
        } else if (strcmp(key, "kind") == 0) {
            if (!json_is_string(value) || !cfg_enum_check(json_string_value(value), k_kind_allowed)) {
                pav_error_set(err, "bad_args", "kind must be jmap or imap", 0);
                return -1;
            }
            kind = json_string_value(value);
        } else if (strcmp(key, "sessionUrl") == 0) {
            if (!json_is_string(value) || json_string_value(value)[0] == '\0') {
                pav_error_set(err, "bad_args", "sessionUrl must be a non-empty string", 0);
                return -1;
            }
            session_url = json_string_value(value);
        } else if (strcmp(key, "username") == 0) {
            if (!json_is_string(value) || json_string_value(value)[0] == '\0' ||
                strlen(json_string_value(value)) >= 256) {
                pav_error_set(err, "bad_args", "username must be a non-empty string under 256 bytes", 0);
                return -1;
            }
            username = json_string_value(value);
        } else if (strcmp(key, "auth") == 0) {
            if (!json_is_string(value) || !cfg_enum_check(json_string_value(value), k_auth_allowed)) {
                pav_error_set(err, "bad_args", "auth must be basic", 0);
                return -1;
            }
            auth_arg = json_string_value(value);
        } else if (strcmp(key, "secret") == 0) {
            if (!json_is_string(value)) {
                pav_error_set(err, "bad_args", "secret must be a string", 0);
                return -1;
            }
            secret = json_string_value(value);
        } else if (strcmp(key, "color") == 0) {
            if (!json_is_string(value)) {
                pav_error_set(err, "bad_args", "color must be a string", 0);
                return -1;
            }
            color = json_string_value(value);
        } else if (strcmp(key, "syncMail") == 0) {
            if (!json_is_boolean(value)) {
                pav_error_set(err, "bad_args", "syncMail must be a boolean", 0);
                return -1;
            }
            sync_mail = json_is_true(value) ? 1 : 0;
        } else if (strcmp(key, "syncContacts") == 0) {
            if (!json_is_boolean(value)) {
                pav_error_set(err, "bad_args", "syncContacts must be a boolean", 0);
                return -1;
            }
            sync_contacts = json_is_true(value) ? 1 : 0;
        } else if (strcmp(key, "syncCalendars") == 0) {
            if (!json_is_boolean(value)) {
                pav_error_set(err, "bad_args", "syncCalendars must be a boolean", 0);
                return -1;
            }
            sync_calendars = json_is_true(value) ? 1 : 0;
        } else if (strcmp(key, "fetchBodies") == 0) {
            if (!json_is_string(value) ||
                !cfg_enum_check(json_string_value(value), k_fetch_allowed)) {
                pav_error_set(err, "bad_args", "fetchBodies must be none, recent, or all", 0);
                return -1;
            }
            fetch_bodies = json_string_value(value);
        } else if (strcmp(key, "email") == 0) {
            if (!json_is_string(value) || !email_is_valid(json_string_value(value))) {
                pav_error_set(err, "bad_args", "email must be empty or name@domain under 256 bytes", 0);
                return -1;
            }
            email = json_string_value(value);
        } else if (strcmp(key, "icon") == 0) {
            if (!json_is_string(value) || !icon_is_valid(json_string_value(value))) {
                pav_error_set(err, "bad_args", "icon must be empty or a Font Awesome HTML snippet under 4096 bytes", 0);
                return -1;
            }
            icon = json_string_value(value);
        } else if (strcmp(key, "capabilities") == 0) {
            if (!json_is_array(value)) {
                pav_error_set(err, "bad_args", "capabilities must be an array", 0);
                return -1;
            }
            caps = value; /* borrowed from args; cleaned when stored */
        } else if (strcmp(key, "capabilitiesChecked") == 0) {
            if (!json_is_string(value) || !checked_is_valid(json_string_value(value))) {
                pav_error_set(err, "bad_args", "capabilitiesChecked must be UTC ISO 8601", 0);
                return -1;
            }
            checked = json_string_value(value);
        } else if (strcmp(key, "caFile") == 0) {
            if (!json_is_string(value) || !ca_file_is_valid(json_string_value(value))) {
                pav_error_set(err, "bad_args", "caFile must be an absolute path", 0);
                return -1;
            }
            ca_file = json_string_value(value);
        } else if (strcmp(key, "enabled") == 0) {
            if (!json_is_boolean(value)) {
                pav_error_set(err, "bad_args", "enabled must be a boolean", 0);
                return -1;
            }
            enabled = json_is_true(value) ? 1 : 0;
        } else {
            snprintf(errbuf, sizeof(errbuf), "unknown argument %s", key);
            pav_error_set(err, "bad_args", errbuf, 0);
            return -1;
        }
    }

    if (kind == NULL) {
        pav_error_set(err, "bad_args", "kind is required", 0);
        return -1;
    }
    if (session_url == NULL) {
        pav_error_set(err, "bad_args", "sessionUrl is required", 0);
        return -1;
    }
    if (username == NULL) {
        pav_error_set(err, "bad_args", "username is required", 0);
        return -1;
    }
    if (secret == NULL) {
        pav_error_set(err, "bad_args", "secret is required", 0);
        return -1;
    }
    if (strncmp(session_url, "https://", 8) != 0) {
        pav_error_set(err, "bad_args", "sessionUrl must be an https URL", 0);
        return -1;
    }
    if (color != NULL && !color_is_valid(color)) {
        pav_error_set(err, "bad_args", "color must be #rgb or #rrggbb", 0);
        return -1;
    }
    if (fetch_bodies != NULL && !cfg_enum_check(fetch_bodies, k_fetch_allowed)) {
        pav_error_set(err, "bad_args", "fetchBodies must be none, recent, or all", 0);
        return -1;
    }

    account_id = id_in;
    if (account_id == NULL || account_id[0] == '\0') {
        if (name == NULL) {
            pav_error_set(err, "bad_args", "id or name is required", 0);
            return -1;
        }
        id_from_name(name, id_gen, sizeof(id_gen));
        account_id = id_gen;
    }
    if (!id_is_valid(account_id)) {
        pav_error_set(err, "bad_args", "id must match ^[a-z0-9][a-z0-9-]{0,62}$", 0);
        return -1;
    }

    {
        PavConfigLoad *load = pav_core_load();
        json_t *doc = (json_t *)load->profile->doc;
        json_t *accounts = json_object_get(doc, "accounts");

        if (accounts == NULL || !json_is_array(accounts)) {
            pav_error_set(err, "internal", "missing accounts array", 0);
            return -1;
        }
        if (account_has(accounts, account_id)) {
            snprintf(errbuf, sizeof(errbuf), "account %s already exists", account_id);
            pav_error_set(err, "bad_args", errbuf, 0);
            return -1;
        }

        next_num = load->next_account_number;
        if (next_num < 1) {
            next_num = 1;
        }
        snprintf(number_str, sizeof(number_str), "%03d", next_num);

        snprintf(keyring_user, sizeof(keyring_user), "%s/%s", load->profile->id, account_id);
        keyring_rc = pav_secret_set("pavillion", keyring_user, "account", secret);
        if (keyring_rc == PAV_SECRET_LOCKED) {
            pav_error_set(err, "locked", "keyring is locked", 0);
            return -1;
        }
        if (keyring_rc != 0) {
            pav_error_set(err, "failed", "cannot store secret", 0);
            return -1;
        }

        json_t *clean_caps = caps != NULL ? pav_account_caps_clean(caps) : NULL;
        json_t *acct = build_account_json(account_id, number_str, name, kind, session_url,
                                          username, auth_arg, color, enabled, sync_mail,
                                          sync_contacts, sync_calendars, fetch_bodies, ca_file, email, icon, clean_caps, checked);
        json_decref(clean_caps);
        if (acct == NULL) {
            pav_secret_delete("pavillion", keyring_user);
            pav_error_set(err, "internal", "out of memory", 0);
            return -1;
        }
        if (json_array_append_new(accounts, acct) != 0) {
            json_decref(acct);
            pav_secret_delete("pavillion", keyring_user);
            pav_error_set(err, "internal", "out of memory", 0);
            return -1;
        }
        idx = json_array_size(accounts) - 1;

        db_rc = store_account_state(account_id);
        if (db_rc != 0) {
            json_array_remove(accounts, idx);
            pav_secret_delete("pavillion", keyring_user);
            pav_error_set(err, "internal", "cannot create account state", 0);
            return -1;
        }
        load->next_account_number = next_num + 1;

        errbuf[0] = '\0';
        if (pav_profile_store(load->profile_path, load->profile, errbuf, sizeof(errbuf)) != 0) {
            json_array_remove(accounts, idx);
            pav_secret_delete("pavillion", keyring_user);
            delete_account_state(account_id);
            load->next_account_number = next_num;
            pav_error_set(err, "internal", errbuf[0] != '\0' ? errbuf : "cannot write profile", 0);
            return -1;
        }

        {
            PavIndex *index = NULL;
            char idxerr[512];

            idxerr[0] = '\0';
            if (load_index_for_write(&index, idxerr, sizeof(idxerr)) != 0 ||
                cfg_set_int(index->doc, "next_account_number", next_num + 1, idxerr,
                            sizeof(idxerr)) != 0 ||
                write_index(index, idxerr, sizeof(idxerr)) != 0) {
                json_array_remove(accounts, idx);
                pav_secret_delete("pavillion", keyring_user);
                delete_account_state(account_id);
                load->next_account_number = next_num;
                pav_error_set(err, "internal",
                              idxerr[0] != '\0' ? idxerr : "cannot write index", 0);
                if (index != NULL) {
                    pav_index_free(index);
                }
                return -1;
            }
            pav_index_free(index);
        }

        result = json_deep_copy(acct);
        if (result == NULL) {
            pav_error_set(err, "internal", "out of memory", 0);
            emit_accounts_changed(accounts);
            return -1;
        }
        *data = result;
        emit_accounts_changed(accounts);
        return 0;
    }
}

int pav_account_remove(json_t *args, json_t **data, PavError *err) {
    const char *id;
    const char *key;
    json_t *value;
    char buf[256];
    char keyring_user[256];
    int purge_local = 0;
    int any = 0;
    int found = 0;
    size_t idx;
    size_t n;
    json_t *result;

    if (data == NULL) {
        pav_error_set(err, "internal", "missing data", 0);
        return -1;
    }
    *data = NULL;

    json_object_foreach(args, key, value) {
        any = 1;
        if (strcmp(key, "id") == 0) {
            if (!json_is_string(value) || json_string_value(value)[0] == '\0' ||
                strlen(json_string_value(value)) >= PAV_ACCOUNT_ID_MAX) {
                pav_error_set(err, "bad_args", "id must be a non-empty string", 0);
                return -1;
            }
            id = json_string_value(value);
        } else if (strcmp(key, "purgeLocal") == 0) {
            if (!json_is_boolean(value)) {
                pav_error_set(err, "bad_args", "purgeLocal must be a boolean", 0);
                return -1;
            }
            purge_local = json_is_true(value) ? 1 : 0;
        } else {
            snprintf(buf, sizeof(buf), "unknown argument %s", key);
            pav_error_set(err, "bad_args", buf, 0);
            return -1;
        }
    }
    if (!any) {
        pav_error_set(err, "bad_args", "id is required", 0);
        return -1;
    }

    if (!profile_is_ready(err)) {
        return -1;
    }

    {
        PavConfigLoad *load = pav_core_load();
        json_t *doc = (json_t *)load->profile->doc;
        json_t *accounts = json_object_get(doc, "accounts");
        char errbuf[512];

        if (accounts == NULL || !json_is_array(accounts)) {
            pav_error_set(err, "internal", "missing accounts array", 0);
            return -1;
        }

        n = json_array_size(accounts);
        for (idx = 0; idx < n; idx++) {
            json_t *acct = json_array_get(accounts, idx);
            const char *acct_id = json_string_value(json_object_get(acct, "id"));
            if (acct_id != NULL && strcmp(acct_id, id) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            snprintf(buf, sizeof(buf), "account %s not found", id);
            pav_error_set(err, "not_found", buf, 0);
            return -1;
        }

        snprintf(keyring_user, sizeof(keyring_user), "%s/%s", load->profile->id, id);
        pav_secret_delete("pavillion", keyring_user);
        json_array_remove(accounts, idx);
        delete_account_state(id);

        if (purge_local) {
            pav_db_call(db_purge_local_job, (void *)id);
        }

        errbuf[0] = '\0';
        if (pav_profile_store(load->profile_path, load->profile, errbuf, sizeof(errbuf)) != 0) {
            pav_error_set(err, "internal", errbuf[0] != '\0' ? errbuf : "cannot write profile", 0);
            return -1;
        }

        result = json_object();
        if (result == NULL || json_object_set_new(result, "id", json_string(id)) != 0) {
            json_decref(result);
            pav_error_set(err, "internal", "out of memory", 0);
            emit_accounts_changed(accounts);
            return -1;
        }
        *data = result;
        emit_accounts_changed(accounts);
        return 0;
    }
}

int pav_account_update(json_t *args, json_t **data, PavError *err) {
    const char *id;
    json_t *patch;
    const char *key;
    json_t *value;
    char buf[256];
    json_t *acct;
    json_t *result;
    char errbuf[512];

    if (data == NULL) {
        pav_error_set(err, "internal", "missing data", 0);
        return -1;
    }
    *data = NULL;

    id = json_string_value(json_object_get(args, "id"));
    patch = json_object_get(args, "patch");
    if (!json_is_string(json_object_get(args, "id")) ||
        json_string_value(json_object_get(args, "id"))[0] == '\0' ||
        strlen(json_string_value(json_object_get(args, "id"))) >= PAV_ACCOUNT_ID_MAX) {
        pav_error_set(err, "bad_args", "id must be a non-empty string", 0);
        return -1;
    }
    if (!json_is_object(patch)) {
        pav_error_set(err, "bad_args", "patch must be an object", 0);
        return -1;
    }
    json_object_foreach(args, key, value) {
        if (strcmp(key, "id") == 0 || strcmp(key, "patch") == 0) {
            continue;
        }
        snprintf(buf, sizeof(buf), "unknown argument %s", key);
        pav_error_set(err, "bad_args", buf, 0);
        return -1;
    }
    if (json_object_size(args) == 0) {
        pav_error_set(err, "bad_args", "nothing to update", 0);
        return -1;
    }

    {
        const char *pkey;
        json_t *pval;
        json_object_foreach(patch, pkey, pval) {
            if (strcmp(pkey, "sessionUrl") == 0) {
                if (!json_is_string(pval) ||
                    strncmp(json_string_value(pval), "https://", 8) != 0) {
                    pav_error_set(err, "bad_args", "sessionUrl must be an https URL", 0);
                    return -1;
                }
            } else if (strcmp(pkey, "username") == 0) {
                if (!json_is_string(pval) || json_string_value(pval)[0] == '\0' ||
                    strlen(json_string_value(pval)) >= 256) {
                    pav_error_set(err, "bad_args", "username must be a non-empty string", 0);
                    return -1;
                }
            } else if (strcmp(pkey, "color") == 0) {
                if (!json_is_string(pval) || !color_is_valid(json_string_value(pval))) {
                    pav_error_set(err, "bad_args", "color must be #rgb or #rrggbb", 0);
                    return -1;
                }
            } else if (strcmp(pkey, "syncMail") == 0 || strcmp(pkey, "syncContacts") == 0 ||
                       strcmp(pkey, "syncCalendars") == 0 || strcmp(pkey, "enabled") == 0) {
                if (!json_is_boolean(pval)) {
                    snprintf(buf, sizeof(buf), "%s must be a boolean", pkey);
                    pav_error_set(err, "bad_args", buf, 0);
                    return -1;
                }
            } else if (strcmp(pkey, "fetchBodies") == 0) {
                if (!json_is_string(pval) ||
                    !cfg_enum_check(json_string_value(pval), k_fetch_allowed)) {
                    pav_error_set(err, "bad_args", "fetchBodies must be none, recent, or all", 0);
                    return -1;
                }
            } else if (strcmp(pkey, "name") == 0) {
                if (!json_is_string(pval) || json_string_value(pval)[0] == '\0' ||
                    strlen(json_string_value(pval)) >= 256) {
                    pav_error_set(err, "bad_args", "name must be a non-empty string", 0);
                    return -1;
                }
            } else if (strcmp(pkey, "caFile") == 0) {
                if (!json_is_string(pval) ||
                    (json_string_value(pval)[0] != '\0' && !ca_file_is_valid(json_string_value(pval)))) {
                    pav_error_set(err, "bad_args", "caFile must be an absolute path or empty", 0);
                    return -1;
                }
            } else if (strcmp(pkey, "email") == 0) {
                if (!json_is_string(pval) || !email_is_valid(json_string_value(pval))) {
                    pav_error_set(err, "bad_args", "email must be empty or name@domain under 256 bytes", 0);
                    return -1;
                }
            } else if (strcmp(pkey, "icon") == 0) {
                if (!json_is_string(pval) || !icon_is_valid(json_string_value(pval))) {
                    pav_error_set(err, "bad_args", "icon must be empty or a Font Awesome HTML snippet under 4096 bytes", 0);
                    return -1;
                }
            } else {
                snprintf(buf, sizeof(buf), "unknown patch key %s", pkey);
                pav_error_set(err, "bad_args", buf, 0);
                return -1;
            }
        }
    }

    if (!profile_is_ready(err)) {
        return -1;
    }

    {
        PavConfigLoad *load = pav_core_load();
        json_t *accounts = json_object_get((json_t *)load->profile->doc, "accounts");
        const char *pkey;
        json_t *pval;

        if (accounts == NULL || !json_is_array(accounts)) {
            pav_error_set(err, "internal", "missing accounts array", 0);
            return -1;
        }
        acct = account_find(accounts, id);
        if (acct == NULL) {
            snprintf(buf, sizeof(buf), "account %s not found", id);
            pav_error_set(err, "not_found", buf, 0);
            return -1;
        }
        json_object_foreach(patch, pkey, pval) {
            if (strcmp(pkey, "caFile") == 0 && json_string_value(pval)[0] == '\0') {
                json_object_del(acct, "caFile");
                continue;
            }
            if (json_object_set(acct, pkey, pval) != 0) {
                pav_error_set(err, "internal", "out of memory", 0);
                return -1;
            }
        }

        errbuf[0] = '\0';
        if (pav_profile_store(load->profile_path, load->profile, errbuf, sizeof(errbuf)) != 0) {
            pav_error_set(err, "internal", errbuf[0] != '\0' ? errbuf : "cannot write profile", 0);
            return -1;
        }

        result = json_deep_copy(acct);
        if (result == NULL) {
            pav_error_set(err, "internal", "out of memory", 0);
            emit_accounts_changed(accounts);
            return -1;
        }
        *data = result;
        emit_accounts_changed(accounts);
        return 0;
    }
}

int pav_account_set_secret(json_t *args, json_t **data, PavError *err) {
    const char *id;
    const char *secret;
    const char *key;
    json_t *value;
    char buf[256];
    char keyring_user[256];
    int any = 0;
    int secret_set = 0;
    int rc;
    json_t *result;

    if (data == NULL) {
        pav_error_set(err, "internal", "missing data", 0);
        return -1;
    }
    *data = NULL;

    json_object_foreach(args, key, value) {
        any = 1;
        if (strcmp(key, "id") == 0) {
            if (!json_is_string(value) || json_string_value(value)[0] == '\0' ||
                strlen(json_string_value(value)) >= PAV_ACCOUNT_ID_MAX) {
                pav_error_set(err, "bad_args", "id must be a non-empty string", 0);
                return -1;
            }
            id = json_string_value(value);
        } else if (strcmp(key, "secret") == 0) {
            if (!json_is_string(value)) {
                pav_error_set(err, "bad_args", "secret must be a string", 0);
                return -1;
            }
            secret = json_string_value(value);
            secret_set = 1;
        } else {
            snprintf(buf, sizeof(buf), "unknown argument %s", key);
            pav_error_set(err, "bad_args", buf, 0);
            return -1;
        }
    }
    if (!any) {
        pav_error_set(err, "bad_args", "nothing to set", 0);
        return -1;
    }
    if (!secret_set) {
        pav_error_set(err, "bad_args", "secret is required", 0);
        return -1;
    }

    if (!profile_is_ready(err)) {
        return -1;
    }

    {
        PavConfigLoad *load = pav_core_load();
        json_t *accounts = json_object_get((json_t *)load->profile->doc, "accounts");

        if (accounts == NULL || !json_is_array(accounts)) {
            pav_error_set(err, "internal", "missing accounts array", 0);
            return -1;
        }
        if (!account_has(accounts, id)) {
            snprintf(buf, sizeof(buf), "account %s not found", id);
            pav_error_set(err, "not_found", buf, 0);
            return -1;
        }

        snprintf(keyring_user, sizeof(keyring_user), "%s/%s", load->profile->id, id);
        rc = pav_secret_set("pavillion", keyring_user, "account", secret);
        if (rc == PAV_SECRET_LOCKED) {
            pav_error_set(err, "locked", "keyring is locked", 0);
            return -1;
        }
        if (rc != 0) {
            pav_error_set(err, "failed", "cannot store secret", 0);
            return -1;
        }

        result = json_object();
        if (result == NULL || json_object_set_new(result, "id", json_string(id)) != 0) {
            json_decref(result);
            pav_error_set(err, "internal", "out of memory", 0);
            return -1;
        }
        *data = result;
        emit_accounts_changed(accounts);
        return 0;
    }
}

int pav_account_saved_secret(const char *id, char **out, PavError *err) {
    PavConfigLoad *load;
    json_t *accounts;
    char keyring_user[256];
    char buf[160];
    int rc;

    *out = NULL;
    if (!profile_is_ready(err)) {
        return -1;
    }
    load = pav_core_load();
    accounts = json_object_get((json_t *)load->profile->doc, "accounts");
    if (accounts == NULL || !json_is_array(accounts) || !account_has(accounts, id)) {
        snprintf(buf, sizeof(buf), "account %.100s not found", id);
        pav_error_set(err, "not_found", buf, 0);
        return -1;
    }
    snprintf(keyring_user, sizeof(keyring_user), "%s/%s", load->profile->id, id);
    rc = pav_secret_get("pavillion", keyring_user, out);
    if (rc == PAV_SECRET_LOCKED) {
        pav_error_set(err, "locked", "keyring is locked", 0);
        return -1;
    }
    if (rc != 0 || *out == NULL || (*out)[0] == '\0') {
        if (*out != NULL) {
            pav_secret_free(*out);
            *out = NULL;
        }
        pav_error_set(err, "bad_args", "no saved password for this account", 0);
        return -1;
    }
    return 0;
}

/* N-1. Fetch one message body, cache the render text, and answer message.get.
   htmlSanitized stays null until N-2. Attachment bytes stay on the server. */

#define _POSIX_C_SOURCE 200809L

#include "pav_body.h"

#include "dispatch.h"
#include "pav_activity.h"
#include "pav_blob.h"
#include "pav_db.h"
#include "pav_jmap.h"
#include "pav_mime.h"
#include "pav_platform.h"
#include "pav_sync.h"
#include "pavillion.h"

#include <jansson.h>
#include <sqlite3.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BODY_CAP 1024
#define SLOT_MAX 32
#define FLIGHT_MAX 8
#define RECENT_MS (30LL * 24LL * 3600LL * 1000LL)
#define MAIL_URN "urn:ietf:params:jmap:mail"
#define CORE_URN "urn:ietf:params:jmap:core"

typedef struct BodyJob BodyJob;

struct BodyJob {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int wait;
    int done;
    int cancelled;
    int status;
    int auth_rc;
    int stage;
    int64_t activity;
    int64_t rowid;
    char account_id[64];
    char ra[256];
    char email_id[256];
    char jmap_id[128];
    char blob_id[256];
    char api_url[2048];
    char download_url[2048];
    char username[256];
    char auth[32];
    char ca_file[1024];
    char keyring_user[192];
    char final_path[4096];
    char tmp_path[4096];
    char *request;
    char *copied;
    size_t copied_len;
    PavReq req;
};

static pthread_mutex_t g_slot_mu = PTHREAD_MUTEX_INITIALIZER;
static struct {
    char id[64];
    int users;
    int inflight;
    int used;
} g_slots[SLOT_MAX];

static pthread_mutex_t g_flight_mu = PTHREAD_MUTEX_INITIALIZER;
static struct {
    int64_t activity;
    PavReq *req;
} g_flights[FLIGHT_MAX];

static char *dup_text(const char *text) {
    size_t n;
    char *out;

    if (text == NULL) {
        text = "";
    }
    n = strlen(text);
    out = malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, text, n + 1);
    return out;
}

static char *dup_cap(const char *text, size_t max) {
    size_t n;
    char *out;

    if (text == NULL) {
        text = "";
    }
    n = strlen(text);
    if (n > max) {
        n = max;
        while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80) {
            n--;
        }
    }
    out = malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, text, n);
    out[n] = '\0';
    return out;
}

static int copy_fit(char *dst, size_t n, const char *src) {
    size_t len;

    if (dst == NULL || n == 0) {
        return -1;
    }
    if (src == NULL) {
        src = "";
    }
    len = strlen(src);
    if (len >= n) {
        dst[0] = '\0';
        return -1;
    }
    memcpy(dst, src, len + 1);
    return 0;
}

static const char *profile_account_mode(const char *account_id) {
    PavConfigLoad *load = pav_core_load();
    json_t *accounts;
    size_t i;

    if (load == NULL || load->profile == NULL || load->profile->doc == NULL) {
        return "recent";
    }
    accounts = json_object_get((json_t *)load->profile->doc, "accounts");
    if (!json_is_array(accounts)) {
        return "recent";
    }
    for (i = 0; i < json_array_size(accounts); i++) {
        json_t *acct = json_array_get(accounts, i);
        const char *id = json_string_value(json_object_get(acct, "id"));
        const char *mode;

        if (id == NULL || strcmp(id, account_id) != 0) {
            continue;
        }
        mode = json_string_value(json_object_get(acct, "fetchBodies"));
        if (mode == NULL || mode[0] == '\0') {
            return "recent";
        }
        if (strcmp(mode, "none") == 0 || strcmp(mode, "all") == 0 || strcmp(mode, "recent") == 0) {
            return mode;
        }
        return "recent";
    }
    return "recent";
}

static int profile_account_copy(const char *account_id, char *user, size_t user_n, char *auth, size_t auth_n,
                                char *ca, size_t ca_n, char *keyring, size_t key_n) {
    PavConfigLoad *load = pav_core_load();
    json_t *accounts;
    size_t i;
    const char *auth_in;

    if (load == NULL || load->profile == NULL || load->profile->doc == NULL || load->profile->id[0] == '\0') {
        return -1;
    }
    accounts = json_object_get((json_t *)load->profile->doc, "accounts");
    if (!json_is_array(accounts)) {
        return -1;
    }
    for (i = 0; i < json_array_size(accounts); i++) {
        json_t *acct = json_array_get(accounts, i);
        const char *id = json_string_value(json_object_get(acct, "id"));

        if (id == NULL || strcmp(id, account_id) != 0) {
            continue;
        }
        if (copy_fit(user, user_n, json_string_value(json_object_get(acct, "username"))) != 0) {
            return -1;
        }
        auth_in = json_string_value(json_object_get(acct, "auth"));
        if (copy_fit(auth, auth_n, auth_in != NULL && auth_in[0] != '\0' ? auth_in : "basic") != 0) {
            return -1;
        }
        if (copy_fit(ca, ca_n, json_string_value(json_object_get(acct, "caFile"))) != 0) {
            return -1;
        }
        if ((size_t)snprintf(keyring, key_n, "%s/%s", load->profile->id, account_id) >= key_n) {
            return -1;
        }
        return 0;
    }
    return -1;
}

static int slot_index(const char *account_id, int create) {
    int i;
    int free_i = -1;

    for (i = 0; i < SLOT_MAX; i++) {
        if (g_slots[i].used && strcmp(g_slots[i].id, account_id) == 0) {
            return i;
        }
        if (!g_slots[i].used && free_i < 0) {
            free_i = i;
        }
    }
    if (!create || free_i < 0) {
        return -1;
    }
    memset(&g_slots[free_i], 0, sizeof(g_slots[free_i]));
    if (copy_fit(g_slots[free_i].id, sizeof(g_slots[free_i].id), account_id) != 0) {
        return -1;
    }
    g_slots[free_i].used = 1;
    return free_i;
}

static void slot_user(const char *account_id, int delta) {
    int i;

    pthread_mutex_lock(&g_slot_mu);
    i = slot_index(account_id, delta > 0);
    if (i >= 0) {
        g_slots[i].users += delta;
        if (g_slots[i].users < 0) {
            g_slots[i].users = 0;
        }
        if (g_slots[i].users == 0 && g_slots[i].inflight == 0) {
            g_slots[i].used = 0;
        }
    }
    pthread_mutex_unlock(&g_slot_mu);
}

static int slot_users(const char *account_id) {
    int i;
    int n = 0;

    pthread_mutex_lock(&g_slot_mu);
    i = slot_index(account_id, 0);
    if (i >= 0) {
        n = g_slots[i].users;
    }
    pthread_mutex_unlock(&g_slot_mu);
    return n;
}

static int slot_begin_prefetch(const char *account_id) {
    int i;
    int ok = 0;

    pthread_mutex_lock(&g_slot_mu);
    i = slot_index(account_id, 1);
    if (i >= 0 && g_slots[i].users == 0 && g_slots[i].inflight < PAV_NET_PER_ACCOUNT_CAP) {
        g_slots[i].inflight++;
        ok = 1;
    }
    pthread_mutex_unlock(&g_slot_mu);
    return ok;
}

static void slot_end_prefetch(const char *account_id) {
    int i;

    pthread_mutex_lock(&g_slot_mu);
    i = slot_index(account_id, 0);
    if (i >= 0 && g_slots[i].inflight > 0) {
        g_slots[i].inflight--;
        if (g_slots[i].users == 0 && g_slots[i].inflight == 0) {
            g_slots[i].used = 0;
        }
    }
    pthread_mutex_unlock(&g_slot_mu);
}

static void flight_add(int64_t activity, PavReq *req) {
    int i;

    if (activity <= 0 || req == NULL) {
        return;
    }
    pthread_mutex_lock(&g_flight_mu);
    for (i = 0; i < FLIGHT_MAX; i++) {
        if (g_flights[i].req == NULL) {
            g_flights[i].activity = activity;
            g_flights[i].req = req;
            break;
        }
    }
    pthread_mutex_unlock(&g_flight_mu);
}

static void flight_del(int64_t activity) {
    int i;

    pthread_mutex_lock(&g_flight_mu);
    for (i = 0; i < FLIGHT_MAX; i++) {
        if (g_flights[i].activity == activity) {
            g_flights[i].req = NULL;
            g_flights[i].activity = 0;
        }
    }
    pthread_mutex_unlock(&g_flight_mu);
}

void pav_body_cancel(int64_t activity_id) {
    int i;
    PavReq *req = NULL;

    pthread_mutex_lock(&g_flight_mu);
    for (i = 0; i < FLIGHT_MAX; i++) {
        if (g_flights[i].activity == activity_id && g_flights[i].req != NULL) {
            req = g_flights[i].req;
            req->cancelled = 1;
            break;
        }
    }
    pthread_mutex_unlock(&g_flight_mu);
    if (req != NULL) {
        pav_net_wakeup();
    }
}

char *pav_body_email_get_request(const char *jmap_account, const char *email_id) {
    json_t *root;
    json_t *using;
    json_t *calls;
    json_t *call;
    json_t *args;
    json_t *props;
    json_t *ids;
    char *text;
    static const char *names[] = {
        "id", "blobId", "textBody", "htmlBody", "bodyStructure", "bodyValues", "attachments", "hasAttachment",
        "header:List-Id:asText", "header:Authentication-Results:asText"};
    size_t i;

    if (jmap_account == NULL || jmap_account[0] == '\0' || email_id == NULL || email_id[0] == '\0') {
        return NULL;
    }
    root = json_object();
    using = json_array();
    calls = json_array();
    call = json_array();
    args = json_object();
    props = json_array();
    ids = json_array();
    if (root == NULL || using == NULL || calls == NULL || call == NULL || args == NULL || props == NULL ||
        ids == NULL) {
        json_decref(root);
        json_decref(using);
        json_decref(calls);
        json_decref(call);
        json_decref(args);
        json_decref(props);
        json_decref(ids);
        return NULL;
    }
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (json_array_append_new(props, json_string(names[i])) != 0) {
            json_decref(root);
            json_decref(using);
            json_decref(calls);
            json_decref(call);
            json_decref(args);
            json_decref(props);
            json_decref(ids);
            return NULL;
        }
    }
    if (json_array_append_new(using, json_string(CORE_URN)) != 0 ||
        json_array_append_new(using, json_string(MAIL_URN)) != 0 ||
        json_array_append_new(ids, json_string(email_id)) != 0 ||
        json_object_set_new(args, "accountId", json_string(jmap_account)) != 0) {
        goto fail;
    }
    if (json_object_set_new(args, "ids", ids) != 0) {
        ids = NULL;
        goto fail;
    }
    ids = NULL;
    if (json_object_set_new(args, "properties", props) != 0) {
        props = NULL;
        goto fail;
    }
    props = NULL;
    if (json_object_set_new(args, "fetchTextBodyValues", json_true()) != 0 ||
        json_object_set_new(args, "fetchHTMLBodyValues", json_true()) != 0 ||
        json_object_set_new(args, "maxBodyValueBytes", json_integer(2097152)) != 0 ||
        json_array_append_new(call, json_string("Email/get")) != 0) {
        goto fail;
    }
    if (json_array_append_new(call, args) != 0) {
        args = NULL;
        goto fail;
    }
    args = NULL;
    if (json_array_append_new(call, json_string("b1")) != 0) {
        goto fail;
    }
    if (json_array_append_new(calls, call) != 0) {
        call = NULL;
        goto fail;
    }
    call = NULL;
    if (json_object_set_new(root, "using", using) != 0) {
        using = NULL;
        goto fail;
    }
    using = NULL;
    if (json_object_set_new(root, "methodCalls", calls) != 0) {
        calls = NULL;
        goto fail;
    }
    text = json_dumps(root, JSON_COMPACT);
    json_decref(root);
    return text;

fail:
    json_decref(ids);
    json_decref(props);
    json_decref(args);
    json_decref(call);
    json_decref(calls);
    json_decref(using);
    json_decref(root);
    return NULL;
}

static int pct_append(char *out, size_t len, size_t *used, const char *text) {
    static const char *hex = "0123456789ABCDEF";
    size_t o = *used;

    if (text == NULL) {
        text = "";
    }
    for (; *text != '\0'; text++) {
        unsigned char c = (unsigned char)*text;
        int safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                   c == '_' || c == '.' || c == '~';

        if (safe) {
            if (o + 1 >= len) {
                return -1;
            }
            out[o++] = (char)c;
        } else {
            if (o + 3 >= len) {
                return -1;
            }
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 15];
        }
    }
    out[o] = '\0';
    *used = o;
    return 0;
}

static int append_raw(char *out, size_t len, size_t *used, const char *text, size_t n) {
    if (*used + n >= len) {
        return -1;
    }
    memcpy(out + *used, text, n);
    *used += n;
    out[*used] = '\0';
    return 0;
}

static int build_download(char *out, size_t len, const char *templ, const char *account, const char *blob) {
    size_t i = 0;
    size_t o = 0;
    int saw = 0;

    if (templ == NULL || templ[0] == '\0' || account == NULL || blob == NULL || blob[0] == '\0') {
        return -1;
    }
    while (templ[i] != '\0') {
        const char *key = NULL;
        const char *value = NULL;
        size_t key_n = 0;

        if (templ[i] == '{') {
            if (strncmp(templ + i, "{accountId}", 11) == 0) {
                key = "{accountId}";
                key_n = 11;
                value = account;
            } else if (strncmp(templ + i, "{blobId}", 8) == 0) {
                key = "{blobId}";
                key_n = 8;
                value = blob;
            } else if (strncmp(templ + i, "{name}", 6) == 0) {
                key = "{name}";
                key_n = 6;
                value = "message.eml";
            } else if (strncmp(templ + i, "{type}", 6) == 0) {
                key = "{type}";
                key_n = 6;
                value = "message/rfc822";
            }
        }
        if (key != NULL) {
            saw = 1;
            if (pct_append(out, len, &o, value) != 0) {
                return -1;
            }
            i += key_n;
            continue;
        }
        if (append_raw(out, len, &o, templ + i, 1) != 0) {
            return -1;
        }
        i++;
    }
    if (!saw) {
        if (append_raw(out, len, &o, "/", 1) != 0 || pct_append(out, len, &o, account) != 0 ||
            append_raw(out, len, &o, "/", 1) != 0 || pct_append(out, len, &o, blob) != 0 ||
            append_raw(out, len, &o, "/message.eml?accept=", 20) != 0 ||
            pct_append(out, len, &o, "message/rfc822") != 0) {
            return -1;
        }
    }
    return out[0] != '\0' ? 0 : -1;
}

typedef struct StoreCtx {
    int64_t rowid;
    const char *ra;
    const char *list_id;
    const char *auth;
    const char *text_path;
    const char *html_path;
    const PavMimeDoc *doc;
    int headers_only;
    int64_t now_ms;
    const char *state;
} StoreCtx;

static int store_job(void *ctx, sqlite3 *db) {
    StoreCtx *s = ctx;
    sqlite3_stmt *stmt = NULL;
    int i;

    if (s->headers_only) {
        if (pav_db_prepare(db, "body.headers", "UPDATE email SET list_id = ?, auth_summary = ? WHERE rowid = ?",
                           &stmt) != 0 ||
            pav_db_bind_text(stmt, 1, s->list_id) != 0 || pav_db_bind_text(stmt, 2, s->auth) != 0 ||
            pav_db_bind_int64(stmt, 3, s->rowid) != 0 || pav_db_step(stmt) != SQLITE_DONE) {
            return -1;
        }
        return 0;
    }
    if (pav_db_prepare(db, "body.email",
                       "UPDATE email SET list_id = ?, auth_summary = ?, body_state = 'fetched' WHERE rowid = ?",
                       &stmt) != 0 ||
        pav_db_bind_text(stmt, 1, s->list_id) != 0 || pav_db_bind_text(stmt, 2, s->auth) != 0 ||
        pav_db_bind_int64(stmt, 3, s->rowid) != 0 || pav_db_step(stmt) != SQLITE_DONE) {
        return -1;
    }
    stmt = NULL;
    if (pav_db_prepare(db, "body.cache",
                       "INSERT INTO body_cache (email_rowid, text_plain_path, html_raw_path, html_sanitized_path, "
                       "has_remote, sanitizer_version, fetched_at) VALUES (?, ?, ?, NULL, 0, 0, ?) "
                       "ON CONFLICT(email_rowid) DO UPDATE SET text_plain_path = excluded.text_plain_path, "
                       "html_raw_path = excluded.html_raw_path, html_sanitized_path = NULL, has_remote = 0, "
                       "sanitizer_version = 0, fetched_at = excluded.fetched_at",
                       &stmt) != 0 ||
        pav_db_bind_int64(stmt, 1, s->rowid) != 0 || pav_db_bind_text(stmt, 2, s->text_path) != 0 ||
        pav_db_bind_text(stmt, 3, s->html_path) != 0 || pav_db_bind_int64(stmt, 4, s->now_ms) != 0 ||
        pav_db_step(stmt) != SQLITE_DONE) {
        return -1;
    }
    stmt = NULL;
    if (pav_db_prepare(db, "body.attach.clear", "DELETE FROM attachment WHERE email_rowid = ?", &stmt) != 0 ||
        pav_db_bind_int64(stmt, 1, s->rowid) != 0 || pav_db_step(stmt) != SQLITE_DONE) {
        return -1;
    }
    if (s->doc == NULL) {
        return 0;
    }
    for (i = 0; i < s->doc->part_n; i++) {
        const PavMimePart *part = &s->doc->parts[i];

        stmt = NULL;
        if (pav_db_prepare(db, "body.attach.insert",
                           "INSERT INTO attachment (email_rowid, part_id, blob_id, name, mime, size, cid, "
                           "disposition, local_path) VALUES (?, ?, NULL, ?, ?, ?, ?, ?, NULL)",
                           &stmt) != 0 ||
            pav_db_bind_int64(stmt, 1, s->rowid) != 0 || pav_db_bind_text(stmt, 2, part->part_id) != 0 ||
            pav_db_bind_text(stmt, 3, part->name) != 0 || pav_db_bind_text(stmt, 4, part->mime) != 0 ||
            pav_db_bind_int64(stmt, 5, part->size) != 0 || pav_db_bind_text(stmt, 6, part->cid) != 0 ||
            pav_db_bind_text(stmt, 7, part->disposition) != 0 || pav_db_step(stmt) != SQLITE_DONE) {
            return -1;
        }
    }
    (void)s->state;
    return 0;
}

static int write_body_file(const char *ra, int64_t rowid, const char *kind, const char *text, char *path,
                           size_t path_n) {
    char id[64];
    int n;

    n = snprintf(id, sizeof(id), "body-%s:%lld", kind, (long long)rowid);
    if (n < 0 || (size_t)n >= sizeof(id)) {
        return -1;
    }
    if (text == NULL || text[0] == '\0') {
        (void)pav_blob_remove(ra, id);
        if (path != NULL && path_n > 0) {
            path[0] = '\0';
        }
        return 0;
    }
    if (pav_blob_write(ra, id, text, strlen(text)) != 0) {
        return -1;
    }
    if (path != NULL && pav_blob_path(ra, id, path, path_n) != 0) {
        return -1;
    }
    return 0;
}

static int store_doc(int64_t rowid, const char *ra, const PavMimeDoc *doc, int headers_only) {
    StoreCtx ctx;
    char text_path[4096];
    char html_path[4096];
    char *list_id;
    char *auth;
    int rc;

    if (doc == NULL || ra == NULL || ra[0] == '\0' || rowid <= 0) {
        return -1;
    }
    list_id = dup_cap(doc->list_id, BODY_CAP);
    auth = dup_cap(doc->auth_summary, BODY_CAP);
    if (list_id == NULL || auth == NULL) {
        free(list_id);
        free(auth);
        return -1;
    }
    memset(&ctx, 0, sizeof(ctx));
    ctx.rowid = rowid;
    ctx.ra = ra;
    ctx.list_id = list_id;
    ctx.auth = auth;
    ctx.doc = doc;
    ctx.headers_only = headers_only;
    ctx.now_ms = pav_now_ms();
    if (ctx.now_ms < 0) {
        ctx.now_ms = 0;
    }
    text_path[0] = '\0';
    html_path[0] = '\0';
    if (!headers_only) {
        if (write_body_file(ra, rowid, "text", doc->text, text_path, sizeof(text_path)) != 0 ||
            write_body_file(ra, rowid, "html", doc->html, html_path, sizeof(html_path)) != 0) {
            free(list_id);
            free(auth);
            return -1;
        }
        ctx.text_path = text_path[0] != '\0' ? text_path : NULL;
        ctx.html_path = html_path[0] != '\0' ? html_path : NULL;
    }
    rc = pav_db_call(store_job, &ctx);
    free(list_id);
    free(auth);
    return rc;
}

static int part_skipped(json_t *skip, const char *part_id) {
    return part_id != NULL && json_object_get(skip, part_id) != NULL;
}

static int add_json_part(PavMimeDoc *doc, json_t *part) {
    const char *part_id = json_string_value(json_object_get(part, "partId"));
    const char *name = json_string_value(json_object_get(part, "name"));
    const char *mime = json_string_value(json_object_get(part, "type"));
    const char *cid = json_string_value(json_object_get(part, "cid"));
    const char *disp = json_string_value(json_object_get(part, "disposition"));
    json_t *size = json_object_get(part, "size");
    char *cid_copy = NULL;
    int rc;

    if (part_id == NULL || part_id[0] == '\0') {
        part_id = "0";
    }
    if (cid != NULL && cid[0] == '<' && cid[strlen(cid) - 1] == '>') {
        cid_copy = dup_cap(cid + 1, strlen(cid));
        if (cid_copy != NULL && cid_copy[0] != '\0') {
            cid_copy[strlen(cid_copy) - 1] = '\0';
        }
        cid = cid_copy;
    }
    rc = 0;
    {
        PavMimePart *grown = realloc(doc->parts, (size_t)(doc->part_n + 1) * sizeof(*grown));
        PavMimePart *slot;

        if (grown == NULL) {
            free(cid_copy);
            return -1;
        }
        doc->parts = grown;
        slot = &doc->parts[doc->part_n];
        memset(slot, 0, sizeof(*slot));
        slot->part_id = dup_text(part_id);
        slot->name = dup_text(name != NULL ? name : "");
        slot->mime = dup_text(mime != NULL ? mime : "application/octet-stream");
        slot->size = json_is_integer(size) ? (int64_t)json_integer_value(size) : 0;
        slot->cid = cid != NULL && cid[0] != '\0' ? dup_text(cid) : NULL;
        slot->disposition = disp != NULL && disp[0] != '\0' ? dup_text(disp) : NULL;
        if (slot->part_id == NULL || slot->name == NULL || slot->mime == NULL) {
            rc = -1;
        } else {
            doc->part_n++;
        }
    }
    free(cid_copy);
    return rc;
}

static int walk_structure(PavMimeDoc *doc, json_t *part, json_t *skip) {
    json_t *subs;
    const char *part_id;

    if (!json_is_object(part)) {
        return 0;
    }
    subs = json_object_get(part, "subParts");
    if (json_is_array(subs) && json_array_size(subs) > 0) {
        size_t i;

        for (i = 0; i < json_array_size(subs); i++) {
            if (walk_structure(doc, json_array_get(subs, i), skip) != 0) {
                return -1;
            }
        }
        return 0;
    }
    part_id = json_string_value(json_object_get(part, "partId"));
    if (part_skipped(skip, part_id)) {
        return 0;
    }
    return add_json_part(doc, part);
}

static int body_value_bad(json_t *values, const char *part_id) {
    json_t *one;
    json_t *truncated;
    json_t *problem;

    if (part_id == NULL) {
        return 1;
    }
    one = json_object_get(values, part_id);
    if (!json_is_object(one) || !json_is_string(json_object_get(one, "value"))) {
        return 1;
    }
    truncated = json_object_get(one, "isTruncated");
    problem = json_object_get(one, "isEncodingProblem");
    if (json_is_true(truncated) || json_is_true(problem)) {
        return 1;
    }
    return 0;
}

static const char *first_value(json_t *values, json_t *parts) {
    size_t i;

    if (!json_is_array(parts)) {
        return NULL;
    }
    for (i = 0; i < json_array_size(parts); i++) {
        const char *part_id = json_string_value(json_object_get(json_array_get(parts, i), "partId"));
        json_t *one = part_id != NULL ? json_object_get(values, part_id) : NULL;
        const char *value = json_string_value(json_object_get(one, "value"));

        if (value != NULL) {
            return value;
        }
    }
    return NULL;
}

static int note_part_ids(json_t *skip, json_t *parts) {
    size_t i;

    if (!json_is_array(parts)) {
        return 0;
    }
    for (i = 0; i < json_array_size(parts); i++) {
        const char *part_id = json_string_value(json_object_get(json_array_get(parts, i), "partId"));

        if (part_id != NULL && json_object_set_new(skip, part_id, json_true()) != 0) {
            return -1;
        }
    }
    return 0;
}

int pav_body_apply_jmap(int64_t email_rowid, const char *ra, const char *email_json) {
    json_error_t jerr;
    json_t *email;
    json_t *values;
    json_t *text_parts;
    json_t *html_parts;
    json_t *skip = NULL;
    PavMimeDoc doc;
    int need_raw = 0;
    int rc;
    size_t i;

    if (email_json == NULL) {
        return -1;
    }
    email = json_loads(email_json, 0, &jerr);
    if (!json_is_object(email)) {
        json_decref(email);
        return -1;
    }
    memset(&doc, 0, sizeof(doc));
    doc.list_id = dup_cap(json_string_value(json_object_get(email, "header:List-Id:asText")), BODY_CAP);
    doc.auth_summary =
        dup_cap(json_string_value(json_object_get(email, "header:Authentication-Results:asText")), BODY_CAP);
    if (doc.list_id == NULL || doc.auth_summary == NULL) {
        pav_mime_free(&doc);
        json_decref(email);
        return -1;
    }
    values = json_object_get(email, "bodyValues");
    text_parts = json_object_get(email, "textBody");
    html_parts = json_object_get(email, "htmlBody");
    if (json_is_array(text_parts)) {
        for (i = 0; i < json_array_size(text_parts); i++) {
            const char *part_id = json_string_value(json_object_get(json_array_get(text_parts, i), "partId"));

            if (body_value_bad(values, part_id)) {
                need_raw = 1;
            }
        }
    }
    if (json_is_array(html_parts)) {
        for (i = 0; i < json_array_size(html_parts); i++) {
            const char *part_id = json_string_value(json_object_get(json_array_get(html_parts, i), "partId"));

            if (body_value_bad(values, part_id)) {
                need_raw = 1;
            }
        }
    }
    if (need_raw) {
        rc = store_doc(email_rowid, ra, &doc, 1);
        pav_mime_free(&doc);
        json_decref(email);
        return rc == 0 ? 1 : -1;
    }
    doc.text = dup_cap(first_value(values, text_parts), PAV_MIME_BODY_MAX);
    doc.html = dup_cap(first_value(values, html_parts), PAV_MIME_BODY_MAX);
    if (doc.text == NULL) {
        doc.text = dup_text("");
    }
    if (doc.text == NULL || (first_value(values, html_parts) != NULL && doc.html == NULL)) {
        pav_mime_free(&doc);
        json_decref(email);
        return -1;
    }
    skip = json_object();
    if (skip == NULL || note_part_ids(skip, text_parts) != 0 || note_part_ids(skip, html_parts) != 0 ||
        walk_structure(&doc, json_object_get(email, "bodyStructure"), skip) != 0) {
        json_decref(skip);
        pav_mime_free(&doc);
        json_decref(email);
        return -1;
    }
    json_decref(skip);
    rc = store_doc(email_rowid, ra, &doc, 0);
    pav_mime_free(&doc);
    json_decref(email);
    return rc == 0 ? 0 : -1;
}

int pav_body_store_bytes(int64_t email_rowid, const char *ra, const void *bytes, size_t len) {
    PavMimeDoc doc;
    int rc;

    if (pav_mime_parse(bytes, len, &doc) != 0) {
        return -1;
    }
    rc = store_doc(email_rowid, ra, &doc, 0);
    pav_mime_free(&doc);
    return rc;
}

static int state_job(void *ctx, sqlite3 *db) {
    StoreCtx *s = ctx;
    sqlite3_stmt *stmt = NULL;

    if (pav_db_prepare(db, "body.state", "UPDATE email SET body_state = ? WHERE rowid = ?", &stmt) != 0 ||
        pav_db_bind_text(stmt, 1, s->state) != 0 || pav_db_bind_int64(stmt, 2, s->rowid) != 0 ||
        pav_db_step(stmt) != SQLITE_DONE) {
        return -1;
    }
    return 0;
}

static void mark_state(int64_t rowid, const char *state) {
    StoreCtx ctx;

    memset(&ctx, 0, sizeof(ctx));
    ctx.rowid = rowid;
    ctx.state = state;
    (void)pav_db_call(state_job, &ctx);
}

static int open_job(void *ctx, sqlite3 *db) {
    StoreCtx *s = ctx;
    sqlite3_stmt *stmt = NULL;

    if (pav_db_prepare(db, "body.opened", "UPDATE email SET last_opened = ? WHERE rowid = ?", &stmt) != 0 ||
        pav_db_bind_int64(stmt, 1, s->now_ms) != 0 || pav_db_bind_int64(stmt, 2, s->rowid) != 0 ||
        pav_db_step(stmt) != SQLITE_DONE) {
        return -1;
    }
    return 0;
}

static void touch_opened(int64_t rowid) {
    StoreCtx ctx;

    memset(&ctx, 0, sizeof(ctx));
    ctx.rowid = rowid;
    ctx.now_ms = pav_now_ms();
    if (ctx.now_ms < 0) {
        ctx.now_ms = 0;
    }
    (void)pav_db_call(open_job, &ctx);
}

int pav_body_select(const char *account_id, const char *mode, int64_t now_ms, int limit, int64_t *out, int *out_n) {
    PavDbRead *read = NULL;
    sqlite3_stmt *stmt = NULL;
    int64_t cutoff;
    int n = 0;
    static const char k_sql[] =
        "SELECT e.rowid FROM email e JOIN remote_account r ON r.id = e.ra_id "
        "WHERE r.account_id = ? AND e.body_state IN ('none', 'evicted') "
        "AND (? = 'all' OR (? = 'recent' AND IFNULL(e.received_at, 0) >= ?)) "
        "ORDER BY e.received_at DESC, e.rowid DESC LIMIT ?";

    if (out_n == NULL) {
        return -1;
    }
    *out_n = 0;
    if (account_id == NULL || mode == NULL || out == NULL || limit <= 0) {
        return -1;
    }
    if (strcmp(mode, "none") == 0) {
        return 0;
    }
    if (strcmp(mode, "recent") != 0 && strcmp(mode, "all") != 0) {
        return -1;
    }
    cutoff = now_ms - RECENT_MS;
    if (pav_db_read_begin(&read) != 0) {
        return -1;
    }
    if (pav_db_prepare(pav_db_read_handle(read), "body.select", k_sql, &stmt) != 0 ||
        pav_db_bind_text(stmt, 1, account_id) != 0 || pav_db_bind_text(stmt, 2, mode) != 0 ||
        pav_db_bind_text(stmt, 3, mode) != 0 || pav_db_bind_int64(stmt, 4, cutoff) != 0 ||
        pav_db_bind_int(stmt, 5, limit) != 0) {
        pav_db_read_end(read);
        return -1;
    }
    while (pav_db_step(stmt) == SQLITE_ROW && n < limit) {
        out[n++] = sqlite3_column_int64(stmt, 0);
    }
    pav_db_read_end(read);
    *out_n = n;
    return 0;
}

typedef struct MailRow {
    int found;
    int cache;
    int64_t rowid;
    int64_t sent_at;
    int64_t received_at;
    int sent_null;
    int received_null;
    int draft;
    char ra[256];
    char email_id[256];
    char blob_id[256];
    char account_id[64];
    char jmap_id[128];
    char body_state[32];
    char *message_id;
    char *subject;
    char *from_json;
    char *to_json;
    char *cc_json;
    char *bcc_json;
    char *reply_json;
    char *list_id;
    char *auth;
    char *session_json;
    char *text_path;
    char *html_path;
} MailRow;

static void mail_row_free(MailRow *row) {
    if (row == NULL) {
        return;
    }
    free(row->message_id);
    free(row->subject);
    free(row->from_json);
    free(row->to_json);
    free(row->cc_json);
    free(row->bcc_json);
    free(row->reply_json);
    free(row->list_id);
    free(row->auth);
    free(row->session_json);
    free(row->text_path);
    free(row->html_path);
}

static char *col_dup(sqlite3_stmt *stmt, int col) {
    const char *text = (const char *)sqlite3_column_text(stmt, col);

    if (text == NULL) {
        return NULL;
    }
    return dup_text(text);
}

static int load_row(const char *wire_id, MailRow *row) {
    PavDbRead *read = NULL;
    sqlite3_stmt *stmt = NULL;
    static const char k_sql[] =
        "SELECT e.rowid, e.ra_id, e.id, e.blob_id, e.message_id, e.subject, e.from_json, e.to_json, "
        "e.cc_json, e.bcc_json, e.reply_to_json, e.sent_at, e.received_at, e.draft, e.body_state, "
        "e.list_id, e.auth_summary, r.account_id, r.jmap_account_id, s.session_json, "
        "b.text_plain_path, b.html_raw_path "
        "FROM email e JOIN remote_account r ON r.id = e.ra_id "
        "LEFT JOIN account_state s ON s.account_id = r.account_id "
        "LEFT JOIN body_cache b ON b.email_rowid = e.rowid "
        "WHERE ('msg:' || e.ra_id || ':' || e.id) = ?";

    memset(row, 0, sizeof(*row));
    if (pav_db_read_begin(&read) != 0) {
        return -1;
    }
    if (pav_db_prepare(pav_db_read_handle(read), "body.row", k_sql, &stmt) != 0 ||
        pav_db_bind_text(stmt, 1, wire_id) != 0) {
        pav_db_read_end(read);
        return -1;
    }
    if (pav_db_step(stmt) != SQLITE_ROW) {
        pav_db_read_end(read);
        return 0;
    }
    row->found = 1;
    row->rowid = sqlite3_column_int64(stmt, 0);
    copy_fit(row->ra, sizeof(row->ra), (const char *)sqlite3_column_text(stmt, 1));
    copy_fit(row->email_id, sizeof(row->email_id), (const char *)sqlite3_column_text(stmt, 2));
    copy_fit(row->blob_id, sizeof(row->blob_id), (const char *)sqlite3_column_text(stmt, 3));
    row->message_id = col_dup(stmt, 4);
    row->subject = col_dup(stmt, 5);
    row->from_json = col_dup(stmt, 6);
    row->to_json = col_dup(stmt, 7);
    row->cc_json = col_dup(stmt, 8);
    row->bcc_json = col_dup(stmt, 9);
    row->reply_json = col_dup(stmt, 10);
    row->sent_null = sqlite3_column_type(stmt, 11) == SQLITE_NULL;
    row->sent_at = row->sent_null ? 0 : sqlite3_column_int64(stmt, 11);
    row->received_null = sqlite3_column_type(stmt, 12) == SQLITE_NULL;
    row->received_at = row->received_null ? 0 : sqlite3_column_int64(stmt, 12);
    row->draft = sqlite3_column_int(stmt, 13);
    copy_fit(row->body_state, sizeof(row->body_state), (const char *)sqlite3_column_text(stmt, 14));
    row->list_id = col_dup(stmt, 15);
    row->auth = col_dup(stmt, 16);
    copy_fit(row->account_id, sizeof(row->account_id), (const char *)sqlite3_column_text(stmt, 17));
    copy_fit(row->jmap_id, sizeof(row->jmap_id), (const char *)sqlite3_column_text(stmt, 18));
    row->session_json = col_dup(stmt, 19);
    row->text_path = col_dup(stmt, 20);
    row->html_path = col_dup(stmt, 21);
    pav_db_read_end(read);
    if (strcmp(row->body_state, "fetched") == 0) {
        int text_ok = row->text_path == NULL || pav_exists(row->text_path);
        int html_ok = row->html_path == NULL || pav_exists(row->html_path);

        row->cache = text_ok && html_ok;
    }
    return 0;
}

static char *read_capped(const char *path) {
    FILE *fp;
    char *buf;
    size_t n = 0;
    size_t cap = 4096;

    if (path == NULL || path[0] == '\0') {
        return dup_text("");
    }
    fp = fopen(path, "rb");
    if (fp == NULL) {
        return NULL;
    }
    buf = malloc(cap);
    if (buf == NULL) {
        fclose(fp);
        return NULL;
    }
    while (n < PAV_MIME_BODY_MAX) {
        size_t got;

        if (n + 2048 >= cap) {
            char *grown = realloc(buf, cap * 2);

            if (grown == NULL) {
                free(buf);
                fclose(fp);
                return NULL;
            }
            buf = grown;
            cap *= 2;
        }
        got = fread(buf + n, 1, 2048, fp);
        n += got;
        if (got == 0) {
            break;
        }
    }
    fclose(fp);
    if (n > PAV_MIME_BODY_MAX) {
        n = PAV_MIME_BODY_MAX;
        while (n > 0 && ((unsigned char)buf[n] & 0xC0) == 0x80) {
            n--;
        }
    }
    buf[n] = '\0';
    return buf;
}

static json_t *parse_array(const char *raw) {
    json_error_t jerr;
    json_t *value = raw != NULL && raw[0] != '\0' ? json_loads(raw, 0, &jerr) : NULL;

    if (!json_is_array(value)) {
        json_decref(value);
        value = json_array();
    }
    return value;
}

static json_t *parse_message_id(const char *raw) {
    json_error_t jerr;
    json_t *value;
    json_t *arr;

    if (raw == NULL || raw[0] == '\0') {
        return json_array();
    }
    value = json_loads(raw, 0, &jerr);
    if (json_is_array(value)) {
        return value;
    }
    arr = json_array();
    if (arr == NULL) {
        json_decref(value);
        return NULL;
    }
    if (json_is_string(value)) {
        json_array_append(arr, value);
    } else {
        json_array_append_new(arr, json_string(raw));
    }
    json_decref(value);
    return arr;
}

static char *identity_for(const char *ra, const char *from_json) {
    json_t *addrs;
    const char *email;
    PavDbRead *read = NULL;
    sqlite3_stmt *stmt = NULL;
    char *id = NULL;
    static const char k_sql[] = "SELECT id FROM identity WHERE ra_id = ? AND lower(email) = lower(?) LIMIT 1";

    addrs = parse_array(from_json);
    email = json_string_value(json_object_get(json_array_get(addrs, 0), "email"));
    if (email == NULL || email[0] == '\0') {
        json_decref(addrs);
        return NULL;
    }
    if (pav_db_read_begin(&read) == 0) {
        if (pav_db_prepare(pav_db_read_handle(read), "body.identity", k_sql, &stmt) == 0 &&
            pav_db_bind_text(stmt, 1, ra) == 0 && pav_db_bind_text(stmt, 2, email) == 0 &&
            pav_db_step(stmt) == SQLITE_ROW) {
            id = col_dup(stmt, 0);
        }
        pav_db_read_end(read);
    }
    json_decref(addrs);
    return id;
}

static json_t *attachment_json(int64_t rowid) {
    PavDbRead *read = NULL;
    sqlite3_stmt *stmt = NULL;
    json_t *arr = json_array();
    static const char k_sql[] =
        "SELECT part_id, name, mime, size, cid, disposition FROM attachment WHERE email_rowid = ? ORDER BY part_id";

    if (arr == NULL || pav_db_read_begin(&read) != 0) {
        json_decref(arr);
        return NULL;
    }
    if (pav_db_prepare(pav_db_read_handle(read), "body.attach.list", k_sql, &stmt) != 0 ||
        pav_db_bind_int64(stmt, 1, rowid) != 0) {
        pav_db_read_end(read);
        json_decref(arr);
        return NULL;
    }
    while (pav_db_step(stmt) == SQLITE_ROW) {
        json_t *one = json_object();
        const char *disp = (const char *)sqlite3_column_text(stmt, 5);

        if (one == NULL || json_object_set_new(one, "partId", json_string((const char *)sqlite3_column_text(stmt, 0))) != 0 ||
            json_object_set_new(one, "name", json_string((const char *)sqlite3_column_text(stmt, 1) != NULL
                                                              ? (const char *)sqlite3_column_text(stmt, 1)
                                                              : "")) != 0 ||
            json_object_set_new(one, "mime", json_string((const char *)sqlite3_column_text(stmt, 2) != NULL
                                                              ? (const char *)sqlite3_column_text(stmt, 2)
                                                              : "")) != 0 ||
            json_object_set_new(one, "size", json_integer(sqlite3_column_int64(stmt, 3))) != 0 ||
            json_object_set_new(one, "cid", sqlite3_column_type(stmt, 4) == SQLITE_NULL
                                                ? json_null()
                                                : json_string((const char *)sqlite3_column_text(stmt, 4))) != 0 ||
            json_object_set_new(one, "disposition", disp != NULL ? json_string(disp) : json_null()) != 0 ||
            json_array_append_new(arr, one) != 0) {
            json_decref(one);
            json_decref(arr);
            pav_db_read_end(read);
            return NULL;
        }
    }
    pav_db_read_end(read);
    return arr;
}

static int build_reply(const char *wire_id, MailRow *row, json_t **data, PavError *err) {
    json_t *obj;
    json_t *from;
    json_t *msgid;
    json_t *attachments;
    char *text;
    char *hint;
    int64_t when = 0;
    int when_null = 1;

    text = read_capped(row->text_path);
    if (text == NULL) {
        pav_error_set(err, "internal", "cannot read cached body", 0);
        return -1;
    }
    from = parse_array(row->from_json);
    msgid = parse_message_id(row->message_id);
    attachments = attachment_json(row->rowid);
    hint = identity_for(row->ra, row->from_json);
    obj = json_object();
    if (from == NULL || msgid == NULL || attachments == NULL || obj == NULL) {
        free(text);
        free(hint);
        json_decref(from);
        json_decref(msgid);
        json_decref(attachments);
        json_decref(obj);
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    if (!row->sent_null) {
        when = row->sent_at;
        when_null = 0;
    } else if (!row->received_null) {
        when = row->received_at;
        when_null = 0;
    }
    if (json_object_set_new(obj, "id", json_string(wire_id)) != 0 ||
        json_object_set_new(obj, "from", from) != 0 ||
        json_object_set_new(obj, "to", parse_array(row->to_json)) != 0 ||
        json_object_set_new(obj, "cc", parse_array(row->cc_json)) != 0 ||
        json_object_set_new(obj, "bcc", parse_array(row->bcc_json)) != 0 ||
        json_object_set_new(obj, "replyTo", parse_array(row->reply_json)) != 0 ||
        json_object_set_new(obj, "subject", json_string(row->subject != NULL ? row->subject : "")) != 0 ||
        json_object_set_new(obj, "date", when_null ? json_null() : json_integer(when)) != 0 ||
        json_object_set_new(obj, "messageId", msgid) != 0 ||
        json_object_set_new(obj, "listId", json_string(row->list_id != NULL ? row->list_id : "")) != 0 ||
        json_object_set_new(obj, "authenticationResults", json_string(row->auth != NULL ? row->auth : "")) != 0 ||
        json_object_set_new(obj, "text", json_string(text)) != 0 ||
        json_object_set_new(obj, "htmlSanitized", json_null()) != 0 ||
        json_object_set_new(obj, "hasRemote", json_false()) != 0 ||
        json_object_set_new(obj, "attachments", attachments) != 0 ||
        json_object_set_new(obj, "isDraft", row->draft ? json_true() : json_false()) != 0 ||
        json_object_set_new(obj, "identityHint", hint != NULL ? json_string(hint) : json_null()) != 0) {
        free(text);
        free(hint);
        json_decref(obj);
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    free(text);
    free(hint);
    from = NULL;
    msgid = NULL;
    attachments = NULL;
    *data = obj;
    return 0;
}

static void job_clear_net(BodyJob *job) {
    pav_net_free(job->copied);
    job->copied = NULL;
    job->copied_len = 0;
    job->done = 0;
    job->status = 0;
    job->auth_rc = 0;
    job->cancelled = 0;
}

static void on_net(PavReq *req, void *user) {
    BodyJob *job = user;

    pthread_mutex_lock(&job->mu);
    job->status = req->status_code;
    job->auth_rc = req->auth_rc;
    job->cancelled = req->cancelled;
    if (req->body_out != NULL && req->body_out_len > 0) {
        job->copied = malloc(req->body_out_len + 1);
        if (job->copied != NULL) {
            memcpy(job->copied, req->body_out, req->body_out_len);
            job->copied[req->body_out_len] = '\0';
            job->copied_len = req->body_out_len;
        }
    }
    pav_net_free(req->body_out);
    req->body_out = NULL;
    req->body_out_len = 0;
    job->done = 1;
    pthread_cond_signal(&job->cv);
    pthread_mutex_unlock(&job->mu);
}

static BodyJob *job_new(const MailRow *row) {
    BodyJob *job = calloc(1, sizeof(*job));

    if (job == NULL) {
        return NULL;
    }
    pthread_mutex_init(&job->mu, NULL);
    pthread_cond_init(&job->cv, NULL);
    job->rowid = row->rowid;
    job->activity = 0;
    copy_fit(job->account_id, sizeof(job->account_id), row->account_id);
    copy_fit(job->ra, sizeof(job->ra), row->ra);
    copy_fit(job->email_id, sizeof(job->email_id), row->email_id);
    copy_fit(job->jmap_id, sizeof(job->jmap_id), row->jmap_id);
    copy_fit(job->blob_id, sizeof(job->blob_id), row->blob_id);
    return job;
}

static void job_free(BodyJob *job) {
    if (job == NULL) {
        return;
    }
    flight_del(job->activity);
    free(job->request);
    free(job->copied);
    json_decref(job->req.headers);
    json_decref(job->req.resp_headers);
    pthread_mutex_destroy(&job->mu);
    pthread_cond_destroy(&job->cv);
    free(job);
}

static int fill_session(BodyJob *job, const char *session_json, PavError *err) {
    PavJmapSession session;
    char parse_err[256];

    if (session_json == NULL || session_json[0] == '\0') {
        pav_error_set(err, "not_ready", "mail session is not stored", 0);
        return -1;
    }
    memset(&session, 0, sizeof(session));
    if (pav_jmap_parse_session(session_json, strlen(session_json), &session, parse_err, sizeof(parse_err)) != 0 ||
        session.api_url[0] == '\0') {
        pav_jmap_session_free(&session);
        pav_error_set(err, "not_ready", "mail session is not stored", 0);
        return -1;
    }
    if (copy_fit(job->api_url, sizeof(job->api_url), session.api_url) != 0 ||
        copy_fit(job->download_url, sizeof(job->download_url), session.download_url) != 0) {
        pav_jmap_session_free(&session);
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    pav_jmap_session_free(&session);
    if (profile_account_copy(job->account_id, job->username, sizeof(job->username), job->auth, sizeof(job->auth),
                             job->ca_file, sizeof(job->ca_file), job->keyring_user, sizeof(job->keyring_user)) != 0) {
        pav_error_set(err, "not_ready", "mail session is not stored", 0);
        return -1;
    }
    return 0;
}

static int submit_json(BodyJob *job) {
    job->request = pav_body_email_get_request(job->jmap_id, job->email_id);
    if (job->request == NULL) {
        return -1;
    }
    job->stage = 0;
    job->req.method = "POST";
    job->req.url = job->api_url;
    job->req.account_id = job->account_id;
    job->req.keyring_user = job->keyring_user;
    job->req.username = job->username;
    job->req.auth_method = job->auth;
    job->req.ca_file = job->ca_file[0] != '\0' ? job->ca_file : NULL;
    job->req.body = job->request;
    job->req.body_len = strlen(job->request);
    job->req.download_path = NULL;
    job->req.headers = json_pack("{s:s}", "Content-Type", "application/json");
    job->req.on_done = on_net;
    job->req.user = job;
    if (job->req.headers == NULL) {
        return -1;
    }
    flight_add(job->activity, &job->req);
    return pav_net_request(&job->req);
}

static int submit_download(BodyJob *job) {
    char url[4096];
    char blob[256];
    const PavPlatform *platform = pav_platform();
    char parent[4096];

    if (job->blob_id[0] == '\0') {
        snprintf(blob, sizeof(blob), "raw:%lld", (long long)job->rowid);
    } else {
        snprintf(blob, sizeof(blob), "%s", job->blob_id);
    }
    if (build_download(url, sizeof(url), job->download_url, job->jmap_id, blob) != 0) {
        return -1;
    }
    if (pav_blob_path(job->ra, blob, job->final_path, sizeof(job->final_path)) != 0) {
        return -1;
    }
    {
        size_t n = strlen(job->final_path);

        if (n + 5 > sizeof(job->tmp_path)) {
            return -1;
        }
        memcpy(job->tmp_path, job->final_path, n);
        memcpy(job->tmp_path + n, ".tmp", 5);
    }
    if (platform == NULL || platform->mkdir_private == NULL ||
        pav_path_parent(parent, sizeof(parent), job->final_path) != 0 || platform->mkdir_private(parent) != 0) {
        return -1;
    }
    free(job->request);
    job->request = NULL;
    json_decref(job->req.headers);
    job->req.headers = NULL;
    job->req.method = "GET";
    if (copy_fit(job->api_url, sizeof(job->api_url), url) != 0) {
        return -1;
    }
    job->req.url = job->api_url;
    job->req.body = NULL;
    job->req.body_len = 0;
    job->req.download_path = job->tmp_path;
    job->stage = 1;
    job_clear_net(job);
    return pav_net_request(&job->req);
}

static int wait_net(BodyJob *job) {
    struct timespec ts;
    int spins = 0;

    pthread_mutex_lock(&job->mu);
    while (!job->done && spins < 600) {
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 100000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        pthread_cond_timedwait(&job->cv, &job->mu, &ts);
        spins++;
        if (!job->done && (job->req.cancelled || pav_activity_cancelled(job->activity))) {
            job->req.cancelled = 1;
            pav_net_wakeup();
        }
    }
    pthread_mutex_unlock(&job->mu);
    return job->done ? 0 : -1;
}

static int take_email_object(BodyJob *job, json_t **email, int *missing) {
    json_error_t jerr;
    json_t *root;
    json_t *calls;
    json_t *call;
    json_t *args;
    json_t *list;
    json_t *not_found;
    const char *blob;

    *email = NULL;
    *missing = 0;
    if (job->copied == NULL) {
        return -1;
    }
    root = json_loads(job->copied, 0, &jerr);
    if (!json_is_object(root)) {
        json_decref(root);
        return -1;
    }
    calls = json_object_get(root, "methodResponses");
    call = json_is_array(calls) ? json_array_get(calls, 0) : NULL;
    args = json_is_array(call) ? json_array_get(call, 1) : NULL;
    list = json_object_get(args, "list");
    not_found = json_object_get(args, "notFound");
    if (json_is_array(not_found)) {
        size_t i;

        for (i = 0; i < json_array_size(not_found); i++) {
            const char *id = json_string_value(json_array_get(not_found, i));

            if (id != NULL && strcmp(id, job->email_id) == 0) {
                *missing = 1;
            }
        }
    }
    if (json_is_array(list) && json_array_size(list) > 0 && json_is_object(json_array_get(list, 0))) {
        *email = json_incref(json_array_get(list, 0));
        blob = json_string_value(json_object_get(*email, "blobId"));
        if (blob != NULL && blob[0] != '\0') {
            copy_fit(job->blob_id, sizeof(job->blob_id), blob);
        }
    }
    json_decref(root);
    return *email != NULL || *missing ? 0 : -1;
}

static int finish_download_file(BodyJob *job) {
    if (job->status != 200) {
        remove(job->tmp_path);
        return -1;
    }
    remove(job->final_path);
    if (rename(job->tmp_path, job->final_path) != 0) {
        remove(job->tmp_path);
        return -1;
    }
    if (pav_file_private(job->final_path) != 0) {
        return -1;
    }
    return 0;
}

static int store_path(BodyJob *job) {
    PavMimeDoc doc;
    int rc;

    if (pav_mime_parse_path(job->final_path, &doc) != 0) {
        return -1;
    }
    rc = store_doc(job->rowid, job->ra, &doc, 0);
    pav_mime_free(&doc);
    return rc;
}

static void map_status(BodyJob *job, int missing, PavError *err) {
    if (job->cancelled || pav_activity_cancelled(job->activity)) {
        pav_error_set(err, "internal", "cancelled", 0);
        return;
    }
    if (job->auth_rc == PAV_SECRET_LOCKED) {
        pav_error_set(err, "auth", "keyring is locked", 0);
        return;
    }
    if (job->auth_rc == PAV_SECRET_MISSING || job->auth_rc == PAV_SECRET_FAILED || job->status == 401) {
        pav_error_set(err, "auth", "authentication failed", 0);
        return;
    }
    if (missing) {
        pav_error_set(err, "not_found", "unknown message", 0);
        return;
    }
    if (job->status >= 500) {
        pav_error_set(err, "server", "server error", 1);
        return;
    }
    if (job->status >= 400) {
        pav_error_set(err, "server", "request rejected", 0);
        return;
    }
    pav_error_set(err, "network", "request failed", 1);
}

static int run_fetch(BodyJob *job, PavError *err) {
    json_t *email = NULL;
    char *email_text = NULL;
    int missing = 0;
    int applied;
    int rc = -1;

    pav_sync_set_body_hold(job->account_id, 1);
    slot_user(job->account_id, 1);
    job->activity = pav_activity_begin("body.fetch", job->account_id, job->email_id, 1, 0);
    if (submit_json(job) != 0 || wait_net(job) != 0) {
        map_status(job, 0, err);
        goto done;
    }
    if (job->cancelled || pav_activity_cancelled(job->activity)) {
        map_status(job, 0, err);
        goto done;
    }
    if (job->status == 200 && take_email_object(job, &email, &missing) == 0 && email != NULL && !missing) {
        email_text = json_dumps(email, JSON_COMPACT);
        applied = email_text != NULL ? pav_body_apply_jmap(job->rowid, job->ra, email_text) : -1;
        if (applied == 0) {
            rc = 0;
            goto done;
        }
        if (applied < 0) {
            pav_error_set(err, "internal", "cannot store body", 0);
            goto done;
        }
    } else if (missing) {
        map_status(job, 1, err);
        goto done;
    } else if (job->status != 200 && job->status != 0) {
        map_status(job, 0, err);
        goto done;
    }
    if (submit_download(job) != 0 || wait_net(job) != 0) {
        map_status(job, 0, err);
        goto done;
    }
    if (job->cancelled || pav_activity_cancelled(job->activity)) {
        remove(job->tmp_path);
        map_status(job, 0, err);
        goto done;
    }
    if (finish_download_file(job) != 0 || store_path(job) != 0) {
        if (err->code[0] == '\0') {
            map_status(job, 0, err);
        }
        mark_state(job->rowid, "failed");
        goto done;
    }
    rc = 0;

done:
    json_decref(email);
    free(email_text);
    if (rc == 0) {
        pav_log("mail", PAV_INFO, 1, "body fetched %s", job->email_id);
        if (job->activity > 0) {
            pav_activity_end(job->activity, PAV_ACT_DONE, NULL);
        }
    } else if (job->cancelled || strcmp(err->message, "cancelled") == 0) {
        if (job->activity > 0) {
            pav_activity_end(job->activity, PAV_ACT_CANCELLED, NULL);
        }
    } else {
        if (strcmp(err->code, "not_found") != 0 && strcmp(err->code, "not_ready") != 0) {
            mark_state(job->rowid, "failed");
        }
        if (job->activity > 0) {
            pav_activity_end(job->activity, PAV_ACT_FAILED, err->message);
        }
    }
    flight_del(job->activity);
    slot_user(job->account_id, -1);
    pav_sync_set_body_hold(job->account_id, 0);
    return rc;
}

typedef struct BodyCore {
    int kind;
    BodyJob *job;
} BodyCore;

static void prefetch_one(const char *account_id, int64_t rowid);

static void prefetch_kick(const char *account_id) {
    const char *mode;
    int64_t ids[2];
    int n = 0;
    int i;
    int64_t now;

    if (account_id == NULL || slot_users(account_id) > 0 || !pav_sync_is_idle(account_id)) {
        return;
    }
    mode = profile_account_mode(account_id);
    if (strcmp(mode, "none") == 0) {
        return;
    }
    now = pav_now_ms();
    if (pav_body_select(account_id, mode, now > 0 ? now : 0, 2, ids, &n) != 0) {
        return;
    }
    for (i = 0; i < n; i++) {
        if (!slot_begin_prefetch(account_id)) {
            break;
        }
        prefetch_one(account_id, ids[i]);
    }
}

static int load_rowid(int64_t rowid, MailRow *row) {
    char wire[600];
    PavDbRead *read = NULL;
    sqlite3_stmt *stmt = NULL;
    const char *ra;
    const char *id;
    int n;

    if (pav_db_read_begin(&read) != 0) {
        return -1;
    }
    if (pav_db_prepare(pav_db_read_handle(read), "body.rowid", "SELECT ra_id, id FROM email WHERE rowid = ?", &stmt) !=
            0 ||
        pav_db_bind_int64(stmt, 1, rowid) != 0 || pav_db_step(stmt) != SQLITE_ROW) {
        pav_db_read_end(read);
        return -1;
    }
    ra = (const char *)sqlite3_column_text(stmt, 0);
    id = (const char *)sqlite3_column_text(stmt, 1);
    n = snprintf(wire, sizeof(wire), "msg:%s:%s", ra != NULL ? ra : "", id != NULL ? id : "");
    pav_db_read_end(read);
    if (n < 0 || (size_t)n >= sizeof(wire)) {
        return -1;
    }
    return load_row(wire, row) == 0 && row->found ? 0 : -1;
}

static void prefetch_done(BodyJob *job, int ok, int cancelled) {
    flight_del(job->activity);
    if (cancelled) {
        remove(job->tmp_path);
        if (job->activity > 0) {
            pav_activity_end(job->activity, PAV_ACT_CANCELLED, NULL);
        }
    } else if (ok) {
        pav_log("mail", PAV_INFO, 1, "body fetched %s", job->email_id);
        if (job->activity > 0) {
            pav_activity_end(job->activity, PAV_ACT_DONE, NULL);
        }
    } else {
        mark_state(job->rowid, "failed");
        if (job->activity > 0) {
            pav_activity_end(job->activity, PAV_ACT_FAILED, NULL);
        }
    }
    slot_end_prefetch(job->account_id);
    {
        char account[64];

        copy_fit(account, sizeof(account), job->account_id);
        job_free(job);
        prefetch_kick(account);
    }
}

static void prefetch_after_json(BodyJob *job) {
    json_t *email = NULL;
    char *text = NULL;
    int missing = 0;
    int applied = -1;
    int cancelled;

    pthread_mutex_lock(&job->mu);
    cancelled = job->cancelled;
    pthread_mutex_unlock(&job->mu);
    if (cancelled || pav_activity_cancelled(job->activity)) {
        prefetch_done(job, 0, 1);
        return;
    }
    if (job->status == 200 && take_email_object(job, &email, &missing) == 0 && email != NULL && !missing) {
        text = json_dumps(email, JSON_COMPACT);
        applied = text != NULL ? pav_body_apply_jmap(job->rowid, job->ra, text) : -1;
    }
    json_decref(email);
    free(text);
    if (applied == 0) {
        prefetch_done(job, 1, 0);
        return;
    }
    if (missing || applied < 0) {
        prefetch_done(job, 0, 0);
        return;
    }
    {
        BodyCore *core = calloc(1, sizeof(*core));

        if (core == NULL || (core->kind = PAV_BODY_CORE_KIND, core->job = job, pav_core_post_sync(core) != 0)) {
            free(core);
            prefetch_done(job, 0, 0);
        }
    }
}

static void prefetch_on_net(PavReq *req, void *user) {
    BodyJob *job = user;

    on_net(req, job);
    if (job->stage == 0) {
        prefetch_after_json(job);
        return;
    }
    if (job->cancelled || pav_activity_cancelled(job->activity)) {
        prefetch_done(job, 0, 1);
        return;
    }
    if (finish_download_file(job) != 0 || store_path(job) != 0) {
        prefetch_done(job, 0, 0);
        return;
    }
    prefetch_done(job, 1, 0);
}

static void body_continue(BodyJob *job) {
    PavError err;

    if (job == NULL) {
        return;
    }
    memset(&err, 0, sizeof(err));
    job->req.on_done = prefetch_on_net;
    if (job->cancelled || pav_activity_cancelled(job->activity) || submit_download(job) != 0) {
        prefetch_done(job, 0, job->cancelled || pav_activity_cancelled(job->activity));
    }
}

int pav_body_take_core(void *result) {
    BodyCore *core = result;

    if (core == NULL || core->kind != PAV_BODY_CORE_KIND) {
        return 0;
    }
    body_continue(core->job);
    free(core);
    return 1;
}

static void prefetch_one(const char *account_id, int64_t rowid) {
    MailRow row;
    BodyJob *job;
    PavError err;

    memset(&row, 0, sizeof(row));
    memset(&err, 0, sizeof(err));
    if (load_rowid(rowid, &row) != 0) {
        slot_end_prefetch(account_id);
        return;
    }
    job = job_new(&row);
    if (job == NULL || fill_session(job, row.session_json, &err) != 0) {
        /* No session yet: leave body_state alone so the next sync can retry. */
        mail_row_free(&row);
        job_free(job);
        slot_end_prefetch(account_id);
        return;
    }
    mail_row_free(&row);
    job->wait = 0;
    job->req.on_done = prefetch_on_net;
    job->activity = pav_activity_begin("body.fetch", job->account_id, job->email_id, 1, 0);
    if (submit_json(job) != 0) {
        if (job->activity > 0) {
            pav_activity_end(job->activity, PAV_ACT_FAILED, NULL);
        }
        job_free(job);
        slot_end_prefetch(account_id);
    }
}

void pav_body_prefetch(const char *account_id) {
    prefetch_kick(account_id);
}

int pav_cmd_message_get(json_t *args, json_t **data, PavError *err) {
    const char *key;
    json_t *value;
    const char *id = NULL;
    int saw_id = 0;
    int allow_remote = 0;
    MailRow row;
    BodyJob *job = NULL;
    int fetched = 0;

    if (!json_is_object(args)) {
        pav_error_set(err, "bad_args", "arguments are not an object", 0);
        return -1;
    }
    json_object_foreach(args, key, value) {
        if (strcmp(key, "id") == 0) {
            if (!json_is_string(value) || json_string_value(value)[0] == '\0') {
                pav_error_set(err, "bad_args", "id is required", 0);
                return -1;
            }
            id = json_string_value(value);
            saw_id = 1;
        } else if (strcmp(key, "allowRemote") == 0) {
            if (!json_is_boolean(value)) {
                pav_error_set(err, "bad_args", "allowRemote must be a boolean", 0);
                return -1;
            }
            allow_remote = json_is_true(value);
        } else {
            char buf[128];

            snprintf(buf, sizeof(buf), "unknown argument %s", key);
            pav_error_set(err, "bad_args", buf, 0);
            return -1;
        }
    }
    (void)allow_remote;
    if (!saw_id || strncmp(id, "msg:", 4) != 0) {
        pav_error_set(err, "bad_args", "id is required", 0);
        return -1;
    }
    if (load_row(id, &row) != 0) {
        pav_error_set(err, "internal", "cannot read message", 0);
        return -1;
    }
    if (!row.found) {
        pav_error_set(err, "not_found", "unknown message", 0);
        return -1;
    }
    if (!row.cache) {
        job = job_new(&row);
        if (job == NULL || fill_session(job, row.session_json, err) != 0) {
            if (err->code[0] == '\0') {
                pav_error_set(err, "internal", "out of memory", 0);
            }
            job_free(job);
            mail_row_free(&row);
            return -1;
        }
        job->wait = 1;
        if (run_fetch(job, err) != 0) {
            job_free(job);
            mail_row_free(&row);
            return -1;
        }
        job_free(job);
        fetched = 1;
        mail_row_free(&row);
        memset(&row, 0, sizeof(row));
        if (load_row(id, &row) != 0 || !row.found) {
            mail_row_free(&row);
            pav_error_set(err, "internal", "cannot read message", 0);
            return -1;
        }
    }
    if (build_reply(id, &row, data, err) != 0) {
        mail_row_free(&row);
        return -1;
    }
    touch_opened(row.rowid);
    mail_row_free(&row);
    (void)fetched;
    return 0;
}

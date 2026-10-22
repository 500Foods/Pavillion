/* cache.usage (L-2): on-disk body and blob bytes, each account's server
   counts and sizes, and free space on the blob volume. Refresh posts the
   quick counters. measure walks file sizes a single-kind quota did not
   already give, and always walks mail so the text and attachment split
   is known. Several windows per request. A mixed quota stays serverBytes.
   Cancel is handled on the core thread before this worker runs. */

#define _POSIX_C_SOURCE 200809L

#include "pav_blob.h"

#include "dispatch.h"
#include "pav_db.h"
#include "pav_net.h"
#include "pav_platform.h"
#include "pav_secret.h"
#include "pavillion.h"

#include <jansson.h>
#include <sqlite3.h>

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define USAGE_QUICK_MS 8000
#define USAGE_MEASURE_MS 20000
#define USAGE_PAGE 100
#define USAGE_PAGE_CAP 500
#define USAGE_BATCH_MAX 8
#define USAGE_MAIL_BATCH_MAX 4
#define USAGE_MAX_PAGES 1000
#define USAGE_BODY_MAX (1 << 20)
#define USAGE_PROGRESS_MS 250

#define STORE_MAIL 0
#define STORE_CONTACTS 1
#define STORE_CALENDARS 2
#define STORE_FILES 3
#define STORE_MIXED (-1)
#define STORE_NONE (-2)

static const char URN_CORE[] = "urn:ietf:params:jmap:core";
static const char URN_QUOTA[] = "urn:ietf:params:jmap:quota";
static const char URN_MAIL[] = "urn:ietf:params:jmap:mail";
static const char URN_CONTACTS[] = "urn:ietf:params:jmap:contacts";
static const char URN_CALENDARS[] = "urn:ietf:params:jmap:calendars";
static const char URN_FILENODE[] = "urn:ietf:params:jmap:filenode";
static const char KV_LIMITS[] = "limits.server";

typedef struct UsageCtx {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    PavReq req;
    char url[2048];
    char account_id[128];
    char keyring_user[192];
    char username[256];
    char auth_method[32];
    char ca_file[4096];
    char *request;
    char *body;
    size_t body_len;
    int status;
    int auth_rc;
    int done;
    int abandoned;
    int refs;
} UsageCtx;

typedef struct JmapAuth {
    const char *url;
    const char *account_id;
    const char *keyring_user;
    const char *username;
    const char *auth;
    const char *ca;
} JmapAuth;

static atomic_int g_usage_pending;
static pthread_mutex_t g_usage_mu = PTHREAD_MUTEX_INITIALIZER;
static PavReq *g_usage_req;

static void usage_signal_cancel(void) {
    atomic_store(&g_usage_pending, 1);
    pthread_mutex_lock(&g_usage_mu);
    if (g_usage_req != NULL) {
        g_usage_req->cancelled = 1;
    }
    pthread_mutex_unlock(&g_usage_mu);
}

static int usage_cancelled(void) {
    return atomic_load(&g_usage_pending) != 0;
}

static void usage_arm(PavReq *req) {
    pthread_mutex_lock(&g_usage_mu);
    g_usage_req = req;
    if (req != NULL && atomic_load(&g_usage_pending) != 0) {
        req->cancelled = 1;
    }
    pthread_mutex_unlock(&g_usage_mu);
}

static void usage_disarm(PavReq *req) {
    pthread_mutex_lock(&g_usage_mu);
    if (g_usage_req == req) {
        g_usage_req = NULL;
    }
    pthread_mutex_unlock(&g_usage_mu);
}

typedef struct KvPut {
    const char *key;
    const char *value;
} KvPut;

static int types_include_mail(json_t *quota) {
    json_t *types = json_object_get(quota, "types");
    size_t i;
    json_t *item;

    if (!json_is_array(types)) {
        return 0;
    }
    json_array_foreach(types, i, item) {
        const char *name = json_string_value(item);

        if (name != NULL && strcmp(name, "Mail") == 0) {
            return 1;
        }
    }
    return 0;
}

int pav_quota_pick_octets(const char *body, int64_t *used) {
    json_error_t jerr;
    json_t *root;
    json_t *responses;
    size_t i;
    int found = 0;
    int mail = 0;
    int64_t value = 0;

    if (used != NULL) {
        *used = 0;
    }
    if (body == NULL || body[0] == '\0') {
        return 0;
    }
    root = json_loads(body, 0, &jerr);
    if (root == NULL) {
        return 0;
    }
    responses = json_object_get(root, "methodResponses");
    if (!json_is_array(responses)) {
        json_decref(root);
        return 0;
    }
    for (i = 0; i < json_array_size(responses); i++) {
        json_t *call = json_array_get(responses, i);
        json_t *payload;
        json_t *list;
        const char *name;
        size_t q;

        if (!json_is_array(call) || json_array_size(call) < 2) {
            continue;
        }
        name = json_string_value(json_array_get(call, 0));
        if (name == NULL || strcmp(name, "Quota/get") != 0) {
            continue;
        }
        payload = json_array_get(call, 1);
        list = json_is_object(payload) ? json_object_get(payload, "list") : NULL;
        if (!json_is_array(list)) {
            continue;
        }
        for (q = 0; q < json_array_size(list); q++) {
            json_t *quota = json_array_get(list, q);
            const char *kind;
            json_t *used_json;
            int is_mail;

            if (!json_is_object(quota)) {
                continue;
            }
            kind = json_string_value(json_object_get(quota, "resourceType"));
            used_json = json_object_get(quota, "used");
            if (kind == NULL || strcmp(kind, "octets") != 0 || !json_is_integer(used_json)) {
                continue;
            }
            is_mail = types_include_mail(quota);
            if (!found || (!mail && is_mail)) {
                value = json_integer_value(used_json);
                found = 1;
                mail = is_mail;
            }
        }
    }
    json_decref(root);
    if (!found) {
        return 0;
    }
    if (used != NULL) {
        *used = value;
    }
    return 1;
}

static int id_set(const char *id) {
    return id != NULL && id[0] != '\0';
}

static int type_store(const char *name) {
    if (name == NULL) {
        return STORE_MIXED;
    }
    if (strcmp(name, "Email") == 0 || strcmp(name, "Mail") == 0) {
        return STORE_MAIL;
    }
    if (strcmp(name, "ContactCard") == 0 || strcmp(name, "Contact") == 0) {
        return STORE_CONTACTS;
    }
    if (strcmp(name, "CalendarEvent") == 0 || strcmp(name, "Calendar") == 0) {
        return STORE_CALENDARS;
    }
    if (strcmp(name, "FileNode") == 0 || strcmp(name, "File") == 0) {
        return STORE_FILES;
    }
    return STORE_MIXED;
}

/* Every type must name one kind. SieveScript, a mix, or an empty list
   is the account-wide total, not a column. */
static int quota_store(json_t *quota) {
    json_t *types = json_object_get(quota, "types");
    size_t i;
    json_t *item;
    int store = STORE_NONE;
    int seen = 0;

    if (!json_is_array(types) || json_array_size(types) == 0) {
        return STORE_MIXED;
    }
    json_array_foreach(types, i, item) {
        int one = type_store(json_string_value(item));

        if (one == STORE_MIXED) {
            return STORE_MIXED;
        }
        if (!seen) {
            store = one;
            seen = 1;
        } else if (one != store) {
            return STORE_MIXED;
        }
    }
    return seen ? store : STORE_MIXED;
}

static PavStoreFigure *figure_at(PavUsageParse *out, int store) {
    if (store == STORE_MAIL) {
        return &out->mail;
    }
    if (store == STORE_CONTACTS) {
        return &out->contacts;
    }
    if (store == STORE_CALENDARS) {
        return &out->calendars;
    }
    if (store == STORE_FILES) {
        return &out->files;
    }
    return NULL;
}

static void apply_quota(PavUsageParse *out, json_t *quota) {
    const char *kind = json_string_value(json_object_get(quota, "resourceType"));
    json_t *used_json = json_object_get(quota, "used");
    int store;
    int64_t used;
    PavStoreFigure *fig;

    if (kind == NULL || !json_is_integer(used_json)) {
        return;
    }
    used = json_integer_value(used_json);
    if (used < 0) {
        return;
    }
    store = quota_store(quota);
    if (strcmp(kind, "octets") == 0) {
        if (store == STORE_MIXED) {
            if (!out->has_server_bytes) {
                out->has_server_bytes = 1;
                out->server_bytes = used;
            }
            return;
        }
        fig = figure_at(out, store);
        if (fig != NULL && !fig->has_bytes) {
            fig->has_bytes = 1;
            fig->bytes = used;
        }
        return;
    }
    if (strcmp(kind, "count") != 0) {
        return;
    }
    fig = figure_at(out, store);
    if (fig != NULL && !fig->count_from_query) {
        fig->has_count = 1;
        fig->count = used;
    }
}

static int query_store(const char *name) {
    if (strcmp(name, "Email/query") == 0) {
        return STORE_MAIL;
    }
    if (strcmp(name, "ContactCard/query") == 0) {
        return STORE_CONTACTS;
    }
    if (strcmp(name, "CalendarEvent/query") == 0) {
        return STORE_CALENDARS;
    }
    if (strcmp(name, "FileNode/query") == 0) {
        return STORE_FILES;
    }
    return STORE_NONE;
}

static int is_size_get(const char *name) {
    return strcmp(name, "Email/get") == 0 || strcmp(name, "FileNode/get") == 0;
}

static void read_page_list(PavUsageParse *out, json_t *list) {
    size_t i;
    json_t *item;

    out->page_list = 0;
    out->page_sized = 0;
    out->page_unsized = 0;
    out->page_bytes = 0;
    if (!json_is_array(list)) {
        return;
    }
    if (json_array_size(list) > 1000000) {
        out->page_error = 1;
        out->page_ok = 0;
        return;
    }
    out->page_list = (int)json_array_size(list);
    json_array_foreach(list, i, item) {
        json_t *size;
        int64_t n;

        if (!json_is_object(item)) {
            out->page_unsized++;
            continue;
        }
        size = json_object_get(item, "size");
        if (!json_is_integer(size)) {
            out->page_unsized++;
            continue;
        }
        n = json_integer_value(size);
        if (n < 0 || out->page_bytes > INT64_MAX - n) {
            out->page_error = 1;
            out->page_ok = 0;
            return;
        }
        out->page_bytes += n;
        out->page_sized++;
    }
}

static void read_not_found(PavUsageParse *out, json_t *payload) {
    json_t *missing = json_object_get(payload, "notFound");

    out->page_not_found = 0;
    if (!json_is_array(missing)) {
        return;
    }
    if (json_array_size(missing) > 1000000) {
        out->page_error = 1;
        out->page_ok = 0;
        return;
    }
    out->page_not_found = (int)json_array_size(missing);
}

int pav_usage_parse(const char *body, PavUsageParse *out) {
    json_error_t jerr;
    json_t *root;
    json_t *responses;
    size_t i;
    int saw_ids = 0;
    int locked = 0;

    if (out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (body == NULL || body[0] == '\0') {
        return -1;
    }
    root = json_loads(body, 0, &jerr);
    if (!json_is_object(root)) {
        json_decref(root);
        return -1;
    }
    responses = json_object_get(root, "methodResponses");
    if (json_is_array(responses)) {
        for (i = 0; i < json_array_size(responses); i++) {
            json_t *call = json_array_get(responses, i);
            json_t *payload;
            const char *name;
            int store;

            if (!json_is_array(call) || json_array_size(call) < 2) {
                continue;
            }
            name = json_string_value(json_array_get(call, 0));
            payload = json_array_get(call, 1);
            if (name == NULL) {
                continue;
            }
            if (strcmp(name, "error") == 0) {
                out->page_error = 1;
                out->page_ok = 0;
                continue;
            }
            if (!json_is_object(payload)) {
                continue;
            }
            if (strcmp(name, "Quota/get") == 0) {
                json_t *list = json_object_get(payload, "list");
                size_t q;
                json_t *quota;

                if (!json_is_array(list)) {
                    continue;
                }
                json_array_foreach(list, q, quota) {
                    if (json_is_object(quota)) {
                        apply_quota(out, quota);
                    }
                }
                continue;
            }
            store = query_store(name);
            if (store != STORE_NONE) {
                PavStoreFigure *fig = figure_at(out, store);
                json_t *total = json_object_get(payload, "total");
                json_t *ids = json_object_get(payload, "ids");

                if (fig != NULL && json_is_integer(total) && json_integer_value(total) >= 0) {
                    fig->count = json_integer_value(total);
                    fig->has_count = 1;
                    fig->count_from_query = 1;
                    if (!locked) {
                        out->page_total = fig->count;
                        out->page_has_total = 1;
                    }
                }
                if (!locked && json_is_array(ids)) {
                    if (json_array_size(ids) > 1000000) {
                        out->page_error = 1;
                        out->page_ok = 0;
                    } else {
                        out->page_ids = (int)json_array_size(ids);
                        saw_ids = 1;
                    }
                }
                continue;
            }
            if (is_size_get(name)) {
                read_not_found(out, payload);
                read_page_list(out, json_object_get(payload, "list"));
                if (saw_ids && !out->page_error) {
                    out->page_ok = 1;
                }
                locked = 1;
            }
        }
    }
    json_decref(root);
    return 0;
}

static int append_urn(json_t *using, const char *urn) {
    json_t *text = json_string(urn);

    if (text == NULL) {
        return -1;
    }
    if (json_array_append_new(using, text) != 0) {
        return -1;
    }
    return 0;
}

static int append_method(json_t *calls, json_t *call) {
    if (call == NULL) {
        return -1;
    }
    if (json_array_append_new(calls, call) != 0) {
        return -1;
    }
    return 0;
}

static json_t *query_call(const char *method, const char *account, int limit, int position,
                          int with_position, const char *tag) {
    if (with_position) {
        return json_pack("[s,{s:s,s:b,s:i,s:i},s]",
                         method,
                         "accountId", account,
                         "calculateTotal", 1,
                         "limit", limit,
                         "position", position,
                         tag);
    }
    return json_pack("[s,{s:s,s:b,s:i},s]",
                     method,
                     "accountId", account,
                     "calculateTotal", 1,
                     "limit", limit,
                     tag);
}

static json_t *size_get_call(const char *method, const char *query_method, const char *account,
                             const char *result_of, const char *tag) {
    /* Mail also asks for decoded part sizes. bodyProperties is size only,
       so the response does not include the text. Files stay a size get. */
    if (method != NULL && strcmp(method, "Email/get") == 0) {
        return json_pack("[s,{s:s,s:{s:s,s:s,s:s},s:[s,s,s,s],s:[s]},s]",
                         method,
                         "accountId", account,
                         "#ids",
                         "resultOf", result_of,
                         "name", query_method,
                         "path", "/ids",
                         "properties", "size", "textBody", "htmlBody", "attachments",
                         "bodyProperties", "size",
                         tag);
    }
    return json_pack("[s,{s:s,s:{s:s,s:s,s:s},s:[s]},s]",
                     method,
                     "accountId", account,
                     "#ids",
                     "resultOf", result_of,
                     "name", query_method,
                     "path", "/ids",
                     "properties", "size",
                     tag);
}

static char *dump_request(json_t *root) {
    char *text;

    if (root == NULL) {
        return NULL;
    }
    text = json_dumps(root, JSON_COMPACT);
    json_decref(root);
    return text;
}

char *pav_quota_request(const char *jmap_id) {
    json_t *root;
    char *text;

    if (!id_set(jmap_id)) {
        return NULL;
    }
    root = json_pack("{s:[s,s], s:[[s,{s:s,s:n},s]]}",
                     "using",
                     URN_CORE,
                     URN_QUOTA,
                     "methodCalls",
                     "Quota/get",
                     "accountId", jmap_id,
                     "ids",
                     "q0");
    if (root == NULL) {
        return NULL;
    }
    text = json_dumps(root, JSON_COMPACT);
    json_decref(root);
    return text;
}

char *pav_usage_quick_request(const PavUsageAsk *ask) {
    json_t *root;
    json_t *using;
    json_t *calls;
    const char *files_urn;

    if (ask == NULL) {
        return NULL;
    }
    if (!id_set(ask->quota_id) && !id_set(ask->mail_id) && !id_set(ask->contacts_id) &&
        !id_set(ask->calendars_id) && !id_set(ask->files_id)) {
        return NULL;
    }
    root = json_object();
    using = json_array();
    calls = json_array();
    if (root == NULL || using == NULL || calls == NULL) {
        goto fail;
    }
    if (append_urn(using, URN_CORE) != 0) {
        goto fail;
    }
    if (id_set(ask->quota_id)) {
        if (append_urn(using, URN_QUOTA) != 0 ||
            append_method(calls, json_pack("[s,{s:s,s:n},s]",
                                            "Quota/get",
                                            "accountId", ask->quota_id,
                                            "ids",
                                            "q0")) != 0) {
            goto fail;
        }
    }
    if (id_set(ask->mail_id)) {
        if (append_urn(using, URN_MAIL) != 0 ||
            append_method(calls, query_call("Email/query", ask->mail_id, 1, 0, 0, "m")) != 0) {
            goto fail;
        }
    }
    if (id_set(ask->contacts_id)) {
        if (append_urn(using, URN_CONTACTS) != 0 ||
            append_method(calls, query_call("ContactCard/query", ask->contacts_id, 1, 0, 0, "c")) != 0) {
            goto fail;
        }
    }
    if (id_set(ask->calendars_id)) {
        if (append_urn(using, URN_CALENDARS) != 0 ||
            append_method(calls, query_call("CalendarEvent/query", ask->calendars_id, 1, 0, 0, "e")) != 0) {
            goto fail;
        }
    }
    if (id_set(ask->files_id)) {
        files_urn = id_set(ask->files_urn) ? ask->files_urn : URN_FILENODE;
        if (append_urn(using, files_urn) != 0 ||
            append_method(calls, query_call("FileNode/query", ask->files_id, 1, 0, 0, "f")) != 0) {
            goto fail;
        }
    }
    if (json_object_set_new(root, "using", using) != 0) {
        using = NULL;
        goto fail;
    }
    using = NULL;
    if (json_object_set_new(root, "methodCalls", calls) != 0) {
        calls = NULL;
        goto fail;
    }
    return dump_request(root);

fail:
    json_decref(root);
    json_decref(using);
    json_decref(calls);
    return NULL;
}

char *pav_usage_measure_request(const char *jmap_id, const char *store, const char *files_urn,
                                int position) {
    const char *urn;
    const char *query;
    const char *get;
    json_t *root;
    json_t *using;
    json_t *calls;

    if (!id_set(jmap_id) || store == NULL || position < 0) {
        return NULL;
    }
    if (strcmp(store, "mail") == 0) {
        urn = URN_MAIL;
        query = "Email/query";
        get = "Email/get";
    } else if (strcmp(store, "files") == 0) {
        urn = id_set(files_urn) ? files_urn : URN_FILENODE;
        query = "FileNode/query";
        get = "FileNode/get";
    } else {
        return NULL;
    }
    root = json_object();
    using = json_array();
    calls = json_array();
    if (root == NULL || using == NULL || calls == NULL) {
        goto fail;
    }
    if (append_urn(using, URN_CORE) != 0 || append_urn(using, urn) != 0 ||
        append_method(calls, query_call(query, jmap_id, USAGE_PAGE, position, 1, "q")) != 0 ||
        append_method(calls, size_get_call(get, query, jmap_id, "q", "g")) != 0) {
        goto fail;
    }
    if (json_object_set_new(root, "using", using) != 0) {
        using = NULL;
        goto fail;
    }
    using = NULL;
    if (json_object_set_new(root, "methodCalls", calls) != 0) {
        calls = NULL;
        goto fail;
    }
    return dump_request(root);

fail:
    json_decref(root);
    json_decref(using);
    json_decref(calls);
    return NULL;
}

typedef struct UsageWindow {
    int saw_query;
    int saw_get;
    int ids;
    int has_total;
    int64_t total;
    int list_n;
    int unsized;
    int not_found;
    int error;
    int64_t bytes;
    int64_t body_bytes;
    int64_t part_bytes;
    int64_t part_count;
    char first_id[256];
} UsageWindow;

/* 0 added the sizes. -1 on overflow or a huge list. A missing list or a
   part without an integer size bumps *bad so the window is not a total.
   count, when set, receives one per sized part. */
static int add_part_sizes(json_t *list, int64_t *sum, int64_t *count, int *bad) {
    size_t i;
    json_t *item;

    if (!json_is_array(list)) {
        *bad += 1;
        return 0;
    }
    if (json_array_size(list) > 1000000) {
        return -1;
    }
    json_array_foreach(list, i, item) {
        json_t *size;
        int64_t n;

        if (!json_is_object(item)) {
            *bad += 1;
            continue;
        }
        size = json_object_get(item, "size");
        if (!json_is_integer(size)) {
            *bad += 1;
            continue;
        }
        n = json_integer_value(size);
        if (n < 0 || *sum > INT64_MAX - n) {
            return -1;
        }
        if (count != NULL && *count == INT64_MAX) {
            return -1;
        }
        *sum += n;
        if (count != NULL) {
            *count += 1;
        }
    }
    return 0;
}

static void window_read_query(UsageWindow *w, json_t *payload) {
    json_t *total = json_object_get(payload, "total");
    json_t *ids = json_object_get(payload, "ids");
    const char *id;

    w->saw_query = 1;
    if (json_is_integer(total) && json_integer_value(total) >= 0) {
        w->has_total = 1;
        w->total = json_integer_value(total);
    }
    if (!json_is_array(ids) || json_array_size(ids) > 1000000) {
        w->error = 1;
        return;
    }
    w->ids = (int)json_array_size(ids);
    id = json_string_value(json_array_get(ids, 0));
    if (id != NULL && strlen(id) < sizeof(w->first_id)) {
        snprintf(w->first_id, sizeof(w->first_id), "%s", id);
    }
}

static void window_read_get(UsageWindow *w, json_t *payload, int split) {
    json_t *missing = json_object_get(payload, "notFound");
    json_t *list = json_object_get(payload, "list");
    size_t i;

    w->saw_get = 1;
    if (json_is_array(missing)) {
        if (json_array_size(missing) > 1000000) {
            w->error = 1;
            return;
        }
        w->not_found = (int)json_array_size(missing);
    }
    if (!json_is_array(list) || json_array_size(list) > 1000000) {
        w->error = 1;
        return;
    }
    w->list_n = (int)json_array_size(list);
    for (i = 0; i < json_array_size(list); i++) {
        json_t *item = json_array_get(list, i);
        json_t *size;
        int64_t n;

        if (!json_is_object(item)) {
            w->unsized++;
            continue;
        }
        size = json_object_get(item, "size");
        if (!json_is_integer(size)) {
            w->unsized++;
            continue;
        }
        n = json_integer_value(size);
        if (n < 0 || w->bytes > INT64_MAX - n) {
            w->error = 1;
            return;
        }
        w->bytes += n;
        if (!split) {
            continue;
        }
        if (add_part_sizes(json_object_get(item, "textBody"), &w->body_bytes, NULL, &w->unsized) != 0 ||
            add_part_sizes(json_object_get(item, "htmlBody"), &w->body_bytes, NULL, &w->unsized) != 0 ||
            add_part_sizes(json_object_get(item, "attachments"), &w->part_bytes, &w->part_count, &w->unsized) != 0) {
            w->error = 1;
            return;
        }
    }
}

int pav_usage_parse_batch(const char *body, const char *store, PavUsageBatch *out) {
    const char *query_name;
    const char *get_name;
    json_error_t jerr;
    json_t *root;
    json_t *responses;
    UsageWindow win[USAGE_BATCH_MAX];
    size_t i;
    int n = 0;
    int waiting = 0;
    int bad = 0;
    int64_t sum = 0;
    int64_t body_sum = 0;
    int64_t part_sum = 0;
    int64_t part_count = 0;
    int ids_sum = 0;

    if (out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (store == NULL) {
        return -1;
    }
    if (strcmp(store, "mail") == 0) {
        query_name = "Email/query";
        get_name = "Email/get";
    } else if (strcmp(store, "files") == 0) {
        query_name = "FileNode/query";
        get_name = "FileNode/get";
    } else {
        return -1;
    }
    if (body == NULL || body[0] == '\0') {
        return -1;
    }
    root = json_loads(body, 0, &jerr);
    if (!json_is_object(root)) {
        json_decref(root);
        return -1;
    }
    memset(win, 0, sizeof(win));
    responses = json_object_get(root, "methodResponses");
    if (json_is_array(responses)) {
        for (i = 0; i < json_array_size(responses); i++) {
            json_t *call = json_array_get(responses, i);
            json_t *payload;
            const char *name;

            if (!json_is_array(call) || json_array_size(call) < 2) {
                continue;
            }
            name = json_string_value(json_array_get(call, 0));
            payload = json_array_get(call, 1);
            if (name == NULL) {
                continue;
            }
            if (strcmp(name, "error") == 0) {
                bad = 1;
                if (waiting) {
                    win[n - 1].error = 1;
                    waiting = 0;
                }
                continue;
            }
            if (strcmp(name, query_name) == 0) {
                if (waiting || n >= USAGE_BATCH_MAX || !json_is_object(payload)) {
                    bad = 1;
                    waiting = 0;
                    continue;
                }
                window_read_query(&win[n], payload);
                waiting = 1;
                n++;
                continue;
            }
            if (strcmp(name, get_name) == 0) {
                if (!waiting || !json_is_object(payload)) {
                    bad = 1;
                    continue;
                }
                window_read_get(&win[n - 1], payload, strcmp(store, "mail") == 0);
                waiting = 0;
            }
        }
    }
    json_decref(root);
    if (waiting) {
        bad = 1;
    }
    for (i = 0; i < (size_t)n; i++) {
        UsageWindow *w = &win[i];
        int j;

        if (!w->saw_query || !w->saw_get || w->error || w->not_found != 0 || w->unsized != 0 ||
            w->list_n != w->ids) {
            bad = 1;
        }
        if (i + 1 < (size_t)n && w->ids < win[0].ids) {
            bad = 1;
        }
        if (w->first_id[0] != '\0') {
            for (j = 0; j < (int)i; j++) {
                if (win[j].first_id[0] != '\0' && strcmp(w->first_id, win[j].first_id) == 0) {
                    bad = 1;
                }
            }
        }
        if (sum > INT64_MAX - w->bytes || body_sum > INT64_MAX - w->body_bytes ||
            part_sum > INT64_MAX - w->part_bytes || part_count > INT64_MAX - w->part_count ||
            ids_sum > 2147483647 - w->ids) {
            bad = 1;
        } else {
            sum += w->bytes;
            body_sum += w->body_bytes;
            part_sum += w->part_bytes;
            part_count += w->part_count;
            ids_sum += w->ids;
        }
    }
    out->ok = !bad && n > 0;
    out->windows = n;
    out->ids = ids_sum;
    out->bytes = sum;
    out->body_bytes = body_sum;
    out->attachment_bytes = part_sum;
    out->attachment_count = part_count;
    out->last_ids = n > 0 ? win[n - 1].ids : 0;
    if (n > 0) {
        snprintf(out->first_id, sizeof(out->first_id), "%s", win[0].first_id);
    }
    for (i = 0; i < (size_t)n; i++) {
        if (win[i].has_total) {
            out->has_total = 1;
            out->total = win[i].total;
            break;
        }
    }
    return 0;
}

char *pav_usage_measure_batch(const char *jmap_id, const char *store, const char *files_urn, int page,
                              const int *positions, int count) {
    const char *urn;
    const char *query;
    const char *get;
    json_t *root;
    json_t *using;
    json_t *calls;
    int i;

    if (!id_set(jmap_id) || store == NULL || page < 1 || positions == NULL || count < 1 ||
        count > USAGE_BATCH_MAX) {
        return NULL;
    }
    for (i = 0; i < count; i++) {
        if (positions[i] < 0) {
            return NULL;
        }
    }
    if (strcmp(store, "mail") == 0) {
        urn = URN_MAIL;
        query = "Email/query";
        get = "Email/get";
    } else if (strcmp(store, "files") == 0) {
        urn = id_set(files_urn) ? files_urn : URN_FILENODE;
        query = "FileNode/query";
        get = "FileNode/get";
    } else {
        return NULL;
    }
    root = json_object();
    using = json_array();
    calls = json_array();
    if (root == NULL || using == NULL || calls == NULL) {
        goto fail;
    }
    if (append_urn(using, URN_CORE) != 0 || append_urn(using, urn) != 0) {
        goto fail;
    }
    for (i = 0; i < count; i++) {
        char qtag[8];
        char gtag[8];

        snprintf(qtag, sizeof(qtag), "q%d", i);
        snprintf(gtag, sizeof(gtag), "g%d", i);
        if (append_method(calls, query_call(query, jmap_id, page, positions[i], 1, qtag)) != 0 ||
            append_method(calls, size_get_call(get, query, jmap_id, qtag, gtag)) != 0) {
            goto fail;
        }
    }
    if (json_object_set_new(root, "using", using) != 0) {
        using = NULL;
        goto fail;
    }
    using = NULL;
    if (json_object_set_new(root, "methodCalls", calls) != 0) {
        calls = NULL;
        goto fail;
    }
    return dump_request(root);

fail:
    json_decref(root);
    json_decref(using);
    json_decref(calls);
    return NULL;
}

static void usage_release(UsageCtx *ctx) {
    int last;

    pthread_mutex_lock(&ctx->mu);
    last = --ctx->refs == 0;
    pthread_mutex_unlock(&ctx->mu);
    if (!last) {
        return;
    }
    if (ctx->req.headers != NULL) {
        pav_secret_wipe_json(ctx->req.headers, "Authorization");
        json_decref(ctx->req.headers);
    }
    free(ctx->request);
    free(ctx->body);
    pthread_mutex_destroy(&ctx->mu);
    pthread_cond_destroy(&ctx->cv);
    free(ctx);
}

static void usage_done(PavReq *req, void *user) {
    UsageCtx *ctx = user;
    int abandoned;

    pthread_mutex_lock(&ctx->mu);
    abandoned = ctx->abandoned;
    if (!abandoned) {
        ctx->done = 1;
        ctx->status = req->status_code;
        ctx->auth_rc = req->auth_rc;
        if (req->body_out != NULL && req->body_out_len > 0 && req->body_out_len <= USAGE_BODY_MAX) {
            ctx->body = malloc(req->body_out_len + 1);
            if (ctx->body != NULL) {
                memcpy(ctx->body, req->body_out, req->body_out_len);
                ctx->body[req->body_out_len] = '\0';
                ctx->body_len = req->body_out_len;
            }
        }
        pthread_cond_broadcast(&ctx->cv);
    }
    pthread_mutex_unlock(&ctx->mu);
    if (req->resp_headers != NULL) {
        json_decref(req->resp_headers);
        req->resp_headers = NULL;
    }
    pav_net_free(req->body_out);
    req->body_out = NULL;
    usage_release(ctx);
}

/* One attempt. The body is copied only for HTTP 200. Caller frees. */
static char *jmap_exchange(const JmapAuth *auth, const char *request, int wait_ms) {
    UsageCtx *ctx;
    char *copy;
    char *body = NULL;
    int got = 0;
    struct timespec ts;

    if (auth == NULL || auth->url == NULL || strncmp(auth->url, "https://", 8) != 0 ||
        !id_set(request)) {
        return NULL;
    }
    copy = strdup(request);
    ctx = calloc(1, sizeof(*ctx));
    if (copy == NULL || ctx == NULL) {
        free(copy);
        free(ctx);
        return NULL;
    }
    pthread_mutex_init(&ctx->mu, NULL);
    pthread_cond_init(&ctx->cv, NULL);
    ctx->refs = 2;
    ctx->request = copy;
    snprintf(ctx->url, sizeof(ctx->url), "%s", auth->url);
    snprintf(ctx->account_id, sizeof(ctx->account_id), "%s",
             auth->account_id != NULL ? auth->account_id : "");
    snprintf(ctx->keyring_user, sizeof(ctx->keyring_user), "%s",
             auth->keyring_user != NULL ? auth->keyring_user : "");
    snprintf(ctx->username, sizeof(ctx->username), "%s",
             auth->username != NULL ? auth->username : "");
    snprintf(ctx->auth_method, sizeof(ctx->auth_method), "%s",
             auth->auth != NULL && auth->auth[0] != '\0' ? auth->auth : "basic");
    if (auth->ca != NULL && auth->ca[0] != '\0') {
        snprintf(ctx->ca_file, sizeof(ctx->ca_file), "%s", auth->ca);
    }
    ctx->req.method = "POST";
    ctx->req.url = ctx->url;
    ctx->req.account_id = ctx->account_id;
    ctx->req.keyring_user = ctx->keyring_user;
    ctx->req.username = ctx->username;
    ctx->req.auth_method = ctx->auth_method;
    ctx->req.ca_file = ctx->ca_file[0] != '\0' ? ctx->ca_file : NULL;
    ctx->req.body = ctx->request;
    ctx->req.body_len = strlen(ctx->request);
    ctx->req.headers = json_pack("{s:s}", "Content-Type", "application/json");
    ctx->req.on_done = usage_done;
    ctx->req.user = ctx;
    ctx->req.retry_count = PAV_NET_RETRY_MAX - 1;
    if (ctx->req.headers == NULL) {
        ctx->refs = 1;
        usage_release(ctx);
        return NULL;
    }
    usage_arm(&ctx->req);
    if (pav_net_request(&ctx->req) != 0) {
        usage_disarm(&ctx->req);
        ctx->refs = 1;
        usage_release(ctx);
        return NULL;
    }
    if (wait_ms < 1) {
        wait_ms = USAGE_QUICK_MS;
    }
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += wait_ms / 1000;
    ts.tv_nsec += (long)(wait_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
    }
    pthread_mutex_lock(&ctx->mu);
    while (!ctx->done) {
        struct timespec slice;

        if (usage_cancelled()) {
            ctx->req.cancelled = 1;
        }
        clock_gettime(CLOCK_REALTIME, &slice);
        slice.tv_nsec += 250000000L;
        if (slice.tv_nsec >= 1000000000L) {
            slice.tv_sec++;
            slice.tv_nsec -= 1000000000L;
        }
        if (slice.tv_sec > ts.tv_sec || (slice.tv_sec == ts.tv_sec && slice.tv_nsec > ts.tv_nsec)) {
            slice = ts;
        }
        if (pthread_cond_timedwait(&ctx->cv, &ctx->mu, &slice) == ETIMEDOUT &&
            slice.tv_sec == ts.tv_sec && slice.tv_nsec == ts.tv_nsec) {
            break;
        }
    }
    got = ctx->done;
    if (got && ctx->auth_rc == 0 && ctx->status == 200 && ctx->body != NULL) {
        body = ctx->body;
        ctx->body = NULL;
    } else if (!got) {
        ctx->abandoned = 1;
        ctx->req.cancelled = 1;
    }
    pthread_mutex_unlock(&ctx->mu);
    usage_disarm(&ctx->req);
    usage_release(ctx);
    return body;
}

static char *load_session(const char *account_id) {
    static const char k_key[] = "cache.usage_session";
    static const char k_sql[] = "SELECT session_json FROM account_state WHERE account_id = ?";
    PavDbRead *read = NULL;
    sqlite3_stmt *stmt = NULL;
    char *text = NULL;
    const unsigned char *col;

    if (pav_db_read_begin(&read) != 0) {
        return NULL;
    }
    if (pav_db_prepare(pav_db_read_handle(read), k_key, k_sql, &stmt) != 0 ||
        pav_db_bind_text(stmt, 1, account_id) != 0) {
        pav_db_read_end(read);
        return NULL;
    }
    if (pav_db_step(stmt) == SQLITE_ROW) {
        col = sqlite3_column_text(stmt, 0);
        if (col != NULL && col[0] != '\0') {
            text = strdup((const char *)col);
        }
    }
    pav_db_read_end(read);
    return text;
}

static char *load_kv(const char *key) {
    static const char k_key[] = "limits.server_get";
    static const char k_sql[] = "SELECT value FROM kv WHERE key = ?";
    PavDbRead *read = NULL;
    sqlite3_stmt *stmt = NULL;
    char *text = NULL;
    const unsigned char *col;

    if (pav_db_read_begin(&read) != 0) {
        pav_log("store", PAV_WARN, 0, "cannot read measured sizes");
        return NULL;
    }
    if (pav_db_prepare(pav_db_read_handle(read), k_key, k_sql, &stmt) != 0 ||
        pav_db_bind_text(stmt, 1, key) != 0) {
        pav_db_read_end(read);
        return NULL;
    }
    if (pav_db_step(stmt) == SQLITE_ROW) {
        col = sqlite3_column_text(stmt, 0);
        if (col != NULL && col[0] != '\0') {
            text = strdup((const char *)col);
        }
    }
    pav_db_read_end(read);
    return text;
}

static int kv_put_job(void *ctx, sqlite3 *db) {
    static const char k_key[] = "limits.server_put";
    static const char k_sql[] = "INSERT OR REPLACE INTO kv (key, value) VALUES (?, ?)";
    KvPut *put = ctx;
    sqlite3_stmt *stmt = NULL;

    if (put == NULL || pav_db_prepare(db, k_key, k_sql, &stmt) != 0 ||
        pav_db_bind_text(stmt, 1, put->key) != 0 ||
        pav_db_bind_text(stmt, 2, put->value) != 0) {
        return -1;
    }
    return pav_db_step(stmt) == SQLITE_DONE ? 0 : -1;
}

static int session_has(const char *session, const char *urn) {
    return session != NULL && urn != NULL && strstr(session, urn) != NULL;
}

static const char *session_files_urn(const char *session) {
    if (session_has(session, URN_FILENODE)) {
        return URN_FILENODE;
    }
    if (session_has(session, "urn:ietf:params:jmap:files:0")) {
        return "urn:ietf:params:jmap:files:0";
    }
    if (session_has(session, "urn:apache:jmap:files")) {
        return "urn:apache:jmap:files";
    }
    if (session_has(session, "urn:ietf:params:jmap:files")) {
        return "urn:ietf:params:jmap:files";
    }
    return NULL;
}

static int copy_id(const char *id, char *out, size_t len) {
    size_t n;

    if (!id_set(id) || out == NULL || len == 0) {
        return -1;
    }
    n = strlen(id);
    if (n >= len) {
        return -1;
    }
    memcpy(out, id, n + 1);
    return 0;
}

static const char *personal_id(json_t *accounts) {
    const char *key;
    const char *first = NULL;
    json_t *acct;
    size_t i;

    if (json_is_object(accounts)) {
        json_object_foreach(accounts, key, acct) {
            if (!id_set(key)) {
                continue;
            }
            if (first == NULL) {
                first = key;
            }
            if (json_is_true(json_object_get(acct, "isPersonal"))) {
                return key;
            }
        }
        return first;
    }
    if (json_is_array(accounts)) {
        json_array_foreach(accounts, i, acct) {
            const char *id = json_string_value(json_object_get(acct, "id"));

            if (!id_set(id)) {
                continue;
            }
            if (first == NULL) {
                first = id;
            }
            if (json_is_true(json_object_get(acct, "isPersonal"))) {
                return id;
            }
        }
        return first;
    }
    return NULL;
}

static int jmap_account_id(json_t *session, const char *urn, char *out, size_t len) {
    json_t *primary;
    const char *id = NULL;

    out[0] = '\0';
    if (!json_is_object(session)) {
        return -1;
    }
    primary = json_object_get(session, "primaryAccounts");
    if (json_is_object(primary)) {
        if (urn != NULL) {
            id = json_string_value(json_object_get(primary, urn));
        }
        if (!id_set(id)) {
            id = json_string_value(json_object_get(primary, URN_MAIL));
        }
    }
    if (!id_set(id)) {
        id = personal_id(json_object_get(session, "accounts"));
    }
    return copy_id(id, out, len);
}

static int add_empty_figure(json_t *row, const char *name) {
    json_t *fig;

    if (name != NULL && strcmp(name, "mail") == 0) {
        fig = json_pack("{s:n,s:n,s:n,s:n,s:n}", "count", "bytes", "bodiesBytes", "attachmentsBytes",
                        "attachmentsCount");
    } else {
        fig = json_pack("{s:n,s:n}", "count", "bytes");
    }
    if (fig == NULL) {
        return -1;
    }
    if (json_object_set_new(row, name, fig) != 0) {
        return -1;
    }
    return 0;
}

static json_t *account_row(const char *id, const char *name) {
    json_t *row = json_object();

    if (row == NULL) {
        return NULL;
    }
    if (json_object_set_new(row, "id", json_string(id)) != 0 ||
        json_object_set_new(row, "name", json_string(name != NULL && name[0] != '\0' ? name : id)) != 0 ||
        json_object_set_new(row, "bodiesBytes", json_integer(0)) != 0 ||
        json_object_set_new(row, "blobsBytes", json_integer(0)) != 0 ||
        json_object_set_new(row, "serverBytes", json_null()) != 0 ||
        add_empty_figure(row, "mail") != 0 ||
        add_empty_figure(row, "contacts") != 0 ||
        add_empty_figure(row, "calendars") != 0 ||
        add_empty_figure(row, "files") != 0) {
        json_decref(row);
        return NULL;
    }
    return row;
}

static json_t *figure_json(const PavStoreFigure *fig, int split) {
    json_t *count;
    json_t *bytes;
    json_t *obj;

    count = fig != NULL && fig->has_count ? json_integer(fig->count) : json_null();
    bytes = fig != NULL && fig->has_bytes ? json_integer(fig->bytes) : json_null();
    if (count == NULL || bytes == NULL) {
        json_decref(count);
        json_decref(bytes);
        return NULL;
    }
    obj = json_object();
    if (obj == NULL) {
        json_decref(count);
        json_decref(bytes);
        return NULL;
    }
    if (json_object_set_new(obj, "count", count) != 0) {
        json_decref(bytes);
        json_decref(obj);
        return NULL;
    }
    if (json_object_set_new(obj, "bytes", bytes) != 0) {
        json_decref(obj);
        return NULL;
    }
    if (split) {
        json_t *bodies = fig != NULL && fig->has_split ? json_integer(fig->body_bytes) : json_null();
        json_t *parts = fig != NULL && fig->has_split ? json_integer(fig->part_bytes) : json_null();
        json_t *part_count = fig != NULL && fig->has_split ? json_integer(fig->part_count) : json_null();

        if (bodies == NULL || parts == NULL || part_count == NULL) {
            json_decref(bodies);
            json_decref(parts);
            json_decref(part_count);
            json_decref(obj);
            return NULL;
        }
        if (json_object_set_new(obj, "bodiesBytes", bodies) != 0) {
            json_decref(parts);
            json_decref(part_count);
            json_decref(obj);
            return NULL;
        }
        if (json_object_set_new(obj, "attachmentsBytes", parts) != 0) {
            json_decref(part_count);
            json_decref(obj);
            return NULL;
        }
        if (json_object_set_new(obj, "attachmentsCount", part_count) != 0) {
            json_decref(obj);
            return NULL;
        }
    }
    return obj;
}

static int put_figure(json_t *row, const char *name, const PavStoreFigure *fig) {
    json_t *obj = figure_json(fig, name != NULL && strcmp(name, "mail") == 0);

    if (obj == NULL) {
        return -1;
    }
    if (json_object_set_new(row, name, obj) != 0) {
        return -1;
    }
    return 0;
}

static json_t *find_row(json_t *accounts, const char *id) {
    size_t i;
    json_t *row;

    json_array_foreach(accounts, i, row) {
        const char *have = json_string_value(json_object_get(row, "id"));

        if (have != NULL && strcmp(have, id) == 0) {
            return row;
        }
    }
    return NULL;
}

static void add_local(json_t *row, const char *field, int64_t extra) {
    int64_t cur = json_integer_value(json_object_get(row, field));

    json_object_set_new(row, field, json_integer(cur + extra));
}

/* The worker does not walk profile->doc. The core thread edits that object.
   The profile file is replaced by rename, so this sees one complete copy. */
static json_t *accounts_from_file(const char *path) {
    FILE *f;
    long n;
    char *buf;
    size_t got;
    json_error_t jerr;
    json_t *root;
    json_t *accounts;
    json_t *copy;

    if (path == NULL || path[0] == '\0') {
        return json_array();
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return json_array();
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return json_array();
    }
    n = ftell(f);
    if (n < 0 || n > 1024 * 1024) {
        fclose(f);
        return json_array();
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return json_array();
    }
    buf = malloc((size_t)n + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) {
        free(buf);
        return json_array();
    }
    buf[n] = '\0';
    root = json_loads(buf, 0, &jerr);
    free(buf);
    if (!json_is_object(root)) {
        json_decref(root);
        return json_array();
    }
    accounts = json_object_get(root, "accounts");
    copy = json_is_array(accounts) ? json_deep_copy(accounts) : json_array();
    json_decref(root);
    return copy;
}

static void note_bytes(json_t *fresh, const char *account_id, const char *key, int64_t bytes) {
    json_t *acct;
    json_t *value;

    if (fresh == NULL || !id_set(account_id) || key == NULL) {
        return;
    }
    acct = json_object_get(fresh, account_id);
    if (!json_is_object(acct)) {
        json_t *created = json_object();

        if (created == NULL || json_object_set_new(fresh, account_id, created) != 0) {
            return;
        }
        acct = created;
    }
    value = json_integer(bytes);
    if (value == NULL || json_object_set_new(acct, key, value) != 0) {
        return;
    }
}

typedef struct UsageRun {
    const char *account_id;
    int64_t scanned;
    int64_t known_part;
    int pending_unknown;
    int64_t scan_total;
    int64_t last_event_ms;
} UsageRun;

static int session_cap(json_t *session, const char *field, int fallback, int cap) {
    json_t *caps;
    json_t *core;
    json_t *n;
    json_int_t v;

    if (!json_is_object(session) || field == NULL) {
        return fallback;
    }
    caps = json_object_get(session, "capabilities");
    core = json_is_object(caps) ? json_object_get(caps, URN_CORE) : NULL;
    n = json_is_object(core) ? json_object_get(core, field) : NULL;
    if (!json_is_integer(n)) {
        return fallback;
    }
    v = json_integer_value(n);
    if (v < 1) {
        return fallback;
    }
    if (v > cap) {
        return cap;
    }
    return (int)v;
}

static void post_progress(UsageRun *run, const char *store, int64_t done, int64_t total, int64_t bytes,
                          int force) {
    json_t *obj;
    char *text;
    int64_t now;
    int64_t scanned;

    if (run == NULL || !id_set(run->account_id) || store == NULL) {
        return;
    }
    now = pav_now_ms();
    if (!force && run->last_event_ms >= 0 && now >= 0 && now - run->last_event_ms < USAGE_PROGRESS_MS) {
        return;
    }
    scanned = run->scanned;
    if (done > 0 && scanned <= INT64_MAX - done) {
        scanned += done;
    }
    obj = json_object();
    if (obj == NULL) {
        return;
    }
    if (json_object_set_new(obj, "accountId", json_string(run->account_id)) != 0 ||
        json_object_set_new(obj, "store", json_string(store)) != 0 ||
        json_object_set_new(obj, "done", json_integer(done)) != 0 ||
        json_object_set_new(obj, "total", json_integer(total > 0 ? total : 0)) != 0 ||
        json_object_set_new(obj, "bytes", json_integer(bytes)) != 0 ||
        json_object_set_new(obj, "scanned", json_integer(scanned)) != 0 ||
        json_object_set_new(obj, "scanTotal", json_integer(run->scan_total)) != 0) {
        json_decref(obj);
        return;
    }
    text = json_dumps(obj, JSON_COMPACT);
    json_decref(obj);
    if (text == NULL) {
        return;
    }
    (void)pav_event_post("cache.change", text);
    free(text);
    if (now >= 0) {
        run->last_event_ms = now;
    }
}

typedef struct UsageWalk {
    int64_t bytes;
    int64_t body_bytes;
    int64_t attachment_bytes;
    int64_t attachment_count;
} UsageWalk;

static void walk_set(UsageWalk *out, int64_t bytes, int64_t bodies, int64_t parts, int64_t part_count) {
    out->bytes = bytes;
    out->body_bytes = bodies;
    out->attachment_bytes = parts;
    out->attachment_count = part_count;
}

/* 0 finished, 1 cancelled, -1 failed. A later page may omit total.
   Mail also fills the decoded text and attachment sums. Files leave those at 0. */
static int walk_sizes(const JmapAuth *auth, const char *jmap_id, const char *store, const char *files_urn,
                      int page, int windows, int64_t known_total, UsageRun *run, UsageWalk *out) {
    int64_t total = known_total;
    int total_known = known_total >= 0;
    int64_t position = 0;
    int64_t sum = 0;
    int64_t body_sum = 0;
    int64_t part_sum = 0;
    int64_t count_sum = 0;
    int pages = 0;
    char prev_first[256];

    if (out == NULL || !id_set(jmap_id) || store == NULL || page < 1) {
        return -1;
    }
    if (windows < 1) {
        windows = 1;
    }
    if (windows > USAGE_BATCH_MAX) {
        windows = USAGE_BATCH_MAX;
    }
    prev_first[0] = '\0';
    while (pages < USAGE_MAX_PAGES) {
        int pos[USAGE_BATCH_MAX];
        int nwin = 0;
        int64_t cursor = position;
        char *request;
        char *body;
        PavUsageBatch batch;

        if (usage_cancelled()) {
            return 1;
        }
        if (total_known && position >= total) {
            walk_set(out, sum, body_sum, part_sum, count_sum);
            post_progress(run, store, position, total, sum, 1);
            return 0;
        }
        while (nwin < windows && pages + nwin < USAGE_MAX_PAGES) {
            if (total_known && cursor >= total) {
                break;
            }
            if (cursor > 2147483647LL) {
                return -1;
            }
            pos[nwin++] = (int)cursor;
            if (cursor > INT64_MAX - page) {
                return -1;
            }
            cursor += page;
        }
        if (nwin == 0) {
            walk_set(out, sum, body_sum, part_sum, count_sum);
            post_progress(run, store, position, total_known ? total : 0, sum, 1);
            return 0;
        }
        request = pav_usage_measure_batch(jmap_id, store, files_urn, page, pos, nwin);
        if (request == NULL) {
            return -1;
        }
        body = jmap_exchange(auth, request, USAGE_MEASURE_MS);
        free(request);
        if (usage_cancelled()) {
            free(body);
            return 1;
        }
        if (body == NULL || pav_usage_parse_batch(body, store, &batch) != 0) {
            free(body);
            if (nwin > 1) {
                windows = 1;
                continue;
            }
            return -1;
        }
        free(body);
        if (!batch.ok) {
            return -1;
        }
        if (batch.first_id[0] != '\0' && strcmp(batch.first_id, prev_first) == 0) {
            return -1;
        }
        if (batch.first_id[0] != '\0') {
            snprintf(prev_first, sizeof(prev_first), "%s", batch.first_id);
        }
        if (sum > INT64_MAX - batch.bytes || body_sum > INT64_MAX - batch.body_bytes ||
            part_sum > INT64_MAX - batch.attachment_bytes ||
            count_sum > INT64_MAX - batch.attachment_count || position > INT64_MAX - batch.ids) {
            return -1;
        }
        sum += batch.bytes;
        body_sum += batch.body_bytes;
        part_sum += batch.attachment_bytes;
        count_sum += batch.attachment_count;
        position += batch.ids;
        pages += batch.windows;
        if (batch.has_total && !total_known) {
            total = batch.total;
            total_known = 1;
            if (run != NULL && run->pending_unknown > 0) {
                if (run->known_part <= INT64_MAX - total) {
                    run->known_part += total;
                }
                run->pending_unknown--;
                run->scan_total = run->pending_unknown == 0 ? run->known_part : 0;
            }
        }
        post_progress(run, store, position, total_known ? total : 0, sum, 0);
        if (total_known && position >= total) {
            walk_set(out, sum, body_sum, part_sum, count_sum);
            post_progress(run, store, position, total, sum, 1);
            return 0;
        }
        if (batch.last_ids < page) {
            if (!total_known || position >= total) {
                walk_set(out, sum, body_sum, part_sum, count_sum);
                post_progress(run, store, position, total_known ? total : 0, sum, 1);
                return 0;
            }
            return -1;
        }
    }
    return -1;
}

static int finish_size(PavStoreFigure *fig, int enabled, int measure, json_t *fresh, const char *account_id,
                       const char *key, const JmapAuth *auth, const char *jmap_id, const char *store,
                       const char *files_urn, int page, int windows, UsageRun *run) {
    int split = store != NULL && strcmp(store, "mail") == 0;
    UsageWalk got;
    int rc;

    if (!enabled) {
        return 0;
    }
    /* A quota size is not a split. Mail is walked on Check sizes anyway.
       A count of 0 is 0 for the whole message and for both parts, with no walk. */
    if (fig->has_count && fig->count == 0) {
        fig->has_bytes = 1;
        fig->bytes = 0;
        if (split) {
            fig->has_split = 1;
            fig->body_bytes = 0;
            fig->part_bytes = 0;
            fig->part_count = 0;
        }
        if (measure) {
            note_bytes(fresh, account_id, key, 0);
            if (split) {
                note_bytes(fresh, account_id, "mailBodiesBytes", 0);
                note_bytes(fresh, account_id, "mailAttachmentsBytes", 0);
                note_bytes(fresh, account_id, "mailAttachmentsCount", 0);
            }
        }
        return 0;
    }
    if (!measure) {
        return 0;
    }
    if (fig->has_bytes && !split) {
        return 0;
    }
    memset(&got, 0, sizeof(got));
    rc = walk_sizes(auth, jmap_id, store, files_urn, page, windows, fig->has_count ? fig->count : -1, run, &got);
    if (rc == 1) {
        return 1;
    }
    if (rc != 0) {
        pav_log("store", PAV_WARN, 1, "size check failed for %s", account_id);
        return 0;
    }
    fig->has_bytes = 1;
    fig->bytes = got.bytes;
    note_bytes(fresh, account_id, key, got.bytes);
    if (split) {
        fig->has_split = 1;
        fig->body_bytes = got.body_bytes;
        fig->part_bytes = got.attachment_bytes;
        fig->part_count = got.attachment_count;
        note_bytes(fresh, account_id, "mailBodiesBytes", got.body_bytes);
        note_bytes(fresh, account_id, "mailAttachmentsBytes", got.attachment_bytes);
        note_bytes(fresh, account_id, "mailAttachmentsCount", got.attachment_count);
    }
    if (run != NULL && fig->has_count && run->scanned <= INT64_MAX - fig->count) {
        run->scanned += fig->count;
    }
    return 0;
}

static int cached_kind_bytes(json_t *cache, const char *account_id, const char *key, int64_t *out) {
    json_t *accounts;
    json_t *acct;
    json_t *val;

    if (!json_is_object(cache) || !id_set(account_id)) {
        return 0;
    }
    accounts = json_object_get(cache, "accounts");
    acct = json_is_object(accounts) ? json_object_get(accounts, account_id) : NULL;
    val = json_is_object(acct) ? json_object_get(acct, key) : NULL;
    if (!json_is_integer(val) || json_integer_value(val) < 0) {
        return 0;
    }
    *out = json_integer_value(val);
    return 1;
}

static void merge_cached(json_t *row, json_t *cache, const char *account_id, const char *field,
                         const char *key) {
    json_t *fig = json_object_get(row, field);
    int64_t bytes = 0;

    if (!json_is_object(fig) || !json_is_null(json_object_get(fig, "bytes"))) {
        return;
    }
    if (!cached_kind_bytes(cache, account_id, key, &bytes)) {
        return;
    }
    json_object_set_new(fig, "bytes", json_integer(bytes));
}

/* Both split keys are stored together. One without the other is ignored. */
static void merge_cached_split(json_t *row, json_t *cache, const char *account_id) {
    json_t *mail = json_object_get(row, "mail");
    int64_t bodies = 0;
    int64_t parts = 0;
    int64_t count = 0;

    if (!json_is_object(mail) || !json_is_null(json_object_get(mail, "bodiesBytes"))) {
        return;
    }
    if (!cached_kind_bytes(cache, account_id, "mailBodiesBytes", &bodies) ||
        !cached_kind_bytes(cache, account_id, "mailAttachmentsBytes", &parts)) {
        return;
    }
    json_object_set_new(mail, "bodiesBytes", json_integer(bodies));
    json_object_set_new(mail, "attachmentsBytes", json_integer(parts));
    if (cached_kind_bytes(cache, account_id, "mailAttachmentsCount", &count)) {
        json_object_set_new(mail, "attachmentsCount", json_integer(count));
    }
}

static int resolve_id(json_t *session, int wanted, const char *urn, char *out, size_t len,
                      const char **dest) {
    *dest = NULL;
    out[0] = '\0';
    if (!wanted) {
        return 0;
    }
    if (jmap_account_id(session, urn, out, len) != 0) {
        return 0;
    }
    *dest = out;
    return 1;
}

static int fill_server(json_t *row, const char *profile_id, json_t *acct, int measure,
                       json_t *cache, json_t *fresh) {
    const char *id = json_string_value(json_object_get(row, "id"));
    const char *kind = json_string_value(json_object_get(acct, "kind"));
    const char *username = json_string_value(json_object_get(acct, "username"));
    const char *auth_method = json_string_value(json_object_get(acct, "auth"));
    const char *ca = json_string_value(json_object_get(acct, "caFile"));
    char *session_json = NULL;
    json_t *session = NULL;
    const char *files_urn = NULL;
    char quota_buf[256];
    char mail_buf[256];
    char contacts_buf[256];
    char calendars_buf[256];
    char files_buf[256];
    char api_url[2048];
    char keyring_user[192];
    const char *quota_id = NULL;
    const char *mail_id = NULL;
    const char *contacts_id = NULL;
    const char *calendars_id = NULL;
    const char *files_id = NULL;
    int want_quota = 0;
    int want_mail = 0;
    int want_contacts = 0;
    int want_calendars = 0;
    int want_files = 0;
    PavUsageAsk ask;
    PavUsageParse parsed;
    JmapAuth auth;
    UsageRun run;
    char *request = NULL;
    char *body = NULL;
    int page = USAGE_PAGE_CAP;
    int windows = 1;
    int rc = 0;

    if (!id_set(id)) {
        return 0;
    }
    if (kind != NULL && kind[0] != '\0' && strcmp(kind, "jmap") != 0) {
        return 0;
    }
    session_json = load_session(id);
    if (session_json == NULL) {
        return 0;
    }
    want_quota = session_has(session_json, URN_QUOTA);
    want_mail = session_has(session_json, URN_MAIL);
    want_contacts = session_has(session_json, URN_CONTACTS);
    want_calendars = session_has(session_json, URN_CALENDARS);
    files_urn = session_files_urn(session_json);
    want_files = files_urn != NULL;
    if (!want_quota && !want_mail && !want_contacts && !want_calendars && !want_files) {
        free(session_json);
        return 0;
    }
    session = json_loads(session_json, 0, NULL);
    page = session_cap(session, "maxObjectsInGet", USAGE_PAGE_CAP, USAGE_PAGE_CAP);
    windows = session_cap(session, "maxCallsInRequest", USAGE_BATCH_MAX * 2, 1000000) / 2;
    if (windows < 1) {
        windows = 1;
    }
    if (windows > USAGE_BATCH_MAX) {
        windows = USAGE_BATCH_MAX;
    }
    api_url[0] = '\0';
    if (json_is_object(session)) {
        const char *url = json_string_value(json_object_get(session, "apiUrl"));

        if (url != NULL) {
            snprintf(api_url, sizeof(api_url), "%s", url);
        }
    }
    resolve_id(session, want_quota, URN_QUOTA, quota_buf, sizeof(quota_buf), &quota_id);
    resolve_id(session, want_mail, URN_MAIL, mail_buf, sizeof(mail_buf), &mail_id);
    resolve_id(session, want_contacts, URN_CONTACTS, contacts_buf, sizeof(contacts_buf), &contacts_id);
    resolve_id(session, want_calendars, URN_CALENDARS, calendars_buf, sizeof(calendars_buf), &calendars_id);
    resolve_id(session, want_files, files_urn, files_buf, sizeof(files_buf), &files_id);
    json_decref(session);
    free(session_json);
    session_json = NULL;

    memset(&parsed, 0, sizeof(parsed));
    memset(&ask, 0, sizeof(ask));
    memset(&run, 0, sizeof(run));
    run.account_id = id;
    run.last_event_ms = -1;
    ask.quota_id = quota_id;
    ask.mail_id = mail_id;
    ask.contacts_id = contacts_id;
    ask.calendars_id = calendars_id;
    ask.files_id = files_id;
    ask.files_urn = files_urn;
    snprintf(keyring_user, sizeof(keyring_user), "%s/%s", profile_id != NULL ? profile_id : "", id);
    auth.url = api_url;
    auth.account_id = id;
    auth.keyring_user = keyring_user;
    auth.username = username;
    auth.auth = auth_method;
    auth.ca = ca;
    if (id_set(username) && strncmp(api_url, "https://", 8) == 0) {
        request = pav_usage_quick_request(&ask);
        if (request != NULL) {
            body = jmap_exchange(&auth, request, USAGE_QUICK_MS);
            free(request);
            request = NULL;
            if (body == NULL || pav_usage_parse(body, &parsed) != 0) {
                memset(&parsed, 0, sizeof(parsed));
                pav_log("store", PAV_WARN, 1, "quota unavailable for %s", id);
            }
            free(body);
        }
    }
    if (measure && want_mail && !(parsed.mail.has_count && parsed.mail.count == 0)) {
        if (parsed.mail.has_count) {
            run.known_part += parsed.mail.count;
        } else {
            run.pending_unknown++;
        }
    }
    if (measure && want_files && !parsed.files.has_bytes &&
        !(parsed.files.has_count && parsed.files.count == 0)) {
        if (parsed.files.has_count) {
            run.known_part += parsed.files.count;
        } else {
            run.pending_unknown++;
        }
    }
    run.scan_total = run.pending_unknown == 0 ? run.known_part : 0;
    rc = finish_size(&parsed.mail, want_mail, measure, fresh, id, "mailBytes", &auth, mail_id, "mail", NULL,
                     page, windows > USAGE_MAIL_BATCH_MAX ? USAGE_MAIL_BATCH_MAX : windows,
                     measure ? &run : NULL);
    if (rc != 1) {
        int files_rc = finish_size(&parsed.files, want_files, measure, fresh, id, "filesBytes", &auth, files_id,
                                   "files", files_urn, page, windows, measure ? &run : NULL);

        if (files_rc == 1) {
            rc = 1;
        }
    }
    if (put_figure(row, "mail", &parsed.mail) != 0 ||
        put_figure(row, "contacts", &parsed.contacts) != 0 ||
        put_figure(row, "calendars", &parsed.calendars) != 0 ||
        put_figure(row, "files", &parsed.files) != 0) {
        pav_log("store", PAV_WARN, 1, "quota unavailable for %s", id);
        return rc;
    }
    if (parsed.has_server_bytes) {
        json_object_set_new(row, "serverBytes", json_integer(parsed.server_bytes));
    }
    if (want_mail && !parsed.mail.has_bytes) {
        merge_cached(row, cache, id, "mail", "mailBytes");
    }
    if (want_files && !parsed.files.has_bytes) {
        merge_cached(row, cache, id, "files", "filesBytes");
    }
    if (want_mail) {
        merge_cached_split(row, cache, id);
    }
    return rc;
}

static int cache_checked_at(json_t *cache, int64_t *out) {
    json_t *ts = json_is_object(cache) ? json_object_get(cache, "checkedAt") : NULL;

    if (!json_is_integer(ts) || json_integer_value(ts) < 0) {
        return 0;
    }
    *out = json_integer_value(ts);
    return 1;
}

static int save_measurements(json_t *old_cache, json_t *fresh, int64_t now) {
    json_t *root;
    json_t *accounts;
    json_t *old_accounts;
    const char *id;
    json_t *got;
    char *text;
    KvPut put;
    int rc;

    root = json_object();
    old_accounts = json_is_object(old_cache) ? json_object_get(old_cache, "accounts") : NULL;
    accounts = json_is_object(old_accounts) ? json_deep_copy(old_accounts) : json_object();
    if (root == NULL || accounts == NULL) {
        json_decref(root);
        json_decref(accounts);
        return -1;
    }
    json_object_foreach(fresh, id, got) {
        json_t *prev = json_object_get(accounts, id);
        json_t *next = json_is_object(prev) ? json_deep_copy(prev) : json_object();
        json_t *mail = json_is_object(got) ? json_object_get(got, "mailBytes") : NULL;
        json_t *mail_bodies = json_is_object(got) ? json_object_get(got, "mailBodiesBytes") : NULL;
        json_t *mail_parts = json_is_object(got) ? json_object_get(got, "mailAttachmentsBytes") : NULL;
        json_t *mail_count = json_is_object(got) ? json_object_get(got, "mailAttachmentsCount") : NULL;
        json_t *files = json_is_object(got) ? json_object_get(got, "filesBytes") : NULL;

        if (next == NULL) {
            continue;
        }
        if (json_is_integer(mail)) {
            json_object_set(next, "mailBytes", mail);
        }
        if (json_is_integer(mail_bodies)) {
            json_object_set(next, "mailBodiesBytes", mail_bodies);
        }
        if (json_is_integer(mail_parts)) {
            json_object_set(next, "mailAttachmentsBytes", mail_parts);
        }
        if (json_is_integer(mail_count)) {
            json_object_set(next, "mailAttachmentsCount", mail_count);
        }
        if (json_is_integer(files)) {
            json_object_set(next, "filesBytes", files);
        }
        if (json_object_set_new(accounts, id, next) != 0) {
            continue;
        }
    }
    if (json_object_set_new(root, "checkedAt", json_integer(now)) != 0) {
        json_decref(root);
        json_decref(accounts);
        return -1;
    }
    if (json_object_set_new(root, "accounts", accounts) != 0) {
        json_decref(root);
        return -1;
    }
    text = json_dumps(root, JSON_COMPACT);
    json_decref(root);
    if (text == NULL) {
        return -1;
    }
    put.key = KV_LIMITS;
    put.value = text;
    rc = pav_db_call(kv_put_job, &put);
    free(text);
    return rc;
}

static json_t *volume_free_json(const char *profile_path) {
    const PavPlatform *platform = pav_platform();
    char root[4096];
    const char *path = NULL;
    int64_t bytes = 0;

    if (platform == NULL || platform->volume_free == NULL) {
        return json_null();
    }
    if (pav_blob_root(root, sizeof(root)) == 0 && pav_is_dir(root)) {
        path = root;
    } else if (profile_path != NULL && profile_path[0] != '\0') {
        path = profile_path;
    }
    if (path == NULL || platform->volume_free(path, &bytes) != 0) {
        return json_null();
    }
    return json_integer(bytes);
}

int pav_cache_usage_preempt(const char *name, json_t *args, char **reply) {
    const char *key;
    json_t *value;
    json_t *data;
    int nkeys = 0;
    int cancel = 0;

    if (reply == NULL) {
        return 0;
    }
    *reply = NULL;
    if (name == NULL || strcmp(name, "cache.usage") != 0 || !json_is_object(args)) {
        return 0;
    }
    json_object_foreach(args, key, value) {
        nkeys++;
        if (nkeys > 1 || strcmp(key, "cancel") != 0 || !json_is_true(value)) {
            return 0;
        }
        cancel = 1;
    }
    if (!cancel) {
        return 0;
    }
    usage_signal_cancel();
    data = json_pack("{s:b}", "cancelled", 1);
    if (data == NULL) {
        return 1;
    }
    *reply = pav_reply_ok(data);
    json_decref(data);
    return 1;
}

int pav_cache_usage(json_t *args, json_t **data, PavError *err) {
    PavConfigLoad *load;
    json_t *profile_accounts = NULL;
    json_t *result;
    json_t *rows;
    json_t *cache = NULL;
    json_t *fresh = NULL;
    char profile_id[64];
    const char *key;
    json_t *value;
    PavCacheUse *local = NULL;
    size_t local_n = 0;
    size_t i;
    int64_t bodies = 0;
    int64_t blobs = 0;
    int64_t checked = 0;
    int has_checked = 0;
    int measure = 0;
    int cancel = 0;
    int saw_measure = 0;
    int cancelled_early = 0;
    int cancelled = 0;
    int measure_started = 0;
    char buf[256];
    char *cache_text;

    if (data == NULL) {
        pav_error_set(err, "internal", "missing data", 0);
        return -1;
    }
    *data = NULL;
    json_object_foreach(args, key, value) {
        if (strcmp(key, "measure") == 0) {
            if (!json_is_boolean(value)) {
                pav_error_set(err, "bad_args", "measure is not a boolean", 0);
                return -1;
            }
            saw_measure = 1;
            measure = json_is_true(value);
            continue;
        }
        if (strcmp(key, "cancel") == 0) {
            if (!json_is_boolean(value)) {
                pav_error_set(err, "bad_args", "cancel is not a boolean", 0);
                return -1;
            }
            cancel = json_is_true(value);
            continue;
        }
        snprintf(buf, sizeof(buf), "unknown argument %s", key);
        pav_error_set(err, "bad_args", buf, 0);
        return -1;
    }
    if (cancel && saw_measure) {
        pav_error_set(err, "bad_args", "cancel cannot be combined with measure", 0);
        return -1;
    }
    if (cancel) {
        usage_signal_cancel();
        *data = json_pack("{s:b}", "cancelled", 1);
        if (*data == NULL) {
            pav_error_set(err, "internal", "out of memory", 0);
            return -1;
        }
        return 0;
    }
    load = pav_core_load();
    if (load == NULL || load->profile == NULL || strcmp(load->state, "ready") != 0) {
        pav_error_set(err, "not_ready", "profile is not ready", 0);
        return -1;
    }
    snprintf(profile_id, sizeof(profile_id), "%s", load->profile->id);
    profile_accounts = accounts_from_file(load->profile_path);
    if (profile_accounts == NULL) {
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    result = json_object();
    rows = json_array();
    if (result == NULL || rows == NULL) {
        json_decref(result);
        json_decref(rows);
        json_decref(profile_accounts);
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    json_array_foreach(profile_accounts, i, value) {
        const char *id = json_string_value(json_object_get(value, "id"));
        json_t *row;

        if (!id_set(id)) {
            continue;
        }
        row = account_row(id, json_string_value(json_object_get(value, "name")));
        if (row == NULL || json_array_append_new(rows, row) != 0) {
            json_decref(rows);
            json_decref(result);
            json_decref(profile_accounts);
            pav_error_set(err, "internal", "out of memory", 0);
            return -1;
        }
    }
    if (pav_cache_local_usage(&local, &local_n) != 0) {
        json_decref(rows);
        json_decref(result);
        json_decref(profile_accounts);
        pav_error_set(err, "internal", "cannot read cache usage", 0);
        return -1;
    }
    for (i = 0; i < local_n; i++) {
        json_t *row = find_row(rows, local[i].account_id);

        if (row == NULL) {
            row = account_row(local[i].account_id, NULL);
            if (row == NULL || json_array_append_new(rows, row) != 0) {
                pav_cache_local_usage_free(local, local_n);
                json_decref(rows);
                json_decref(result);
                json_decref(profile_accounts);
                pav_error_set(err, "internal", "out of memory", 0);
                return -1;
            }
        }
        add_local(row, "bodiesBytes", local[i].bodies);
        add_local(row, "blobsBytes", local[i].blobs);
    }
    pav_cache_local_usage_free(local, local_n);
    cache_text = load_kv(KV_LIMITS);
    if (cache_text != NULL) {
        cache = json_loads(cache_text, 0, NULL);
        free(cache_text);
        if (!json_is_object(cache)) {
            json_decref(cache);
            cache = NULL;
        }
    }
    has_checked = cache_checked_at(cache, &checked);
    if (measure) {
        measure_started = 1;
        if (atomic_exchange(&g_usage_pending, 0) == 1) {
            measure = 0;
            cancelled_early = 1;
        }
    }
    if (measure) {
        fresh = json_object();
        if (fresh == NULL) {
            json_decref(cache);
            json_decref(rows);
            json_decref(result);
            json_decref(profile_accounts);
            atomic_store(&g_usage_pending, 0);
            pav_error_set(err, "internal", "out of memory", 0);
            return -1;
        }
    }
    json_array_foreach(profile_accounts, i, value) {
        const char *id = json_string_value(json_object_get(value, "id"));
        json_t *row = id_set(id) ? find_row(rows, id) : NULL;

        if (row != NULL && fill_server(row, profile_id, value, measure, cache, fresh) != 0) {
            cancelled = 1;
            break;
        }
    }
    if (measure && json_object_size(fresh) > 0) {
        int64_t now = pav_now_ms();

        if (now >= 0) {
            if (save_measurements(cache, fresh, now) != 0) {
                pav_log("store", PAV_WARN, 0, "cannot store measured sizes");
            }
            checked = now;
            has_checked = 1;
        }
    }
    json_decref(cache);
    json_decref(fresh);
    json_decref(profile_accounts);
    json_array_foreach(rows, i, value) {
        bodies += json_integer_value(json_object_get(value, "bodiesBytes"));
        blobs += json_integer_value(json_object_get(value, "blobsBytes"));
    }
    if ((cancelled_early || cancelled) && json_object_set_new(result, "cancelled", json_true()) != 0) {
        json_decref(rows);
        json_decref(result);
        if (measure_started) {
            atomic_store(&g_usage_pending, 0);
        }
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    if (json_object_set_new(result, "bodiesBytes", json_integer(bodies)) != 0 ||
        json_object_set_new(result, "blobsBytes", json_integer(blobs)) != 0 ||
        json_object_set_new(result, "volumeFreeBytes", volume_free_json(load->profile_path)) != 0 ||
        json_object_set_new(result, "sizesCheckedAt",
                            has_checked ? json_integer(checked) : json_null()) != 0) {
        json_decref(rows);
        json_decref(result);
        if (measure_started) {
            atomic_store(&g_usage_pending, 0);
        }
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    if (json_object_set_new(result, "accounts", rows) != 0) {
        json_decref(result);
        if (measure_started) {
            atomic_store(&g_usage_pending, 0);
        }
        pav_error_set(err, "internal", "out of memory", 0);
        return -1;
    }
    if (measure_started) {
        atomic_store(&g_usage_pending, 0);
    }
    *data = result;
    return 0;
}

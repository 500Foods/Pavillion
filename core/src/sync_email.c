/* Included from sync.c. Email initial sync (M-4) and the delta walk (M-5). */
#ifndef PAV_SYNC_EMAIL_INCLUDED
#error "sync_email.c is included from sync.c"
#endif

typedef struct EmailBox {
    char id[128];
    char role[64];
    int sort_order;
    int total_hint;
    int pos;
    int total;
    int done;
    int counted;
    int seeded;
} EmailBox;

struct EmailWalk {
    EmailBox *boxes;
    int box_n;
    int phase;
    int cursor;
    int restarts;
    int reqs;
    int limit;
    int calls_max;
    char anchor[128];
    struct EmailPage *pending;
    int pending_n;
    struct ThreadRow *threads;
    int thread_n;
    SyncId *thread_ids;
    int thread_id_n;
    int thread_off;
    int kind;
    int step;
    int changes_max;
    int changes_pages;
    int email_more;
    int thread_more;
    int thread_fix;
    int thread_fixed;
    int skip_thread_changes;
    char email_since[128];
    char email_new[128];
    char thread_since[128];
    char thread_new[128];
    SyncId *created;
    int created_n;
    SyncId *updated;
    int updated_n;
    SyncId *destroyed;
    int destroyed_n;
    SyncId *th_get;
    int th_get_n;
    int th_get_off;
    SyncId *th_destroyed;
    int th_destroyed_n;
    SyncId *local_threads;
    int local_thread_n;
    int local_off;
    int fetch_off;
    SyncId *touch;
    int touch_n;
};

typedef struct EmailRow {
    char id[128];
    char thread_id[128];
    char blob_id[128];
    char *message_id;
    char *in_reply_to;
    char *refs;
    char *subject;
    char *from_json;
    char *to_json;
    char *cc_json;
    char *bcc_json;
    char *reply_to_json;
    char *preview;
    char *keywords;
    char *from_text;
    char *to_text;
    SyncId *boxes;
    int box_n;
    int64_t sent_at;
    int64_t received_at;
    int size;
    int has_attachment;
    int seen;
    int flagged;
    int answered;
    int forwarded;
    int draft;
    int junk;
    int is_new;
} EmailRow;

typedef struct EmailPage {
    char mailbox_id[128];
    int position;
    int limit;
    int total;
    int saw_query;
    int saw_get;
    char state[128];
    SyncId *ids;
    int id_n;
    EmailRow *rows;
    int row_n;
} EmailPage;

typedef struct ThreadRow {
    char id[128];
    char *email_ids;
} ThreadRow;

typedef struct EmailResult {
    int kind;
    char account_id[64];
    uint64_t generation;
    char ra_id[256];
    char detail[160];
    int status;
    int auth_rc;
    int email_mode;
    int method_error;
    int email_more;
    int thread_more;
    int email_resync;
    int thread_resync;
    char email_state[128];
    char thread_state[128];
    EmailPage *pages;
    int page_n;
    ThreadRow *threads;
    int thread_n;
    SyncId *created;
    int created_n;
    SyncId *updated;
    int updated_n;
    SyncId *destroyed;
    int destroyed_n;
    SyncId *th_get;
    int th_get_n;
    SyncId *th_destroyed;
    int th_destroyed_n;
    SyncId *thread_missing;
    int thread_missing_n;
    MbxRow *mailboxes;
    int mailbox_n;
} EmailResult;

static void free_delta_extra(EmailResult *r);
static void clear_delta_lists(EmailWalk *w);
static int reduce_delta(const char *body, const SyncReq *owned, EmailResult *out);
static void begin_email_delta(PavSup *s, SupRemote *remote);
static void email_delta_apply(PavSup *s, SupRemote *remote, EmailResult *r);

static void free_email_row(EmailRow *row) {
    if (row == NULL) {
        return;
    }
    free(row->message_id);
    free(row->in_reply_to);
    free(row->refs);
    free(row->subject);
    free(row->from_json);
    free(row->to_json);
    free(row->cc_json);
    free(row->bcc_json);
    free(row->reply_to_json);
    free(row->preview);
    free(row->keywords);
    free(row->from_text);
    free(row->to_text);
    free(row->boxes);
    memset(row, 0, sizeof(*row));
}

static void free_email_pages(EmailPage *pages, int n) {
    int i;
    int r;

    for (i = 0; i < n; i++) {
        for (r = 0; r < pages[i].row_n; r++) {
            free_email_row(&pages[i].rows[r]);
        }
        free(pages[i].rows);
        free(pages[i].ids);
    }
    free(pages);
}

static void free_thread_rows(ThreadRow *rows, int n) {
    int i;

    for (i = 0; i < n; i++) {
        free(rows[i].email_ids);
    }
    free(rows);
}

static void clear_email_walk(SupRemote *remote) {
    EmailWalk *w;

    if (remote == NULL || remote->email == NULL) {
        return;
    }
    w = remote->email;
    free(w->boxes);
    free_email_pages(w->pending, w->pending_n);
    free_thread_rows(w->threads, w->thread_n);
    free(w->thread_ids);
    clear_delta_lists(w);
    free(w);
    remote->email = NULL;
}

static void free_email_result(void *result) {
    EmailResult *r = result;

    if (r == NULL) {
        return;
    }
    free_email_pages(r->pages, r->page_n);
    free_thread_rows(r->threads, r->thread_n);
    free_delta_extra(r);
    free(r);
}

static void email_result_ids(const void *result, const char **account_id, uint64_t *generation) {
    const EmailResult *r = result;

    *account_id = r->account_id;
    *generation = r->generation;
}

static void email_fail(PavSup *s, SupRemote *remote, const char *detail) {
    clear_email_walk(remote);
    publish_state(s, "error:server", detail != NULL && detail[0] != '\0' ? detail : "request rejected", 0,
                  PAV_ACT_FAILED);
}

static int email_cancelled(const PavSup *s) {
    return s != NULL && s->activity_id > 0 && pav_activity_cancelled(s->activity_id);
}

static void finish_email_cancel(PavSup *s, SupRemote *remote) {
    clear_email_walk(remote);
    publish_state(s, "idle", "", 0, PAV_ACT_CANCELLED);
}

/* Keywords are the source of truth. The six flag columns are written in the
   same statement as the keywords text. */
static void pav_email_set_keywords(const json_t *kw, int *seen, int *flagged, int *answered, int *forwarded,
                                   int *draft, int *junk) {
    *seen = json_is_true(json_object_get(kw, "$seen")) ? 1 : 0;
    *flagged = json_is_true(json_object_get(kw, "$flagged")) ? 1 : 0;
    *answered = json_is_true(json_object_get(kw, "$answered")) ? 1 : 0;
    *forwarded = json_is_true(json_object_get(kw, "$forwarded")) ? 1 : 0;
    *draft = json_is_true(json_object_get(kw, "$draft")) ? 1 : 0;
    *junk = json_is_true(json_object_get(kw, "$junk")) ? 1 : 0;
}

static int64_t days_from_civil(int year, int month, int day) {
    int y = year;
    int era;
    unsigned yoe;
    unsigned doy;
    unsigned doe;

    y -= month <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = (unsigned)(y - era * 400);
    doy = (153u * (unsigned)(month + (month > 2 ? -3 : 9)) + 2u) / 5u + (unsigned)day - 1u;
    doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

/* UTCDate to unix milliseconds. No timegm: the Windows CI does not have it. */
static int64_t utc_date_ms(const char *text) {
    int year;
    int month;
    int day;
    int hour;
    int min;
    int sec;
    int64_t days;

    if (text == NULL || sscanf(text, "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &min, &sec) != 6) {
        return 0;
    }
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 ||
        sec > 60) {
        return 0;
    }
    days = days_from_civil(year, month, day);
    return (days * 86400 + (int64_t)hour * 3600 + (int64_t)min * 60 + sec) * 1000;
}

static int json_count(const json_t *value, int fallback) {
    json_int_t n;

    if (!json_is_integer(value)) {
        return fallback;
    }
    n = json_integer_value(value);
    if (n < 0) {
        return 0;
    }
    if (n > 1000000000) {
        return 1000000000;
    }
    return (int)n;
}

static void append_addresses(char *dst, size_t n, int *first, const json_t *arr) {
    size_t i;
    size_t count;

    if (!json_is_array(arr) || n == 0) {
        return;
    }
    count = json_array_size(arr);
    for (i = 0; i < count; i++) {
        json_t *item = json_array_get(arr, i);
        const char *name;
        const char *email;
        char piece[640];
        int wrote;
        size_t have;

        if (!json_is_object(item)) {
            continue;
        }
        name = json_string_value(json_object_get(item, "name"));
        email = json_string_value(json_object_get(item, "email"));
        if ((name == NULL || name[0] == '\0') && (email == NULL || email[0] == '\0')) {
            continue;
        }
        if (name != NULL && name[0] != '\0' && email != NULL && email[0] != '\0') {
            wrote = snprintf(piece, sizeof(piece), "%s <%s>", name, email);
        } else if (email != NULL && email[0] != '\0') {
            wrote = snprintf(piece, sizeof(piece), "%s", email);
        } else {
            wrote = snprintf(piece, sizeof(piece), "%s", name != NULL ? name : "");
        }
        if (wrote < 0) {
            continue;
        }
        have = strlen(dst);
        if (!*first) {
            if (have + 2 >= n) {
                return;
            }
            memcpy(dst + have, ", ", 2);
            have += 2;
            dst[have] = '\0';
        }
        if (have + (size_t)wrote >= n) {
            return;
        }
        memcpy(dst + have, piece, (size_t)wrote);
        dst[have + (size_t)wrote] = '\0';
        *first = 0;
    }
}

static char *addresses_text(const json_t *first_list, const json_t *second_list) {
    char buf[4096];
    int first = 1;

    buf[0] = '\0';
    append_addresses(buf, sizeof(buf), &first, first_list);
    append_addresses(buf, sizeof(buf), &first, second_list);
    if (buf[0] == '\0') {
        return NULL;
    }
    return dup_capped(buf, sizeof(buf) - 1);
}

static int fill_email_row(EmailRow *row, const json_t *obj) {
    const char *id;
    void *it;

    id = json_string_value(json_object_get(obj, "id"));
    if (id == NULL || id[0] == '\0' || !copy_fit(row->id, sizeof(row->id), id)) {
        return 1;
    }
    copy_fit(row->thread_id, sizeof(row->thread_id), json_string_value(json_object_get(obj, "threadId")));
    copy_fit(row->blob_id, sizeof(row->blob_id), json_string_value(json_object_get(obj, "blobId")));
    row->message_id = dump_capped(json_object_get(obj, "messageId"), 65536);
    row->in_reply_to = dump_capped(json_object_get(obj, "inReplyTo"), 65536);
    row->refs = dump_capped(json_object_get(obj, "references"), 65536);
    row->subject = dup_capped(json_string_value(json_object_get(obj, "subject")), 16384);
    row->from_json = dump_capped(json_object_get(obj, "from"), 65536);
    row->to_json = dump_capped(json_object_get(obj, "to"), 65536);
    row->cc_json = dump_capped(json_object_get(obj, "cc"), 65536);
    row->bcc_json = dump_capped(json_object_get(obj, "bcc"), 65536);
    row->reply_to_json = dump_capped(json_object_get(obj, "replyTo"), 65536);
    row->preview = dup_capped(json_string_value(json_object_get(obj, "preview")), 16384);
    row->keywords = dump_capped(json_object_get(obj, "keywords"), 8192);
    row->from_text = addresses_text(json_object_get(obj, "from"), NULL);
    row->to_text = addresses_text(json_object_get(obj, "to"), json_object_get(obj, "cc"));
    row->sent_at = utc_date_ms(json_string_value(json_object_get(obj, "sentAt")));
    row->received_at = utc_date_ms(json_string_value(json_object_get(obj, "receivedAt")));
    row->size = json_count(json_object_get(obj, "size"), 0);
    row->has_attachment = json_is_true(json_object_get(obj, "hasAttachment")) ? 1 : 0;
    pav_email_set_keywords(json_object_get(obj, "keywords"), &row->seen, &row->flagged, &row->answered,
                           &row->forwarded, &row->draft, &row->junk);
    for (it = json_object_iter(json_object_get(obj, "mailboxIds")); it != NULL;
         it = json_object_iter_next(json_object_get(obj, "mailboxIds"), it)) {
        if (!json_is_true(json_object_iter_value(it))) {
            continue;
        }
        if (append_id(&row->boxes, &row->box_n, json_object_iter_key(it)) != 0) {
            return -1;
        }
    }
    return 0;
}

static int append_email_row(EmailPage *page, const json_t *obj, int *parsed) {
    EmailRow *grown;
    int rc;

    if (*parsed >= PAV_SYNC_EMAIL_PARSE_MAX) {
        return -2;
    }
    grown = realloc(page->rows, (size_t)(page->row_n + 1) * sizeof(*grown));
    if (grown == NULL) {
        return -1;
    }
    page->rows = grown;
    memset(&page->rows[page->row_n], 0, sizeof(page->rows[0]));
    rc = fill_email_row(&page->rows[page->row_n], obj);
    if (rc != 0) {
        free_email_row(&page->rows[page->row_n]);
        return rc;
    }
    page->row_n++;
    (*parsed)++;
    return 0;
}

static int email_slot_index(const char *cid, const char *prefix) {
    size_t n;
    int index;

    if (cid == NULL || prefix == NULL) {
        return -1;
    }
    n = strlen(prefix);
    if (strncmp(cid, prefix, n) != 0 || cid[n] == '\0') {
        return -1;
    }
    index = atoi(cid + n);
    if (index < 0 || index >= PAV_SYNC_EMAIL_SLOTS) {
        return -1;
    }
    return index;
}

static int reduce_email(const char *body, const SyncReq *owned, EmailResult *out) {
    json_t *root;
    json_t *responses;
    size_t i;
    size_t n;
    int parsed = 0;
    int slot;

    if (owned->email_mode >= EMAIL_MODE_CHANGES) {
        return reduce_delta(body, owned, out);
    }
    root = json_loads(body, 0, NULL);
    if (!json_is_object(root)) {
        json_decref(root);
        copy_fit(out->detail, sizeof(out->detail), "not a jmap response");
        out->method_error = 1;
        return -1;
    }
    responses = json_object_get(root, "methodResponses");
    if (!json_is_array(responses)) {
        json_decref(root);
        copy_fit(out->detail, sizeof(out->detail), "not a jmap response");
        out->method_error = 1;
        return -1;
    }
    if (owned->email_mode == 1) {
        out->page_n = owned->email_slot_n;
        if (out->page_n > 0) {
            out->pages = calloc((size_t)out->page_n, sizeof(*out->pages));
            if (out->pages == NULL) {
                json_decref(root);
                return -1;
            }
        }
        for (slot = 0; slot < out->page_n; slot++) {
            copy_fit(out->pages[slot].mailbox_id, sizeof(out->pages[slot].mailbox_id),
                     owned->email_slots[slot].mailbox_id);
            out->pages[slot].position = owned->email_slots[slot].position;
            out->pages[slot].limit = owned->email_slots[slot].limit > 0 ? owned->email_slots[slot].limit : 1;
            out->pages[slot].total = -1;
        }
    }
    n = json_array_size(responses);
    for (i = 0; i < n; i++) {
        json_t *entry = json_array_get(responses, i);
        const char *name;
        const char *cid;
        const char *code;
        json_t *args;
        json_t *list;
        size_t k;

        if (!json_is_array(entry) || json_array_size(entry) < 2) {
            continue;
        }
        name = json_string_value(json_array_get(entry, 0));
        args = json_array_get(entry, 1);
        cid = json_array_size(entry) >= 3 ? json_string_value(json_array_get(entry, 2)) : NULL;
        code = method_error_code(entry);
        if (code != NULL) {
            out->method_error = 1;
            copy_fit(out->detail, sizeof(out->detail), code[0] != '\0' ? code : "request rejected");
            continue;
        }
        if (name == NULL || !json_is_object(args)) {
            continue;
        }
        if (strcmp(name, "Email/query") == 0) {
            slot = email_slot_index(cid, "eq");
            if (slot < 0 || slot >= out->page_n) {
                continue;
            }
            out->pages[slot].saw_query = 1;
            if (json_is_integer(json_object_get(args, "position"))) {
                out->pages[slot].position = json_count(json_object_get(args, "position"), out->pages[slot].position);
            }
            if (json_is_integer(json_object_get(args, "total"))) {
                out->pages[slot].total = json_count(json_object_get(args, "total"), 0);
            }
            if (append_id_array(&out->pages[slot].ids, &out->pages[slot].id_n, json_object_get(args, "ids")) != 0) {
                json_decref(root);
                return -1;
            }
        } else if (strcmp(name, "Email/get") == 0) {
            int rc;

            slot = email_slot_index(cid, "eg");
            if (slot < 0 || slot >= out->page_n) {
                continue;
            }
            out->pages[slot].saw_get = 1;
            copy_json_string(out->pages[slot].state, sizeof(out->pages[slot].state), args, "state");
            list = json_object_get(args, "list");
            if (!json_is_array(list)) {
                continue;
            }
            for (k = 0; k < json_array_size(list); k++) {
                rc = append_email_row(&out->pages[slot], json_array_get(list, k), &parsed);
                if (rc == -2) {
                    out->method_error = 1;
                    copy_fit(out->detail, sizeof(out->detail), "response too large");
                    json_decref(root);
                    return -1;
                }
                if (rc < 0) {
                    json_decref(root);
                    return -1;
                }
            }
        } else if (strcmp(name, "Thread/get") == 0) {
            list = json_object_get(args, "list");
            if (!json_is_array(list)) {
                continue;
            }
            for (k = 0; k < json_array_size(list); k++) {
                json_t *obj = json_array_get(list, k);
                ThreadRow *grown;
                const char *id;

                if (!json_is_object(obj)) {
                    continue;
                }
                id = json_string_value(json_object_get(obj, "id"));
                if (id == NULL || id[0] == '\0') {
                    continue;
                }
                grown = realloc(out->threads, (size_t)(out->thread_n + 1) * sizeof(*grown));
                if (grown == NULL) {
                    json_decref(root);
                    return -1;
                }
                out->threads = grown;
                memset(&out->threads[out->thread_n], 0, sizeof(out->threads[0]));
                if (!copy_fit(out->threads[out->thread_n].id, sizeof(out->threads[out->thread_n].id), id)) {
                    continue;
                }
                out->threads[out->thread_n].email_ids = dump_capped(json_object_get(obj, "emailIds"), 65536);
                out->thread_n++;
            }
        }
    }
    if (owned->email_mode == 1 && !out->method_error) {
        for (slot = 0; slot < out->page_n; slot++) {
            if (!out->pages[slot].saw_query || !out->pages[slot].saw_get) {
                out->method_error = 1;
                copy_fit(out->detail, sizeof(out->detail), "not a jmap response");
                break;
            }
        }
    }
    json_decref(root);
    return out->method_error ? -1 : 0;
}

static void email_on_done(SyncReq *owned, int stopped) {
    EmailResult *result;

    if (stopped) {
        release_outputs(&owned->req);
        sync_req_release(owned);
        return;
    }
    result = calloc(1, sizeof(*result));
    if (result == NULL) {
        release_outputs(&owned->req);
        sync_req_release(owned);
        return;
    }
    result->kind = SYNC_KIND_EMAIL;
    copy_fit(result->account_id, sizeof(result->account_id), owned->account_id);
    copy_fit(result->ra_id, sizeof(result->ra_id), owned->ra_id);
    result->generation = owned->generation;
    result->status = owned->req.status_code;
    result->auth_rc = owned->req.auth_rc;
    result->email_mode = owned->email_mode;
    if (result->status == 200 && owned->req.body_out != NULL && owned->req.body_out_len > 0 &&
        owned->req.body_out_len <= PAV_SYNC_TYPE_MAX) {
        if (reduce_email(owned->req.body_out, owned, result) != 0 && !result->method_error) {
            result->method_error = 1;
            copy_fit(result->detail, sizeof(result->detail), "not a jmap response");
        }
    } else if (result->status == 200) {
        result->method_error = 1;
        copy_fit(result->detail, sizeof(result->detail),
                 owned->req.body_out_len > PAV_SYNC_TYPE_MAX ? "response too large" : "not a jmap response");
    }
    if (sync_stopped() || pav_core_post_sync(result) != 0) {
        pav_sync_result_free(result);
    }
    release_outputs(&owned->req);
    sync_req_release(owned);
}

static int email_role_rank(const char *role) {
    if (role != NULL && strcmp(role, "inbox") == 0) {
        return 0;
    }
    if (role != NULL && strcmp(role, "sent") == 0) {
        return 1;
    }
    return 2;
}

static int email_box_before(const EmailBox *a, const EmailBox *b) {
    int ra = email_role_rank(a->role);
    int rb = email_role_rank(b->role);

    if (ra != rb) {
        return ra < rb;
    }
    if (a->sort_order != b->sort_order) {
        return a->sort_order < b->sort_order;
    }
    return strcmp(a->id, b->id) < 0;
}

static void sort_email_boxes(EmailBox *boxes, int n) {
    int i;

    for (i = 1; i < n; i++) {
        EmailBox key = boxes[i];
        int j = i;

        while (j > 0 && email_box_before(&key, &boxes[j - 1])) {
            boxes[j] = boxes[j - 1];
            j--;
        }
        boxes[j] = key;
    }
}

/* Local mail is empty while a folder reports messages. The previous walk
   recorded Email state without storing any, so a later sync would skip. */
static int email_backfill(const char *ra_id) {
    PavDbRead *read = NULL;
    sqlite3_stmt *stmt = NULL;
    int emails = 0;
    int pending = 0;

    if (ra_id == NULL || ra_id[0] == '\0' || pav_db_read_begin(&read) != 0) {
        return 0;
    }
    if (sqlite3_prepare_v2(pav_db_read_handle(read),
                           "SELECT (SELECT count(*) FROM email WHERE ra_id = ?), "
                           "(SELECT count(*) FROM mailbox WHERE ra_id = ? AND total > 0)",
                           -1, &stmt, NULL) == SQLITE_OK &&
        sqlite3_bind_text(stmt, 1, ra_id, -1, SQLITE_TRANSIENT) == SQLITE_OK &&
        sqlite3_bind_text(stmt, 2, ra_id, -1, SQLITE_TRANSIENT) == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        emails = sqlite3_column_int(stmt, 0);
        pending = sqlite3_column_int(stmt, 1);
    }
    sqlite3_finalize(stmt);
    pav_db_read_end(read);
    return emails == 0 && pending > 0;
}

static int email_has_state(const char *ra_id) {
    PavDbRead *read = NULL;
    sqlite3_stmt *stmt = NULL;
    int found = 0;

    if (ra_id == NULL || ra_id[0] == '\0' || pav_db_read_begin(&read) != 0) {
        return 0;
    }
    if (sqlite3_prepare_v2(pav_db_read_handle(read),
                           "SELECT state FROM sync_state WHERE ra_id = ? AND type = 'Email'", -1, &stmt, NULL) ==
            SQLITE_OK &&
        sqlite3_bind_text(stmt, 1, ra_id, -1, SQLITE_TRANSIENT) == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        const char *state = (const char *)sqlite3_column_text(stmt, 0);

        found = state != NULL && state[0] != '\0';
    }
    sqlite3_finalize(stmt);
    pav_db_read_end(read);
    return found;
}

static void consider_query_limit(const json_t *obj, int *query) {
    json_t *value;
    json_int_t n;

    if (!json_is_object(obj)) {
        return;
    }
    value = json_object_get(obj, "maxObjectsInQuery");
    if (!json_is_integer(value)) {
        return;
    }
    n = json_integer_value(value);
    if (n > 0 && n < *query) {
        *query = (int)n;
    }
}

static void consider_calls(const json_t *obj, int *calls, int *saw) {
    json_t *value;
    json_int_t n;

    if (!json_is_object(obj)) {
        return;
    }
    value = json_object_get(obj, "maxCallsInRequest");
    if (!json_is_integer(value)) {
        return;
    }
    n = json_integer_value(value);
    if (n < 1) {
        return;
    }
    if (!*saw || n < *calls) {
        *calls = (int)n;
        *saw = 1;
    }
}

static void load_email_caps(const char *account_id, int *limit, int *calls) {
    PavDbRead *read = NULL;
    sqlite3_stmt *stmt = NULL;
    int query = session_get_limit(account_id);
    int call_n = 16;
    int saw_calls = 0;

    *limit = query > 0 ? query : 1;
    *calls = 16;
    if (account_id == NULL || account_id[0] == '\0' || pav_db_read_begin(&read) != 0) {
        return;
    }
    if (sqlite3_prepare_v2(pav_db_read_handle(read), "SELECT session_json FROM account_state WHERE account_id = ?", -1,
                           &stmt, NULL) == SQLITE_OK &&
        sqlite3_bind_text(stmt, 1, account_id, -1, SQLITE_TRANSIENT) == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        const char *text = (const char *)sqlite3_column_text(stmt, 0);
        json_t *root = text != NULL ? json_loads(text, 0, NULL) : NULL;
        json_t *caps = json_object_get(root, "capabilities");

        consider_query_limit(json_object_get(caps, "urn:ietf:params:jmap:core"), &query);
        consider_query_limit(json_object_get(caps, PAV_MAIL_URN), &query);
        consider_query_limit(json_object_get(root, "limits"), &query);
        consider_calls(json_object_get(caps, "urn:ietf:params:jmap:core"), &call_n, &saw_calls);
        consider_calls(json_object_get(root, "limits"), &call_n, &saw_calls);
        json_decref(root);
    }
    sqlite3_finalize(stmt);
    pav_db_read_end(read);
    if (query < 1) {
        query = 1;
    }
    if (query > PAV_SYNC_MBX_PAGE) {
        query = PAV_SYNC_MBX_PAGE;
    }
    if (call_n < 2) {
        call_n = 2;
    }
    *limit = query;
    *calls = call_n;
}

static int load_email_boxes(const char *ra_id, EmailBox **out, int *out_n) {
    PavDbRead *read = NULL;
    sqlite3_stmt *stmt = NULL;
    int rc;
    int cap = 0;

    *out = NULL;
    *out_n = 0;
    if (pav_db_read_begin(&read) != 0) {
        return -1;
    }
    if (sqlite3_prepare_v2(pav_db_read_handle(read),
                           "SELECT id, role, sort_order, total FROM mailbox WHERE ra_id = ?", -1, &stmt, NULL) !=
            SQLITE_OK ||
        sqlite3_bind_text(stmt, 1, ra_id, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        pav_db_read_end(read);
        return -1;
    }
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *id = (const char *)sqlite3_column_text(stmt, 0);
        const char *role = (const char *)sqlite3_column_text(stmt, 1);
        EmailBox *grown;

        if (id == NULL || id[0] == '\0') {
            continue;
        }
        if (*out_n == cap) {
            cap = cap == 0 ? 8 : cap * 2;
            grown = realloc(*out, (size_t)cap * sizeof(**out));
            if (grown == NULL) {
                free(*out);
                *out = NULL;
                sqlite3_finalize(stmt);
                pav_db_read_end(read);
                return -1;
            }
            *out = grown;
        }
        memset(&(*out)[*out_n], 0, sizeof((*out)[*out_n]));
        if (!copy_fit((*out)[*out_n].id, sizeof((*out)[*out_n].id), id)) {
            continue;
        }
        copy_fit((*out)[*out_n].role, sizeof((*out)[*out_n].role), role);
        (*out)[*out_n].sort_order = sqlite3_column_int(stmt, 2);
        (*out)[*out_n].total_hint = sqlite3_column_int(stmt, 3);
        (*out)[*out_n].total = -1;
        (*out_n)++;
    }
    sqlite3_finalize(stmt);
    pav_db_read_end(read);
    if (rc != SQLITE_DONE) {
        free(*out);
        *out = NULL;
        *out_n = 0;
        return -1;
    }
    sort_email_boxes(*out, *out_n);
    return 0;
}

static int fill_email_slots(EmailWalk *w, EmailSlot *slots, int max_n) {
    int n = 0;
    int calls = 0;
    int budget = 0;
    int i;

    if (max_n > PAV_SYNC_EMAIL_SLOTS) {
        max_n = PAV_SYNC_EMAIL_SLOTS;
    }
    if (w->phase == 2) {
        while (w->cursor < w->box_n && w->boxes[w->cursor].done) {
            w->cursor++;
        }
        if (w->cursor >= w->box_n || max_n < 1) {
            return 0;
        }
        copy_fit(slots[0].mailbox_id, sizeof(slots[0].mailbox_id), w->boxes[w->cursor].id);
        slots[0].position = w->boxes[w->cursor].pos > 0 ? w->boxes[w->cursor].pos : 0;
        slots[0].limit = w->limit;
        return 1;
    }
    for (i = w->cursor; i < w->box_n && n < max_n; i++) {
        EmailBox *box = &w->boxes[i];
        int hint;
        int add;

        if (box->done) {
            w->cursor = i + 1;
            continue;
        }
        if (calls + 2 > w->calls_max) {
            break;
        }
        hint = 0;
        if (box->total >= 0) {
            hint = box->total > box->pos ? box->total - box->pos : 0;
        } else if (box->total_hint > 0) {
            hint = box->total_hint;
        }
        add = hint > w->limit ? w->limit : hint;
        if (budget > 0 && add > 0 && budget + add > w->limit) {
            break;
        }
        copy_fit(slots[n].mailbox_id, sizeof(slots[n].mailbox_id), box->id);
        slots[n].position = 0;
        slots[n].limit = w->limit;
        n++;
        calls += 2;
        budget += add;
        w->cursor = i + 1;
        if (add > 0 && budget >= w->limit) {
            break;
        }
    }
    return n;
}

static json_t *header_properties(void) {
    static const char *const names[] = {
        "id",        "threadId", "mailboxIds", "keywords", "size",     "receivedAt",    "sentAt",
        "messageId", "inReplyTo", "references", "subject", "from",     "to",            "cc",
        "bcc",       "replyTo",   "preview",    "hasAttachment", "blobId"};
    json_t *arr = json_array();
    size_t i;

    if (arr == NULL) {
        return NULL;
    }
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        json_t *text = json_string(names[i]);

        if (text == NULL || json_array_append_new(arr, text) != 0) {
            json_decref(arr);
            return NULL;
        }
    }
    return arr;
}

static json_t *email_query_args(const char *account, const EmailSlot *slot) {
    json_t *args = json_object();
    json_t *filter = json_pack("{s:s}", "inMailbox", slot->mailbox_id);
    json_t *sort = json_pack("[{s:s,s:b}]", "property", "receivedAt", "isAscending", 0);
    json_t *account_id = json_string(account);
    json_t *position = json_integer(slot->position > 0 ? slot->position : 0);
    json_t *limit = json_integer(slot->limit > 0 ? slot->limit : 1);

    if (args == NULL || filter == NULL || sort == NULL || account_id == NULL || position == NULL || limit == NULL) {
        json_decref(args);
        json_decref(filter);
        json_decref(sort);
        json_decref(account_id);
        json_decref(position);
        json_decref(limit);
        return NULL;
    }
    if (put_json(args, "accountId", account_id) != 0) {
        json_decref(args);
        json_decref(filter);
        json_decref(sort);
        json_decref(position);
        json_decref(limit);
        return NULL;
    }
    if (put_json(args, "filter", filter) != 0) {
        json_decref(args);
        json_decref(sort);
        json_decref(position);
        json_decref(limit);
        return NULL;
    }
    if (put_json(args, "sort", sort) != 0) {
        json_decref(args);
        json_decref(position);
        json_decref(limit);
        return NULL;
    }
    if (put_json(args, "position", position) != 0) {
        json_decref(args);
        json_decref(limit);
        return NULL;
    }
    if (put_json(args, "limit", limit) != 0 || put_json(args, "calculateTotal", json_true()) != 0) {
        json_decref(args);
        return NULL;
    }
    return args;
}

static json_t *email_get_args(const char *account, const char *qcid) {
    json_t *args = json_object();
    json_t *ref = json_pack("{s:s,s:s,s:s}", "resultOf", qcid, "name", "Email/query", "path", "/ids");
    json_t *props = header_properties();
    json_t *account_id = json_string(account);

    if (args == NULL || ref == NULL || props == NULL || account_id == NULL) {
        json_decref(args);
        json_decref(ref);
        json_decref(props);
        json_decref(account_id);
        return NULL;
    }
    if (put_json(args, "accountId", account_id) != 0) {
        json_decref(args);
        json_decref(ref);
        json_decref(props);
        return NULL;
    }
    if (put_json(args, "#ids", ref) != 0) {
        json_decref(args);
        json_decref(props);
        return NULL;
    }
    if (put_json(args, "properties", props) != 0) {
        json_decref(args);
        return NULL;
    }
    return args;
}

static char *build_email_body(const char *account, const EmailSlot *slots, int n, int mode, const SyncId *thread_ids,
                              int thread_off, int thread_n, int limit, int calls_max, int *sent) {
    json_t *root = json_object();
    json_t *using = json_array();
    json_t *calls = json_array();
    char *text = NULL;
    int i;

    if (sent != NULL) {
        *sent = 0;
    }
    if (root == NULL || using == NULL || calls == NULL) {
        json_decref(root);
        json_decref(using);
        json_decref(calls);
        return NULL;
    }
    if (json_array_append_new(using, json_string("urn:ietf:params:jmap:core")) != 0 ||
        json_array_append_new(using, json_string(PAV_MAIL_URN)) != 0) {
        json_decref(using);
        json_decref(calls);
        json_decref(root);
        return NULL;
    }
    if (put_json(root, "using", using) != 0) {
        json_decref(calls);
        json_decref(root);
        return NULL;
    }
    using = NULL;
    if (mode == 2) {
        int off = thread_off;
        int chunks = 0;

        while (off < thread_n && chunks < calls_max) {
            int count = thread_n - off;
            json_t *args;
            json_t *ids;
            char cid[16];

            if (count > limit) {
                count = limit;
            }
            ids = ids_array(thread_ids + off, count);
            args = json_object();
            if (args == NULL || ids == NULL) {
                json_decref(args);
                json_decref(ids);
                json_decref(calls);
                json_decref(root);
                return NULL;
            }
            if (put_json(args, "accountId", json_string(account)) != 0) {
                json_decref(args);
                json_decref(ids);
                json_decref(calls);
                json_decref(root);
                return NULL;
            }
            if (put_json(args, "ids", ids) != 0) {
                json_decref(args);
                json_decref(calls);
                json_decref(root);
                return NULL;
            }
            snprintf(cid, sizeof(cid), "th%d", chunks);
            if (append_method(calls, "Thread/get", args, cid) != 0) {
                json_decref(calls);
                json_decref(root);
                return NULL;
            }
            off += count;
            chunks++;
        }
        if (sent != NULL) {
            *sent = off - thread_off;
        }
    } else {
        for (i = 0; i < n; i++) {
            char qcid[16];
            char gcid[16];
            json_t *qargs;
            json_t *gargs;

            snprintf(qcid, sizeof(qcid), "eq%d", i);
            snprintf(gcid, sizeof(gcid), "eg%d", i);
            qargs = email_query_args(account, &slots[i]);
            if (qargs == NULL || append_method(calls, "Email/query", qargs, qcid) != 0) {
                json_decref(calls);
                json_decref(root);
                return NULL;
            }
            gargs = email_get_args(account, qcid);
            if (gargs == NULL || append_method(calls, "Email/get", gargs, gcid) != 0) {
                json_decref(calls);
                json_decref(root);
                return NULL;
            }
        }
    }
    if (put_json(root, "methodCalls", calls) != 0 || put_json(root, "RequestId", json_string("pav-m4")) != 0) {
        json_decref(root);
        return NULL;
    }
    text = json_dumps(root, JSON_COMPACT);
    json_decref(root);
    return text;
}

static SyncReq *make_email_req(PavSup *s, SupRemote *remote, int mode, const EmailSlot *slots, int n, char *body) {
    SyncReq *req = calloc(1, sizeof(*req));
    int i;

    if (req == NULL) {
        free(body);
        return NULL;
    }
    atomic_init(&req->refs, 2);
    req->kind = SYNC_KIND_EMAIL;
    req->generation = s->generation;
    req->email_mode = mode;
    req->email_slot_n = n;
    for (i = 0; i < n && i < PAV_SYNC_EMAIL_SLOTS; i++) {
        req->email_slots[i] = slots[i];
    }
    req->body = body;
    copy_fit(req->account_id, sizeof(req->account_id), s->account_id);
    copy_fit(req->url, sizeof(req->url), s->api_url);
    copy_fit(req->ca_file, sizeof(req->ca_file), s->ca_file);
    copy_fit(req->keyring_user, sizeof(req->keyring_user), s->keyring_user);
    copy_fit(req->username, sizeof(req->username), s->username);
    copy_fit(req->auth_method, sizeof(req->auth_method), s->auth);
    copy_fit(req->ra_id, sizeof(req->ra_id), remote->ra_id);
    copy_fit(req->jmap_id, sizeof(req->jmap_id), remote->jmap_id);
    req->req.method = "POST";
    req->req.url = req->url;
    req->req.account_id = req->account_id;
    req->req.keyring_user = req->keyring_user;
    req->req.username = req->username;
    req->req.auth_method = req->auth_method;
    req->req.ca_file = req->ca_file[0] != '\0' ? req->ca_file : NULL;
    req->req.body = req->body;
    req->req.body_len = body != NULL ? strlen(body) : 0;
    req->req.headers = json_pack("{s:s}", "Content-Type", "application/json");
    req->req.on_done = sync_on_done;
    req->req.user = req;
    if (req->req.headers == NULL) {
        free(req->body);
        free(req);
        return NULL;
    }
    return req;
}

static int submit_email(PavSup *s, SyncReq *req) {
    pthread_mutex_lock(&g_sync_mu);
    if (g_sync_stop || !s->enabled) {
        pthread_mutex_unlock(&g_sync_mu);
        sync_req_release(req);
        sync_req_release(req);
        return -1;
    }
    drop_inflight(s);
    req->generation = s->generation;
    s->inflight = req;
    pthread_mutex_unlock(&g_sync_mu);
    if (pav_net_request(&req->req) != 0) {
        fail_submit(s, req);
        return -1;
    }
    return 0;
}

static void finish_email_state(PavSup *s, SupRemote *remote);
static void start_email_query(PavSup *s, SupRemote *remote);

static void start_email_threads(PavSup *s, SupRemote *remote) {
    EmailWalk *w = remote->email;
    char *body;
    int sent = 0;
    SyncReq *req;

    if (w == NULL) {
        email_fail(s, remote, "request rejected");
        return;
    }
    if (w->reqs >= PAV_SYNC_EMAIL_REQ_MAX) {
        email_fail(s, remote, "too many pages");
        return;
    }
    w->reqs++;
    body = build_email_body(remote->jmap_id, NULL, 0, 2, w->thread_ids, w->thread_off, w->thread_id_n, w->limit,
                            w->calls_max, &sent);
    if (body == NULL || sent <= 0) {
        free(body);
        email_fail(s, remote, "offline");
        return;
    }
    w->thread_off += sent;
    req = make_email_req(s, remote, 2, NULL, 0, body);
    if (req == NULL) {
        email_fail(s, remote, "offline");
        return;
    }
    if (submit_email(s, req) != 0) {
        return;
    }
}

static void start_email_query(PavSup *s, SupRemote *remote) {
    EmailWalk *w = remote->email;
    EmailSlot slots[PAV_SYNC_EMAIL_SLOTS];
    int n;
    char *body;
    SyncReq *req;

    if (w == NULL) {
        email_fail(s, remote, "request rejected");
        return;
    }
    if (email_cancelled(s)) {
        finish_email_cancel(s, remote);
        return;
    }
    /* message.get holds the next page so it does not share the account cap. */
    pthread_mutex_lock(&g_sync_mu);
    if (s->body_hold > 0) {
        copy_fit(s->body_ra, sizeof(s->body_ra), remote->ra_id);
        s->body_paused = 1;
        pthread_mutex_unlock(&g_sync_mu);
        return;
    }
    pthread_mutex_unlock(&g_sync_mu);
    n = fill_email_slots(w, slots, w->calls_max / 2);
    if (n == 0 && w->phase == 1) {
        w->phase = 2;
        w->cursor = 0;
        n = fill_email_slots(w, slots, 1);
    }
    if (n == 0) {
        /* No folders were visible. Recording Email state here would make every
           later sync skip the message download. */
        if (w->box_n == 0) {
            pav_log("sync", PAV_INFO, 1, "no mailboxes visible for %s", s->account_id);
            clear_email_walk(remote);
            begin_remote(s, remote->next);
            return;
        }
        finish_email_state(s, remote);
        return;
    }
    if (w->reqs >= PAV_SYNC_EMAIL_REQ_MAX) {
        email_fail(s, remote, "too many pages");
        return;
    }
    w->reqs++;
    body = build_email_body(remote->jmap_id, slots, n, 1, NULL, 0, 0, w->limit, w->calls_max, NULL);
    if (body == NULL) {
        email_fail(s, remote, "offline");
        return;
    }
    req = make_email_req(s, remote, 1, slots, n, body);
    if (req == NULL) {
        email_fail(s, remote, "offline");
        return;
    }
    if (submit_email(s, req) != 0) {
        return;
    }
}

/* email_fts is contentless. The delete repeats the stored header text and,
   once the body has been fetched, the indexed body. */
static int delete_email_fts(sqlite3 *db, int64_t rowid) {
    return pav_fts_remove(db, rowid);
}

static int clear_email_job(void *ctx, sqlite3 *db) {
    const char *ra = ctx;
    sqlite3_stmt *stmt = NULL;
    int64_t *ids = NULL;
    int n = 0;
    int cap = 0;
    int rc;
    int i;

    if (pav_db_prepare(db, "sync.email_ids", "SELECT rowid FROM email WHERE ra_id = ?", &stmt) != 0 ||
        pav_db_bind_text(stmt, 1, ra) != 0) {
        return -1;
    }
    while ((rc = pav_db_step(stmt)) == SQLITE_ROW) {
        int64_t *grown;

        if (n == cap) {
            cap = cap == 0 ? 32 : cap * 2;
            grown = realloc(ids, (size_t)cap * sizeof(*ids));
            if (grown == NULL) {
                free(ids);
                return -1;
            }
            ids = grown;
        }
        ids[n++] = sqlite3_column_int64(stmt, 0);
    }
    if (rc != SQLITE_DONE) {
        free(ids);
        return -1;
    }
    for (i = 0; i < n; i++) {
        if (delete_email_fts(db, ids[i]) != 0) {
            free(ids);
            return -1;
        }
    }
    free(ids);
    if (pav_db_prepare(db, "sync.email_clear", "DELETE FROM email WHERE ra_id = ?", &stmt) != 0 ||
        pav_db_bind_text(stmt, 1, ra) != 0 || step_done(stmt) != 0 ||
        pav_db_prepare(db, "sync.email_thread_clear", "DELETE FROM thread WHERE ra_id = ?", &stmt) != 0 ||
        pav_db_bind_text(stmt, 1, ra) != 0 || step_done(stmt) != 0 ||
        pav_db_prepare(db, "sync.email_state_del", "DELETE FROM sync_state WHERE ra_id = ? AND type = ?", &stmt) != 0 ||
        pav_db_bind_text(stmt, 1, ra) != 0 || pav_db_bind_text(stmt, 2, "Email") != 0 || step_done(stmt) != 0) {
        return -1;
    }
    return 0;
}

static void seed_email_total(PavSup *s, EmailWalk *w);

static void rewind_email_walk(PavSup *s, EmailWalk *w) {
    int i;

    s->email_done = 0;
    s->email_total = 0;
    w->phase = 1;
    w->cursor = 0;
    w->anchor[0] = '\0';
    for (i = 0; i < w->box_n; i++) {
        w->boxes[i].pos = 0;
        w->boxes[i].total = -1;
        w->boxes[i].done = 0;
        w->boxes[i].counted = 0;
        w->boxes[i].seeded = 0;
    }
    seed_email_total(s, w);
}

static const char *email_page_state(const EmailResult *r) {
    int i;

    for (i = 0; i < r->page_n; i++) {
        if (r->pages[i].state[0] != '\0') {
            return r->pages[i].state;
        }
    }
    return "";
}

static int email_state_conflict(PavSup *s, SupRemote *remote, const EmailResult *r) {
    EmailWalk *w = remote->email;
    const char *state = email_page_state(r);

    if (w == NULL || state[0] == '\0') {
        return 0;
    }
    if (w->anchor[0] == '\0') {
        copy_fit(w->anchor, sizeof(w->anchor), state);
        return 0;
    }
    if (strcmp(w->anchor, state) == 0) {
        return 0;
    }
    if (w->restarts < 1) {
        if (pav_db_call(clear_email_job, remote->ra_id) != 0) {
            email_fail(s, remote, "cannot store messages");
            return 1;
        }
        rewind_email_walk(s, w);
        w->restarts = 1;
        start_email_query(s, remote);
        return 1;
    }
    copy_fit(w->anchor, sizeof(w->anchor), state);
    return 0;
}

static int bind_email_values(sqlite3_stmt *stmt, int at, const EmailRow *row) {
    if (bind_empty_null(stmt, at++, row->thread_id) != 0 || bind_empty_null(stmt, at++, row->blob_id) != 0 ||
        bind_empty_null(stmt, at++, row->message_id) != 0 || bind_empty_null(stmt, at++, row->in_reply_to) != 0 ||
        bind_empty_null(stmt, at++, row->refs) != 0 || bind_empty_null(stmt, at++, row->subject) != 0 ||
        bind_empty_null(stmt, at++, row->from_json) != 0 || bind_empty_null(stmt, at++, row->to_json) != 0 ||
        bind_empty_null(stmt, at++, row->cc_json) != 0 || bind_empty_null(stmt, at++, row->bcc_json) != 0 ||
        bind_empty_null(stmt, at++, row->reply_to_json) != 0 || pav_db_bind_int64(stmt, at++, row->sent_at) != 0 ||
        pav_db_bind_int64(stmt, at++, row->received_at) != 0 || pav_db_bind_int(stmt, at++, row->size) != 0 ||
        bind_empty_null(stmt, at++, row->preview) != 0 || pav_db_bind_int(stmt, at++, row->has_attachment) != 0 ||
        pav_db_bind_int(stmt, at++, row->seen) != 0 || pav_db_bind_int(stmt, at++, row->flagged) != 0 ||
        pav_db_bind_int(stmt, at++, row->answered) != 0 || pav_db_bind_int(stmt, at++, row->forwarded) != 0 ||
        pav_db_bind_int(stmt, at++, row->draft) != 0 || pav_db_bind_int(stmt, at++, row->junk) != 0 ||
        bind_empty_null(stmt, at++, row->keywords) != 0) {
        return -1;
    }
    return 0;
}

static int mailbox_known(sqlite3 *db, const char *ra, const char *id) {
    sqlite3_stmt *stmt = NULL;
    int rc;

    if (pav_db_prepare(db, "sync.email_mbox", "SELECT 1 FROM mailbox WHERE ra_id = ? AND id = ?", &stmt) != 0 ||
        pav_db_bind_text(stmt, 1, ra) != 0 || pav_db_bind_text(stmt, 2, id) != 0) {
        return -1;
    }
    rc = pav_db_step(stmt);
    if (rc == SQLITE_ROW) {
        return 1;
    }
    return rc == SQLITE_DONE ? 0 : -1;
}

static const char *thread_members(const ThreadRow *threads, int n, const char *id) {
    int i;

    for (i = 0; i < n; i++) {
        if (strcmp(threads[i].id, id) == 0 && threads[i].email_ids != NULL && threads[i].email_ids[0] != '\0') {
            return threads[i].email_ids;
        }
    }
    return NULL;
}

static char *fallback_thread_ids(const EmailPage *page, const char *thread_id) {
    json_t *arr = json_array();
    char *text = NULL;
    int i;

    if (arr == NULL) {
        return NULL;
    }
    for (i = 0; i < page->row_n; i++) {
        json_t *id;

        if (strcmp(page->rows[i].thread_id, thread_id) != 0) {
            continue;
        }
        id = json_string(page->rows[i].id);
        if (id == NULL || json_array_append_new(arr, id) != 0) {
            json_decref(arr);
            return NULL;
        }
    }
    text = json_dumps(arr, JSON_COMPACT);
    json_decref(arr);
    return text;
}

static int box_listed(const EmailRow *row, int n, const char *id) {
    int i;

    for (i = 0; i < n; i++) {
        if (strcmp(row->boxes[i].id, id) == 0) {
            return 1;
        }
    }
    return 0;
}

static int remote_present(sqlite3 *db, const char *ra, int *missing) {
    sqlite3_stmt *stmt = NULL;
    int rc;

    *missing = 0;
    if (pav_db_prepare(db, "sync.email_remote", "SELECT 1 FROM remote_account WHERE id = ?", &stmt) != 0 ||
        pav_db_bind_text(stmt, 1, ra) != 0) {
        return -1;
    }
    rc = pav_db_step(stmt);
    if (rc == SQLITE_DONE) {
        *missing = 1;
        return 0;
    }
    return rc == SQLITE_ROW ? 0 : -1;
}

static int store_email_row(sqlite3 *db, const char *ra, EmailPage *page, EmailRow *row, const ThreadRow *threads,
                           int thread_n, int skip_thread) {
    sqlite3_stmt *stmt = NULL;
    int64_t rowid = 0;
    char *kept_body = NULL;
    int rc;
    int found = 0;
    int i;

    if (pav_db_prepare(db, "sync.email_find", "SELECT rowid FROM email WHERE ra_id = ? AND id = ?", &stmt) != 0 ||
        pav_db_bind_text(stmt, 1, ra) != 0 || pav_db_bind_text(stmt, 2, row->id) != 0) {
        return -1;
    }
    rc = pav_db_step(stmt);
    if (rc == SQLITE_ROW) {
        rowid = sqlite3_column_int64(stmt, 0);
        found = 1;
    } else if (rc != SQLITE_DONE) {
        return -1;
    }
    if (found) {
        /* Drop the old index text before the header columns change. The body
           stays, and the insert below puts it back with the new headers. */
        if (pav_fts_indexed_body(db, rowid, &kept_body) != 0 || pav_fts_remove(db, rowid) != 0) {
            free(kept_body);
            return -1;
        }
        if (pav_db_prepare(db, "sync.email_upd",
                           "UPDATE email SET thread_id=?, blob_id=?, message_id=?, in_reply_to=?, refs=?, "
                           "subject=?, from_json=?, to_json=?, cc_json=?, bcc_json=?, reply_to_json=?, "
                           "sent_at=?, received_at=?, size=?, preview=?, has_attachment=?, seen=?, flagged=?, "
                           "answered=?, forwarded=?, draft=?, junk=?, keywords=? WHERE rowid=?",
                           &stmt) != 0 ||
            bind_email_values(stmt, 1, row) != 0 || pav_db_bind_int64(stmt, 24, rowid) != 0 || step_done(stmt) != 0) {
            free(kept_body);
            return -1;
        }
        row->is_new = 0;
    } else {
        if (pav_db_prepare(db, "sync.email_ins",
                           "INSERT INTO email (ra_id, id, thread_id, blob_id, message_id, in_reply_to, refs, "
                           "subject, from_json, to_json, cc_json, bcc_json, reply_to_json, sent_at, received_at, "
                           "size, preview, has_attachment, seen, flagged, answered, forwarded, draft, junk, "
                           "keywords, body_state) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
                           "?, ?, ?, ?, ?, ?, 'none')",
                           &stmt) != 0 ||
            pav_db_bind_text(stmt, 1, ra) != 0 || pav_db_bind_text(stmt, 2, row->id) != 0 ||
            bind_email_values(stmt, 3, row) != 0 || step_done(stmt) != 0) {
            return -1;
        }
        rowid = (int64_t)sqlite3_last_insert_rowid(db);
        row->is_new = 1;
    }
    if (rowid <= 0) {
        free(kept_body);
        return -1;
    }
    if (pav_db_prepare(db, "sync.email_unlink", "DELETE FROM email_mailbox WHERE email_rowid = ?", &stmt) != 0 ||
        pav_db_bind_int64(stmt, 1, rowid) != 0 || step_done(stmt) != 0) {
        free(kept_body);
        return -1;
    }
    for (i = 0; i < row->box_n; i++) {
        int known;

        if (row->boxes[i].id[0] == '\0' || box_listed(row, i, row->boxes[i].id)) {
            continue;
        }
        known = mailbox_known(db, ra, row->boxes[i].id);
        if (known < 0) {
            free(kept_body);
            return -1;
        }
        if (known == 0) {
            continue;
        }
        if (pav_db_prepare(db, "sync.email_link",
                           "INSERT INTO email_mailbox (email_rowid, ra_id, mailbox_id, received_at) "
                           "VALUES (?, ?, ?, ?)",
                           &stmt) != 0 ||
            pav_db_bind_int64(stmt, 1, rowid) != 0 || pav_db_bind_text(stmt, 2, ra) != 0 ||
            pav_db_bind_text(stmt, 3, row->boxes[i].id) != 0 ||
            pav_db_bind_int64(stmt, 4, row->received_at) != 0 || step_done(stmt) != 0) {
            free(kept_body);
            return -1;
        }
    }
    /* A header row (no fetched body) uses the straight-line insert. A fetched
       body is bound as its own parameter. */
    if (kept_body == NULL || kept_body[0] == '\0') {
        if (pav_fts_index_headers(db, rowid, row->subject, row->from_text, row->to_text, 0) != 0) {
            free(kept_body);
            return -1;
        }
    } else if (pav_fts_insert(db, rowid, row->subject, row->from_text, row->to_text, kept_body) != 0) {
        free(kept_body);
        return -1;
    }
    free(kept_body);
    kept_body = NULL;
    /* A delta fetch writes headers only. Thread/get owns membership. */
    if (skip_thread) {
        return 0;
    }
    if (row->thread_id[0] != '\0') {
        const char *members = thread_members(threads, thread_n, row->thread_id);
        char *owned = NULL;
        int ok;

        if (members == NULL) {
            owned = fallback_thread_ids(page, row->thread_id);
            if (owned == NULL) {
                return -1;
            }
            members = owned;
        }
        ok = pav_db_prepare(db, "sync.email_thread",
                            "INSERT INTO thread (ra_id, id, email_ids) VALUES (?, ?, ?) "
                            "ON CONFLICT(ra_id, id) DO UPDATE SET email_ids = excluded.email_ids",
                            &stmt) == 0 &&
             pav_db_bind_text(stmt, 1, ra) == 0 && pav_db_bind_text(stmt, 2, row->thread_id) == 0 &&
             pav_db_bind_text(stmt, 3, members) == 0 && step_done(stmt) == 0;
        free(owned);
        if (!ok) {
            return -1;
        }
    }
    return 0;
}

typedef struct EmailPageJob {
    char ra_id[256];
    EmailPage *page;
    const ThreadRow *threads;
    int thread_n;
    int skip_thread;
    int missing;
    int wrote;
} EmailPageJob;

typedef struct EmailStateJob {
    char ra_id[256];
    char state[128];
    int64_t now_ms;
    int missing;
} EmailStateJob;

static int write_email_page_job(void *ctx, sqlite3 *db) {
    EmailPageJob *job = ctx;
    int i;

    if (job == NULL || job->page == NULL) {
        return -1;
    }
    if (remote_present(db, job->ra_id, &job->missing) != 0) {
        return -1;
    }
    if (job->missing) {
        return 0;
    }
    for (i = 0; i < job->page->row_n; i++) {
        if (store_email_row(db, job->ra_id, job->page, &job->page->rows[i], job->threads, job->thread_n,
                            job->skip_thread) != 0) {
            return -1;
        }
    }
    job->wrote = job->page->row_n;
    return 0;
}

static int write_email_state_job(void *ctx, sqlite3 *db) {
    EmailStateJob *job = ctx;
    sqlite3_stmt *stmt = NULL;

    if (remote_present(db, job->ra_id, &job->missing) != 0) {
        return -1;
    }
    if (job->missing) {
        return 0;
    }
    if (pav_db_prepare(db, "sync.email_state",
                       "INSERT INTO sync_state (ra_id, type, state, updated) VALUES (?, ?, ?, ?) "
                       "ON CONFLICT(ra_id, type) DO UPDATE SET state = excluded.state, updated = excluded.updated",
                       &stmt) != 0 ||
        pav_db_bind_text(stmt, 1, job->ra_id) != 0 || pav_db_bind_text(stmt, 2, "Email") != 0 ||
        pav_db_bind_text(stmt, 3, job->state) != 0 || pav_db_bind_int64(stmt, 4, job->now_ms) != 0 ||
        step_done(stmt) != 0) {
        return -1;
    }
    return 0;
}

static void finish_email_state(PavSup *s, SupRemote *remote) {
    EmailStateJob job;
    EmailWalk *w = remote != NULL ? remote->email : NULL;
    int64_t now;

    if (remote == NULL || w == NULL) {
        email_fail(s, remote, "request rejected");
        return;
    }
    if (email_cancelled(s)) {
        finish_email_cancel(s, remote);
        return;
    }
    memset(&job, 0, sizeof(job));
    if (!copy_fit(job.ra_id, sizeof(job.ra_id), remote->ra_id)) {
        email_fail(s, remote, "request rejected");
        return;
    }
    if (!copy_fit(job.state, sizeof(job.state), w->anchor[0] != '\0' ? w->anchor : "em-1")) {
        email_fail(s, remote, "request rejected");
        return;
    }
    now = pav_now_ms();
    job.now_ms = now > 0 ? now : 0;
    if (pav_db_call(write_email_state_job, &job) != 0) {
        email_fail(s, remote, "cannot store messages");
        return;
    }
    if (job.missing) {
        email_fail(s, remote, "request rejected");
        return;
    }
    clear_email_walk(remote);
    begin_remote(s, remote->next);
}

static void add_email_total(PavSup *s, int add) {
    int64_t sum;

    if (add <= 0) {
        return;
    }
    sum = (int64_t)s->email_total + (int64_t)add;
    if (sum > 1000000000) {
        sum = 1000000000;
    }
    s->email_total = (int)sum;
}

static void sub_email_total(PavSup *s, int sub) {
    if (sub <= 0 || s->email_total <= 0) {
        return;
    }
    if (sub >= s->email_total) {
        s->email_total = 0;
        return;
    }
    s->email_total -= sub;
}

/* Folder counts are already stored. Seeding the activity total from them
   keeps the first small folder from looking like the whole sync. */
static void seed_email_total(PavSup *s, EmailWalk *w) {
    int i;

    if (w == NULL) {
        return;
    }
    for (i = 0; i < w->box_n; i++) {
        if (w->boxes[i].total_hint > 0) {
            add_email_total(s, w->boxes[i].total_hint);
            w->boxes[i].seeded = 1;
        }
    }
}

static void note_email_page(PavSup *s, EmailWalk *w, const EmailPage *page, int wrote) {
    EmailBox *box = NULL;
    int i;
    int pos;

    for (i = 0; i < w->box_n; i++) {
        if (strcmp(w->boxes[i].id, page->mailbox_id) == 0) {
            box = &w->boxes[i];
            break;
        }
    }
    if (box == NULL) {
        return;
    }
    if (page->total >= 0) {
        box->total = page->total;
    }
    if (page->position == 0 && !box->counted) {
        int actual = page->total >= 0 ? page->total : wrote;
        int base = box->seeded ? box->total_hint : 0;
        int64_t delta = (int64_t)actual - (int64_t)base;

        if (delta > 0) {
            add_email_total(s, delta > 1000000000 ? 1000000000 : (int)delta);
        } else if (delta < 0) {
            int64_t drop = -delta;

            sub_email_total(s, drop > 1000000000 ? 1000000000 : (int)drop);
        }
        box->counted = 1;
    }
    pos = page->position + (page->id_n > 0 ? page->id_n : 0);
    box->pos = pos > 0 ? pos : 0;
    if (page->id_n < page->limit || (box->total >= 0 && box->pos >= box->total)) {
        box->done = 1;
    }
    if (wrote > 0) {
        int64_t sum = (int64_t)s->email_done + (int64_t)wrote;

        s->email_done = sum > 1000000000 ? 1000000000 : (int)sum;
    }
    if (s->activity_id > 0) {
        pav_activity_progress(s->activity_id, s->email_done, s->email_total > 0 ? s->email_total : -1, "messages",
                              "Messages");
    }
}

static void emit_messages(const char *account, const EmailPage *page) {
    json_t *obj = json_object();
    json_t *created = json_array();
    json_t *updated = json_array();
    json_t *destroyed = json_array();
    json_t *account_id;
    char *text;
    int i;
    int ncreated = 0;
    int nupdated = 0;

    if (obj == NULL || created == NULL || updated == NULL || destroyed == NULL) {
        json_decref(obj);
        json_decref(created);
        json_decref(updated);
        json_decref(destroyed);
        return;
    }
    for (i = 0; i < page->row_n; i++) {
        json_t *id = json_string(page->rows[i].id);

        if (id == NULL || json_array_append_new(page->rows[i].is_new ? created : updated, id) != 0) {
            json_decref(obj);
            json_decref(created);
            json_decref(updated);
            json_decref(destroyed);
            return;
        }
        if (page->rows[i].is_new) {
            ncreated++;
        } else {
            nupdated++;
        }
    }
    if (ncreated == 0 && nupdated == 0) {
        json_decref(obj);
        json_decref(created);
        json_decref(updated);
        json_decref(destroyed);
        return;
    }
    account_id = json_string(account != NULL ? account : "");
    if (account_id == NULL || put_json(obj, "accountId", account_id) != 0) {
        json_decref(obj);
        json_decref(created);
        json_decref(updated);
        json_decref(destroyed);
        return;
    }
    if (put_json(obj, "created", created) != 0) {
        json_decref(obj);
        json_decref(updated);
        json_decref(destroyed);
        return;
    }
    if (put_json(obj, "updated", updated) != 0) {
        json_decref(obj);
        json_decref(destroyed);
        return;
    }
    if (put_json(obj, "destroyed", destroyed) != 0) {
        json_decref(obj);
        return;
    }
    text = json_dumps(obj, JSON_COMPACT);
    json_decref(obj);
    if (text == NULL) {
        return;
    }
    pav_event_post("mail.messages_changed", text);
    free(text);
    pav_log("sync", PAV_INFO, 1, "messages changed %s", account != NULL ? account : "");
}

static void drop_pending(EmailWalk *w) {
    free_email_pages(w->pending, w->pending_n);
    w->pending = NULL;
    w->pending_n = 0;
    free_thread_rows(w->threads, w->thread_n);
    w->threads = NULL;
    w->thread_n = 0;
    free(w->thread_ids);
    w->thread_ids = NULL;
    w->thread_id_n = 0;
    w->thread_off = 0;
}

static int collect_thread_ids(EmailWalk *w) {
    int p;
    int r;

    free(w->thread_ids);
    w->thread_ids = NULL;
    w->thread_id_n = 0;
    w->thread_off = 0;
    for (p = 0; p < w->pending_n; p++) {
        EmailPage *page = &w->pending[p];

        for (r = 0; r < page->row_n; r++) {
            int i;
            int seen = 0;

            if (page->rows[r].thread_id[0] == '\0') {
                continue;
            }
            for (i = 0; i < w->thread_id_n; i++) {
                if (strcmp(w->thread_ids[i].id, page->rows[r].thread_id) == 0) {
                    seen = 1;
                    break;
                }
            }
            if (seen) {
                continue;
            }
            if (append_id(&w->thread_ids, &w->thread_id_n, page->rows[r].thread_id) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

static int take_threads(EmailWalk *w, EmailResult *r) {
    int i;

    for (i = 0; i < r->thread_n; i++) {
        ThreadRow *grown = realloc(w->threads, (size_t)(w->thread_n + 1) * sizeof(*grown));

        if (grown == NULL) {
            return -1;
        }
        w->threads = grown;
        w->threads[w->thread_n] = r->threads[i];
        r->threads[i].email_ids = NULL;
        w->thread_n++;
    }
    return 0;
}

static void write_pending(PavSup *s, SupRemote *remote) {
    EmailWalk *w = remote->email;
    int i;

    if (w == NULL) {
        email_fail(s, remote, "request rejected");
        return;
    }
    for (i = 0; i < w->pending_n; i++) {
        EmailPage *page = &w->pending[i];
        EmailPageJob job;

        if (email_cancelled(s)) {
            finish_email_cancel(s, remote);
            return;
        }
        if (page->row_n > 0) {
            memset(&job, 0, sizeof(job));
            if (!copy_fit(job.ra_id, sizeof(job.ra_id), remote->ra_id)) {
                email_fail(s, remote, "request rejected");
                return;
            }
            job.page = page;
            job.threads = w->threads;
            job.thread_n = w->thread_n;
            if (pav_db_call(write_email_page_job, &job) != 0) {
                email_fail(s, remote, "cannot store messages");
                return;
            }
            if (job.missing) {
                email_fail(s, remote, "request rejected");
                return;
            }
            emit_messages(s->account_id, page);
        }
        note_email_page(s, w, page, page->row_n);
    }
    drop_pending(w);
    start_email_query(s, remote);
}

static void apply_email(PavSup *s, void *result) {
    EmailResult *r = result;
    SupRemote *remote;
    EmailWalk *w;

    remote = find_remote(s, r->ra_id);
    if (remote == NULL || remote->email == NULL) {
        publish_state(s, "error:server", "request rejected", 0, PAV_ACT_FAILED);
        return;
    }
    if (fail_type_transport(s, r->auth_rc, r->status, r->detail)) {
        clear_email_walk(remote);
        return;
    }
    if (r->method_error) {
        email_fail(s, remote, r->detail);
        return;
    }
    if (email_cancelled(s)) {
        finish_email_cancel(s, remote);
        return;
    }
    if (r->email_mode >= EMAIL_MODE_CHANGES || remote->email->kind == 1) {
        email_delta_apply(s, remote, r);
        return;
    }
    w = remote->email;
    if (r->email_mode == 2) {
        if (take_threads(w, r) != 0) {
            email_fail(s, remote, "offline");
            return;
        }
        if (w->thread_off < w->thread_id_n) {
            start_email_threads(s, remote);
        } else {
            write_pending(s, remote);
        }
        return;
    }
    if (email_state_conflict(s, remote, r)) {
        return;
    }
    w = remote->email;
    if (w == NULL) {
        email_fail(s, remote, "request rejected");
        return;
    }
    drop_pending(w);
    w->pending = r->pages;
    w->pending_n = r->page_n;
    r->pages = NULL;
    r->page_n = 0;
    if (collect_thread_ids(w) != 0) {
        email_fail(s, remote, "offline");
        return;
    }
    if (w->thread_id_n > 0) {
        start_email_threads(s, remote);
    } else {
        write_pending(s, remote);
    }
}

static void begin_email(PavSup *s, SupRemote *remote) {
    EmailWalk *w;

    if (remote == NULL) {
        finish_sync_ok(s);
        return;
    }
    if (email_cancelled(s)) {
        finish_email_cancel(s, remote);
        return;
    }
    if (email_has_state(remote->ra_id)) {
        if (!email_backfill(remote->ra_id)) {
            begin_email_delta(s, remote);
            return;
        }
        pav_log("sync", PAV_INFO, 1, "mail backfill %s", s->account_id);
    }
    s->email_delta = 0;
    w = calloc(1, sizeof(*w));
    if (w == NULL) {
        email_fail(s, remote, "offline");
        return;
    }
    remote->email = w;
    w->phase = 1;
    load_email_caps(s->account_id, &w->limit, &w->calls_max);
    if (load_email_boxes(remote->ra_id, &w->boxes, &w->box_n) != 0) {
        email_fail(s, remote, "cannot store messages");
        return;
    }
    seed_email_total(s, w);
    s->saw_email = 1;
    if (s->activity_id > 0) {
        pav_activity_progress(s->activity_id, s->email_done, s->email_total > 0 ? s->email_total : -1, "messages",
                              "Messages");
    }
    start_email_query(s, remote);
}

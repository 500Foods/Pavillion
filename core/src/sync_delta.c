/* Included from sync.c after sync_email.c. Email/changes and Thread/changes (M-5). */
#ifndef PAV_SYNC_EMAIL_INCLUDED
#error "sync_delta.c is included from sync.c"
#endif

#define DELTA_BOOT 1
#define DELTA_CHANGES 2
#define DELTA_FETCH 3
#define DELTA_THREADS 4
#define DELTA_BOXES 5
#define DELTA_FIX 6
#define DELTA_PAGE_CAP 32

static void start_changes(PavSup *s, SupRemote *remote);
static void start_fetch(PavSup *s, SupRemote *remote);
static void after_fetch(PavSup *s, SupRemote *remote);
static void start_boxes(PavSup *s, SupRemote *remote);
static void delta_continue(PavSup *s, SupRemote *remote);
static void start_fix(PavSup *s, SupRemote *remote);

static int list_has(const SyncId *list, int n, const char *id) {
    int i;

    if (id == NULL || id[0] == '\0') {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (strcmp(list[i].id, id) == 0) {
            return 1;
        }
    }
    return 0;
}

static void free_ids(SyncId **list, int *n) {
    free(*list);
    *list = NULL;
    if (n != NULL) {
        *n = 0;
    }
}

static int append_except(SyncId **dst, int *n, const SyncId *src, int src_n, const SyncId *skip, int skip_n,
                         const SyncId *skip2, int skip2_n) {
    int i;

    for (i = 0; i < src_n; i++) {
        if (list_has(skip, skip_n, src[i].id) || list_has(skip2, skip2_n, src[i].id) || list_has(*dst, *n, src[i].id)) {
            continue;
        }
        if (append_id(dst, n, src[i].id) != 0) {
            return -1;
        }
    }
    return 0;
}

static void clear_delta_page(EmailWalk *w) {
    if (w == NULL) {
        return;
    }
    free_ids(&w->created, &w->created_n);
    free_ids(&w->updated, &w->updated_n);
    free_ids(&w->destroyed, &w->destroyed_n);
    free_ids(&w->th_get, &w->th_get_n);
    free_ids(&w->th_destroyed, &w->th_destroyed_n);
    free_ids(&w->touch, &w->touch_n);
    w->th_get_off = 0;
    w->fetch_off = 0;
}

static void clear_delta_lists(EmailWalk *w) {
    if (w == NULL) {
        return;
    }
    clear_delta_page(w);
    free_ids(&w->local_threads, &w->local_thread_n);
    w->local_off = 0;
}

static void free_delta_extra(EmailResult *r) {
    int i;

    if (r == NULL) {
        return;
    }
    free_ids(&r->created, &r->created_n);
    free_ids(&r->updated, &r->updated_n);
    free_ids(&r->destroyed, &r->destroyed_n);
    free_ids(&r->th_get, &r->th_get_n);
    free_ids(&r->th_destroyed, &r->th_destroyed_n);
    free_ids(&r->thread_missing, &r->thread_missing_n);
    for (i = 0; i < r->mailbox_n; i++) {
        free(r->mailboxes[i].my_rights);
    }
    free(r->mailboxes);
    r->mailboxes = NULL;
    r->mailbox_n = 0;
}

static int load_named_state(const char *ra, const char *type, char *dst, size_t n) {
    PavDbRead *read = NULL;
    sqlite3_stmt *stmt = NULL;
    int rc = 0;

    if (dst == NULL || n == 0) {
        return -1;
    }
    dst[0] = '\0';
    if (ra == NULL || ra[0] == '\0' || type == NULL || pav_db_read_begin(&read) != 0) {
        return -1;
    }
    if (sqlite3_prepare_v2(pav_db_read_handle(read),
                           "SELECT state FROM sync_state WHERE ra_id = ? AND type = ?", -1, &stmt, NULL) != SQLITE_OK) {
        pav_db_read_end(read);
        return -1;
    }
    if (sqlite3_bind_text(stmt, 1, ra, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 2, type, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        pav_db_read_end(read);
        return -1;
    }
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *state = (const char *)sqlite3_column_text(stmt, 0);

        if (!copy_fit(dst, n, state)) {
            rc = -1;
        }
    }
    sqlite3_finalize(stmt);
    pav_db_read_end(read);
    return rc;
}

static int load_id_list(const char *ra, const char *sql, SyncId **list, int *n) {
    PavDbRead *read = NULL;
    sqlite3_stmt *stmt = NULL;
    int rc;

    free_ids(list, n);
    if (ra == NULL || ra[0] == '\0' || pav_db_read_begin(&read) != 0) {
        return -1;
    }
    if (sqlite3_prepare_v2(pav_db_read_handle(read), sql, -1, &stmt, NULL) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 1, ra, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        pav_db_read_end(read);
        return -1;
    }
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *id = (const char *)sqlite3_column_text(stmt, 0);

        if (id == NULL || id[0] == '\0') {
            continue;
        }
        if (append_id(list, n, id) != 0) {
            sqlite3_finalize(stmt);
            pav_db_read_end(read);
            return -1;
        }
    }
    sqlite3_finalize(stmt);
    pav_db_read_end(read);
    return rc == SQLITE_DONE ? 0 : -1;
}

static int take_mailbox(EmailResult *out, const json_t *obj) {
    TypeResult tmp;

    memset(&tmp, 0, sizeof(tmp));
    tmp.mailboxes = out->mailboxes;
    tmp.mailbox_n = out->mailbox_n;
    if (append_mailbox(&tmp, obj) != 0) {
        out->mailboxes = tmp.mailboxes;
        out->mailbox_n = tmp.mailbox_n;
        return -1;
    }
    out->mailboxes = tmp.mailboxes;
    out->mailbox_n = tmp.mailbox_n;
    return 0;
}

static int cid_starts(const char *cid, const char *prefix) {
    size_t n;

    if (cid == NULL || prefix == NULL) {
        return 0;
    }
    n = strlen(prefix);
    return strncmp(cid, prefix, n) == 0;
}

static int reduce_delta(const char *body, const SyncReq *owned, EmailResult *out) {
    json_t *root;
    json_t *responses;
    size_t i;
    size_t n;
    int parsed = 0;
    int saw_email = 0;
    int saw_thread = 0;
    int saw_get = 0;
    int saw_threads = 0;
    int saw_mailbox = 0;
    int want_thread;

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
    want_thread = owned->body != NULL && strstr(owned->body, "\"Thread/changes\"") != NULL;
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
            if (strcmp(code, "cannotCalculateChanges") == 0 && cid_starts(cid, "ec")) {
                out->email_resync = 1;
            } else if (strcmp(code, "cannotCalculateChanges") == 0 && cid_starts(cid, "tc")) {
                out->thread_resync = 1;
            } else {
                out->method_error = 1;
                copy_fit(out->detail, sizeof(out->detail), code[0] != '\0' ? code : "request rejected");
            }
            continue;
        }
        if (name == NULL || !json_is_object(args)) {
            continue;
        }
        if (strcmp(name, "Email/changes") == 0) {
            saw_email = 1;
            copy_json_string(out->email_state, sizeof(out->email_state), args, "newState");
            out->email_more = json_is_true(json_object_get(args, "hasMoreChanges")) ? 1 : 0;
            if (append_id_array(&out->created, &out->created_n, json_object_get(args, "created")) != 0 ||
                append_id_array(&out->updated, &out->updated_n, json_object_get(args, "updated")) != 0 ||
                append_id_array(&out->destroyed, &out->destroyed_n, json_object_get(args, "destroyed")) != 0) {
                json_decref(root);
                return -1;
            }
        } else if (strcmp(name, "Thread/changes") == 0) {
            saw_thread = 1;
            copy_json_string(out->thread_state, sizeof(out->thread_state), args, "newState");
            out->thread_more = json_is_true(json_object_get(args, "hasMoreChanges")) ? 1 : 0;
            if (append_id_array(&out->th_get, &out->th_get_n, json_object_get(args, "created")) != 0 ||
                append_id_array(&out->th_get, &out->th_get_n, json_object_get(args, "updated")) != 0 ||
                append_id_array(&out->th_destroyed, &out->th_destroyed_n, json_object_get(args, "destroyed")) != 0) {
                json_decref(root);
                return -1;
            }
        } else if (strcmp(name, "Email/get") == 0) {
            int rc;

            saw_get = 1;
            if (out->page_n == 0) {
                out->pages = calloc(1, sizeof(*out->pages));
                if (out->pages == NULL) {
                    json_decref(root);
                    return -1;
                }
                out->page_n = 1;
            }
            list = json_object_get(args, "list");
            if (!json_is_array(list)) {
                continue;
            }
            for (k = 0; k < json_array_size(list); k++) {
                rc = append_email_row(&out->pages[0], json_array_get(list, k), &parsed);
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
            saw_threads = 1;
            copy_json_string(out->thread_state, sizeof(out->thread_state), args, "state");
            if (append_id_array(&out->thread_missing, &out->thread_missing_n, json_object_get(args, "notFound")) != 0) {
                json_decref(root);
                return -1;
            }
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
        } else if (strcmp(name, "Mailbox/get") == 0) {
            saw_mailbox = 1;
            list = json_object_get(args, "list");
            if (!json_is_array(list)) {
                continue;
            }
            for (k = 0; k < json_array_size(list); k++) {
                if (take_mailbox(out, json_array_get(list, k)) != 0) {
                    json_decref(root);
                    return -1;
                }
            }
        }
    }
    if (!out->method_error && owned->email_mode == EMAIL_MODE_CHANGES) {
        if (!saw_email && !out->email_resync) {
            out->method_error = 1;
            copy_fit(out->detail, sizeof(out->detail), "not a jmap response");
        } else if (saw_email && !out->email_resync && out->email_state[0] == '\0') {
            out->method_error = 1;
            copy_fit(out->detail, sizeof(out->detail), "not a jmap response");
        } else if (want_thread && !saw_thread && !out->thread_resync) {
            out->method_error = 1;
            copy_fit(out->detail, sizeof(out->detail), "not a jmap response");
        } else if (want_thread && saw_thread && !out->thread_resync && out->thread_state[0] == '\0') {
            out->method_error = 1;
            copy_fit(out->detail, sizeof(out->detail), "not a jmap response");
        }
    } else if (!out->method_error && owned->email_mode == EMAIL_MODE_GETIDS && !saw_get) {
        out->method_error = 1;
        copy_fit(out->detail, sizeof(out->detail), "not a jmap response");
    } else if (!out->method_error && owned->email_mode == EMAIL_MODE_THREAD_GET && !saw_threads) {
        out->method_error = 1;
        copy_fit(out->detail, sizeof(out->detail), "not a jmap response");
    } else if (!out->method_error && owned->email_mode == EMAIL_MODE_MAILBOX && !saw_mailbox) {
        out->method_error = 1;
        copy_fit(out->detail, sizeof(out->detail), "not a jmap response");
    }
    json_decref(root);
    return out->method_error ? -1 : 0;
}

static char *delta_wrap(json_t *calls) {
    json_t *root = json_object();
    json_t *using = json_array();
    char *text;

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
    if (put_json(root, "methodCalls", calls) != 0 || put_json(root, "RequestId", json_string("pav-m5")) != 0) {
        json_decref(root);
        return NULL;
    }
    text = json_dumps(root, JSON_COMPACT);
    json_decref(root);
    return text;
}

static json_t *delta_account_args(const char *account) {
    json_t *args = json_object();
    json_t *account_id = json_string(account);

    if (args == NULL || account_id == NULL || put_json(args, "accountId", account_id) != 0) {
        json_decref(args);
        if (args == NULL) {
            json_decref(account_id);
        }
        return NULL;
    }
    return args;
}

/* put_json steals the value on success and frees it on failure. Do not decref it again. */
static int put_changes(json_t *calls, const char *account, const char *since_text, int max_changes, const char *method,
                       const char *cid) {
    json_t *args = delta_account_args(account);
    json_t *since = json_string(since_text);
    json_t *maxc = json_integer(max_changes > 0 ? max_changes : 1);

    if (args == NULL || since == NULL || maxc == NULL) {
        json_decref(args);
        json_decref(since);
        json_decref(maxc);
        return -1;
    }
    if (put_json(args, "sinceState", since) != 0) {
        json_decref(args);
        json_decref(maxc);
        return -1;
    }
    if (put_json(args, "maxChanges", maxc) != 0) {
        json_decref(args);
        return -1;
    }
    if (append_method(calls, method, args, cid) != 0) {
        return -1;
    }
    return 0;
}

static char *delta_changes_body(const char *account, const EmailWalk *w) {
    json_t *calls = json_array();

    if (calls == NULL) {
        return NULL;
    }
    if (put_changes(calls, account, w->email_since, w->changes_max, "Email/changes", "ec0") != 0) {
        json_decref(calls);
        return NULL;
    }
    if (w->thread_since[0] != '\0' && !w->skip_thread_changes &&
        put_changes(calls, account, w->thread_since, w->changes_max, "Thread/changes", "tc0") != 0) {
        json_decref(calls);
        return NULL;
    }
    return delta_wrap(calls);
}

static char *delta_ids_body(const char *account, const char *method, const char *cid, const SyncId *ids, int n,
                            int with_props) {
    json_t *calls = json_array();
    json_t *args;
    json_t *arr;
    json_t *props = NULL;

    if (calls == NULL) {
        return NULL;
    }
    args = delta_account_args(account);
    arr = ids_array(ids, n);
    if (with_props) {
        props = header_properties();
    }
    if (args == NULL || arr == NULL || (with_props && props == NULL)) {
        json_decref(args);
        json_decref(arr);
        json_decref(props);
        json_decref(calls);
        return NULL;
    }
    if (put_json(args, "ids", arr) != 0) {
        json_decref(args);
        json_decref(props);
        json_decref(calls);
        return NULL;
    }
    if (with_props && put_json(args, "properties", props) != 0) {
        json_decref(args);
        json_decref(calls);
        return NULL;
    }
    if (append_method(calls, method, args, cid) != 0) {
        json_decref(calls);
        return NULL;
    }
    return delta_wrap(calls);
}

static int delta_submit(PavSup *s, SupRemote *remote, int mode, char *body) {
    EmailWalk *w = remote->email;
    SyncReq *req;

    if (w == NULL) {
        free(body);
        email_fail(s, remote, "request rejected");
        return -1;
    }
    if (body == NULL) {
        email_fail(s, remote, "offline");
        return -1;
    }
    if (w->reqs >= PAV_SYNC_EMAIL_REQ_MAX) {
        free(body);
        email_fail(s, remote, "too many pages");
        return -1;
    }
    w->reqs++;
    req = make_email_req(s, remote, mode, NULL, 0, body);
    if (req == NULL) {
        email_fail(s, remote, "offline");
        return -1;
    }
    if (submit_email(s, req) != 0) {
        return -1;
    }
    return 0;
}

typedef struct DeltaDestroy {
    char ra_id[256];
    const SyncId *ids;
    int id_n;
    int all;
    SyncId *boxes;
    int box_n;
    char **paths;
    int path_n;
    SyncId *blobs;
    int blob_n;
    int missing;
} DeltaDestroy;

static void delta_free_paths(DeltaDestroy *job) {
    int i;

    for (i = 0; i < job->path_n; i++) {
        free(job->paths[i]);
    }
    free(job->paths);
    job->paths = NULL;
    job->path_n = 0;
}

static int delta_note_path(DeltaDestroy *job, const char *path) {
    char *copy;
    char **grown;

    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    copy = dup_capped(path, 4095);
    if (copy == NULL) {
        return -1;
    }
    grown = realloc(job->paths, (size_t)(job->path_n + 1) * sizeof(*grown));
    if (grown == NULL) {
        free(copy);
        return -1;
    }
    job->paths = grown;
    job->paths[job->path_n++] = copy;
    return 0;
}

static int delta_note_blob(DeltaDestroy *job, sqlite3 *db, const char *blob) {
    sqlite3_stmt *stmt = NULL;
    int emails = 0;
    int attachments = 0;

    if (blob == NULL || blob[0] == '\0' || list_has(job->blobs, job->blob_n, blob)) {
        return 0;
    }
    if (pav_db_prepare(db, "sync.delta_blob_email", "SELECT count(*) FROM email WHERE ra_id = ? AND blob_id = ?",
                       &stmt) != 0 ||
        pav_db_bind_text(stmt, 1, job->ra_id) != 0 || pav_db_bind_text(stmt, 2, blob) != 0 ||
        pav_db_step(stmt) != SQLITE_ROW) {
        return -1;
    }
    emails = sqlite3_column_int(stmt, 0);
    if (pav_db_prepare(db, "sync.delta_blob_attach",
                       "SELECT count(*) FROM attachment a JOIN email e ON e.rowid = a.email_rowid "
                       "WHERE e.ra_id = ? AND a.blob_id = ?",
                       &stmt) != 0 ||
        pav_db_bind_text(stmt, 1, job->ra_id) != 0 || pav_db_bind_text(stmt, 2, blob) != 0 ||
        pav_db_step(stmt) != SQLITE_ROW) {
        return -1;
    }
    attachments = sqlite3_column_int(stmt, 0);
    if (emails + attachments > 0) {
        return 0;
    }
    return append_id(&job->blobs, &job->blob_n, blob);
}

/* Attachment rows cascade on DELETE. Their blob ids were read before that. */
static int delta_destroy_blobs_from_saved(DeltaDestroy *job, sqlite3 *db, const SyncId *saved, int saved_n) {
    int i;

    for (i = 0; i < saved_n; i++) {
        if (delta_note_blob(job, db, saved[i].id) != 0) {
            return -1;
        }
    }
    return 0;
}

static int delta_collect_attach_blobs(sqlite3 *db, int64_t rowid, SyncId **saved, int *saved_n) {
    sqlite3_stmt *stmt = NULL;
    int rc;

    if (pav_db_prepare(db, "sync.delta_attach", "SELECT blob_id, local_path FROM attachment WHERE email_rowid = ?",
                       &stmt) != 0 ||
        pav_db_bind_int64(stmt, 1, rowid) != 0) {
        return -1;
    }
    while ((rc = pav_db_step(stmt)) == SQLITE_ROW) {
        const char *blob = (const char *)sqlite3_column_text(stmt, 0);

        if (blob != NULL && blob[0] != '\0' && append_id(saved, saved_n, blob) != 0) {
            return -1;
        }
    }
    return rc == SQLITE_DONE ? 0 : -1;
}

static int delta_destroy_one_full(DeltaDestroy *job, sqlite3 *db, const char *id) {
    sqlite3_stmt *stmt = NULL;
    SyncId *attach = NULL;
    int attach_n = 0;
    int64_t rowid = 0;
    char blob[128];
    int rc;
    int ok;

    blob[0] = '\0';
    if (pav_db_prepare(db, "sync.delta_lookup", "SELECT rowid, blob_id FROM email WHERE ra_id = ? AND id = ?",
                       &stmt) != 0 ||
        pav_db_bind_text(stmt, 1, job->ra_id) != 0 || pav_db_bind_text(stmt, 2, id) != 0) {
        return -1;
    }
    rc = pav_db_step(stmt);
    if (rc == SQLITE_DONE) {
        return 0;
    }
    if (rc != SQLITE_ROW) {
        return -1;
    }
    rowid = sqlite3_column_int64(stmt, 0);
    copy_fit(blob, sizeof(blob), (const char *)sqlite3_column_text(stmt, 1));
    if (pav_db_prepare(db, "sync.delta_body",
                       "SELECT text_plain_path, html_raw_path, html_sanitized_path FROM body_cache "
                       "WHERE email_rowid = ?",
                       &stmt) != 0 ||
        pav_db_bind_int64(stmt, 1, rowid) != 0) {
        return -1;
    }
    rc = pav_db_step(stmt);
    if (rc == SQLITE_ROW) {
        if (delta_note_path(job, (const char *)sqlite3_column_text(stmt, 0)) != 0 ||
            delta_note_path(job, (const char *)sqlite3_column_text(stmt, 1)) != 0 ||
            delta_note_path(job, (const char *)sqlite3_column_text(stmt, 2)) != 0) {
            return -1;
        }
    } else if (rc != SQLITE_DONE) {
        return -1;
    }
    if (delta_collect_attach_blobs(db, rowid, &attach, &attach_n) != 0) {
        free_ids(&attach, &attach_n);
        return -1;
    }
    /* local_path was not kept. Read it again before the delete. */
    if (pav_db_prepare(db, "sync.delta_attach_path", "SELECT local_path FROM attachment WHERE email_rowid = ?",
                       &stmt) != 0 ||
        pav_db_bind_int64(stmt, 1, rowid) != 0) {
        free_ids(&attach, &attach_n);
        return -1;
    }
    while ((rc = pav_db_step(stmt)) == SQLITE_ROW) {
        if (delta_note_path(job, (const char *)sqlite3_column_text(stmt, 0)) != 0) {
            free_ids(&attach, &attach_n);
            return -1;
        }
    }
    if (rc != SQLITE_DONE) {
        free_ids(&attach, &attach_n);
        return -1;
    }
    if (pav_db_prepare(db, "sync.delta_boxes", "SELECT mailbox_id FROM email_mailbox WHERE email_rowid = ?", &stmt) !=
            0 ||
        pav_db_bind_int64(stmt, 1, rowid) != 0) {
        free_ids(&attach, &attach_n);
        return -1;
    }
    while ((rc = pav_db_step(stmt)) == SQLITE_ROW) {
        const char *box = (const char *)sqlite3_column_text(stmt, 0);

        if (box != NULL && !list_has(job->boxes, job->box_n, box) && append_id(&job->boxes, &job->box_n, box) != 0) {
            free_ids(&attach, &attach_n);
            return -1;
        }
    }
    if (rc != SQLITE_DONE) {
        free_ids(&attach, &attach_n);
        return -1;
    }
    if (delete_email_fts(db, rowid) != 0 ||
        pav_db_prepare(db, "sync.delta_del", "DELETE FROM email WHERE rowid = ?", &stmt) != 0 ||
        pav_db_bind_int64(stmt, 1, rowid) != 0 || step_done(stmt) != 0) {
        free_ids(&attach, &attach_n);
        return -1;
    }
    ok = delta_note_blob(job, db, blob) == 0 && delta_destroy_blobs_from_saved(job, db, attach, attach_n) == 0;
    free_ids(&attach, &attach_n);
    return ok ? 0 : -1;
}

static int delta_destroy_job(void *ctx, sqlite3 *db) {
    DeltaDestroy *job = ctx;
    sqlite3_stmt *stmt = NULL;
    SyncId *ids = NULL;
    int n = 0;
    int i;
    int rc;

    if (remote_present(db, job->ra_id, &job->missing) != 0) {
        return -1;
    }
    if (job->missing) {
        return 0;
    }
    if (job->all) {
        if (pav_db_prepare(db, "sync.delta_email_all", "SELECT id FROM email WHERE ra_id = ?", &stmt) != 0 ||
            pav_db_bind_text(stmt, 1, job->ra_id) != 0) {
            return -1;
        }
        while ((rc = pav_db_step(stmt)) == SQLITE_ROW) {
            const char *id = (const char *)sqlite3_column_text(stmt, 0);

            if (id != NULL && append_id(&ids, &n, id) != 0) {
                free_ids(&ids, &n);
                return -1;
            }
        }
        if (rc != SQLITE_DONE) {
            free_ids(&ids, &n);
            return -1;
        }
        for (i = 0; i < n; i++) {
            if (delta_destroy_one_full(job, db, ids[i].id) != 0) {
                free_ids(&ids, &n);
                return -1;
            }
        }
        free_ids(&ids, &n);
        if (pav_db_prepare(db, "sync.delta_thread_all", "DELETE FROM thread WHERE ra_id = ?", &stmt) != 0 ||
            pav_db_bind_text(stmt, 1, job->ra_id) != 0 || step_done(stmt) != 0 ||
            pav_db_prepare(db, "sync.delta_state_del", "DELETE FROM sync_state WHERE ra_id = ? AND type = ?", &stmt) !=
                0 ||
            pav_db_bind_text(stmt, 1, job->ra_id) != 0 || pav_db_bind_text(stmt, 2, "Email") != 0 ||
            step_done(stmt) != 0 ||
            pav_db_prepare(db, "sync.delta_state_del", "DELETE FROM sync_state WHERE ra_id = ? AND type = ?", &stmt) !=
                0 ||
            pav_db_bind_text(stmt, 1, job->ra_id) != 0 || pav_db_bind_text(stmt, 2, "Thread") != 0 ||
            step_done(stmt) != 0) {
            return -1;
        }
        return 0;
    }
    for (i = 0; i < job->id_n; i++) {
        if (delta_destroy_one_full(job, db, job->ids[i].id) != 0) {
            return -1;
        }
    }
    return 0;
}

static void delta_release_files(const DeltaDestroy *job) {
    int i;

    for (i = 0; i < job->path_n; i++) {
        if (job->paths[i] != NULL && pav_exists(job->paths[i])) {
            (void)remove(job->paths[i]);
        }
    }
    for (i = 0; i < job->blob_n; i++) {
        (void)pav_blob_remove(job->ra_id, job->blobs[i].id);
    }
}

static int delta_merge_boxes(EmailWalk *w, const SyncId *boxes, int n) {
    return append_except(&w->touch, &w->touch_n, boxes, n, NULL, 0, NULL, 0);
}

static int drop_threads_job(void *ctx, sqlite3 *db) {
    const char *ra = ctx;
    sqlite3_stmt *stmt = NULL;
    int missing = 0;

    if (remote_present(db, ra, &missing) != 0) {
        return -1;
    }
    if (missing) {
        return 0;
    }
    if (pav_db_prepare(db, "sync.delta_thread_all", "DELETE FROM thread WHERE ra_id = ?", &stmt) != 0 ||
        pav_db_bind_text(stmt, 1, ra) != 0 || step_done(stmt) != 0 ||
        pav_db_prepare(db, "sync.delta_state_del", "DELETE FROM sync_state WHERE ra_id = ? AND type = ?", &stmt) != 0 ||
        pav_db_bind_text(stmt, 1, ra) != 0 || pav_db_bind_text(stmt, 2, "Thread") != 0 || step_done(stmt) != 0) {
        return -1;
    }
    return 0;
}

typedef struct DeltaThreadJob {
    char ra_id[256];
    const ThreadRow *rows;
    int row_n;
    const SyncId *missing;
    int missing_n;
    int gone;
} DeltaThreadJob;

static int delta_thread_job(void *ctx, sqlite3 *db) {
    DeltaThreadJob *job = ctx;
    sqlite3_stmt *stmt = NULL;
    int i;

    if (remote_present(db, job->ra_id, &job->gone) != 0) {
        return -1;
    }
    if (job->gone) {
        return 0;
    }
    for (i = 0; i < job->row_n; i++) {
        if (job->rows[i].id[0] == '\0') {
            continue;
        }
        if (pav_db_prepare(db, "sync.delta_thread_up",
                           "INSERT INTO thread (ra_id, id, email_ids) VALUES (?, ?, ?) "
                           "ON CONFLICT(ra_id, id) DO UPDATE SET email_ids = excluded.email_ids",
                           &stmt) != 0 ||
            pav_db_bind_text(stmt, 1, job->ra_id) != 0 || pav_db_bind_text(stmt, 2, job->rows[i].id) != 0 ||
            bind_empty_null(stmt, 3, job->rows[i].email_ids) != 0 || step_done(stmt) != 0) {
            return -1;
        }
    }
    for (i = 0; i < job->missing_n; i++) {
        if (pav_db_prepare(db, "sync.delta_thread_del", "DELETE FROM thread WHERE ra_id = ? AND id = ?", &stmt) != 0 ||
            pav_db_bind_text(stmt, 1, job->ra_id) != 0 || pav_db_bind_text(stmt, 2, job->missing[i].id) != 0 ||
            step_done(stmt) != 0) {
            return -1;
        }
    }
    return 0;
}

typedef struct DeltaStateJob {
    char ra_id[256];
    char email_state[128];
    char thread_state[128];
    int64_t now_ms;
    int missing;
} DeltaStateJob;

static int delta_put_state(sqlite3 *db, const char *ra, const char *type, const char *state, int64_t now) {
    sqlite3_stmt *stmt = NULL;

    if (state == NULL || state[0] == '\0') {
        return 0;
    }
    if (pav_db_prepare(db, "sync.delta_state_up",
                       "INSERT INTO sync_state (ra_id, type, state, updated) VALUES (?, ?, ?, ?) "
                       "ON CONFLICT(ra_id, type) DO UPDATE SET state = excluded.state, updated = excluded.updated",
                       &stmt) != 0 ||
        pav_db_bind_text(stmt, 1, ra) != 0 || pav_db_bind_text(stmt, 2, type) != 0 ||
        pav_db_bind_text(stmt, 3, state) != 0 || pav_db_bind_int64(stmt, 4, now) != 0 || step_done(stmt) != 0) {
        return -1;
    }
    return 0;
}

static int delta_state_job(void *ctx, sqlite3 *db) {
    DeltaStateJob *job = ctx;

    if (remote_present(db, job->ra_id, &job->missing) != 0) {
        return -1;
    }
    if (job->missing) {
        return 0;
    }
    if (delta_put_state(db, job->ra_id, "Email", job->email_state, job->now_ms) != 0 ||
        delta_put_state(db, job->ra_id, "Thread", job->thread_state, job->now_ms) != 0) {
        return -1;
    }
    return 0;
}

static void emit_id_change(const char *account, const SyncId *created, int created_n, const SyncId *updated,
                           int updated_n, const SyncId *destroyed, int destroyed_n) {
    json_t *obj = json_object();
    json_t *c = json_array();
    json_t *u = json_array();
    json_t *d = json_array();
    char *text;
    int i;

    if (created_n + updated_n + destroyed_n <= 0) {
        json_decref(obj);
        json_decref(c);
        json_decref(u);
        json_decref(d);
        return;
    }
    if (obj == NULL || c == NULL || u == NULL || d == NULL) {
        json_decref(obj);
        json_decref(c);
        json_decref(u);
        json_decref(d);
        return;
    }
    for (i = 0; i < created_n; i++) {
        if (json_array_append_new(c, json_string(created[i].id)) != 0) {
            json_decref(obj);
            json_decref(c);
            json_decref(u);
            json_decref(d);
            return;
        }
    }
    for (i = 0; i < updated_n; i++) {
        if (json_array_append_new(u, json_string(updated[i].id)) != 0) {
            json_decref(obj);
            json_decref(c);
            json_decref(u);
            json_decref(d);
            return;
        }
    }
    for (i = 0; i < destroyed_n; i++) {
        if (json_array_append_new(d, json_string(destroyed[i].id)) != 0) {
            json_decref(obj);
            json_decref(c);
            json_decref(u);
            json_decref(d);
            return;
        }
    }
    if (put_json(obj, "accountId", json_string(account != NULL ? account : "")) != 0) {
        json_decref(obj);
        json_decref(c);
        json_decref(u);
        json_decref(d);
        return;
    }
    if (put_json(obj, "created", c) != 0) {
        json_decref(obj);
        json_decref(u);
        json_decref(d);
        return;
    }
    if (put_json(obj, "updated", u) != 0) {
        json_decref(obj);
        json_decref(d);
        return;
    }
    if (put_json(obj, "destroyed", d) != 0) {
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

static int apply_thread_rows(PavSup *s, SupRemote *remote, const ThreadRow *rows, int row_n, const SyncId *missing,
                             int missing_n) {
    DeltaThreadJob job;

    if (row_n <= 0 && missing_n <= 0) {
        return 0;
    }
    memset(&job, 0, sizeof(job));
    if (!copy_fit(job.ra_id, sizeof(job.ra_id), remote->ra_id)) {
        email_fail(s, remote, "request rejected");
        return -1;
    }
    job.rows = rows;
    job.row_n = row_n;
    job.missing = missing;
    job.missing_n = missing_n;
    if (pav_db_call(delta_thread_job, &job) != 0 || job.gone) {
        email_fail(s, remote, job.gone ? "request rejected" : "cannot store messages");
        return -1;
    }
    return 0;
}

static const char *fetch_at(const EmailWalk *w, int index) {
    if (w == NULL || index < 0) {
        return NULL;
    }
    if (index < w->created_n) {
        return w->created[index].id;
    }
    index -= w->created_n;
    if (index < w->updated_n) {
        return w->updated[index].id;
    }
    return NULL;
}

static int fetch_total(const EmailWalk *w) {
    return w->created_n + w->updated_n;
}

static int copy_slice(const EmailWalk *w, int off, int count, SyncId **out) {
    int i;

    *out = calloc((size_t)count, sizeof(**out));
    if (*out == NULL) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        const char *id = fetch_at(w, off + i);

        if (id == NULL || !copy_fit((*out)[i].id, sizeof((*out)[i].id), id)) {
            free(*out);
            *out = NULL;
            return -1;
        }
    }
    return 0;
}

static void start_thread_page(PavSup *s, SupRemote *remote, SyncId *ids, int *off, int n, int step) {
    EmailWalk *w = remote->email;
    int count;
    char *body;

    if (w == NULL) {
        email_fail(s, remote, "request rejected");
        return;
    }
    if (email_cancelled(s)) {
        finish_email_cancel(s, remote);
        return;
    }
    if (*off >= n) {
        return;
    }
    count = n - *off;
    if (count > w->changes_max) {
        count = w->changes_max;
    }
    w->step = step;
    body = delta_ids_body(remote->jmap_id, "Thread/get", "tg0", ids + *off, count, 0);
    *off += count;
    (void)delta_submit(s, remote, EMAIL_MODE_THREAD_GET, body);
}

static void finish_delta(PavSup *s, SupRemote *remote) {
    EmailWalk *w = remote != NULL ? remote->email : NULL;
    DeltaStateJob job;
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
    if (!copy_fit(job.email_state, sizeof(job.email_state), w->email_new[0] != '\0' ? w->email_new : w->email_since) ||
        job.email_state[0] == '\0') {
        email_fail(s, remote, "request rejected");
        return;
    }
    if (w->thread_new[0] != '\0') {
        copy_fit(job.thread_state, sizeof(job.thread_state), w->thread_new);
    } else if (w->thread_since[0] != '\0') {
        copy_fit(job.thread_state, sizeof(job.thread_state), w->thread_since);
    }
    now = pav_now_ms();
    job.now_ms = now > 0 ? now : 0;
    if (pav_db_call(delta_state_job, &job) != 0 || job.missing) {
        email_fail(s, remote, job.missing ? "request rejected" : "cannot store messages");
        return;
    }
    clear_email_walk(remote);
    begin_remote(s, remote->next);
}

static void delta_continue(PavSup *s, SupRemote *remote) {
    EmailWalk *w = remote->email;

    if (w == NULL) {
        email_fail(s, remote, "request rejected");
        return;
    }
    if (email_cancelled(s)) {
        finish_email_cancel(s, remote);
        return;
    }
    if (w->thread_fix && !w->email_more && !w->thread_fixed) {
        start_fix(s, remote);
        return;
    }
    if (w->email_more || (w->thread_more && !w->skip_thread_changes && !w->thread_fix)) {
        if (w->email_new[0] != '\0' && !copy_fit(w->email_since, sizeof(w->email_since), w->email_new)) {
            email_fail(s, remote, "request rejected");
            return;
        }
        if (!w->thread_fix && w->thread_new[0] != '\0' &&
            !copy_fit(w->thread_since, sizeof(w->thread_since), w->thread_new)) {
            email_fail(s, remote, "request rejected");
            return;
        }
        w->email_more = 0;
        w->thread_more = 0;
        clear_delta_page(w);
        start_changes(s, remote);
        return;
    }
    finish_delta(s, remote);
}

/* Drop mailboxes this cycle already fetched, and the ids about to be requested. */
static void drop_touch(EmailWalk *w, const SupRemote *remote, const SyncId *sent, int sent_n) {
    int i;
    int keep = 0;

    for (i = 0; i < w->touch_n; i++) {
        if (mbx_was_seen(remote, w->touch[i].id) || list_has(sent, sent_n, w->touch[i].id)) {
            continue;
        }
        if (keep != i) {
            w->touch[keep] = w->touch[i];
        }
        keep++;
    }
    w->touch_n = keep;
}

static void start_boxes(PavSup *s, SupRemote *remote) {
    EmailWalk *w = remote->email;
    SyncId *ids = NULL;
    int n = 0;
    int i;
    int cap;
    char *body;

    if (w == NULL) {
        email_fail(s, remote, "request rejected");
        return;
    }
    if (email_cancelled(s)) {
        finish_email_cancel(s, remote);
        return;
    }
    cap = w->changes_max > 0 ? w->changes_max : 1;
    for (i = 0; i < w->touch_n && n < cap; i++) {
        if (mbx_was_seen(remote, w->touch[i].id)) {
            continue;
        }
        if (append_id(&ids, &n, w->touch[i].id) != 0) {
            free_ids(&ids, &n);
            email_fail(s, remote, "offline");
            return;
        }
    }
    drop_touch(w, remote, ids, n);
    if (n == 0) {
        free_ids(&ids, &n);
        delta_continue(s, remote);
        return;
    }
    w->step = DELTA_BOXES;
    body = delta_ids_body(remote->jmap_id, "Mailbox/get", "mg0", ids, n, 0);
    free_ids(&ids, &n);
    (void)delta_submit(s, remote, EMAIL_MODE_MAILBOX, body);
}

static void delete_thread_ids(PavSup *s, SupRemote *remote) {
    EmailWalk *w = remote->email;

    if (w == NULL) {
        email_fail(s, remote, "request rejected");
        return;
    }
    if (w->th_destroyed_n > 0) {
        if (apply_thread_rows(s, remote, NULL, 0, w->th_destroyed, w->th_destroyed_n) != 0) {
            return;
        }
        free_ids(&w->th_destroyed, &w->th_destroyed_n);
    }
    start_boxes(s, remote);
}

static void after_threads(PavSup *s, SupRemote *remote) {
    EmailWalk *w = remote->email;

    if (w == NULL) {
        email_fail(s, remote, "request rejected");
        return;
    }
    if (w->th_get_off < w->th_get_n) {
        start_thread_page(s, remote, w->th_get, &w->th_get_off, w->th_get_n, w->step == DELTA_FIX ? DELTA_FIX
                                                                                                  : DELTA_THREADS);
        return;
    }
    delete_thread_ids(s, remote);
}

static void start_fix(PavSup *s, SupRemote *remote) {
    EmailWalk *w = remote->email;

    if (w == NULL) {
        email_fail(s, remote, "request rejected");
        return;
    }
    w->thread_fixed = 1;
    w->step = DELTA_FIX;
    free_ids(&w->th_get, &w->th_get_n);
    w->th_get_off = 0;
    if (load_id_list(remote->ra_id,
                     "SELECT DISTINCT thread_id FROM email WHERE ra_id = ? AND thread_id IS NOT NULL "
                     "AND thread_id != '' ORDER BY thread_id",
                     &w->th_get, &w->th_get_n) != 0) {
        email_fail(s, remote, "cannot store messages");
        return;
    }
    if (w->th_get_n == 0) {
        start_boxes(s, remote);
        return;
    }
    start_thread_page(s, remote, w->th_get, &w->th_get_off, w->th_get_n, DELTA_FIX);
}

static void after_destroy(PavSup *s, SupRemote *remote) {
    EmailWalk *w = remote->email;

    if (w == NULL) {
        email_fail(s, remote, "request rejected");
        return;
    }
    if (w->thread_fix) {
        if (!w->email_more && !w->thread_fixed) {
            start_fix(s, remote);
            return;
        }
        start_boxes(s, remote);
        return;
    }
    if (w->th_get_n > 0) {
        w->step = DELTA_THREADS;
        w->th_get_off = 0;
        start_thread_page(s, remote, w->th_get, &w->th_get_off, w->th_get_n, DELTA_THREADS);
        return;
    }
    delete_thread_ids(s, remote);
}

static void run_destroy(PavSup *s, SupRemote *remote) {
    EmailWalk *w = remote->email;
    DeltaDestroy job;

    if (w == NULL) {
        email_fail(s, remote, "request rejected");
        return;
    }
    if (w->destroyed_n <= 0) {
        after_destroy(s, remote);
        return;
    }
    memset(&job, 0, sizeof(job));
    if (!copy_fit(job.ra_id, sizeof(job.ra_id), remote->ra_id)) {
        email_fail(s, remote, "request rejected");
        return;
    }
    job.ids = w->destroyed;
    job.id_n = w->destroyed_n;
    if (pav_db_call(delta_destroy_job, &job) != 0 || job.missing) {
        delta_free_paths(&job);
        free_ids(&job.boxes, &job.box_n);
        free_ids(&job.blobs, &job.blob_n);
        email_fail(s, remote, job.missing ? "request rejected" : "cannot store messages");
        return;
    }
    delta_release_files(&job);
    delta_free_paths(&job);
    free_ids(&job.blobs, &job.blob_n);
    if (delta_merge_boxes(w, job.boxes, job.box_n) != 0) {
        free_ids(&job.boxes, &job.box_n);
        email_fail(s, remote, "offline");
        return;
    }
    free_ids(&job.boxes, &job.box_n);
    emit_id_change(s->account_id, NULL, 0, NULL, 0, w->destroyed, w->destroyed_n);
    free_ids(&w->destroyed, &w->destroyed_n);
    after_destroy(s, remote);
}

static void after_fetch(PavSup *s, SupRemote *remote) {
    run_destroy(s, remote);
}

static int note_fetched(EmailWalk *w, const EmailPage *page) {
    int i;
    int b;

    if (page == NULL) {
        return 0;
    }
    for (i = 0; i < page->row_n; i++) {
        const EmailRow *row = &page->rows[i];

        for (b = 0; b < row->box_n; b++) {
            if (!list_has(w->touch, w->touch_n, row->boxes[b].id) &&
                append_id(&w->touch, &w->touch_n, row->boxes[b].id) != 0) {
                return -1;
            }
        }
        if (list_has(w->created, w->created_n, row->id) && row->thread_id[0] != '\0' &&
            !list_has(w->th_get, w->th_get_n, row->thread_id) &&
            !list_has(w->th_destroyed, w->th_destroyed_n, row->thread_id) &&
            append_id(&w->th_get, &w->th_get_n, row->thread_id) != 0) {
            return -1;
        }
    }
    return 0;
}

static void on_fetch(PavSup *s, SupRemote *remote, EmailResult *r) {
    EmailWalk *w = remote->email;
    EmailPage *page = r->page_n > 0 ? &r->pages[0] : NULL;
    EmailPageJob job;

    if (w == NULL || page == NULL) {
        email_fail(s, remote, "request rejected");
        return;
    }
    if (page->row_n > 0) {
        memset(&job, 0, sizeof(job));
        if (!copy_fit(job.ra_id, sizeof(job.ra_id), remote->ra_id)) {
            email_fail(s, remote, "request rejected");
            return;
        }
        job.page = page;
        job.skip_thread = 1;
        if (pav_db_call(write_email_page_job, &job) != 0 || job.missing) {
            email_fail(s, remote, job.missing ? "request rejected" : "cannot store messages");
            return;
        }
        emit_messages(s->account_id, page);
        if (note_fetched(w, page) != 0) {
            email_fail(s, remote, "offline");
            return;
        }
    }
    if (w->fetch_off < fetch_total(w)) {
        start_fetch(s, remote);
        return;
    }
    after_fetch(s, remote);
}

static void start_fetch(PavSup *s, SupRemote *remote) {
    EmailWalk *w = remote->email;
    SyncId *slice = NULL;
    int total;
    int count;
    char *body;

    if (w == NULL) {
        email_fail(s, remote, "request rejected");
        return;
    }
    if (email_cancelled(s)) {
        finish_email_cancel(s, remote);
        return;
    }
    total = fetch_total(w);
    if (w->fetch_off >= total) {
        after_fetch(s, remote);
        return;
    }
    count = total - w->fetch_off;
    if (count > w->changes_max) {
        count = w->changes_max;
    }
    if (copy_slice(w, w->fetch_off, count, &slice) != 0) {
        email_fail(s, remote, "offline");
        return;
    }
    w->step = DELTA_FETCH;
    body = delta_ids_body(remote->jmap_id, "Email/get", "eg0", slice, count, 1);
    free(slice);
    w->fetch_off += count;
    (void)delta_submit(s, remote, EMAIL_MODE_GETIDS, body);
}

static void drop_all_and_walk(PavSup *s, SupRemote *remote) {
    DeltaDestroy job;

    pav_log("sync", PAV_WARN, 1, "cannot calculate Email changes for %s", s->account_id);
    memset(&job, 0, sizeof(job));
    if (!copy_fit(job.ra_id, sizeof(job.ra_id), remote->ra_id)) {
        email_fail(s, remote, "request rejected");
        return;
    }
    job.all = 1;
    if (pav_db_call(delta_destroy_job, &job) != 0) {
        delta_free_paths(&job);
        free_ids(&job.boxes, &job.box_n);
        free_ids(&job.blobs, &job.blob_n);
        email_fail(s, remote, "cannot store messages");
        return;
    }
    delta_release_files(&job);
    delta_free_paths(&job);
    free_ids(&job.boxes, &job.box_n);
    free_ids(&job.blobs, &job.blob_n);
    s->email_delta = 0;
    s->email_done = 0;
    s->email_total = 0;
    clear_email_walk(remote);
    begin_email(s, remote);
}

static void on_changes(PavSup *s, SupRemote *remote, EmailResult *r) {
    EmailWalk *w = remote->email;

    if (w == NULL) {
        email_fail(s, remote, "request rejected");
        return;
    }
    if (r->email_resync) {
        drop_all_and_walk(s, remote);
        return;
    }
    if (r->email_state[0] != '\0' && !copy_fit(w->email_new, sizeof(w->email_new), r->email_state)) {
        email_fail(s, remote, "request rejected");
        return;
    }
    w->email_more = r->email_more;
    if (w->email_more && w->email_new[0] != '\0' && strcmp(w->email_new, w->email_since) == 0) {
        email_fail(s, remote, "too many changes");
        return;
    }
    if (r->thread_resync) {
        pav_log("sync", PAV_WARN, 1, "cannot calculate Thread changes for %s", s->account_id);
        if (pav_db_call(drop_threads_job, remote->ra_id) != 0) {
            email_fail(s, remote, "cannot store messages");
            return;
        }
        w->thread_fix = 1;
        w->thread_more = 0;
        w->thread_since[0] = '\0';
        w->thread_new[0] = '\0';
    } else {
        if (r->thread_state[0] != '\0' && !copy_fit(w->thread_new, sizeof(w->thread_new), r->thread_state)) {
            email_fail(s, remote, "request rejected");
            return;
        }
        w->thread_more = r->thread_more;
        if (w->thread_more && w->thread_new[0] != '\0' && w->thread_since[0] != '\0' &&
            strcmp(w->thread_new, w->thread_since) == 0) {
            email_fail(s, remote, "too many changes");
            return;
        }
        if (append_except(&w->th_get, &w->th_get_n, r->th_get, r->th_get_n, r->th_destroyed, r->th_destroyed_n, NULL,
                          0) != 0 ||
            append_except(&w->th_destroyed, &w->th_destroyed_n, r->th_destroyed, r->th_destroyed_n, NULL, 0, NULL, 0) !=
                0) {
            email_fail(s, remote, "offline");
            return;
        }
    }
    if (append_except(&w->created, &w->created_n, r->created, r->created_n, r->destroyed, r->destroyed_n, NULL, 0) !=
            0 ||
        append_except(&w->updated, &w->updated_n, r->updated, r->updated_n, r->destroyed, r->destroyed_n, w->created,
                      w->created_n) != 0 ||
        append_except(&w->destroyed, &w->destroyed_n, r->destroyed, r->destroyed_n, NULL, 0, NULL, 0) != 0) {
        email_fail(s, remote, "offline");
        return;
    }
    w->fetch_off = 0;
    start_fetch(s, remote);
}

static void on_threads(PavSup *s, SupRemote *remote, EmailResult *r) {
    EmailWalk *w = remote->email;

    if (w == NULL) {
        email_fail(s, remote, "request rejected");
        return;
    }
    if (apply_thread_rows(s, remote, r->threads, r->thread_n, r->thread_missing, r->thread_missing_n) != 0) {
        return;
    }
    if (w->step == DELTA_BOOT) {
        if (r->thread_state[0] != '\0') {
            copy_fit(w->thread_since, sizeof(w->thread_since), r->thread_state);
        }
        if (w->local_off < w->local_thread_n) {
            start_thread_page(s, remote, w->local_threads, &w->local_off, w->local_thread_n, DELTA_BOOT);
            return;
        }
        w->skip_thread_changes = 1;
        start_changes(s, remote);
        return;
    }
    if (r->thread_state[0] != '\0') {
        copy_fit(w->thread_new, sizeof(w->thread_new), r->thread_state);
    }
    after_threads(s, remote);
}

static void on_boxes(PavSup *s, SupRemote *remote, EmailResult *r) {
    EmailWalk *w = remote->email;
    TypeWrite write;
    int64_t now;

    if (w == NULL) {
        email_fail(s, remote, "request rejected");
        return;
    }
    if (r->mailbox_n > 0) {
        memset(&write, 0, sizeof(write));
        if (!copy_fit(write.ra_id, sizeof(write.ra_id), remote->ra_id)) {
            email_fail(s, remote, "request rejected");
            return;
        }
        write.mailboxes = r->mailboxes;
        write.mailbox_n = r->mailbox_n;
        now = pav_now_ms();
        write.now_ms = now > 0 ? now : 0;
        if (pav_db_call(write_type_job, &write) != 0 || write.missing) {
            email_fail(s, remote, write.missing ? "request rejected" : "cannot store messages");
            return;
        }
        note_mbx_seen(remote, r->mailboxes, r->mailbox_n);
        emit_mailboxes(s->account_id);
    }
    start_boxes(s, remote);
}

static void start_changes(PavSup *s, SupRemote *remote) {
    EmailWalk *w = remote->email;
    char *body;

    if (w == NULL) {
        email_fail(s, remote, "request rejected");
        return;
    }
    if (email_cancelled(s)) {
        finish_email_cancel(s, remote);
        return;
    }
    if (w->changes_pages >= DELTA_PAGE_CAP) {
        email_fail(s, remote, "too many changes");
        return;
    }
    w->changes_pages++;
    w->step = DELTA_CHANGES;
    body = delta_changes_body(remote->jmap_id, w);
    (void)delta_submit(s, remote, EMAIL_MODE_CHANGES, body);
}

static void email_delta_apply(PavSup *s, SupRemote *remote, EmailResult *r) {
    EmailWalk *w = remote->email;

    if (w == NULL || w->kind != 1) {
        email_fail(s, remote, "request rejected");
        return;
    }
    if (email_cancelled(s)) {
        finish_email_cancel(s, remote);
        return;
    }
    if (r->email_mode == EMAIL_MODE_CHANGES) {
        on_changes(s, remote, r);
    } else if (r->email_mode == EMAIL_MODE_GETIDS) {
        on_fetch(s, remote, r);
    } else if (r->email_mode == EMAIL_MODE_THREAD_GET) {
        on_threads(s, remote, r);
    } else if (r->email_mode == EMAIL_MODE_MAILBOX) {
        on_boxes(s, remote, r);
    } else {
        email_fail(s, remote, "request rejected");
    }
}

static void begin_email_delta(PavSup *s, SupRemote *remote) {
    EmailWalk *w;

    if (remote == NULL) {
        finish_sync_ok(s);
        return;
    }
    if (email_cancelled(s)) {
        finish_email_cancel(s, remote);
        return;
    }
    w = calloc(1, sizeof(*w));
    if (w == NULL) {
        email_fail(s, remote, "offline");
        return;
    }
    remote->email = w;
    w->kind = 1;
    w->changes_max = session_get_limit(s->account_id);
    if (w->changes_max < 1) {
        w->changes_max = 1;
    }
    if (load_named_state(remote->ra_id, "Email", w->email_since, sizeof(w->email_since)) != 0 ||
        w->email_since[0] == '\0') {
        email_fail(s, remote, "cannot store messages");
        return;
    }
    if (load_named_state(remote->ra_id, "Thread", w->thread_since, sizeof(w->thread_since)) != 0) {
        email_fail(s, remote, "cannot store messages");
        return;
    }
    s->email_delta = 1;
    if (s->activity_id > 0) {
        pav_activity_progress(s->activity_id, 0, -1, NULL, "Changes");
    }
    if (w->thread_since[0] == '\0') {
        if (load_id_list(remote->ra_id, "SELECT id FROM thread WHERE ra_id = ? ORDER BY id", &w->local_threads,
                         &w->local_thread_n) != 0) {
            email_fail(s, remote, "cannot store messages");
            return;
        }
        if (w->local_thread_n > 0) {
            w->local_off = 0;
            start_thread_page(s, remote, w->local_threads, &w->local_off, w->local_thread_n, DELTA_BOOT);
            return;
        }
        w->skip_thread_changes = 1;
    }
    start_changes(s, remote);
}

#define _POSIX_C_SOURCE 200809L

#include "pav_net.h"
#include "pav_auth.h"
#include "pav_platform.h"
#include "pav_secret.h"
#include "pavillion.h"

#include <curl/curl.h>

#include <jansson.h>

#include <errno.h>
#include <pthread.h>
#include <sys/select.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#define PAV_NET_CONNECT_TIMEOUT_MS 10000
#define PAV_NET_REQUEST_TIMEOUT_MS 60000
#define PAV_NET_LOW_SPEED_LIMIT 1024
#define PAV_NET_LOW_SPEED_TIME 30
#define PAV_NET_BACKOFF_BASE_MS 1000
#define PAV_NET_BACKOFF_MAX_MS (5 * 60 * 1000)
#define PAV_NET_JITTER_PCT 20
#define PAV_NET_RETRY_MAX 8
#define PAV_NET_ACCOUNT_CAP_MAX 16
#define PAV_NET_SERVER_MAX 64
#define PAV_NET_BODY_MAX (2u * 1024u * 1024u)

typedef struct {
    FILE *dl_file;
    size_t *body_len;
} DlCtx;

typedef struct {
    json_t *headers;
} HeaderCtx;

typedef struct NetReqItem {
    PavReq *req;
    int retry_count;
    int64_t ready_at_ms;
    int64_t start_ms;
    CURL *eh;
    FILE *up_file;
    FILE *dl_file;
    DlCtx dlctx;
    HeaderCtx hctx;
    struct curl_slist *chunks;
    int in_flight;
    struct NetReqItem *next;
} NetReqItem;

static size_t header_callback(char *buffer, size_t size, size_t nitems,
                              void *userdata) {
    size_t len = size * nitems;
    HeaderCtx *hctx = (HeaderCtx *)userdata;

    if (len > 0 && buffer[len - 1] == '\n') {
        len--;
        if (len > 0 && buffer[len - 1] == '\r') {
            len--;
        }
    }
    if (len > 0) {
        char *colon = memchr(buffer, ':', len);
        if (colon != NULL && hctx->headers != NULL &&
            json_is_object(hctx->headers)) {
            size_t key_len = (size_t)(colon - buffer);
            char key[256];
            char *val = colon + 1;
            size_t val_len = len - key_len - 1;

            if (key_len > 0 && key_len < sizeof(key)) {
                memcpy(key, buffer, key_len);
                key[key_len] = '\0';
                char *val_copy = NULL;
                while (val_len > 0 && *val == ' ') {
                    val++;
                    val_len--;
                }
                while (val_len > 0 && (val[val_len - 1] == ' ' ||
                                       val[val_len - 1] == '\t')) {
                    val_len--;
                }
                if (val_len > 0) {
                    val_copy = malloc(val_len + 1);
                    if (val_copy != NULL) {
                        memcpy(val_copy, val, val_len);
                        val_copy[val_len] = '\0';
                    }
                }
                if (val_copy != NULL) {
                    json_object_set_new(hctx->headers, key, json_string(val_copy));
                    free(val_copy);
                }
            }
        }
    }
    return size * nitems;
}

typedef struct NetAccountEntry {
    char id[128];
    int in_flight;
} NetAccountEntry;

typedef struct PavNetServer {
    char key[512];
    CURLM *multi;
    void *thread;
    pthread_mutex_t mu;
    pthread_cond_t cv;
    NetReqItem *q_head;
    NetReqItem *q_tail;
    int q_count;
    NetReqItem *active_head;
    int in_flight;
    int running;
    int stop;
    int account_count;
    NetAccountEntry accounts[PAV_NET_ACCOUNT_CAP_MAX];
    int active_accounts;
} PavNetServer;

static PavNetServer *g_servers[PAV_NET_SERVER_MAX];
static int g_server_count;
static pthread_mutex_t g_reg_mu = PTHREAD_MUTEX_INITIALIZER;
static int g_net_started;

static int g_overall_cap;
static int g_overall_in_flight;
static pthread_mutex_t g_overall_mu = PTHREAD_MUTEX_INITIALIZER;

static int endpoint_key(const char *url, char *out, size_t out_len) {
    const char *p = url;
    if (strncmp(p, "https://", 8) == 0) p += 8;
    else if (strncmp(p, "http://", 7) == 0) p += 7;
    const char *slash = strchr(p, '/');
    size_t host_len = slash ? (size_t)(slash - p) : strlen(p);
    if (host_len >= out_len) return -1;
    memcpy(out, p, host_len);
    out[host_len] = '\0';
    if (strchr(out, ':') == NULL) {
        const char *def = (strncmp(url, "https://", 8) == 0) ? ":443" : ":80";
        size_t n = strlen(out);
        size_t dlen = strlen(def);
        if (n + dlen + 1 >= out_len) return -1;
        memcpy(out + n, def, dlen + 1);
    }
    return 0;
}

static int account_index(PavNetServer *s, const char *id) {
    if (id == NULL || id[0] == '\0') return -1;
    int i;
    for (i = 0; i < s->account_count; i++) {
        if (strcmp(s->accounts[i].id, id) == 0) return i;
    }
    if (s->account_count >= PAV_NET_ACCOUNT_CAP_MAX) return -1;
    strncpy(s->accounts[s->account_count].id, id,
            sizeof(s->accounts[0].id) - 1);
    s->accounts[s->account_count].id[sizeof(s->accounts[0].id) - 1] = '\0';
    s->accounts[s->account_count].in_flight = 0;
    return s->account_count++;
}

static void log_transfer(const char *method, const char *url, int status,
                         int64_t bytes, int64_t duration_ms) {
    const char *host_start = url;
    if (strncmp(url, "https://", 8) == 0) host_start += 8;
    else if (strncmp(url, "http://", 7) == 0) host_start += 7;
    const char *slash = strchr(host_start, '/');
    size_t host_len = slash ? (size_t)(slash - host_start) : strlen(host_start);
    const char *colon = memchr(host_start, ':', host_len);
    if (colon) host_len = (size_t)(colon - host_start);
    char host_only[256];
    if (host_len >= sizeof(host_only)) host_len = sizeof(host_only) - 1;
    memcpy(host_only, host_start, host_len);
    host_only[host_len] = '\0';

    const char *path = slash ? slash : "/";
    const char *qmark = strchr(path, '?');
    char path_only[1024];
    if (qmark) {
        size_t plen = (size_t)(qmark - path);
        if (plen >= sizeof(path_only)) plen = sizeof(path_only) - 1;
        memcpy(path_only, path, plen);
        path_only[plen] = '\0';
    } else {
        strncpy(path_only, path, sizeof(path_only) - 1);
        path_only[sizeof(path_only) - 1] = '\0';
    }
    pav_log("http", PAV_DEBUG, 6,
            "%s %s%s -> %d (%lld bytes, %lld ms)",
            method, host_only, path_only, status,
            (long long)bytes, (long long)duration_ms);
}

static int apply_auth_header(PavReq *req) {
    char errbuf[512];
    PavAccount acct;
    const PavAuth *auth;
    char *secret_val = NULL;
    PavSecret secret;
    int rc;

    if (req->keyring_user == NULL || req->username == NULL ||
        req->auth_method == NULL) {
        return 0;
    }
    acct.username = req->username;
    acct.auth = req->auth_method;
    auth = pav_auth_for(&acct, errbuf, sizeof(errbuf));
    if (auth == NULL) {
        pav_log("http", PAV_WARN, 0,
                "auth setup failed: %s", errbuf);
        return -1;
    }
    rc = pav_secret_get("pavillion", req->keyring_user, &secret_val);
    if (rc == PAV_SECRET_LOCKED) {
        pav_log("http", PAV_WARN, 0, "keyring is locked");
        return -1;
    }
    if (rc != 0) {
        pav_log("http", PAV_ERROR, 0, "cannot read secret");
        return -1;
    }
    secret.val = secret_val;
    secret.len = secret_val ? strlen(secret_val) : 0;
    rc = auth->apply(&acct, req, &secret);
    if (secret_val) {
        memset(secret_val, 0, secret.len);
        pav_secret_free(secret_val);
    }
    return rc;
}

static size_t write_to_memory(void *ptr, size_t size, size_t nmemb, void *userp) {
    size_t total = size * nmemb;
    PavReq *req = userp;
    if (req->body_out_len + total > PAV_NET_BODY_MAX) return 0;
    char *nb = realloc(req->body_out, req->body_out_len + total + 1);
    if (!nb) return 0;
    memcpy(nb + req->body_out_len, ptr, total);
    req->body_out = nb;
    req->body_out_len += total;
    req->body_out[req->body_out_len] = '\0';
    return total;
}

static size_t write_to_file(void *ptr, size_t size, size_t nmemb, void *userp) {
    size_t total = size * nmemb;
    DlCtx *ctx = (DlCtx *)userp;
    if (ctx == NULL || ctx->dl_file == NULL) return 0;
    size_t written = fwrite(ptr, 1, total, ctx->dl_file);
    if (ctx->body_len) *ctx->body_len += written;
    return written;
}

static int xfer_info_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                              curl_off_t ultotal, curl_off_t ulnow) {
    (void)dltotal;
    (void)dlnow;
    (void)ultotal;
    (void)ulnow;
    PavReq *req = clientp;
    if (req->cancelled) return 1;
    return 0;
}

static int64_t backoff_ms(int retry_count) {
    int64_t base = PAV_NET_BACKOFF_BASE_MS;
    int i;
    for (i = 0; i < retry_count; i++) {
        base *= 2;
        if (base > PAV_NET_BACKOFF_MAX_MS) {
            base = PAV_NET_BACKOFF_MAX_MS;
            break;
        }
    }
    int64_t jitter = (base * PAV_NET_JITTER_PCT) / 100;
    int64_t rand_ms = (int64_t)(pav_now_ms() % 1000);
    int64_t delta = (jitter > 0) ? (rand_ms % (2 * jitter + 1)) - jitter : 0;
    int64_t result = base + delta;
    if (result < PAV_NET_BACKOFF_BASE_MS) result = PAV_NET_BACKOFF_BASE_MS;
    return result;
}

static int should_retry(CURLcode res, int http_status, int retry_count) {
    if (res == CURLE_SSL_CACERT || res == CURLE_SSL_CACERT_BADFILE ||
        res == CURLE_PEER_FAILED_VERIFICATION) return 0;
    if (res == CURLE_WRITE_ERROR || res == CURLE_READ_ERROR) return 0;
    if (res != CURLE_OK) return (retry_count < PAV_NET_RETRY_MAX - 1) ? 1 : 0;
    if (http_status == 401) return 0;
    if (http_status == 429 || http_status == 503) return (retry_count < PAV_NET_RETRY_MAX - 1) ? 1 : 0;
    if (http_status >= 500) return (retry_count < PAV_NET_RETRY_MAX - 1) ? 1 : 0;
    return 0;
}

static void add_active(PavNetServer *s, NetReqItem *item) {
    item->next = s->active_head;
    s->active_head = item;
}

static void remove_active(PavNetServer *s, NetReqItem *item) {
    NetReqItem *cur = s->active_head;
    NetReqItem *prev = NULL;
    while (cur) {
        if (cur == item) {
            if (prev) prev->next = cur->next;
            else s->active_head = cur->next;
            item->next = NULL;
            return;
        }
        prev = cur;
        cur = cur->next;
    }
}

static int setup_request(PavNetServer *s, NetReqItem *item) {
    PavReq *req = item->req;
    CURL *eh = NULL;
    struct curl_slist *chunks = NULL;
    FILE *up_file = NULL;
    FILE *dl_file = NULL;

    req->body_out = NULL;
    req->body_out_len = 0;
    req->status_code = 0;

    eh = curl_easy_init();
    if (!eh) return -1;

    curl_easy_setopt(eh, CURLOPT_URL, req->url);
    curl_easy_setopt(eh, CURLOPT_CUSTOMREQUEST, req->method);
    curl_easy_setopt(eh, CURLOPT_CONNECTTIMEOUT_MS, PAV_NET_CONNECT_TIMEOUT_MS);
    curl_easy_setopt(eh, CURLOPT_TIMEOUT_MS, PAV_NET_REQUEST_TIMEOUT_MS);
    curl_easy_setopt(eh, CURLOPT_LOW_SPEED_LIMIT, PAV_NET_LOW_SPEED_LIMIT);
    curl_easy_setopt(eh, CURLOPT_LOW_SPEED_TIME, PAV_NET_LOW_SPEED_TIME);
    curl_easy_setopt(eh, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    curl_easy_setopt(eh, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(eh, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(eh, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(eh, CURLOPT_XFERINFOFUNCTION, xfer_info_callback);
    curl_easy_setopt(eh, CURLOPT_XFERINFODATA, req);
    curl_easy_setopt(eh, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(eh, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(eh, CURLOPT_HEADERDATA, &item->hctx);

    if (req->ca_file && req->ca_file[0] != '\0') {
        curl_easy_setopt(eh, CURLOPT_CAINFO, req->ca_file);
    }

    if (apply_auth_header(req) != 0) {
        req->status_code = 0;
        curl_easy_cleanup(eh);
        return -2;
    }

    if (req->headers && json_is_object(req->headers)) {
        json_t *hdr = req->headers;
        const char *key;
        json_t *val;
        json_object_foreach(hdr, key, val) {
            if (json_is_string(val)) {
                char line[512];
                int n = snprintf(line, sizeof(line), "%s: %s",
                                 key, json_string_value(val));
                if (n > 0 && n < (int)sizeof(line)) {
                    chunks = curl_slist_append(chunks, line);
                }
            }
        }
    }
    curl_easy_setopt(eh, CURLOPT_HTTPHEADER, chunks);

    if (req->body && req->body_len > 0) {
        curl_easy_setopt(eh, CURLOPT_POSTFIELDS, req->body);
        curl_easy_setopt(eh, CURLOPT_POSTFIELDSIZE, (long)req->body_len);
    }

    if (req->upload_path) {
        up_file = fopen(req->upload_path, "rb");
        if (up_file) {
            curl_easy_setopt(eh, CURLOPT_READFUNCTION, fread);
            curl_easy_setopt(eh, CURLOPT_READDATA, up_file);
            fseek(up_file, 0, SEEK_END);
            long flen = ftell(up_file);
            fseek(up_file, 0, SEEK_SET);
            curl_easy_setopt(eh, CURLOPT_INFILESIZE, flen);
            curl_easy_setopt(eh, CURLOPT_UPLOAD, 1L);
        }
    }

    if (req->download_path) {
        dl_file = fopen(req->download_path, "wb");
        if (dl_file) {
            item->dlctx.dl_file = dl_file;
            item->dlctx.body_len = &req->body_out_len;
            curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, write_to_file);
            curl_easy_setopt(eh, CURLOPT_WRITEDATA, &item->dlctx);
        }
    } else {
        curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, write_to_memory);
        curl_easy_setopt(eh, CURLOPT_WRITEDATA, req);
    }

    if (req->resp_headers == NULL) {
        req->resp_headers = json_object();
    }
    item->hctx.headers = req->resp_headers;

    item->eh = eh;
    item->up_file = up_file;
    item->dl_file = dl_file;
    item->chunks = chunks;
    item->start_ms = pav_now_ms();

    CURLMcode mc = curl_multi_add_handle(s->multi, eh);
    if (mc != CURLM_OK) {
        item->eh = NULL;
        item->chunks = NULL;
        if (up_file) fclose(up_file);
        if (dl_file) fclose(dl_file);
        curl_easy_cleanup(eh);
        return -1;
    }

    add_active(s, item);
    item->in_flight = 1;
    return 0;
}

static void release_item_resources(NetReqItem *item) {
    if (item->eh) {
        curl_easy_cleanup(item->eh);
        item->eh = NULL;
    }
    if (item->chunks) {
        curl_slist_free_all(item->chunks);
        item->chunks = NULL;
    }
    if (item->up_file) {
        fclose(item->up_file);
        item->up_file = NULL;
    }
    if (item->dl_file) {
        fclose(item->dl_file);
        item->dl_file = NULL;
    }
}

static int complete_request(PavNetServer *s, NetReqItem *item, CURLcode res) {
    PavReq *req = item->req;
    int64_t end_ms = pav_now_ms();
    int64_t duration_ms = (end_ms - item->start_ms > 0) ? (end_ms - item->start_ms) : 0;
    int http_status = 0;

    curl_multi_remove_handle(s->multi, item->eh);

    if (res == CURLE_OK) {
        long http = 0;
        curl_easy_getinfo(item->eh, CURLINFO_RESPONSE_CODE, &http);
        http_status = (int)http;
    }
    req->status_code = http_status;

    if (req->resp_headers && (http_status == 429 || http_status == 503)) {
        long retry_after_val = 0;
        curl_easy_getinfo(item->eh, CURLINFO_RETRY_AFTER, &retry_after_val);
        if (retry_after_val > 0) {
            json_object_set_new(req->resp_headers, "Retry-After",
                                json_integer((json_int_t)retry_after_val));
        }
    }

    log_transfer(req->method, req->url, req->status_code,
                 (int64_t)req->body_out_len, duration_ms);

    int retry = 0;
    int64_t retry_after_ms = 0;
    if (req->cancelled) {
        req->status_code = 0;
    } else {
        retry = should_retry(res, req->status_code, req->retry_count);
        if (retry) {
            req->retry_count++;
            req->status_code = 0;
            req->body_out = NULL;
            req->body_out_len = 0;
            if (req->resp_headers) {
                json_t *ra = json_object_get(req->resp_headers, "Retry-After");
                if (json_is_integer(ra)) {
                    retry_after_ms = json_integer_value(ra) * 1000;
                }
            }
            if (retry_after_ms == 0) {
                retry_after_ms = backoff_ms(req->retry_count);
            }
        }
    }

    release_item_resources(item);
    remove_active(s, item);
    s->in_flight--;

    if (item->req->account_id && item->req->account_id[0] != '\0') {
        int idx = account_index(s, item->req->account_id);
        if (idx >= 0 && s->accounts[idx].in_flight > 0)
            s->accounts[idx].in_flight--;
    }

    if (g_overall_cap > 0 && g_overall_in_flight > 0) {
        pthread_mutex_lock(&g_overall_mu);
        g_overall_in_flight--;
        pthread_mutex_unlock(&g_overall_mu);
    }

    if (retry && !req->cancelled) {
        item->ready_at_ms = pav_now_ms() + retry_after_ms;
        item->in_flight = 0;
        return 1;
    }

    if (req->on_done) {
        req->on_done(req, req->user);
    }
    return 0;
}

static void *server_thread_main(void *arg) {
    PavNetServer *s = arg;

    s->multi = curl_multi_init();
    if (!s->multi) {
        pav_log("http", PAV_ERROR, 0,
                "curl_multi_init failed for %s", s->key);
        pthread_mutex_lock(&s->mu);
        s->running = -1;
        pthread_cond_broadcast(&s->cv);
        pthread_mutex_unlock(&s->mu);
        return NULL;
    }

    curl_multi_setopt(s->multi, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
    curl_multi_setopt(s->multi, CURLMOPT_MAX_HOST_CONNECTIONS, 2L);
    curl_multi_setopt(s->multi, CURLMOPT_MAX_CONCURRENT_STREAMS,
                      (long)PAV_NET_PER_SERVER_CAP);

    pthread_mutex_lock(&s->mu);
    s->running = 1;
    pthread_cond_broadcast(&s->cv);
    pthread_mutex_unlock(&s->mu);

    for (;;) {
        int did_work = 0;
        int64_t now_ms;

        pthread_mutex_lock(&s->mu);
        now_ms = pav_now_ms();

        if (s->in_flight == 0 && s->q_count == 0) {
            if (s->stop) {
                pthread_mutex_unlock(&s->mu);
                break;
            }
            pthread_cond_wait(&s->cv, &s->mu);
            pthread_mutex_unlock(&s->mu);
            continue;
        }

        if (s->stop) {
            NetReqItem *it = s->q_head;
            while (it) {
                NetReqItem *next = it->next;
                it->in_flight = 0;
                if (it->req->on_done) {
                    it->req->status_code = 0;
                    it->req->on_done(it->req, it->req->user);
                }
                release_item_resources(it);
                free(it);
                it = next;
            }
            s->q_head = NULL;
            s->q_tail = NULL;
            s->q_count = 0;
            s->in_flight = 0;
            pthread_mutex_unlock(&s->mu);
            break;
        }

        while (s->q_count > 0 && s->in_flight < PAV_NET_PER_SERVER_CAP) {
            int overall_ok = 1;
            pthread_mutex_lock(&g_overall_mu);
            if (g_overall_cap > 0 && g_overall_in_flight >= g_overall_cap) {
                overall_ok = 0;
            }
            if (overall_ok && g_overall_cap > 0) {
                g_overall_in_flight++;
            }
            pthread_mutex_unlock(&g_overall_mu);
            if (!overall_ok) break;

            NetReqItem *cur = s->q_head;
            NetReqItem *prev = NULL;
            NetReqItem *picked = NULL;
            NetReqItem *picked_prev = NULL;

            while (cur) {
                if (cur->ready_at_ms <= now_ms) {
                    const char *aid = cur->req->account_id;
                    int acc_ok = 1;
                    if (aid && aid[0] != '\0') {
                        int idx = account_index(s, aid);
                        if (idx >= 0 &&
                            s->accounts[idx].in_flight >= PAV_NET_PER_ACCOUNT_CAP) {
                            acc_ok = 0;
                        }
                    }
                    if (acc_ok) {
                        picked = cur;
                        picked_prev = prev;
                        break;
                    }
                }
                prev = cur;
                cur = cur->next;
            }

            if (!picked) {
                if (overall_ok && g_overall_cap > 0) {
                    pthread_mutex_lock(&g_overall_mu);
                    g_overall_in_flight--;
                    pthread_mutex_unlock(&g_overall_mu);
                }
                break;
            }

            if (picked_prev) {
                picked_prev->next = picked->next;
            } else {
                s->q_head = picked->next;
            }
            if (picked == s->q_tail) {
                s->q_tail = picked_prev;
            }
            s->q_count--;
            picked->next = NULL;

            if (picked->req->account_id && picked->req->account_id[0] != '\0') {
                int idx = account_index(s, picked->req->account_id);
                if (idx >= 0) s->accounts[idx].in_flight++;
            }

            pthread_cond_broadcast(&s->cv);
            pthread_mutex_unlock(&s->mu);

            int rc = setup_request(s, picked);

            pthread_mutex_lock(&s->mu);
            if (rc != 0) {
                if (picked->req->account_id && picked->req->account_id[0] != '\0') {
                    int idx = account_index(s, picked->req->account_id);
                    if (idx >= 0 && s->accounts[idx].in_flight > 0)
                        s->accounts[idx].in_flight--;
                }
                if (overall_ok && g_overall_cap > 0) {
                    pthread_mutex_lock(&g_overall_mu);
                    g_overall_in_flight--;
                    pthread_mutex_unlock(&g_overall_mu);
                }
                release_item_resources(picked);
                if (picked->req->on_done) {
                    picked->req->status_code = 0;
                    picked->req->on_done(picked->req, picked->req->user);
                }
                free(picked);
            } else {
                s->in_flight++;
            }
        }

        did_work = (s->in_flight > 0);
        pthread_cond_broadcast(&s->cv);
        pthread_mutex_unlock(&s->mu);

        if (did_work) {
            int still_running = 0;
             curl_multi_perform(s->multi, &still_running);

            CURLMsg *msg;
            int msgs_left;
            while ((msg = curl_multi_info_read(s->multi, &msgs_left))) {
                if (msg->msg == CURLMSG_DONE) {
                    CURL *eh = msg->easy_handle;
                    CURLcode res = msg->data.result;

                    pthread_mutex_lock(&s->mu);
                    NetReqItem *item_to_finish = NULL;
                    for (NetReqItem *it = s->active_head; it; it = it->next) {
                        if (it->eh == eh) {
                            item_to_finish = it;
                            break;
                        }
                    }
                    if (item_to_finish) {
                        int requeue = complete_request(s, item_to_finish, res);
                        if (requeue) {
                            item_to_finish->in_flight = 0;
                            if (s->q_tail) s->q_tail->next = item_to_finish;
                            else s->q_head = item_to_finish;
                            s->q_tail = item_to_finish;
                            s->q_count++;
                        } else {
                            free(item_to_finish);
                            item_to_finish = NULL;
                        }
                    }
                    if (!item_to_finish) {
                        if (eh) curl_easy_cleanup(eh);
                    }
                    pthread_cond_broadcast(&s->cv);
                    pthread_mutex_unlock(&s->mu);
                }
            }

            if (still_running) {
                long timeout_ms = -1;
                curl_multi_timeout(s->multi, &timeout_ms);
                if (timeout_ms < 0) timeout_ms = 100;
                if (timeout_ms > 500) timeout_ms = 500;
                fd_set fdread, fdwrite, fdexcep;
                int fdmax = -1;
                FD_ZERO(&fdread);
                FD_ZERO(&fdwrite);
                FD_ZERO(&fdexcep);
                CURLMcode mc = curl_multi_fdset(s->multi, &fdread, &fdwrite, &fdexcep, &fdmax);
                if (mc == CURLM_OK && fdmax >= 0) {
                    struct timeval tv;
                    tv.tv_sec = timeout_ms / 1000;
                    tv.tv_usec = (timeout_ms % 1000) * 1000;
                    select(fdmax + 1, &fdread, &fdwrite, &fdexcep, &tv);
                } else if (mc == CURLM_OK) {
                    struct timespec ts;
                    ts.tv_sec = timeout_ms / 1000;
                    ts.tv_nsec = (timeout_ms % 1000) * 1000000L;
                    nanosleep(&ts, NULL);
                }
            }
        } else {
            pthread_mutex_lock(&s->mu);
            if (s->q_count == 0 && s->in_flight == 0) {
                if (s->stop) {
                    pthread_mutex_unlock(&s->mu);
                    break;
                }
                pthread_cond_wait(&s->cv, &s->mu);
            } else if (s->in_flight == 0 && s->q_count > 0) {
                pthread_cond_broadcast(&s->cv);
            }
            pthread_cond_broadcast(&s->cv);
            pthread_mutex_unlock(&s->mu);
        }
    }

    pthread_mutex_lock(&s->mu);
    for (NetReqItem *it = s->active_head; it; ) {
        NetReqItem *next = it->next;
        if (it->eh) {
            curl_multi_remove_handle(s->multi, it->eh);
        }
        release_item_resources(it);
        it->in_flight = 0;
        s->in_flight--;
        if (it->req->on_done) {
            it->req->status_code = 0;
            it->req->on_done(it->req, it->req->user);
        }
        free(it);
        it = next;
    }
    s->active_head = NULL;
    pthread_mutex_unlock(&s->mu);

    if (s->multi) {
        curl_multi_cleanup(s->multi);
        s->multi = NULL;
    }
    s->running = 0;
    return NULL;
}

static PavNetServer *server_create_locked(const char *key) {
    PavNetServer *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    strncpy(s->key, key, sizeof(s->key) - 1);
    s->key[sizeof(s->key) - 1] = '\0';
    pthread_mutex_init(&s->mu, NULL);
    pthread_cond_init(&s->cv, NULL);
    s->stop = 0;

    int rc = pav_thread_create((void **)&s->thread, server_thread_main, s);
    if (rc != 0) {
        pthread_mutex_destroy(&s->mu);
        pthread_cond_destroy(&s->cv);
        free(s);
        return NULL;
    }

    pthread_mutex_lock(&s->mu);
    while (s->running == 0) {
        pthread_cond_wait(&s->cv, &s->mu);
    }
    pthread_mutex_unlock(&s->mu);

    if (s->running < 0) {
        free(s);
        return NULL;
    }
    return s;
}

static PavNetServer *find_server_locked(const char *key) {
    int i;
    for (i = 0; i < g_server_count; i++) {
        if (strcmp(g_servers[i]->key, key) == 0) return g_servers[i];
    }
    return NULL;
}

int pav_net_start(void) {
    if (g_net_started) return 0;
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) return -1;
    g_net_started = 1;
    g_overall_cap = 0;
    g_overall_in_flight = 0;
    return 0;
}

void pav_net_stop(void) {
    int i;
    if (!g_net_started) return;

    pthread_mutex_lock(&g_reg_mu);
    for (i = 0; i < g_server_count; i++) {
        PavNetServer *s = g_servers[i];
        pthread_mutex_lock(&s->mu);
        s->stop = 1;
        pthread_cond_broadcast(&s->cv);
        pthread_mutex_unlock(&s->mu);
    }
    pthread_mutex_unlock(&g_reg_mu);

    for (i = 0; i < g_server_count; i++) {
        PavNetServer *s = g_servers[i];
        pav_thread_join(s->thread);
        pthread_mutex_destroy(&s->mu);
        pthread_cond_destroy(&s->cv);
        free(s);
    }
    g_server_count = 0;

    curl_global_cleanup();
    g_net_started = 0;
}

PavNetServer *pav_net_server_for(const char *url) {
    char key[512];
    if (endpoint_key(url, key, sizeof(key)) != 0) return NULL;

    pthread_mutex_lock(&g_reg_mu);
    PavNetServer *s = find_server_locked(key);
    if (!s) {
        if (g_server_count >= PAV_NET_SERVER_MAX) {
            pthread_mutex_unlock(&g_reg_mu);
            return NULL;
        }
        s = server_create_locked(key);
        if (s) g_servers[g_server_count++] = s;
    }
    pthread_mutex_unlock(&g_reg_mu);
    return s;
}

static void track_account(PavNetServer *s, const char *account_id) {
    if (!account_id || account_id[0] == '\0') return;
    account_index(s, account_id);
    s->active_accounts++;
}

int pav_net_request(PavReq *req) {
    if (!req || !req->url || !req->method) return -1;
    PavNetServer *s = pav_net_server_for(req->url);
    if (!s) return -1;

    NetReqItem *item = calloc(1, sizeof(*item));
    if (!item) return -1;
    item->req = req;
    item->retry_count = req->retry_count;
    item->ready_at_ms = pav_now_ms();
    item->next = NULL;

    pthread_mutex_lock(&s->mu);
    track_account(s, req->account_id);

    if (s->q_tail) s->q_tail->next = item;
    else s->q_head = item;
    s->q_tail = item;
    s->q_count++;

    pthread_cond_signal(&s->cv);
    pthread_mutex_unlock(&s->mu);

    return 0;
}

int pav_net_set_overall_cap(int cap) {
    int prev = g_overall_cap;
    pthread_mutex_lock(&g_overall_mu);
    g_overall_cap = cap;
    pthread_mutex_unlock(&g_overall_mu);
    return prev;
}

int pav_net_release_account(PavNetServer *s, const char *account_id) {
    int i;
    int found = 0;

    if (!s || !account_id || account_id[0] == '\0') return -1;
    pthread_mutex_lock(&s->mu);
    for (i = 0; i < s->account_count; i++) {
        if (strcmp(s->accounts[i].id, account_id) == 0) {
            s->accounts[i].id[0] = '\0';
            s->accounts[i].in_flight = 0;
            found = 1;
            if (s->active_accounts > 0) s->active_accounts--;
            break;
        }
    }
    if (found && s->in_flight == 0 && s->q_count == 0 && s->active_accounts == 0) {
        s->stop = 1;
        pthread_cond_broadcast(&s->cv);
    }
    pthread_mutex_unlock(&s->mu);
    return found ? 0 : -1;
}

void pav_net_free(void *p) {
    free(p);
}

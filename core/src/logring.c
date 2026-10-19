#define _POSIX_C_SOURCE 200809L

#include "logring.h"

#include "pavillion.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

typedef struct RingSlot {
    uint64_t seq;
    char *line;
} RingSlot;

static pthread_mutex_t g_ring_mu = PTHREAD_MUTEX_INITIALIZER;
static RingSlot g_ring[PAV_LOGRING_CAP];
static size_t g_ring_start;
static size_t g_ring_count;

static int parse_line(const char *line, char *ts, size_t ts_len, char *level, size_t level_len,
                      char *subsystem, size_t subsystem_len, const char **text) {
    size_t level_n;
    const char *body;
    const char *gap;
    size_t sub_n;

    if (line == NULL || ts == NULL || level == NULL || subsystem == NULL || text == NULL) {
        return -1;
    }
    if (strlen(line) < 33 || line[24] != ' ' || line[30] != ' ') {
        return -1;
    }
    if (ts_len < 25 || level_len < 6) {
        return -1;
    }
    memcpy(ts, line, 24);
    ts[24] = '\0';
    memcpy(level, line + 25, 5);
    level[5] = '\0';
    level_n = 5;
    while (level_n > 0 && level[level_n - 1] == ' ') {
        level[--level_n] = '\0';
    }
    if (level_n == 0) {
        return -1;
    }
    body = line + 31;
    gap = strstr(body, "  ");
    if (gap == NULL || gap == body) {
        return -1;
    }
    sub_n = (size_t)(gap - body);
    if (sub_n >= subsystem_len) {
        return -1;
    }
    memcpy(subsystem, body, sub_n);
    subsystem[sub_n] = '\0';
    *text = gap + 2;
    return 0;
}

static int level_rank(const char *name) {
    if (strcmp(name, "ERROR") == 0) {
        return PAV_ERROR;
    }
    if (strcmp(name, "WARN") == 0) {
        return PAV_WARN;
    }
    if (strcmp(name, "INFO") == 0) {
        return PAV_INFO;
    }
    if (strcmp(name, "DEBUG") == 0) {
        return PAV_DEBUG;
    }
    if (strcmp(name, "TRACE") == 0) {
        return PAV_TRACE;
    }
    return -1;
}

static char *message_copy(const char *text) {
    size_t n;
    char *out;

    if (text == NULL) {
        return NULL;
    }
    n = strlen(text);
    if (n > 0 && text[n - 1] == '\n') {
        n--;
    }
    if (n > 0 && text[n - 1] == '\r') {
        n--;
    }
    out = malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, text, n);
    out[n] = '\0';
    return out;
}

static int subsystem_wanted(const json_t *subsystems, const char *name) {
    size_t i;
    size_t n;

    if (subsystems == NULL) {
        return 1;
    }
    n = json_array_size(subsystems);
    for (i = 0; i < n; i++) {
        const char *item = json_string_value(json_array_get(subsystems, i));

        if (item != NULL && strcmp(item, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static void slot_clear(RingSlot *slot) {
    free(slot->line);
    slot->line = NULL;
    slot->seq = 0;
}

void pav_logring_reset(void) {
    size_t i;

    pthread_mutex_lock(&g_ring_mu);
    for (i = 0; i < g_ring_count; i++) {
        slot_clear(&g_ring[(g_ring_start + i) % PAV_LOGRING_CAP]);
    }
    g_ring_start = 0;
    g_ring_count = 0;
    pthread_mutex_unlock(&g_ring_mu);
}

void pav_logring_renumber(uint64_t from_seq, uint64_t offset) {
    size_t i;

    pthread_mutex_lock(&g_ring_mu);
    for (i = 0; i < g_ring_count; i++) {
        RingSlot *slot = &g_ring[(g_ring_start + i) % PAV_LOGRING_CAP];

        if (slot->seq >= from_seq) {
            slot->seq += offset;
        }
    }
    pthread_mutex_unlock(&g_ring_mu);
}

int pav_logring_push(uint64_t seq, const char *line) {
    char *copy;
    size_t index;

    if (line == NULL) {
        return -1;
    }
    copy = strdup(line);
    if (copy == NULL) {
        return -1;
    }
    pthread_mutex_lock(&g_ring_mu);
    if (g_ring_count == PAV_LOGRING_CAP) {
        slot_clear(&g_ring[g_ring_start]);
        g_ring_start = (g_ring_start + 1) % PAV_LOGRING_CAP;
        g_ring_count--;
    }
    index = (g_ring_start + g_ring_count) % PAV_LOGRING_CAP;
    g_ring[index].seq = seq;
    g_ring[index].line = copy;
    g_ring_count++;
    pthread_mutex_unlock(&g_ring_mu);
    return 0;
}

json_t *pav_logring_line_json(uint64_t seq, const char *line) {
    char ts[32];
    char level[8];
    char subsystem[128];
    const char *text = NULL;
    char *message;
    json_t *obj;

    if (parse_line(line, ts, sizeof(ts), level, sizeof(level), subsystem, sizeof(subsystem), &text) !=
        0) {
        return NULL;
    }
    message = message_copy(text);
    if (message == NULL) {
        return NULL;
    }
    obj = json_object();
    if (obj == NULL || json_object_set_new(obj, "seq", json_integer((json_int_t)seq)) != 0 ||
        json_object_set_new(obj, "ts", json_string(ts)) != 0 ||
        json_object_set_new(obj, "level", json_string(level)) != 0 ||
        json_object_set_new(obj, "subsystem", json_string(subsystem)) != 0 ||
        json_object_set_new(obj, "text", json_string(message)) != 0) {
        free(message);
        json_decref(obj);
        return NULL;
    }
    free(message);
    return obj;
}

static int line_kept(const char *line, int min_level, const json_t *subsystems) {
    char ts[32];
    char level[8];
    char subsystem[128];
    const char *text = NULL;
    int rank;

    if (parse_line(line, ts, sizeof(ts), level, sizeof(level), subsystem, sizeof(subsystem), &text) !=
        0) {
        return 0;
    }
    rank = level_rank(level);
    if (rank < 0 || rank > min_level) {
        return 0;
    }
    return subsystem_wanted(subsystems, subsystem);
}

int pav_logring_read(uint64_t since_seq, int limit, int min_level, const json_t *subsystems,
                     json_t **out) {
    typedef struct Match {
        uint64_t seq;
        char *line;
    } Match;

    Match *matches = NULL;
    size_t n = 0;
    size_t i;
    size_t from;
    json_t *lines;
    json_t *root;

    if (out == NULL) {
        return -1;
    }
    *out = NULL;
    if (limit < 1) {
        limit = 1;
    }
    if (limit > PAV_LOGRING_CAP) {
        limit = PAV_LOGRING_CAP;
    }
    if (min_level < PAV_ERROR) {
        min_level = PAV_ERROR;
    }
    if (min_level > PAV_TRACE) {
        min_level = PAV_TRACE;
    }

    pthread_mutex_lock(&g_ring_mu);
    if (g_ring_count > 0) {
        matches = calloc(g_ring_count, sizeof(*matches));
        if (matches == NULL) {
            pthread_mutex_unlock(&g_ring_mu);
            return -1;
        }
        for (i = 0; i < g_ring_count; i++) {
            RingSlot *slot = &g_ring[(g_ring_start + i) % PAV_LOGRING_CAP];

            if (slot->seq <= since_seq || !line_kept(slot->line, min_level, subsystems)) {
                continue;
            }
            matches[n].line = strdup(slot->line);
            if (matches[n].line == NULL) {
                pthread_mutex_unlock(&g_ring_mu);
                for (i = 0; i < n; i++) {
                    free(matches[i].line);
                }
                free(matches);
                return -1;
            }
            matches[n].seq = slot->seq;
            n++;
        }
    }
    pthread_mutex_unlock(&g_ring_mu);

    lines = json_array();
    root = json_object();
    if (lines == NULL || root == NULL) {
        json_decref(lines);
        json_decref(root);
        for (i = 0; i < n; i++) {
            free(matches[i].line);
        }
        free(matches);
        return -1;
    }
    from = n > (size_t)limit ? n - (size_t)limit : 0;
    for (i = 0; i < n; i++) {
        if (i >= from) {
            json_t *obj = pav_logring_line_json(matches[i].seq, matches[i].line);

            if (obj != NULL && json_array_append_new(lines, obj) != 0) {
                json_decref(obj);
            }
        }
        free(matches[i].line);
    }
    free(matches);
    if (json_object_set_new(root, "lines", lines) != 0) {
        json_decref(lines);
        json_decref(root);
        return -1;
    }
    *out = root;
    return 0;
}

#define PAV_LOG_EXPORT_MAX (32u * 1024u * 1024u)

static int level_listed(const json_t *levels, const char *level) {
    size_t i;
    size_t n;

    if (!json_is_array(levels)) {
        return 0;
    }
    n = json_array_size(levels);
    for (i = 0; i < n; i++) {
        const char *item = json_string_value(json_array_get(levels, i));

        if (item != NULL && strcmp(item, level) == 0) {
            return 1;
        }
    }
    return 0;
}

static char fold_ascii(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

static int ascii_has(const char *hay, size_t hay_n, const char *needle) {
    size_t needle_n;
    size_t i;
    size_t j;

    if (needle == NULL) {
        return 1;
    }
    needle_n = strlen(needle);
    if (needle_n == 0) {
        return 1;
    }
    if (needle_n > hay_n) {
        return 0;
    }
    for (i = 0; i + needle_n <= hay_n; i++) {
        for (j = 0; j < needle_n; j++) {
            if (fold_ascii(hay[i + j]) != fold_ascii(needle[j])) {
                break;
            }
        }
        if (j == needle_n) {
            return 1;
        }
    }
    return 0;
}

static int export_kept(const char *line, const json_t *levels, const json_t *subsystems, const char *text) {
    char ts[32];
    char level[8];
    char subsystem[128];
    const char *message = NULL;
    size_t n;

    if (parse_line(line, ts, sizeof(ts), level, sizeof(level), subsystem, sizeof(subsystem), &message) != 0) {
        return 0;
    }
    if (!level_listed(levels, level) || !subsystem_wanted(subsystems, subsystem)) {
        return 0;
    }
    n = strlen(line);
    if (n > 0 && line[n - 1] == '\n') {
        n--;
    }
    return ascii_has(line, n, text);
}

int pav_logring_export(const json_t *levels, const json_t *subsystems, const char *text, char **out,
                       size_t *out_len, int *lines) {
    char **kept = NULL;
    size_t n = 0;
    size_t i;
    size_t total = 0;
    char *buf;
    size_t off;

    if (out == NULL || out_len == NULL || lines == NULL) {
        return -1;
    }
    *out = NULL;
    *out_len = 0;
    *lines = 0;
    pthread_mutex_lock(&g_ring_mu);
    if (g_ring_count > 0) {
        kept = calloc(g_ring_count, sizeof(*kept));
        if (kept == NULL) {
            pthread_mutex_unlock(&g_ring_mu);
            return -1;
        }
        for (i = 0; i < g_ring_count; i++) {
            RingSlot *slot = &g_ring[(g_ring_start + i) % PAV_LOGRING_CAP];
            size_t len;

            if (!export_kept(slot->line, levels, subsystems, text)) {
                continue;
            }
            len = strlen(slot->line);
            if (total > PAV_LOG_EXPORT_MAX || len > PAV_LOG_EXPORT_MAX - total) {
                pthread_mutex_unlock(&g_ring_mu);
                for (i = 0; i < n; i++) {
                    free(kept[i]);
                }
                free(kept);
                return -2;
            }
            kept[n] = strdup(slot->line);
            if (kept[n] == NULL) {
                pthread_mutex_unlock(&g_ring_mu);
                for (i = 0; i < n; i++) {
                    free(kept[i]);
                }
                free(kept);
                return -1;
            }
            total += len;
            n++;
        }
    }
    pthread_mutex_unlock(&g_ring_mu);

    buf = malloc(total + 1);
    if (buf == NULL) {
        for (i = 0; i < n; i++) {
            free(kept[i]);
        }
        free(kept);
        return -1;
    }
    off = 0;
    for (i = 0; i < n; i++) {
        size_t len = strlen(kept[i]);

        memcpy(buf + off, kept[i], len);
        off += len;
        free(kept[i]);
    }
    free(kept);
    buf[off] = '\0';
    *out = buf;
    *out_len = off;
    *lines = (int)n;
    return 0;
}

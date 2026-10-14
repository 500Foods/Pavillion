#define _POSIX_C_SOURCE 200809L

#include "pavillion.h"

#include <ctype.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int pavillion_start(const char *workdir, char *err, size_t err_len) {
    (void)workdir;
    (void)err;
    (void)err_len;
    return 0;
}

void pavillion_stop(void) {
}

int pavillion_status(PavillionStatus *out) {
    if (out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    snprintf(out->state, sizeof(out->state), "ready");
    return 0;
}

typedef struct LogNode {
    struct LogNode *next;
    char *line;
} LogNode;

static pthread_mutex_t g_log_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_log_cv = PTHREAD_COND_INITIALIZER;
static pthread_t g_log_thread;
static FILE *g_log_out;
static LogNode *g_q_head;
static LogNode *g_q_tail;
static int g_log_own;
static int g_log_level = PAV_INFO;
static int g_log_alive;
static int g_log_shutdown;

/* Same specifier count as Hydrogen's count_format_specifiers. */
static size_t count_format_specifiers(const char *format) {
    size_t count = 0;
    const char *ptr = format;

    if (format == NULL) {
        return 0;
    }

    while ((ptr = strchr(ptr, '%')) != NULL) {
        ptr++;
        if (*ptr == '%') {
            ptr++;
            continue;
        }
        while (*ptr == '-' || *ptr == '+' || *ptr == ' ' || *ptr == '#' || *ptr == '0' || *ptr == '\'') {
            ptr++;
        }
        if (*ptr == '*') {
            ptr++;
        } else {
            while (isdigit((unsigned char)*ptr)) {
                ptr++;
            }
        }
        if (*ptr == '.') {
            ptr++;
            if (*ptr == '*') {
                ptr++;
            } else {
                while (isdigit((unsigned char)*ptr)) {
                    ptr++;
                }
            }
        }
        if (*ptr == 'h' || *ptr == 'l' || *ptr == 'L' || *ptr == 'z' || *ptr == 'j' || *ptr == 't') {
            if ((*ptr == 'l' && *(ptr + 1) == 'l') || (*ptr == 'h' && *(ptr + 1) == 'h')) {
                ptr += 2;
            } else {
                ptr++;
            }
        }
        if (strchr("diouxXeEfFgGaAcspn", *ptr) != NULL) {
            count++;
            ptr++;
        }
    }
    return count;
}

static int level_kept(int level) {
    if (level < PAV_ERROR || level > PAV_DEBUG) {
        return 0;
    }
    return level <= g_log_level;
}

static const char *level_name(int level) {
    switch (level) {
    case PAV_ERROR:
        return "ERROR";
    case PAV_WARN:
        return "WARN";
    case PAV_INFO:
        return "INFO";
    case PAV_DEBUG:
        return "DEBUG";
    default:
        return "?????";
    }
}

static void format_stamp(char *out, size_t out_len) {
    struct timespec ts;
    struct tm tm;
    int msec;

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        snprintf(out, out_len, "0000-00-00T00:00:00.000Z");
        return;
    }
    gmtime_r(&ts.tv_sec, &tm);
    msec = (int)(ts.tv_nsec / 1000000L);
    if (msec > 999) {
        msec = 999;
    }
    snprintf(out, out_len, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec, msec);
}

static void emit_bytes_unlocked(const char *line) {
    FILE *out = g_log_out != NULL ? g_log_out : stderr;
    fputs(line, out);
    fflush(out);
}

static int enqueue_line_unlocked(const char *line) {
    LogNode *node = malloc(sizeof(*node));
    char *copy;

    if (node == NULL) {
        return -1;
    }
    copy = strdup(line);
    if (copy == NULL) {
        free(node);
        return -1;
    }
    node->line = copy;
    node->next = NULL;
    if (g_q_tail != NULL) {
        g_q_tail->next = node;
    } else {
        g_q_head = node;
    }
    g_q_tail = node;
    pthread_cond_signal(&g_log_cv);
    return 0;
}

/* The caller holds g_log_mu. A running log thread writes the bytes later.
   Otherwise the caller writes them now, so a line before start is not lost. */
static void deliver_unlocked(int level, const char *subsystem, const char *msg) {
    char stamp[32];
    char line[1200];

    if (!level_kept(level)) {
        return;
    }
    format_stamp(stamp, sizeof(stamp));
    snprintf(line, sizeof(line), "%s %-5s %s  %s\n",
             stamp, level_name(level), subsystem, msg);
    if (g_log_alive && enqueue_line_unlocked(line) == 0) {
        return;
    }
    emit_bytes_unlocked(line);
}

static void *log_thread_main(void *arg) {
    (void)arg;
    pthread_mutex_lock(&g_log_mu);
    for (;;) {
        LogNode *node;

        while (g_q_head == NULL && !g_log_shutdown) {
            pthread_cond_wait(&g_log_cv, &g_log_mu);
        }
        if (g_q_head == NULL && g_log_shutdown) {
            g_log_alive = 0;
            pthread_mutex_unlock(&g_log_mu);
            break;
        }
        node = g_q_head;
        g_q_head = node->next;
        if (g_q_head == NULL) {
            g_q_tail = NULL;
        }
        emit_bytes_unlocked(node->line);
        free(node->line);
        free(node);
    }
    return NULL;
}

int pav_log_thread_start(void) {
    int rc;

    pthread_mutex_lock(&g_log_mu);
    if (g_log_alive) {
        pthread_mutex_unlock(&g_log_mu);
        return -1;
    }
    g_log_shutdown = 0;
    rc = pthread_create(&g_log_thread, NULL, log_thread_main, NULL);
    if (rc != 0) {
        pthread_mutex_unlock(&g_log_mu);
        return -1;
    }
    g_log_alive = 1;
    pthread_mutex_unlock(&g_log_mu);
    return 0;
}

void pav_log_thread_shutdown(void) {
    pthread_t thread;

    pthread_mutex_lock(&g_log_mu);
    if (!g_log_alive) {
        pthread_mutex_unlock(&g_log_mu);
        return;
    }
    g_log_shutdown = 1;
    thread = g_log_thread;
    pthread_cond_broadcast(&g_log_cv);
    pthread_mutex_unlock(&g_log_mu);
    pthread_join(thread, NULL);
}

int pav_log_set_output(const char *path) {
    pthread_mutex_lock(&g_log_mu);
    if (g_log_own && g_log_out != NULL) {
        fclose(g_log_out);
    }
    g_log_own = 0;
    g_log_out = NULL;
    if (path != NULL) {
        g_log_out = fopen(path, "w");
        if (g_log_out == NULL) {
            pthread_mutex_unlock(&g_log_mu);
            return -1;
        }
        g_log_own = 1;
    }
    pthread_mutex_unlock(&g_log_mu);
    return 0;
}

void pav_log(const char *subsystem, int level, int num_args, const char *fmt, ...) {
    size_t specifiers;
    char msg[1024];
    va_list ap;

    if (subsystem == NULL || subsystem[0] == '\0') {
        subsystem = "log";
    }
    if (fmt == NULL) {
        fmt = "";
    }

    pthread_mutex_lock(&g_log_mu);
    specifiers = count_format_specifiers(fmt);
    if (num_args < 0 || (size_t)num_args != specifiers) {
        snprintf(msg, sizeof(msg),
                 "format specifier count mismatch: format has %zu, num_args is %d",
                 specifiers, num_args);
        deliver_unlocked(PAV_ERROR, "log", msg);
        pthread_mutex_unlock(&g_log_mu);
        return;
    }
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    deliver_unlocked(level, subsystem, msg);
    pthread_mutex_unlock(&g_log_mu);
}

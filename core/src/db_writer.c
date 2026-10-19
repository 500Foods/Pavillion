#define _GNU_SOURCE

#include "pav_db.h"

#include "pav_platform.h"
#include "pavillion.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BATCH_MAX 50
#define BATCH_MS 20

typedef struct DbJob {
    PavDbJob fn;
    void *ctx;
    PavDbDone on_done;
    void *done_ctx;
    int rc;
    struct DbJob *next;
} DbJob;

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv;
static int g_cv_ready;
static sqlite3 *g_write;
static void *g_thread;
static int g_started;
static int g_stop;
static int g_hold;
static int g_in_batch;
static int g_ready;
static int g_pragma_rc;
static int g_batch_gen;
static size_t g_stack;
static atomic_int g_checkpoints;
static DbJob *g_head;
static DbJob *g_tail;

static int64_t mono_ms(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int apply_pragmas(sqlite3 *db) {
    static const char *const k_sql[] = {
        "PRAGMA journal_mode=WAL",
        "PRAGMA foreign_keys=ON",
        "PRAGMA synchronous=NORMAL",
        "PRAGMA busy_timeout=5000",
        "PRAGMA cache_size=-16384",
        "PRAGMA mmap_size=67108864",
        "PRAGMA temp_store=MEMORY",
        "PRAGMA wal_autocheckpoint=1000",
    };
    size_t i;

    for (i = 0; i < sizeof(k_sql) / sizeof(k_sql[0]); i++) {
        if (sqlite3_exec(db, k_sql[i], NULL, NULL, NULL) != SQLITE_OK) {
            return -1;
        }
    }
    return 0;
}

static void checkpoint_passive(sqlite3 *db) {
    sqlite3_exec(db, "PRAGMA wal_checkpoint(PASSIVE)", NULL, NULL, NULL);
    atomic_fetch_add(&g_checkpoints, 1);
}

static DbJob *take_unlocked(void) {
    DbJob *job = g_head;

    if (job == NULL) {
        return NULL;
    }
    g_head = job->next;
    if (g_head == NULL) {
        g_tail = NULL;
    }
    job->next = NULL;
    return job;
}

static int run_one(sqlite3 *db, DbJob *job) {
    int rc;

    if (pav_db_tx_begin(db) != 0) {
        return -1;
    }
    rc = job->fn(job->ctx, db);
    if (rc == SQLITE_BUSY) {
        pav_log("store", PAV_DEBUG, 0, "write busy, checkpoint and retry");
        pav_db_tx_rollback(db);
        checkpoint_passive(db);
        if (pav_db_tx_begin(db) != 0) {
            return -1;
        }
        rc = job->fn(job->ctx, db);
        if (rc == SQLITE_BUSY) {
            pav_log("store", PAV_ERROR, 0, "write failed after one busy retry");
        }
    }
    if (rc != 0) {
        pav_db_tx_rollback(db);
        return rc;
    }
    if (pav_db_tx_commit(db) != 0) {
        pav_db_tx_rollback(db);
        return -1;
    }
    return 0;
}

static void run_batch(DbJob **jobs, int n) {
    int i;
    int failed = -1;
    int rc;
    sqlite3 *db = g_write;

    if (n <= 0 || db == NULL) {
        return;
    }
    g_batch_gen++;
    if (g_batch_gen <= 0) {
        g_batch_gen = 1;
    }
    if (pav_db_tx_begin(db) != 0) {
        for (i = 0; i < n; i++) {
            jobs[i]->rc = -1;
        }
        return;
    }
    for (i = 0; i < n; i++) {
        rc = jobs[i]->fn(jobs[i]->ctx, db);
        jobs[i]->rc = rc;
        if (rc != 0) {
            failed = i;
            break;
        }
    }
    if (failed < 0) {
        if (pav_db_tx_commit(db) != 0) {
            pav_db_tx_rollback(db);
            failed = 0;
        }
    } else {
        pav_db_tx_rollback(db);
    }
    if (failed < 0) {
        for (i = 0; i < n; i++) {
            jobs[i]->rc = 0;
        }
        return;
    }
    if (n == 1 && jobs[0]->rc == SQLITE_BUSY) {
        checkpoint_passive(db);
        if (pav_db_tx_begin(db) != 0) {
            jobs[0]->rc = -1;
            return;
        }
        rc = jobs[0]->fn(jobs[0]->ctx, db);
        if (rc == SQLITE_BUSY) {
            pav_log("store", PAV_ERROR, 0, "write failed after one busy retry");
            pav_db_tx_rollback(db);
            jobs[0]->rc = rc;
            return;
        }
        if (rc != 0) {
            pav_db_tx_rollback(db);
            jobs[0]->rc = rc;
            return;
        }
        if (pav_db_tx_commit(db) != 0) {
            pav_db_tx_rollback(db);
            jobs[0]->rc = -1;
            return;
        }
        jobs[0]->rc = 0;
        return;
    }
    for (i = 0; i < n; i++) {
        g_batch_gen++;
        jobs[i]->rc = run_one(db, jobs[i]);
    }
}

static void fire_done(DbJob **jobs, int n) {
    int i;

    for (i = 0; i < n; i++) {
        if (jobs[i]->on_done != NULL) {
            jobs[i]->on_done(jobs[i]->done_ctx, jobs[i]->rc);
        }
        free(jobs[i]);
    }
}

static void collect(DbJob **batch, int *n) {
    int64_t t0;
    struct timespec ts;

    t0 = mono_ms();
    while (*n < BATCH_MAX) {
        while (g_head != NULL && *n < BATCH_MAX) {
            batch[(*n)++] = take_unlocked();
        }
        if (*n >= BATCH_MAX || g_stop) {
            return;
        }
        if (mono_ms() - t0 >= BATCH_MS) {
            return;
        }
#if defined(__APPLE__) || defined(_WIN32)
        /* macOS lacks pthread_condattr_setclock and winpthreads accepts only
           CLOCK_REALTIME, so g_cv uses the default clock there; wait for the
           remaining monotonic budget as a relative interval. */
        {
            int64_t left = t0 + BATCH_MS - mono_ms();

            if (left < 1) {
                left = 1;
            }
            ts.tv_sec = left / 1000;
            ts.tv_nsec = (left % 1000) * 1000000L;
            pthread_cond_timedwait_relative_np(&g_cv, &g_mu, &ts);
        }
#else
        ts.tv_sec = (t0 + BATCH_MS) / 1000;
        ts.tv_nsec = ((t0 + BATCH_MS) % 1000) * 1000000L;
        pthread_cond_timedwait(&g_cv, &g_mu, &ts);
#endif
    }
}

static void *writer_main(void *arg) {
    (void)arg;
    pthread_mutex_lock(&g_mu);
    g_pragma_rc = apply_pragmas(g_write);
    g_ready = 1;
    pthread_cond_broadcast(&g_cv);
    pthread_mutex_unlock(&g_mu);
    if (g_pragma_rc != 0) {
        return NULL;
    }
    for (;;) {
        DbJob *batch[BATCH_MAX];
        int n = 0;

        pthread_mutex_lock(&g_mu);
        while (!g_stop && (g_hold || g_head == NULL)) {
            pthread_cond_wait(&g_cv, &g_mu);
        }
        if (g_stop && g_head == NULL) {
            pthread_mutex_unlock(&g_mu);
            break;
        }
        g_in_batch = 1;
        collect(batch, &n);
        pthread_mutex_unlock(&g_mu);
        run_batch(batch, n);
        pthread_mutex_lock(&g_mu);
        g_in_batch = 0;
        pthread_cond_broadcast(&g_cv);
        pthread_mutex_unlock(&g_mu);
        fire_done(batch, n);
    }
    return NULL;
}

static int ensure_cv(void) {
    pthread_condattr_t attr;

    if (g_cv_ready) {
        return 0;
    }
    if (pthread_condattr_init(&attr) != 0) {
        return -1;
    }
#if defined(__APPLE__) || defined(_WIN32)
    if (pthread_cond_init(&g_cv, &attr) != 0) {
#else
    if (pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) != 0 || pthread_cond_init(&g_cv, &attr) != 0) {
#endif
        pthread_condattr_destroy(&attr);
        return -1;
    }
    pthread_condattr_destroy(&attr);
    g_cv_ready = 1;
    return 0;
}

static void note_stack(void) {
    pthread_t *thread;
    pthread_attr_t attr;
    size_t sz = 0;

    if (g_thread == NULL) {
        return;
    }
    thread = g_thread;
#if defined(__APPLE__)
    /* No pthread_getattr_np on macOS; this reports the same stack size. */
    (void)attr;
    sz = pthread_get_stacksize_np(*thread);
    if (sz != 0) {
        g_stack = sz;
    }
#elif defined(_WIN32)
    /* winpthreads cannot read a thread's stack size; g_stack stays 0. */
    (void)attr;
    (void)sz;
    (void)thread;
#else
    if (pthread_getattr_np(*thread, &attr) != 0) {
        return;
    }
    if (pthread_attr_getstacksize(&attr, &sz) == 0) {
        g_stack = sz;
    }
    pthread_attr_destroy(&attr);
#endif
}

int pav_db_writer_start(sqlite3 *db) {
    if (db == NULL) {
        return -1;
    }
    if (ensure_cv() != 0) {
        return -1;
    }
    pthread_mutex_lock(&g_mu);
    if (g_started) {
        int same = g_write == db;
        pthread_mutex_unlock(&g_mu);
        return same ? 0 : -1;
    }
    g_write = db;
    g_stop = 0;
    g_hold = 0;
    g_ready = 0;
    g_pragma_rc = -1;
    g_in_batch = 0;
    pthread_mutex_unlock(&g_mu);
    if (pav_thread_create(&g_thread, writer_main, NULL) != 0) {
        g_write = NULL;
        return -1;
    }
    note_stack();
    pthread_mutex_lock(&g_mu);
    while (!g_ready) {
        pthread_cond_wait(&g_cv, &g_mu);
    }
    g_started = g_pragma_rc == 0;
    pthread_mutex_unlock(&g_mu);
    if (!g_started) {
        pav_log("store", PAV_ERROR, 0, "writer pragmas failed");
        pav_thread_join(g_thread);
        g_thread = NULL;
        g_write = NULL;
        return -1;
    }
    pav_log("store", PAV_DEBUG, 0, "writer started");
    return 0;
}

int pav_db_writer_running(void) {
    int on;

    pthread_mutex_lock(&g_mu);
    on = g_started;
    pthread_mutex_unlock(&g_mu);
    return on;
}

int pav_db_writer_shutdown(sqlite3 *db) {
    void *thread = NULL;

    pthread_mutex_lock(&g_mu);
    if (!g_started || (db != NULL && g_write != db)) {
        pthread_mutex_unlock(&g_mu);
        return 0;
    }
    g_hold = 0;
    g_stop = 1;
    thread = g_thread;
    g_thread = NULL;
    pthread_cond_broadcast(&g_cv);
    pthread_mutex_unlock(&g_mu);
    if (thread != NULL) {
        pav_thread_join(thread);
    }
    pthread_mutex_lock(&g_mu);
    g_started = 0;
    g_write = NULL;
    g_stop = 0;
    g_in_batch = 0;
    g_ready = 0;
    pthread_mutex_unlock(&g_mu);
    pav_log("store", PAV_DEBUG, 0, "writer stopped");
    return 1;
}

int pav_db_write(PavDbJob job, void *ctx, PavDbDone on_done, void *done_ctx) {
    DbJob *node;

    if (job == NULL) {
        return -1;
    }
    node = calloc(1, sizeof(*node));
    if (node == NULL) {
        return -1;
    }
    node->fn = job;
    node->ctx = ctx;
    node->on_done = on_done;
    node->done_ctx = done_ctx;
    pthread_mutex_lock(&g_mu);
    if (!g_started || g_stop) {
        pthread_mutex_unlock(&g_mu);
        free(node);
        return -1;
    }
    if (g_tail != NULL) {
        g_tail->next = node;
    } else {
        g_head = node;
    }
    g_tail = node;
    pthread_cond_signal(&g_cv);
    pthread_mutex_unlock(&g_mu);
    return 0;
}

struct SyncWait {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int rc;
    int done;
};

static void sync_done(void *ctx, int rc) {
    struct SyncWait *wait = ctx;

    pthread_mutex_lock(&wait->mu);
    wait->rc = rc;
    wait->done = 1;
    pthread_cond_signal(&wait->cv);
    pthread_mutex_unlock(&wait->mu);
}

int pav_db_call(PavDbJob job, void *ctx) {
    struct SyncWait wait;

    memset(&wait, 0, sizeof(wait));
    if (pthread_mutex_init(&wait.mu, NULL) != 0) {
        return -1;
    }
    if (pthread_cond_init(&wait.cv, NULL) != 0) {
        pthread_mutex_destroy(&wait.mu);
        return -1;
    }
    if (pav_db_write(job, ctx, sync_done, &wait) != 0) {
        pthread_cond_destroy(&wait.cv);
        pthread_mutex_destroy(&wait.mu);
        return -1;
    }
    pthread_mutex_lock(&wait.mu);
    while (!wait.done) {
        pthread_cond_wait(&wait.cv, &wait.mu);
    }
    pthread_mutex_unlock(&wait.mu);
    pthread_cond_destroy(&wait.cv);
    pthread_mutex_destroy(&wait.mu);
    return wait.rc;
}

int pav_db_drain(void) {
    pthread_mutex_lock(&g_mu);
    while (g_started && (g_head != NULL || g_in_batch)) {
        pthread_cond_wait(&g_cv, &g_mu);
    }
    pthread_mutex_unlock(&g_mu);
    return 0;
}

void pav_db_writer_hold(int hold) {
    pthread_mutex_lock(&g_mu);
    g_hold = hold ? 1 : 0;
    pthread_cond_broadcast(&g_cv);
    pthread_mutex_unlock(&g_mu);
}

int pav_db_writer_batch(void) {
    return g_batch_gen;
}

int pav_db_writer_stack(size_t *out) {
    if (out == NULL || g_stack == 0) {
        return -1;
    }
    *out = g_stack;
    return 0;
}

int pav_db_busy_checkpoints(void) {
    return atomic_load(&g_checkpoints);
}

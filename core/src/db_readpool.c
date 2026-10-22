#include "pav_db.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define POOL_N 2

struct PavDbRead {
    int slot;
};

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
static sqlite3 *g_db[POOL_N];
static int g_used[POOL_N];
static int g_open;

static int apply_reader(sqlite3 *db) {
    static const char *const k_sql[] = {
        "PRAGMA foreign_keys=ON",
        "PRAGMA busy_timeout=5000",
        "PRAGMA cache_size=-16384",
        "PRAGMA mmap_size=67108864",
        "PRAGMA temp_store=MEMORY",
        "PRAGMA query_only=ON",
    };
    size_t i;

    for (i = 0; i < sizeof(k_sql) / sizeof(k_sql[0]); i++) {
        if (sqlite3_exec(db, k_sql[i], NULL, NULL, NULL) != SQLITE_OK) {
            return -1;
        }
    }
    return 0;
}

int pav_db_readpool_open(const char *path) {
    int i;

    if (path == NULL || path[0] == '\0') {
        return -1;
    }
    pthread_mutex_lock(&g_mu);
    if (g_open) {
        pthread_mutex_unlock(&g_mu);
        return 0;
    }
    for (i = 0; i < POOL_N; i++) {
        if (sqlite3_open_v2(path, &g_db[i], SQLITE_OPEN_READONLY, NULL) != SQLITE_OK ||
            g_db[i] == NULL || apply_reader(g_db[i]) != 0) {
            int j;
            for (j = 0; j <= i; j++) {
                if (g_db[j] != NULL) {
                    pav_db_cache_clear(g_db[j]);
                    sqlite3_close(g_db[j]);
                    g_db[j] = NULL;
                }
                g_used[j] = 0;
            }
            pthread_mutex_unlock(&g_mu);
            return -1;
        }
        g_used[i] = 0;
    }
    g_open = 1;
    pthread_mutex_unlock(&g_mu);
    return 0;
}

void pav_db_readpool_close(void) {
    int i;

    pthread_mutex_lock(&g_mu);
    while (g_open) {
        int busy = 0;
        for (i = 0; i < POOL_N; i++) {
            if (g_used[i]) {
                busy = 1;
            }
        }
        if (!busy) {
            break;
        }
        pthread_cond_wait(&g_cv, &g_mu);
    }
    for (i = 0; i < POOL_N; i++) {
        if (g_db[i] != NULL) {
            pav_db_cache_clear(g_db[i]);
            sqlite3_close(g_db[i]);
            g_db[i] = NULL;
        }
        g_used[i] = 0;
    }
    g_open = 0;
    pthread_mutex_unlock(&g_mu);
}

int pav_db_read_begin(PavDbRead **out) {
    PavDbRead *read;
    int slot = -1;

    if (out == NULL) {
        return -1;
    }
    *out = NULL;
    read = calloc(1, sizeof(*read));
    if (read == NULL) {
        return -1;
    }
    pthread_mutex_lock(&g_mu);
    if (!g_open) {
        pthread_mutex_unlock(&g_mu);
        free(read);
        return -1;
    }
    for (;;) {
        int i;
        for (i = 0; i < POOL_N; i++) {
            if (!g_used[i]) {
                g_used[i] = 1;
                slot = i;
                break;
            }
        }
        if (slot >= 0) {
            break;
        }
        pthread_cond_wait(&g_cv, &g_mu);
        if (!g_open) {
            pthread_mutex_unlock(&g_mu);
            free(read);
            return -1;
        }
    }
    pthread_mutex_unlock(&g_mu);
    read->slot = slot;
    /* A statement left stepped on this connection keeps the old WAL snapshot. */
    pav_db_cache_reset(g_db[slot]);
    if (sqlite3_exec(g_db[slot], "BEGIN", NULL, NULL, NULL) != SQLITE_OK) {
        pthread_mutex_lock(&g_mu);
        g_used[slot] = 0;
        pthread_cond_broadcast(&g_cv);
        pthread_mutex_unlock(&g_mu);
        free(read);
        return -1;
    }
    *out = read;
    return 0;
}

void pav_db_read_end(PavDbRead *read) {
    int slot;

    if (read == NULL) {
        return;
    }
    slot = read->slot;
    if (slot >= 0 && slot < POOL_N && g_db[slot] != NULL) {
        pav_db_cache_reset(g_db[slot]);
        sqlite3_exec(g_db[slot], "ROLLBACK", NULL, NULL, NULL);
    }
    pthread_mutex_lock(&g_mu);
    if (slot >= 0 && slot < POOL_N) {
        g_used[slot] = 0;
    }
    pthread_cond_broadcast(&g_cv);
    pthread_mutex_unlock(&g_mu);
    free(read);
}

sqlite3 *pav_db_read_handle(PavDbRead *read) {
    if (read == NULL || read->slot < 0 || read->slot >= POOL_N) {
        return NULL;
    }
    return g_db[read->slot];
}

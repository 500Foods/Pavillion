#define _POSIX_C_SOURCE 200809L

#include "pav_db.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct StmtNode {
    sqlite3 *db;
    const char *key;
    sqlite3_stmt *stmt;
    struct StmtNode *next;
} StmtNode;

static pthread_mutex_t g_cache_mu = PTHREAD_MUTEX_INITIALIZER;
static StmtNode *g_cache;

static int is_select(const char *sql) {
    static const char k_word[] = "select";
    const char *p = sql;
    int i;

    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        p++;
    }
    for (i = 0; k_word[i] != '\0'; i++) {
        char c = p[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        if (c != k_word[i]) {
            return 0;
        }
    }
    return p[i] == '\0' || p[i] == ' ' || p[i] == '\t' || p[i] == '\n' || p[i] == '\r';
}

static int has_format(const char *sql) {
    const char *p;

    for (p = sql; *p != '\0'; p++) {
        if (*p != '%') {
            continue;
        }
        if (p[1] == '%') {
            p++;
            continue;
        }
        return 1;
    }
    return 0;
}

int pav_db_prepare(sqlite3 *db, const char *key, const char *sql, sqlite3_stmt **out) {
    StmtNode *node;
    sqlite3_stmt *stmt = NULL;
    int readonly;

    if (db == NULL || key == NULL || sql == NULL || out == NULL) {
        return -1;
    }
    *out = NULL;
    readonly = sqlite3_db_readonly(db, "main");
    if (readonly == 1 && !is_select(sql)) {
        return -1;
    }
    pthread_mutex_lock(&g_cache_mu);
    for (node = g_cache; node != NULL; node = node->next) {
        if (node->db == db && node->key == key) {
            sqlite3_reset(node->stmt);
            sqlite3_clear_bindings(node->stmt);
            *out = node->stmt;
            pthread_mutex_unlock(&g_cache_mu);
            return 0;
        }
    }
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&g_cache_mu);
        return -1;
    }
    if (readonly == 1 && !sqlite3_stmt_readonly(stmt)) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&g_cache_mu);
        return -1;
    }
    node = calloc(1, sizeof(*node));
    if (node == NULL) {
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&g_cache_mu);
        return -1;
    }
    node->db = db;
    node->key = key;
    node->stmt = stmt;
    node->next = g_cache;
    g_cache = node;
    *out = stmt;
    pthread_mutex_unlock(&g_cache_mu);
    return 0;
}

int pav_db_bind_text(sqlite3_stmt *stmt, int index, const char *text) {
    if (stmt == NULL) {
        return -1;
    }
    if (text == NULL) {
        return sqlite3_bind_null(stmt, index) == SQLITE_OK ? 0 : -1;
    }
    return sqlite3_bind_text(stmt, index, text, -1, SQLITE_TRANSIENT) == SQLITE_OK ? 0 : -1;
}

int pav_db_bind_int64(sqlite3_stmt *stmt, int index, int64_t value) {
    if (stmt == NULL) {
        return -1;
    }
    return sqlite3_bind_int64(stmt, index, value) == SQLITE_OK ? 0 : -1;
}

int pav_db_bind_int(sqlite3_stmt *stmt, int index, int value) {
    if (stmt == NULL) {
        return -1;
    }
    return sqlite3_bind_int(stmt, index, value) == SQLITE_OK ? 0 : -1;
}

int pav_db_bind_null(sqlite3_stmt *stmt, int index) {
    if (stmt == NULL) {
        return -1;
    }
    return sqlite3_bind_null(stmt, index) == SQLITE_OK ? 0 : -1;
}

int pav_db_step(sqlite3_stmt *stmt) {
    if (stmt == NULL) {
        return -1;
    }
    return sqlite3_step(stmt);
}

int pav_db_exec(sqlite3 *db, int num_args, const char *sql, ...) {
    (void)num_args;
    if (db == NULL || sql == NULL || num_args != 0 || has_format(sql)) {
        return -1;
    }
    if (sqlite3_db_readonly(db, "main") == 1 && !is_select(sql)) {
        return -1;
    }
    return sqlite3_exec(db, sql, NULL, NULL, NULL) == SQLITE_OK ? 0 : -1;
}

int pav_db_tx_begin(sqlite3 *db) {
    if (db == NULL) {
        return -1;
    }
    return sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, NULL) == SQLITE_OK ? 0 : -1;
}

int pav_db_tx_commit(sqlite3 *db) {
    if (db == NULL) {
        return -1;
    }
    return sqlite3_exec(db, "COMMIT", NULL, NULL, NULL) == SQLITE_OK ? 0 : -1;
}

int pav_db_tx_rollback(sqlite3 *db) {
    if (db == NULL) {
        return -1;
    }
    return sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL) == SQLITE_OK ? 0 : -1;
}

void pav_db_cache_reset(sqlite3 *db) {
    StmtNode *node;

    pthread_mutex_lock(&g_cache_mu);
    for (node = g_cache; node != NULL; node = node->next) {
        if (db == NULL || node->db == db) {
            sqlite3_reset(node->stmt);
            sqlite3_clear_bindings(node->stmt);
        }
    }
    pthread_mutex_unlock(&g_cache_mu);
}

void pav_db_cache_clear(sqlite3 *db) {
    StmtNode *prev = NULL;
    StmtNode *node;

    pthread_mutex_lock(&g_cache_mu);
    node = g_cache;
    while (node != NULL) {
        StmtNode *next = node->next;

        if (db == NULL || node->db == db) {
            sqlite3_finalize(node->stmt);
            if (prev == NULL) {
                g_cache = next;
            } else {
                prev->next = next;
            }
            free(node);
            node = next;
            continue;
        }
        prev = node;
        node = next;
    }
    pthread_mutex_unlock(&g_cache_mu);
}

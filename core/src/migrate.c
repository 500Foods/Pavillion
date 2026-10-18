#include "pav_db.h"

#include "migrations_embed.h"
#include "pav_platform.h"

#include <stdio.h>
#include <string.h>

static void set_err(char *err, size_t err_len, const char *msg) {
    if (err == NULL || err_len == 0 || msg == NULL) {
        return;
    }
    snprintf(err, err_len, "%s", msg);
}

static void private_sidecars(const char *path) {
    char side[4096];
    int n;

    if (path == NULL) {
        return;
    }
    pav_file_private(path);
    n = snprintf(side, sizeof(side), "%s-wal", path);
    if (n > 0 && (size_t)n < sizeof(side) && pav_exists(side)) {
        pav_file_private(side);
    }
    n = snprintf(side, sizeof(side), "%s-shm", path);
    if (n > 0 && (size_t)n < sizeof(side) && pav_exists(side)) {
        pav_file_private(side);
    }
}

typedef struct MigrateJob {
    char *err;
    size_t err_len;
} MigrateJob;

static int read_version(sqlite3 *db, int *out) {
    sqlite3_stmt *stmt = NULL;
    int rc;

    *out = -1;
    if (pav_db_prepare(db, "user_version", "PRAGMA user_version", &stmt) != 0) {
        return -1;
    }
    rc = pav_db_step(stmt);
    if (rc != SQLITE_ROW) {
        return -1;
    }
    *out = sqlite3_column_int(stmt, 0);
    return 0;
}

static int migrate_job(void *ctx, sqlite3 *db) {
    MigrateJob *job = ctx;
    const PavMigration *table;
    int count = 0;
    int current = 0;
    int i;
    int binary;

    if (read_version(db, &current) != 0) {
        set_err(job->err, job->err_len, "cannot read user_version");
        return -1;
    }
    table = pav_migration_table(&count);
    binary = count > 0 ? table[count - 1].version : 0;
    if (current > binary) {
        if (job->err != NULL && job->err_len > 0) {
            snprintf(job->err, job->err_len, "database version %d is newer than binary %d", current, binary);
        }
        return -1;
    }
    for (i = 0; i < count; i++) {
        if (table[i].version <= current) {
            continue;
        }
        if (sqlite3_exec(db, table[i].sql, NULL, NULL, NULL) != SQLITE_OK) {
            set_err(job->err, job->err_len, sqlite3_errmsg(db));
            return -1;
        }
        if (sqlite3_exec(db, table[i].set_version, NULL, NULL, NULL) != SQLITE_OK) {
            set_err(job->err, job->err_len, sqlite3_errmsg(db));
            return -1;
        }
        current = table[i].version;
    }
    return 0;
}

int pav_db_migrate(sqlite3 *db, const char *path, char *err, size_t err_len) {
    MigrateJob job;

    if (err != NULL && err_len > 0) {
        err[0] = '\0';
    }
    if (db == NULL || path == NULL) {
        set_err(err, err_len, "missing store");
        return -1;
    }
    if (pav_db_writer_start(db) != 0) {
        set_err(err, err_len, "cannot start store writer");
        return -1;
    }
    memset(&job, 0, sizeof(job));
    job.err = err;
    job.err_len = err_len;
    if (pav_db_call(migrate_job, &job) != 0) {
        if (err == NULL || err_len == 0 || err[0] == '\0') {
            set_err(err, err_len, "cannot migrate");
        }
        return -1;
    }
    if (pav_db_readpool_open(path) != 0) {
        set_err(err, err_len, "cannot open read pool");
        return -1;
    }
    private_sidecars(path);
    return 0;
}

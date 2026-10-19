#include "pav_logdb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "migrations_embed.h"
#include "pav_platform.h"

static void set_err(char *err, size_t err_len, const char *msg) {
    if (err != NULL && err_len > 0) {
        snprintf(err, err_len, "%s", msg);
    }
}

static int exec_sql(sqlite3 *db, const char *sql) {
    return sqlite3_exec(db, sql, NULL, NULL, NULL) == SQLITE_OK ? 0 : -1;
}

static int query_int64(sqlite3 *db, const char *sql, int64_t *out) {
    sqlite3_stmt *stmt = NULL;
    int rc = -1;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        *out = sqlite3_column_type(stmt, 0) == SQLITE_NULL ? 0 : sqlite3_column_int64(stmt, 0);
        rc = 0;
    }
    sqlite3_finalize(stmt);
    return rc;
}

static int migrate(sqlite3 *db, char *err, size_t err_len) {
    const PavMigration *table;
    int count = 0;
    int64_t current = 0;
    int binary;
    int i;

    table = pav_log_migration_table(&count);
    binary = count > 0 ? table[count - 1].version : 0;
    if (query_int64(db, "PRAGMA user_version", &current) != 0) {
        set_err(err, err_len, "cannot read log.db version");
        return -1;
    }
    if (current > binary) {
        if (err != NULL && err_len > 0) {
            snprintf(err, err_len, "log.db version %lld is newer than binary %d", (long long)current, binary);
        }
        return -1;
    }
    for (i = 0; i < count; i++) {
        if (table[i].version <= current) {
            continue;
        }
        if (exec_sql(db, "BEGIN") != 0 || exec_sql(db, table[i].sql) != 0 ||
            exec_sql(db, table[i].set_version) != 0 || exec_sql(db, "COMMIT") != 0) {
            (void)exec_sql(db, "ROLLBACK");
            set_err(err, err_len, "cannot migrate log.db");
            return -1;
        }
    }
    return 0;
}

int pav_logdb_open(const char *path, sqlite3 **out, int64_t *session, int64_t *max_seq, char *err,
                   size_t err_len) {
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;

    if (path == NULL || path[0] == '\0' || out == NULL || session == NULL || max_seq == NULL) {
        set_err(err, err_len, "missing log.db path");
        return -1;
    }
    *out = NULL;
    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, NULL) !=
        SQLITE_OK) {
        sqlite3_close(db);
        set_err(err, err_len, "cannot open log.db");
        return -1;
    }
    (void)pav_file_private(path);
    sqlite3_busy_timeout(db, 1000);
    /* auto_vacuum takes effect only before the first table exists. */
    if (exec_sql(db, "PRAGMA auto_vacuum=INCREMENTAL") != 0 || exec_sql(db, "PRAGMA journal_mode=WAL") != 0 ||
        exec_sql(db, "PRAGMA synchronous=NORMAL") != 0) {
        sqlite3_close(db);
        set_err(err, err_len, "cannot configure log.db");
        return -1;
    }
    if (migrate(db, err, err_len) != 0) {
        sqlite3_close(db);
        return -1;
    }
    if (query_int64(db, "SELECT max(seq) FROM log", max_seq) != 0 ||
        sqlite3_prepare_v2(db, "INSERT INTO session (started, source) VALUES (?, 'run')", -1, &stmt, NULL) !=
            SQLITE_OK) {
        sqlite3_close(db);
        set_err(err, err_len, "cannot read log.db");
        return -1;
    }
    sqlite3_bind_int64(stmt, 1, pav_now_ms());
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        set_err(err, err_len, "cannot start a log session");
        return -1;
    }
    sqlite3_finalize(stmt);
    *session = sqlite3_last_insert_rowid(db);
    {
        char wal[4200];

        if (snprintf(wal, sizeof(wal), "%s-wal", path) < (int)sizeof(wal)) {
            (void)pav_file_private(wal);
        }
        if (snprintf(wal, sizeof(wal), "%s-shm", path) < (int)sizeof(wal)) {
            (void)pav_file_private(wal);
        }
    }
    *out = db;
    return 0;
}

int pav_logdb_insert(sqlite3 *db, int64_t session, const PavLogRow *rows, size_t count) {
    sqlite3_stmt *stmt = NULL;
    size_t i;
    int rc = 0;

    if (db == NULL || (rows == NULL && count > 0)) {
        return -1;
    }
    if (count == 0) {
        return 0;
    }
    if (exec_sql(db, "BEGIN") != 0) {
        return -1;
    }
    if (sqlite3_prepare_v2(db,
                           "INSERT OR REPLACE INTO log (seq, ts, session, level, subsystem, text) "
                           "VALUES (?, ?, ?, ?, ?, ?)",
                           -1, &stmt, NULL) != SQLITE_OK) {
        (void)exec_sql(db, "ROLLBACK");
        return -1;
    }
    for (i = 0; i < count && rc == 0; i++) {
        sqlite3_bind_int64(stmt, 1, rows[i].seq);
        sqlite3_bind_int64(stmt, 2, rows[i].ts);
        sqlite3_bind_int64(stmt, 3, session);
        sqlite3_bind_int(stmt, 4, rows[i].level);
        sqlite3_bind_text(stmt, 5, rows[i].subsystem != NULL ? rows[i].subsystem : "log", -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 6, rows[i].text != NULL ? rows[i].text : "", -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            rc = -1;
        }
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }
    sqlite3_finalize(stmt);
    if (rc != 0 || exec_sql(db, "COMMIT") != 0) {
        (void)exec_sql(db, "ROLLBACK");
        return -1;
    }
    return 0;
}

int pav_logdb_stats(sqlite3 *db, PavLogStats *out) {
    int64_t pages = 0;
    int64_t free_pages = 0;
    int64_t page_size = 0;
    sqlite3_stmt *stmt = NULL;

    if (db == NULL || out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (query_int64(db, "PRAGMA page_count", &pages) != 0 ||
        query_int64(db, "PRAGMA freelist_count", &free_pages) != 0 ||
        query_int64(db, "PRAGMA page_size", &page_size) != 0 ||
        query_int64(db, "SELECT count(*) FROM log", &out->records) != 0) {
        return -1;
    }
    out->bytes = (pages - free_pages) * page_size;
    if (sqlite3_prepare_v2(db, "SELECT seq, ts FROM log ORDER BY seq LIMIT 1", -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out->oldest_seq = sqlite3_column_int64(stmt, 0);
        out->oldest_ts = sqlite3_column_int64(stmt, 1);
    }
    sqlite3_finalize(stmt);
    return 0;
}

int pav_logdb_trim(sqlite3 *db, int64_t cap_bytes, int64_t keep_session, PavLogStats *out) {
    PavLogStats stats;
    sqlite3_stmt *stmt = NULL;

    if (db == NULL || cap_bytes <= 0) {
        return -1;
    }
    if (pav_logdb_stats(db, &stats) != 0) {
        return -1;
    }
    if (sqlite3_prepare_v2(db, "DELETE FROM log WHERE seq IN (SELECT seq FROM log ORDER BY seq LIMIT ?)", -1,
                           &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    /* Chunks of at most 1,000 rows, sized from the average row so the
       result lands near the cap instead of up to a whole chunk under it. */
    while (stats.bytes > cap_bytes && stats.records > 0) {
        int64_t avg = stats.bytes / stats.records;
        int64_t chunk = avg > 0 ? (stats.bytes - cap_bytes) / avg + 1 : 1000;

        if (chunk > 1000) {
            chunk = 1000;
        }
        sqlite3_bind_int64(stmt, 1, chunk);
        if (sqlite3_step(stmt) != SQLITE_DONE || sqlite3_reset(stmt) != SQLITE_OK ||
            pav_logdb_stats(db, &stats) != 0) {
            sqlite3_finalize(stmt);
            return -1;
        }
    }
    sqlite3_finalize(stmt);
    stmt = NULL;
    if (sqlite3_prepare_v2(db,
                           "DELETE FROM session WHERE id <> ? AND NOT EXISTS "
                           "(SELECT 1 FROM log WHERE log.session = session.id)",
                           -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_int64(stmt, 1, keep_session);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return -1;
    }
    sqlite3_finalize(stmt);
    if (exec_sql(db, "PRAGMA incremental_vacuum") != 0) {
        return -1;
    }
    if (out != NULL) {
        return pav_logdb_stats(db, out);
    }
    return 0;
}

int pav_logdb_trim_preview(sqlite3 *db, int64_t cap_bytes, PavLogStats *out) {
    PavLogStats now;
    sqlite3_stmt *stmt = NULL;
    int64_t total_est = 0;
    double scale;
    double kept = 0;

    if (db == NULL || out == NULL || cap_bytes <= 0 || pav_logdb_stats(db, &now) != 0) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (now.bytes <= cap_bytes || now.records == 0) {
        *out = now;
        return 0;
    }
    if (query_int64(db, "SELECT sum(length(text) + length(subsystem) + 24) FROM log", &total_est) != 0 ||
        total_est <= 0) {
        return -1;
    }
    scale = (double)now.bytes / (double)total_est;
    if (sqlite3_prepare_v2(db, "SELECT seq, ts, length(text) + length(subsystem) + 24 FROM log ORDER BY seq DESC",
                           -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        double row = (double)sqlite3_column_int64(stmt, 2) * scale;

        if (kept + row > (double)cap_bytes) {
            break;
        }
        kept += row;
        out->records++;
        out->oldest_seq = sqlite3_column_int64(stmt, 0);
        out->oldest_ts = sqlite3_column_int64(stmt, 1);
    }
    sqlite3_finalize(stmt);
    out->bytes = (int64_t)kept;
    return 0;
}

void pav_logdb_close(sqlite3 *db) {
    if (db == NULL) {
        return;
    }
    (void)exec_sql(db, "PRAGMA wal_checkpoint(TRUNCATE)");
    sqlite3_close(db);
}

static const char *row_level_name(int level) {
    static const char *const names[] = {"ERROR", "WARN", "INFO", "DEBUG", "TRACE"};

    return level >= 0 && level <= 4 ? names[level] : "?????";
}

int pav_logdb_walk(sqlite3 *db, const PavLogQuery *q, PavLogLineFn fn, void *ctx) {
    char sql[512];
    char levels[32];
    sqlite3_stmt *stmt = NULL;
    size_t off = 0;
    int bind = 1;
    int rc = 0;
    int i;

    if (db == NULL || q == NULL || fn == NULL) {
        return -1;
    }
    levels[0] = '\0';
    for (i = 0; i <= 4; i++) {
        if (q->level_mask & (1u << i)) {
            off += (size_t)snprintf(levels + off, sizeof(levels) - off, "%s%d", off > 0 ? "," : "", i);
        }
    }
    if (off == 0) {
        return 0;
    }
    snprintf(sql, sizeof(sql),
             "SELECT seq, ts, level, subsystem, text, session FROM log WHERE level IN (%s)%s%s%s%s ORDER BY seq DESC", levels,
             q->before_seq > 0 ? " AND seq < ?" : "", q->has_since ? " AND ts >= ?" : "",
             q->has_until ? " AND ts < ?" : "", q->session > 0 ? " AND session = ?" : "");
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    if (q->before_seq > 0) {
        sqlite3_bind_int64(stmt, bind++, q->before_seq);
    }
    if (q->has_since) {
        sqlite3_bind_int64(stmt, bind++, q->since);
    }
    if (q->has_until) {
        sqlite3_bind_int64(stmt, bind++, q->until);
    }
    if (q->session > 0) {
        sqlite3_bind_int64(stmt, bind++, q->session);
    }
    while (rc == 0) {
        int step = sqlite3_step(stmt);
        char stamp[32];
        char *line;
        const char *sub;
        const char *text;
        const char *name;
        int n;

        if (step == SQLITE_DONE) {
            break;
        }
        if (step != SQLITE_ROW) {
            rc = -1;
            break;
        }
        if (pav_format_utc(stamp, sizeof(stamp), sqlite3_column_int64(stmt, 1)) != 0) {
            snprintf(stamp, sizeof(stamp), "0000-00-00T00:00:00.000Z");
        }
        name = row_level_name(sqlite3_column_int(stmt, 2));
        sub = (const char *)sqlite3_column_text(stmt, 3);
        text = (const char *)sqlite3_column_text(stmt, 4);
        if (sub == NULL) {
            sub = "";
        }
        if (text == NULL) {
            text = "";
        }
        n = snprintf(NULL, 0, "%s %-5s %s  %s\n", stamp, name, sub, text);
        line = n >= 0 ? malloc((size_t)n + 1) : NULL;
        if (line == NULL) {
            rc = -1;
            break;
        }
        snprintf(line, (size_t)n + 1, "%s %-5s %s  %s\n", stamp, name, sub, text);
        rc = fn(ctx, sqlite3_column_int64(stmt, 0), sqlite3_column_int64(stmt, 5), line);
        free(line);
    }
    sqlite3_finalize(stmt);
    return rc < 0 ? -1 : 0;
}

int pav_logdb_previous_session(sqlite3 *db, int64_t current, int64_t *out) {
    sqlite3_stmt *stmt = NULL;

    if (db == NULL || out == NULL) {
        return -1;
    }
    *out = 0;
    if (sqlite3_prepare_v2(db, "SELECT max(id) FROM session WHERE id < ?", -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_int64(stmt, 1, current);
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
        *out = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return 0;
}

int pav_logdb_session_started(sqlite3 *db, int64_t id, int64_t *started) {
    sqlite3_stmt *stmt = NULL;
    int rc = -1;

    if (db == NULL || started == NULL ||
        sqlite3_prepare_v2(db, "SELECT started FROM session WHERE id = ?", -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_int64(stmt, 1, id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        *started = sqlite3_column_int64(stmt, 0);
        rc = 0;
    }
    sqlite3_finalize(stmt);
    return rc;
}

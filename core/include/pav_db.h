#ifndef PAV_DB_H
#define PAV_DB_H

#include <sqlite3.h>

#include <stddef.h>
#include <stdint.h>

typedef struct PavDbRead PavDbRead;

typedef int (*PavDbJob)(void *ctx, sqlite3 *db);
typedef void (*PavDbDone)(void *done_ctx, int rc);

int pav_db_writer_start(sqlite3 *db);
int pav_db_writer_running(void);
int pav_db_writer_shutdown(sqlite3 *db);

int pav_db_write(PavDbJob job, void *ctx, PavDbDone on_done, void *done_ctx);
int pav_db_call(PavDbJob job, void *ctx);
int pav_db_drain(void);

void pav_db_writer_hold(int hold);
int pav_db_writer_batch(void);
int pav_db_writer_stack(size_t *out);
int pav_db_busy_checkpoints(void);

int pav_db_prepare(sqlite3 *db, const char *key, const char *sql, sqlite3_stmt **out);
int pav_db_bind_text(sqlite3_stmt *stmt, int index, const char *text);
int pav_db_bind_int64(sqlite3_stmt *stmt, int index, int64_t value);
int pav_db_bind_int(sqlite3_stmt *stmt, int index, int value);
int pav_db_bind_null(sqlite3_stmt *stmt, int index);
int pav_db_step(sqlite3_stmt *stmt);
int pav_db_exec(sqlite3 *db, int num_args, const char *sql, ...);
int pav_db_tx_begin(sqlite3 *db);
int pav_db_tx_commit(sqlite3 *db);
int pav_db_tx_rollback(sqlite3 *db);
void pav_db_cache_clear(sqlite3 *db);

int pav_db_readpool_open(const char *path);
void pav_db_readpool_close(void);
int pav_db_read_begin(PavDbRead **out);
void pav_db_read_end(PavDbRead *read);
sqlite3 *pav_db_read_handle(PavDbRead *read);

int pav_db_migrate(sqlite3 *db, const char *path, char *err, size_t err_len);

#endif

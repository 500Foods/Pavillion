#ifndef PAV_STORE_H
#define PAV_STORE_H

#include <stddef.h>

typedef struct PavStore PavStore;

typedef struct PavStoreVtable {
    int (*open)(PavStore *store, const char *path_or_conn, char *err, size_t err_len);
    void (*close)(PavStore *store);
    int (*exec)(PavStore *store, const char *sql, char *err, size_t err_len);
    int (*migrate)(PavStore *store, char *err, size_t err_len);
} PavStoreVtable;

struct PavStore {
    const PavStoreVtable *vt;
    void *handle;
    char *path;
};

/* Opens path with the named engine. sqlite creates the file mode 0600 and
   does not create mail tables. Any other engine fails and the error names it.
   Returns 0, or -1 and err. */
int pav_store_open(PavStore **out, const char *engine, const char *path, char *err, size_t err_len);

/* Runs sql. Returns 0, or -1 and err. Does not return rows. */
int pav_store_exec(PavStore *store, const char *sql, char *err, size_t err_len);

/* Closes the database and frees store. The file stays on disk. NULL is safe. */
void pav_store_close(PavStore *store);

/* The engine handle. SQLite returns sqlite3 * before migrate. NULL when
   closed. After migrate the writer owns that connection; this still returns
   it so close can release it. Callers do not step it. */
void *pav_store_handle(const PavStore *store);

/* Applies PRAGMAs, runs embedded migrations, and opens the read pool.
   Failure leaves the store open and the caller publishes state error. */
int pav_store_migrate(PavStore *store, char *err, size_t err_len);

#endif

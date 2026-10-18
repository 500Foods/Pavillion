#define _POSIX_C_SOURCE 200809L

#include "pav_store.h"

#include "pav_db.h"
#include "pav_platform.h"

#include <sqlite3.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_err(char *err, size_t err_len, const char *fmt, ...) {
    va_list ap;

    if (err == NULL || err_len == 0) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(err, err_len, fmt, ap);
    va_end(ap);
}

static int sqlite_open(PavStore *store, const char *path, char *err, size_t err_len) {
    sqlite3 *db = NULL;
    int existed;
    int rc;

    if (path == NULL || path[0] == '\0') {
        set_err(err, err_len, "missing path");
        return -1;
    }
    existed = pav_exists(path);
    rc = sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    if (rc != SQLITE_OK) {
        const char *msg = db != NULL ? sqlite3_errmsg(db) : "cannot open database";
        set_err(err, err_len, "%s", msg);
        if (db != NULL) {
            sqlite3_close(db);
        }
        if (!existed) {
            remove(path);
        }
        return -1;
    }
    if (pav_file_private(path) != 0) {
        set_err(err, err_len, "cannot create %s", path);
        sqlite3_close(db);
        if (!existed) {
            remove(path);
        }
        return -1;
    }
    store->path = strdup(path);
    if (store->path == NULL) {
        set_err(err, err_len, "out of memory");
        sqlite3_close(db);
        if (!existed) {
            remove(path);
        }
        return -1;
    }
    store->handle = db;
    return 0;
}

static void sqlite_close(PavStore *store) {
    if (pav_db_writer_shutdown(store->handle)) {
        pav_db_readpool_close();
    }
    if (store->handle != NULL) {
        pav_db_cache_clear(store->handle);
        sqlite3_close(store->handle);
        store->handle = NULL;
    }
    free(store->path);
    store->path = NULL;
}

static int sqlite_migrate(PavStore *store, char *err, size_t err_len) {
    if (store->handle == NULL || store->path == NULL) {
        set_err(err, err_len, "store is closed");
        return -1;
    }
    return pav_db_migrate(store->handle, store->path, err, err_len);
}

static int sqlite_exec(PavStore *store, const char *sql, char *err, size_t err_len) {
    sqlite3 *db = store->handle;
    char *errmsg = NULL;
    int rc;

    if (db == NULL) {
        set_err(err, err_len, "store is closed");
        return -1;
    }
    if (sql == NULL) {
        set_err(err, err_len, "missing sql");
        return -1;
    }
    rc = sqlite3_exec(db, sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        set_err(err, err_len, "%s", errmsg != NULL ? errmsg : sqlite3_errmsg(db));
        sqlite3_free(errmsg);
        return -1;
    }
    return 0;
}

static const PavStoreVtable g_sqlite_vtable = {
    sqlite_open,
    sqlite_close,
    sqlite_exec,
    sqlite_migrate,
};

int pav_store_open(PavStore **out, const char *engine, const char *path, char *err, size_t err_len) {
    PavStore *store;

    if (out == NULL) {
        set_err(err, err_len, "missing store");
        return -1;
    }
    *out = NULL;
    if (engine == NULL || strcmp(engine, "sqlite") != 0) {
        set_err(err, err_len, "unknown engine %s", engine != NULL ? engine : "");
        return -1;
    }
    store = calloc(1, sizeof(*store));
    if (store == NULL) {
        set_err(err, err_len, "out of memory");
        return -1;
    }
    store->vt = &g_sqlite_vtable;
    if (store->vt->open(store, path, err, err_len) != 0) {
        free(store);
        return -1;
    }
    *out = store;
    return 0;
}

int pav_store_exec(PavStore *store, const char *sql, char *err, size_t err_len) {
    if (store == NULL || store->vt == NULL || store->vt->exec == NULL) {
        set_err(err, err_len, "missing store");
        return -1;
    }
    return store->vt->exec(store, sql, err, err_len);
}

void pav_store_close(PavStore *store) {
    if (store == NULL) {
        return;
    }
    if (store->vt != NULL && store->vt->close != NULL) {
        store->vt->close(store);
    }
    free(store);
}

void *pav_store_handle(const PavStore *store) {
    if (store == NULL) {
        return NULL;
    }
    return store->handle;
}

int pav_store_migrate(PavStore *store, char *err, size_t err_len) {
    if (store == NULL || store->vt == NULL || store->vt->migrate == NULL) {
        set_err(err, err_len, "missing store");
        return -1;
    }
    return store->vt->migrate(store, err, err_len);
}

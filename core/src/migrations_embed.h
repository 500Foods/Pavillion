#ifndef PAV_MIGRATIONS_EMBED_H
#define PAV_MIGRATIONS_EMBED_H

typedef struct PavMigration {
    int version;
    const char *name;
    const char *sql;
    const char *set_version;
} PavMigration;

const PavMigration *pav_migration_table(int *count);

#endif

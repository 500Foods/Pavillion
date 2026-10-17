# L — STORE

**Status:** Not started (scoped 2026-10-16 from plan §4).
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

The SQLite schema, migrations, the statement helpers, the blob and cache directories, and their eviction. The open and close are [C-CORE.md](C-CORE.md) C-4. Account configuration is the profile ([B-CONFIG.md](B-CONFIG.md)); this store holds what sync learns and what the user does with mail.

## Phases

One phase is one session. Do not start the next one in the same conversation.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| L-1 | Migration runner, statement helpers, schema v1 (accounts, mailboxes, identities, sync state) | Medium | Not started |
| L-2 | Schema v2: mail (email, threads, bodies, attachments, FTS, queue, outbox, drafts) and the blob store | Medium | Not started |
| L-3 | PIM migration: contacts, calendars, files, rules and script runs (CLI tables are V-2) | Medium | Not started |

Kind: **Easy** is one module and a test. **Medium** is the database driver or the filesystem.

## How a session works

1. Read Resume in [PAVILLION_PLAN.md](../PAVILLION_PLAN.md), the Status column above, and the previous phase's Status, Accomplished, Lessons learned, and Handoff.
2. Re-read only this phase: Goal, Entry gate, Work items, Done means, Exit gate. Open the lock section this phase cites.
3. Ask the qualifying questions. Wait for approval before editing source.
4. Do this phase only. Check a work item `[x]` only after its check has passed. Defer with `[~]`, one line, and the phase it moves to.
5. Before marking complete, write Status, Accomplished, Lessons learned, and Handoff, update the Status column here and in the index, and append the Working log. Before that, update `ui/src/help/pages.js` for what this phase changed: augment, replace, fix, or extend the user page, or add a Developer section when the user page has nothing new. Then stop.
6. A sitting that ends early sets Status to **in progress**, names the last finished work item, and leaves the rest unchecked.

Leave Accomplished, Lessons learned, and Handoff off a phase that has not started.

## Lock

- PRAGMAs on every open: `journal_mode=WAL`, `foreign_keys=ON`, `synchronous=NORMAL`, `busy_timeout=5000`, `cache_size=-16384` (16 MB), `mmap_size=67108864` (64 MB), `temp_store=MEMORY`, `wal_autocheckpoint=1000`. The cache is large enough that a folder listing stays hot and small enough that it does not grow with the number of accounts. Schema version is `PRAGMA user_version`. Migrations are hand-written SQL in `core/src/migrations/NNN_name.sql`, compiled into the binary (CMake turns each into a C string), applied in order inside one transaction each. A database newer than the binary is state `error` naming both versions. Never edit a shipped migration; add one.
- I-1 creates table `activity` in this file before `user_version` exists. The statement is in the I Lock. It does not set `user_version`. L-1's first migration repeats that `CREATE TABLE IF NOT EXISTS` and keeps the rows. From L-1 on, those writes go through `pav_db_write`, and I-1's direct use of the open connection stops.
- **One writer.** A single **store writer thread** owns the only read-write connection. Every mutation goes through its serial write queue, whoever makes it: sync, rules, the dispatcher (UI commands), or the CLI (which arrives through the dispatcher).
  - A job is `pav_db_write(job_fn, ctx, on_done)`. The writer runs it in one transaction. Consecutive queued jobs are batched into one transaction, up to 50 jobs or 20 ms. A failed job rolls back its batch, and the batch is retried one job at a time, so one bad job fails alone. `on_done` posts to the core queue.
  - Nothing else ever runs `INSERT`, `UPDATE`, `DELETE`, or DDL. `pav_db_prepare` on a read connection refuses a non-`SELECT` statement.
- **Readers** use a small read pool: 2 read-only connections (`SQLITE_OPEN_READONLY`, WAL). Command reads (lists, gets, search) borrow a connection and return it before the command returns. A read transaction never spans a command, a rule, or a sync page.
  - WAL lets readers run alongside the single writer.
  - A `SQLITE_BUSY` on the writer means a reader is still inside a transaction during a checkpoint. The writer runs `PRAGMA wal_checkpoint(PASSIVE)` and retries that job once. A second `SQLITE_BUSY` is logged at `error` and the job fails. There is no retry loop beyond that one.
  - Migrations run on the writer before the pool opens.
- This supersedes the C-CORE rule that the core thread holds the only `sqlite3 *`. The core thread now submits writes and borrows readers. It never holds a write connection.
- All times are UTC unix milliseconds (`INTEGER`). Formatting to `America/Vancouver` (or the profile timezone) and 24 h happens in the UI.
- Ids: server ids are kept as-is with their `ra_id` (remote account). A local row id (`rowid INTEGER PRIMARY KEY`) is used for joins and FTS. The UI sees a composite id `<raId>/<serverId>` as a string.
- Account configuration (name, URLs, username, auth, color, flags, number) lives in the profile `accounts` array. The store's `account_state` holds only runtime state keyed by the same id. This replaces plan §4's `account` table, which duplicated the profile.
- Message bodies, raw `.eml`, and attachments are files under `blobs/<profile>/`, mode 0600 in 0700 directories. The path is `blobs/<profile>/<ra>/<hh>/<encoded>`, where `encoded` is the blob id in base64url with no padding. A blob id is never used as a path segment raw: `/`, `..`, and any other byte outside the unreserved set cannot escape the shard. `body_cache` stores paths only (`text_plain_path`, `html_raw_path`, `html_sanitized_path`). Plain text and sanitized HTML are files under the same budget as bodies. A `text_plain` or `html_sanitized` column is not added. FTS reads the plain-text file, capped at 256 KB per message (Q Lock).
- Cache limits: `preferences.cache.bodiesMb` (default 2,048) and `preferences.cache.blobsMb` (default 4,096). An eviction pass (least recently opened first, never drafts, outbox, or flagged) runs after sync when over the limit, registered as an activity. Evicted bodies refetch on open. This is a core-interpreted preference: it lives at `preferences.cache`, outside the opaque `preferences.ui` blob (F Lock, Preferences split). L-2 adds it to the B Lock.
- Opened attachments are copied to `$XDG_RUNTIME_DIR/pavillion/open/` (via `PAV_DIR_RUNTIME`) and that directory is removed on exit.
- FTS5 tables are contentless (`content=''`), keyed by the row id of the source table. A header-only row is inserted on sync; when the body is fetched the row is deleted and reinserted with the body. Rebuild is a command (`index.rebuild`) and an activity.
- Statement helpers: `pav_db_prepare(cached key, sql)`, `pav_db_bind_*`, `pav_db_step_row`, a statement cache keyed by a static string, `pav_db_tx_begin/commit/rollback`. No string-built SQL with values; always bind.

### Schema v1 (L-1)

```sql
CREATE TABLE account_state (account_id TEXT PRIMARY KEY, session_json TEXT, session_state TEXT,
  session_fetched INT, state TEXT NOT NULL DEFAULT 'idle', last_error TEXT, last_sync INT);
CREATE TABLE remote_account (id TEXT PRIMARY KEY, account_id TEXT NOT NULL REFERENCES account_state ON DELETE CASCADE,
  jmap_account_id TEXT NOT NULL, name TEXT, is_personal INT, caps TEXT, UNIQUE (account_id, jmap_account_id));
CREATE TABLE identity (ra_id TEXT REFERENCES remote_account ON DELETE CASCADE, id TEXT,
  name TEXT, email TEXT, reply_to TEXT, bcc TEXT, sig_html TEXT, sig_text TEXT, may_delete INT,
  PRIMARY KEY (ra_id, id));
CREATE TABLE mailbox (ra_id TEXT REFERENCES remote_account ON DELETE CASCADE, id TEXT,
  parent_id TEXT, name TEXT, role TEXT, sort_order INT, total INT, unread INT,
  total_threads INT, unread_threads INT, my_rights TEXT, is_subscribed INT,
  PRIMARY KEY (ra_id, id));
CREATE TABLE sync_state (ra_id TEXT REFERENCES remote_account ON DELETE CASCADE, type TEXT, state TEXT, updated INT,
  PRIMARY KEY (ra_id, type));  -- Mailbox|Email|Thread|Identity|EmailSubmission|AddressBook|ContactCard|Calendar|CalendarEvent|FileNode
CREATE TABLE kv (key TEXT PRIMARY KEY, value TEXT);  -- core-local state that is not a preference
```

### Schema v2 (L-2)

```sql
CREATE TABLE email (rowid INTEGER PRIMARY KEY, ra_id TEXT NOT NULL REFERENCES remote_account ON DELETE CASCADE, id TEXT NOT NULL,
  thread_id TEXT, blob_id TEXT, message_id TEXT, in_reply_to TEXT, refs TEXT,
  subject TEXT, from_json TEXT, to_json TEXT, cc_json TEXT, bcc_json TEXT, reply_to_json TEXT,
  sent_at INT, received_at INT, size INT, preview TEXT, has_attachment INT,
  seen INT, flagged INT, answered INT, forwarded INT, draft INT, junk INT, keywords TEXT, -- keywords is the source of truth; the six ints cache $seen $flagged $answered $forwarded $draft $junk and are written only by pav_email_set_keywords, in the same statement as keywords
  body_state TEXT NOT NULL DEFAULT 'none',  -- none|fetched|failed|evicted
  last_opened INT, local_marks TEXT,        -- local_marks: JSON, e.g. {"bot":"name"} (V Lock), never sent to the server
  UNIQUE (ra_id, id));
CREATE INDEX email_thread ON email(ra_id, thread_id);
CREATE INDEX email_recv ON email(received_at DESC);
CREATE TABLE email_mailbox (
  email_rowid INT NOT NULL REFERENCES email ON DELETE CASCADE,
  ra_id TEXT NOT NULL,
  mailbox_id TEXT NOT NULL,
  received_at INT NOT NULL,             -- copy of email.received_at, written in the same job, so a folder lists by date without sorting the account
  PRIMARY KEY (email_rowid, ra_id, mailbox_id),
  FOREIGN KEY (ra_id, mailbox_id) REFERENCES mailbox (ra_id, id) ON DELETE CASCADE);
CREATE INDEX em_list ON email_mailbox(ra_id, mailbox_id, received_at DESC, email_rowid);
CREATE TABLE thread (ra_id TEXT, id TEXT, email_ids TEXT, PRIMARY KEY (ra_id, id));
CREATE TABLE body_cache (email_rowid INT PRIMARY KEY REFERENCES email ON DELETE CASCADE,
  text_plain_path TEXT, html_raw_path TEXT, html_sanitized_path TEXT,
  has_remote INT, sanitizer_version INT, fetched_at INT);
CREATE TABLE attachment (email_rowid INT REFERENCES email ON DELETE CASCADE, part_id TEXT,
  blob_id TEXT, name TEXT, mime TEXT, size INT, cid TEXT, disposition TEXT, local_path TEXT,
  PRIMARY KEY (email_rowid, part_id));
CREATE VIRTUAL TABLE email_fts USING fts5(subject, from_text, to_text, body,
  content='', tokenize='unicode61 remove_diacritics 2');
CREATE TABLE remote_allow (pattern TEXT PRIMARY KEY, kind TEXT NOT NULL);  -- address|domain
CREATE TABLE retention_rule (ra_id TEXT, mailbox_id TEXT, keep_newest INT, max_age_days INT,
  action TEXT NOT NULL DEFAULT 'trash', last_run INT, PRIMARY KEY (ra_id, mailbox_id));
CREATE TABLE op_queue (id INTEGER PRIMARY KEY, ra_id TEXT, kind TEXT, payload TEXT,
  base_state TEXT, created INT, not_before INT, attempts INT DEFAULT 0, last_error TEXT,
  status TEXT NOT NULL DEFAULT 'pending', undo_payload TEXT);  -- pending|held|sending|done|failed|dropped
CREATE TABLE outbox (id INTEGER PRIMARY KEY, ra_id TEXT, identity_id TEXT, draft_email_id TEXT,
  mime_path TEXT, envelope_json TEXT, send_at INT, status TEXT, attempts INT, last_error TEXT, created_by TEXT);
CREATE TABLE draft_local (id TEXT PRIMARY KEY, ra_id TEXT, json TEXT, server_email_id TEXT, updated INT, saved_server INT);
CREATE TABLE recent_address (email TEXT PRIMARY KEY, name TEXT, last_used INT, count INT);
```

### Schema v3 (L-3)

```sql
CREATE TABLE addressbook (ra_id TEXT, id TEXT, name TEXT, is_default INT, my_rights TEXT, PRIMARY KEY (ra_id, id));
CREATE TABLE contact (rowid INTEGER PRIMARY KEY, ra_id TEXT, id TEXT, book_ids TEXT, uid TEXT,
  full_name TEXT, sort_name TEXT, emails_json TEXT, card_json TEXT, updated INT, UNIQUE (ra_id, id));
CREATE VIRTUAL TABLE contact_fts USING fts5(full_name, emails, org, notes, content='');
CREATE TABLE calendar (ra_id TEXT, id TEXT, name TEXT, color TEXT, tz TEXT, is_visible INT,
  is_default INT, my_rights TEXT, PRIMARY KEY (ra_id, id));
CREATE TABLE event (rowid INTEGER PRIMARY KEY, ra_id TEXT, id TEXT, calendar_ids TEXT, uid TEXT,
  title TEXT, start_utc INT, end_utc INT, tz TEXT, all_day INT, rrule_json TEXT,
  overrides_json TEXT, alerts_json TEXT, raw_json TEXT, updated INT, UNIQUE (ra_id, id));
CREATE TABLE event_instance (event_rowid INT REFERENCES event ON DELETE CASCADE,
  start_utc INT, end_utc INT, PRIMARY KEY (event_rowid, start_utc));  -- rolling window -6/+18 months
CREATE TABLE reminder (event_rowid INT REFERENCES event ON DELETE CASCADE, fire_at INT, occurrence_start INT,
  snoozed_until INT, fired INT DEFAULT 0, PRIMARY KEY (event_rowid, occurrence_start, fire_at));
CREATE TABLE file_node (rowid INTEGER PRIMARY KEY, ra_id TEXT, id TEXT, parent_id TEXT, name TEXT,
  is_dir INT, size INT, mime TEXT, blob_id TEXT, etag TEXT, modified INT, UNIQUE (ra_id, id));
CREATE TABLE rule_run (id INTEGER PRIMARY KEY, rule_id TEXT, ra_id TEXT, email_id TEXT, started INT,
  dry_run INT, intents_json TEXT, op_ids TEXT, error TEXT);
-- bot_key and cli_audit are V-2's migration 003_cli.sql, which runs before this one. Do not create them here.
```

## L-1: Migration runner, helpers, schema v1

**Kind:** Medium
**Status:** Not started.

**Goal:** The store migrates itself on open and offers safe statement helpers.

**Entry gate:** J-6 complete.

**Work items:**

- [ ] `core/src/db.c`, `core/include/pav_db.h`: PRAGMAs, the statement cache, bind and step helpers, transactions.
- [ ] `core/src/migrate.c`: embedded migrations, `user_version` check, newer-than-binary error. CMake step embeds `core/src/migrations/*.sql`.
- [ ] `001_accounts.sql` with schema v1.
- [ ] `PavStoreVtable` gains `migrate`; C-6 calls it after open; failure is state `error`.
- [ ] `core/src/db_writer.c`: the store writer thread, the serial write queue, `pav_db_write`, batching (50 jobs or 20 ms), and a failing batch retried one job at a time. `core/src/db_readpool.c`: two read-only connections, with `pav_db_read_begin` and `pav_db_read_end`. Read connections refuse non-`SELECT` statements. Migrations run on the writer before the pool opens.
- [ ] Unity: `test_writes_are_serial_fifo`, `test_write_batch_one_transaction`, `test_failed_job_rolls_back_alone`, `test_read_conn_refuses_insert`, `test_concurrent_reads_during_write_no_busy` (4 reader threads plus a writer, 10,000 writes, zero `SQLITE_BUSY`), `test_writer_busy_retries_once`, `test_store_writer_stack_is_512kb`.
- [ ] Unity: `test_migrate_fresh_to_v1`, `test_migrate_is_idempotent`, `test_newer_db_is_error`, `test_db_bind_never_concatenates` (helper refuses a `%` format with args), `test_foreign_keys_on`.

**Done means:** A fresh profile opens at `user_version` 1 with the v1 tables, and a second start does nothing.

**Exit gate:** `make test` exits 0. `sqlite3 default.sqlite .schema` on a `make dev` workdir shows the v1 tables, and `PRAGMA user_version` is 1.

## L-2: Mail schema and the blob store

**Kind:** Medium
**Status:** Not started.

**Goal:** The mail tables and the blob directory exist with their helpers before sync writes email.

**Entry gate:** M-3 complete.

**Work items:**

- [ ] `002_mail.sql` with schema v2.
- [ ] `core/src/blobs.c`: `pav_blob_path(ra, blob_id)`, `pav_blob_write(ra, blob_id, buf, n)` (via `write_private`), `pav_blob_exists`, `pav_blob_remove`, sharded directories.
- [ ] Add `preferences.cache {bodiesMb, blobsMb}` (not under `preferences.ui`) to the B Lock and the profile defaults, and set it through `prefs.set`. The eviction pass `pav_cache_evict()` runs as activity kind `cache.evict` (I Lock).
- [ ] Runtime open directory creation and removal on stop.
- [ ] Unity: `test_migrate_v1_to_v2`, `test_blob_path_sharding`, `test_blob_id_is_base64url`, `test_blob_modes`, `test_evict_skips_flagged_and_drafts`, `test_evict_clears_body_paths`, `test_runtime_dir_removed_on_stop`.

**Done means:** The v2 schema is in place and blobs can be written, found, and evicted.

**Exit gate:** `make test` exits 0. A v1 database from L-1's `make dev` run migrates to v2 on the next launch.

## L-3: Contacts, calendars, files, rules, CLI tables

**Kind:** Medium
**Status:** Not started.

**Goal:** The remaining tables exist before their features start.

**Entry gate:** R-5 complete.

**Work items:**

- [ ] The PIM migration, numbered after V-2's `003_cli.sql` (so `004_pim.sql` if V-2 has landed, which it has by this phase). It does not create `bot_key` or `cli_audit`.
- [ ] Unity: `test_migrate_v2_to_v3`, `test_event_instance_cascade`, `test_contact_fts_contentless`.

**Done means:** The v3 schema is in place.

**Exit gate:** `make test` exits 0. A v2 database migrates.

## Open questions for Andrew

- L-Q1. Default cache limits (2 GB bodies, 4 GB attachments) for about 20 accounts: right order of magnitude?

## Working log

- 2026-10-16 Opened from plan §4. Decisions: profile is the source of account configuration and `account_state` replaces the `account` table; unix ms everywhere; embedded migrations; blobs sharded by blobId; cache limits and eviction; forwarded and junk columns; `local_marks` for bot markers; `rule_run`, `bot_key`, `cli_audit`, `reminder`, and `file_node` tables added.
- 2026-10-16 One store writer thread with a serial, batched write queue, plus a read pool of 2. A write path never sees `SQLITE_BUSY`. Added to L-1 with five tests.

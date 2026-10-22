# L — STORE

**Status:** L-1 complete (2026-10-18), step 42. The phase stamp at that close was `0.46`. `make test` was 81 Unity tests and 134 Vitest tests. L-2 is complete (2026-10-22), step 55. Andrew accepted the Limits layout the same day. CI on `abba579` (run 39797701301) is green for `fedora`, `macos-core`, and `windows-core`. The phase stamp is `0.61.0`. L-3 is not started. The index names the next phase.
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

The SQLite schema, migrations, the statement helpers, the blob and cache directories, and their eviction. The open and close are [C-CORE.md](C-CORE.md) C-4. Account configuration is the profile ([B-CONFIG.md](B-CONFIG.md)); this store holds what sync learns and what the user does with mail.

## Phases

One phase is one session. Do not start the next one in the same conversation.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| L-1 | Migration runner, statement helpers, schema v1 (accounts, mailboxes, identities, sync state) | Medium | Complete |
| L-2 | Schema v2: mail (email, threads, bodies, attachments, FTS, queue, outbox, drafts) and the blob store | Medium | Complete |
| L-3 | PIM migration: contacts, calendars, files, tasks, notes, rules and script runs (CLI tables are V-2) | Medium | Not started |

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
  - Nothing else ever runs `INSERT`, `UPDATE`, `DELETE`, or DDL on the profile store. `pav_db_prepare` on a read connection refuses a non-`SELECT` statement.
  - The one exception, scoped to `log.db` (from H-4, [H-LOGGING.md](H-LOGGING.md) Lock): the log thread owns its own read-write connection to the per-profile `log.db`, a separate SQLite file next to the profile store. The store writer never opens `log.db`, and the log thread never opens the profile store. `log.db` has its own migrations, `user_version`, and PRAGMAs (WAL); this Lock's PRAGMA list, migrations, and read pool do not cover it.
- **Readers** use a small read pool: 2 read-only connections (`SQLITE_OPEN_READONLY`, WAL). Command reads (lists, gets, search) borrow a connection and return it before the command returns. A read transaction never spans a command, a rule, or a sync page.
  - WAL lets readers run alongside the single writer.
  - A `SQLITE_BUSY` on the writer means a reader is still inside a transaction during a checkpoint. The writer runs `PRAGMA wal_checkpoint(PASSIVE)` and retries that job once. A second `SQLITE_BUSY` is logged at `error` and the job fails. There is no retry loop beyond that one.
  - Migrations run on the writer before the pool opens.
- This supersedes the C-CORE rule that the core thread holds the only `sqlite3 *`. The core thread now submits writes and borrows readers. It never holds a write connection.
- All times are UTC unix milliseconds (`INTEGER`). Formatting to `America/Vancouver` (or the profile timezone) and 24 h happens in the UI.
- Ids: server ids are kept as-is with their `ra_id` (remote account). A local row id (`rowid INTEGER PRIMARY KEY`) is used for joins and FTS. The UI sees a composite id `<raId>/<serverId>` as a string.
- Account configuration (name, URLs, username, auth, color, flags, number) lives in the profile `accounts` array. The store's `account_state` holds only runtime state keyed by the same id. This replaces plan §4's `account` table, which duplicated the profile.
- Message bodies, raw `.eml`, and attachments are files under `blobs/<profile>/`, mode 0600 in 0700 directories. The path is `blobs/<profile>/<ra>/<hh>/<encoded>`, where `encoded` is the blob id in base64url with no padding. A blob id is never used as a path segment raw: `/`, `..`, and any other byte outside the unreserved set cannot escape the shard. `body_cache` stores paths only (`text_plain_path`, `html_raw_path`, `html_sanitized_path`). Plain text and sanitized HTML are files under the same budget as bodies. A `text_plain` or `html_sanitized` column is not added. FTS reads the plain-text file, capped at 256 KB per message (Q Lock).
- Cache limits: `preferences.cache.bodiesMb` (default 2,048) and `preferences.cache.blobsMb` (default 4,096). An eviction pass (least recently opened first, never drafts, outbox, or flagged) runs after sync when over the limit, registered as an activity. Evicted bodies refetch on open. This is a core-interpreted preference: it lives at `preferences.cache`, outside the opaque `preferences.ui` blob (F Lock, Preferences split). L-2 adds it to the B Lock. Settings → Limits (heading id `limits`) edits these two integers. The labels are Database and Document. The stored keys stay `bodiesMb` and `blobsMb`. Database is message text, contacts, and calendars. The cap drops cached message text when the copy outgrows it. Contacts and calendars stay in the profile database and are not evicted. Document is attachments and files. The page clamps each to 1..1048576 and writes them with `prefs.set`. The saved values are used on the next finished sync. Beside each limit the page shows the bytes on disk and the free space on the volume that holds the blob directory, on one line. `cache.usage` carries `volumeFreeBytes`, or null when that volume cannot be read. The free-space figure is left off that line when the value is null. Under the limits, one row per account. The columns are Database (mail, contacts, and calendars) and Document (attachments and files). Each cell is an entry count and a size in megabytes, one decimal. Database entries are messages, contacts, and events. Document entries are attachment parts and files. A missing count or size is left out. `serverBytes` stays on the command and is not a column. Refresh asks for what the server can answer quickly, one JMAP POST per account that has a stored session: `Quota/get` when `urn:ietf:params:jmap:quota` is present, and `Email/query`, `ContactCard/query`, `CalendarEvent/query`, and `FileNode/query` with `calculateTotal` and `limit` 1 for each capability that session advertises. A query `total` is the count. An octets or count quota fills one column only when every type names the same kind (`Email` or `Mail`, `ContactCard` or `Contact`, `CalendarEvent` or `Calendar`, `FileNode` or `File`). A mixed list, including Stalwart's `Email`, `SieveScript`, `FileNode`, `CalendarEvent`, and `ContactCard`, is `serverBytes` and is not split across columns. A query total wins over a quota count. A single-kind octets quota is that kind's size. Sizes the quota does not split are `Email/get` `size` and `FileNode/get` `size`. The page is the session's `maxObjectsInGet`, and at most 500. One request asks for several windows, half of `maxCallsInRequest` and at most 8, so a large mailbox is not one round trip per 100 messages. At most 1,000 windows. That walk is the Check sizes button. It does not run on Refresh. While it runs, the page shows a progress bar and a Cancel button, and `cache.change` carries the account, the store, and how far the sum has got. Cancel stops the walk; a store that did not finish keeps its previous size. The last finished walk is kept in `kv` under `limits.server`, and the result carries `sizesCheckedAt`. A walk that stops early does not replace a size. A count of 0 is 0 bytes with no walk. Calendars and contacts have a size only when a single-kind quota supplies it. A blank cell is a store this account did not report. The page does not say unreported. An account whose `kind` is missing or `jmap` is asked this way. Any other kind, including `imap`, leaves the server cells blank. M-8 fills mail on those same fields. CalDAV and CardDAV fill calendars and contacts the same way later. This phase does not speak those protocols. A failed read of one account does not fail the command. The same mail walk also sums the decoded text and HTML part sizes, and the decoded attachment sizes (`textBody`, `htmlBody`, and `attachments`, each with `bodyProperties` of `size` only). The page turns those into the two account columns. Database size is the decoded text plus HTML, plus contact bytes and calendar bytes when that size was reported. Document size is the decoded attachment sizes plus file bytes when that size was reported. The walk also counts attachment parts (`mail.attachmentsCount`, stored as `mailAttachmentsCount`). A missing size is left out of the sum and is not stored as zero. A pool size stays blank until the mail split for that pool is known, so a file size of zero is not shown as the Document size before the attachment walk. The same holds for the Document entry count. It does not add a sentence naming what was left out. A Total row sums each column on its own. A missing figure is left out of that sum. The two columns are not added together. `mail.bytes` stays the whole message and is not a column. Because that get is larger, the mail walk asks for at most 4 windows in one request; files stay at 8. A response the client will not keep retries that position once with one window, and the rest of that store stays at one window. A part list that is missing, or a part without an integer size, fails that mail walk the same way a missing message size does, and the previous split is kept. A count of 0 is 0 for both with no walk. A quota that already supplied the message size does not skip this walk. The split is stored in `kv` `limits.server` beside the mail size (`mailBodiesBytes`, `mailAttachmentsBytes`, `mailAttachmentsCount`). The body figure is the text plus the HTML. The cleaned HTML kept on disk is another copy of the HTML, so the Database limit needs room above that figure. The attachment figure is the decoded files. A saved raw message shares the Documents limit. The log size cap stays on Logs. A later disk budget is another row on Limits.
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
-- A CalDAV collection that allows VEVENT is a calendar row. One that allows VTODO is a task_list row.
-- One that allows both is both rows, the same (ra_id, id). components is VEVENT, VTODO, or both.
CREATE TABLE task_list (ra_id TEXT, id TEXT, name TEXT, color TEXT, is_visible INT, components TEXT,
  my_rights TEXT, PRIMARY KEY (ra_id, id));
CREATE TABLE task (rowid INTEGER PRIMARY KEY, ra_id TEXT, id TEXT, list_id TEXT, uid TEXT,
  title TEXT, description TEXT, status TEXT, priority INT, due_utc INT, due_date TEXT,
  raw_ics TEXT, updated INT, UNIQUE (ra_id, id));
CREATE VIRTUAL TABLE task_fts USING fts5(title, description, content='');
CREATE TABLE note (rowid INTEGER PRIMARY KEY, ra_id TEXT, id TEXT, notebook TEXT, title TEXT,
  tags_json TEXT, path TEXT, etag TEXT, updated INT, UNIQUE (ra_id, id));
CREATE TABLE note_file (note_rowid INT NOT NULL REFERENCES note ON DELETE CASCADE, name TEXT,
  mime TEXT, size INT, etag TEXT, PRIMARY KEY (note_rowid, name));
CREATE VIRTUAL TABLE note_fts USING fts5(title, body, tags, content='');
-- note_fts body tokens come from the cached file. note has no body column. Argent has no tables. GitHub has no tables.
CREATE TABLE rule_run (id INTEGER PRIMARY KEY, rule_id TEXT, ra_id TEXT, email_id TEXT, started INT,
  dry_run INT, intents_json TEXT, op_ids TEXT, error TEXT);
-- bot_key and cli_audit are V-2's migration 003_cli.sql, which runs before this one. Do not create them here.
```

## L-1: Migration runner, helpers, schema v1

**Kind:** Medium
**Status:** Complete (2026-10-18). The exit gate passed. This is not a window phase. `make test` is 81 Unity tests and 134 Vitest tests. This sitting did not push. Do not start K-1.

**Goal:** The store migrates itself on open and offers safe statement helpers.

**Entry gate:** J-9 accepted. J-9 is the previous phase in the sequence. Read its Handoff after Andrew accepts the window. This phase does not touch the shell quit path, and it does not start while J-9 is still in progress.

**Work items:**

- [x] `core/src/db.c`, `core/include/pav_db.h`: PRAGMAs, the statement cache, bind and step helpers, transactions. `pav_store_open` sets no PRAGMAs today (`SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE` only). Apply the Lock's list on the writer before migrate. Add every new `.c` file to `core/CMakeLists.txt` and to the `$(LIB)` prerequisite list in the Makefile, or `make test` will not rebuild the archive. The Unity binary links `-lsqlite3` from the Makefile. The CMake target links `pthread` only. `src-tauri/build.rs` links `sqlite3` for the shell.
- [x] `core/src/migrate.c`: embedded migrations, `user_version` check, newer-than-binary error. CMake step embeds `core/src/migrations/*.sql`.
- [x] `001_accounts.sql` with schema v1. Repeat the I Lock's `CREATE TABLE IF NOT EXISTS activity` in that migration and keep the rows. Copy that statement. Do not treat `user_version` 0 as an empty database when `activity` is already present. The `DELETE` of `queued` and `running` rows is every open, in `pav_activity_attach`, and does not belong in this file.
- [x] `PavStoreVtable` gains `migrate`; C-6 calls it after open and before `pav_activity_attach`; failure is state `error` and does not attach. Today `pavillion.c` attaches immediately after `pav_store_open`, and that attach creates the table.
- [x] `core/src/db_writer.c`: the store writer thread, the serial write queue, `pav_db_write`, batching (50 jobs or 20 ms), and a failing batch retried one job at a time. `core/src/db_readpool.c`: two read-only connections, with `pav_db_read_begin` and `pav_db_read_end`. Read connections refuse non-`SELECT` statements. Migrations run on the writer before the pool opens.
- [x] Activity writes leave the direct connection. `pav_activity_*` persists through `pav_db_write`. I-1's `sqlite3_exec` on the open handle stops. The I Lock's statement is unchanged. `pav_activity_end` and `end_still_running` call `persist_finished` while `g_mu` is held. Both run on the core thread (`pav_activity_end`, and `core_release_profile` during stop). The demo thread also calls `pav_activity_end`. `on_done` posts to the core queue, so waiting for that callback on the core thread, or while `g_mu` is held, deadlocks. Do not keep `g_insert` and `g_dismiss` on the write connection and step them from those callers. The live profile `~/.local/share/pavillion/default.sqlite` was read on 2026-10-18: `user_version` 0, journal mode `delete`, one table `activity`, 4 rows. Tests use a temp directory. Do not open that file for write. The next real launch after this phase sets WAL and `user_version` 1 and must keep those rows.
- [x] Unity: `test_writes_are_serial_fifo`, `test_write_batch_one_transaction`, `test_failed_job_rolls_back_alone`, `test_read_conn_refuses_insert`, `test_concurrent_reads_during_write_no_busy` (4 reader threads plus a writer, 10,000 writes, zero `SQLITE_BUSY`), `test_writer_busy_retries_once`, `test_store_writer_stack_is_512kb`.
- [x] Unity: `test_migrate_fresh_to_v1`, `test_migrate_is_idempotent`, `test_newer_db_is_error`, `test_db_bind_never_concatenates` (helper refuses a `%` format with args), `test_foreign_keys_on`.

**Done means:** A fresh profile opens at `user_version` 1 with the v1 tables, and a second start does nothing.

**Exit gate:** `make test` exits 0. `sqlite3 default.sqlite .schema` on a `make dev` workdir shows the v1 tables, and `PRAGMA user_version` is 1.

**Accomplished:** The store migrates on open. `core/src/migrate.c` applies embedded SQL from `core/src/migrations/`, one transaction each, and errors when the file is newer than the binary. `001_accounts.sql` is schema v1 and repeats the I Lock `CREATE TABLE IF NOT EXISTS activity`. `core/src/db_writer.c` is the only write connection: serial queue, batches of 50 jobs or 20 ms, a failed batch retried one job at a time, one `SQLITE_BUSY` retry after `PRAGMA wal_checkpoint(PASSIVE)`, 512 KB stack. `core/src/db_readpool.c` is two read-only connections. `pav_db_prepare` on a reader refuses a non-`SELECT`. `pav_store_open` still sets no PRAGMAs. The writer applies the Lock list before migrate. C-6 calls `migrate` after open and before `pav_activity_attach`. A migrate failure does not attach. Activity insert and dismiss go through `pav_db_write` and do not wait on `on_done`. Stop drains the queue before the writer joins. No new command. `PAVILLION_API_VERSION` stays 3. `lib.rs` was not changed. Andrew's `make dev` left `~/.local/share/pavillion/default.sqlite` at `user_version` 1, journal mode `wal`, seven tables, and activity rows 1–4. A second open does not add a migration. `make test` is 81 Unity tests and 134 Vitest tests, exit 0. The phase stamp is `0.46.0`. The release date in `build.rs` stays 2026-10-18.

**Lessons learned:** Waiting on `on_done` from the core thread, or while `g_mu` is held, deadlocks. Enqueue and return. Stop drains before the join. A profile at `user_version` 0 is not empty when `activity` is already there. The migration repeats `CREATE TABLE IF NOT EXISTS` and does not delete queued rows; that delete stays in `pav_activity_attach`. History tests that open a store now see version 1 and seven tables. `strdup` needs `_POSIX_C_SOURCE` in `store.c`. `strncasecmp` is not in this pedantic build; fold ASCII by hand. A `make dev` killed by a tool timeout opens a window whose log is only `core ready`, because the page never ran. That is not a store failure. The Logs view is the `pav_log` ring. A quiet start writes one line. Migrate does not log, and that is not this phase. Do not launch the window from the agent. Andrew runs `make dev`. Shipped SQL is not edited. A file-scoped Vitest run is not `make test`.

**Handoff:** K-1 is next, step 43, in [K-ACCOUNTS.md](K-ACCOUNTS.md). Do not start it in this sitting. Its entry gate is met. Do not rebuild the writer, the read pool, or `001_accounts.sql`. Secrets go to the keyring, not the store. Tests use the fake platform. Do not open `~/.local/share/pavillion/default.sqlite` for write. `account_state` stays empty until K-2. `debug.secret_smoke` is a new command. Adding a command does not bump `PAVILLION_API_VERSION`. Do not touch the shell quit path: the SIGINT block, the counting `sigaction`, and `WEBKIT_DISABLE_COMPOSITING_MODE`. `lib.rs` stays as it is. Only `ui/src/transport/tauri.js` may import `@tauri-apps`. The phase stamp is `0.46.0`. `PAVILLION_BUILD` is dirty at 169 from `make dev`. Do not commit that bump. The committed count is 162. The next `make dev`, `make build`, or `make mini` adds one and dirties that file. `make test` does not. J-4 stays at step 107. J-5 stays at step 108. Do not start them. L-2 waits for M-3.

## L-2: Mail schema and the blob store

**Kind:** Medium
**Status:** Complete (2026-10-22). Andrew accepted the Limits layout on 2026-10-22. Every work item is checked. `make test` is 202 Unity tests, 2 Rust tests, and 363 Vitest tests, exit 0. His 2026-10-21 launch applied migration 2. CI on `abba579` (run 39797701301) is green for `fedora`, `macos-core`, and `windows-core`. The phase stamp is `0.61.0`. The same day he accepted a startup change that is not part of this phase. That change is uncommitted. M-4 was not started in this sitting.

**Goal:** The mail tables and the blob directory exist with their helpers before sync writes email.

**Entry gate:** M-3 complete.

**Work items:**

- [x] `002_mail.sql` with schema v2.
- [x] `core/src/blobs.c`: `pav_blob_path(ra, blob_id)`, `pav_blob_write(ra, blob_id, buf, n)` (via `write_private`), `pav_blob_exists`, `pav_blob_remove`, sharded directories.
- [x] Add `preferences.cache {bodiesMb, blobsMb}` (not under `preferences.ui`) to the B Lock and the profile defaults, and set it through `prefs.set`. The eviction pass `pav_cache_evict()` runs as activity kind `cache.evict` (I Lock).
- [x] Settings → Limits edits `bodiesMb` and `blobsMb` (heading id `limits`). The page clamps each to 1..1048576 and writes both through `prefs.set`. The log size cap stays on Logs.
- [x] `cache.usage` reports on-disk bodies and blobs, and per account the server count and size for mail, contacts, calendars, and files, plus a combined server total. Refresh reads the quick counters. Check sizes walks mail and file sizes a quota does not already split, several windows per request, with a progress bar and Cancel. The last result stays in `kv` `limits.server`. A blank cell is a store that was not reported. Refresh during a check does not drop that result.
- [x] Check sizes sums decoded message text and decoded attachments, and counts attachment parts. The caps are Database and Document. Beside each cap is the size on disk and the free space on the blob volume. Each account row is those two pools: entry count and size. A Total row sums each column on its own. A missing size is left out. A pool size, and the Document entry count, stay blank until that mail split is known. Andrew accepted this on 2026-10-22.
- [x] Runtime open directory creation and removal on stop.
- [x] Unity: `test_migrate_v1_to_v2`, `test_blob_path_sharding`, `test_blob_id_is_base64url`, `test_blob_modes`, `test_evict_skips_flagged_and_drafts`, `test_evict_clears_body_paths`, `test_runtime_dir_removed_on_stop`, `test_quota_request_is_a_get`, `test_quota_pick_prefers_mail`, `test_usage_parse_classifies_quotas`, `test_usage_quick_request_matches_capabilities`, `test_usage_measure_request_uses_result_reference`, `test_usage_measure_batch_packs_windows`, `test_usage_parse_batch_sums_windows`, `test_cache_usage_sums_local_files`, `test_cache_usage_keeps_measured_sizes`.

**Done means:** The v2 schema is in place and blobs can be written, found, and evicted.

**Exit gate:** `make test` exits 0. A v1 database migrates to v2 on the next launch. Andrew's 2026-10-21 launch applied migration 2. CI on `abba579` (https://github.com/500Foods/Pavillion/actions/runs/39797701301) is green for `fedora`, `macos-core`, and `windows-core`. `2234482` is a README line-count commit after that merge and does not change this phase.

**Accomplished:** Schema v2 is in `002_mail.sql`. Blobs are sharded files under the profile, written through `write_private`, and `pav_cache_evict` drops the oldest opened message text and documents over `preferences.cache`. Drafts, flagged mail, and the outbox stay. Contacts and calendars stay in SQLite. The runtime `open` directory is removed on stop. Settings → Limits edits `bodiesMb` and `blobsMb`, labeled Database and Document. Beside each cap is the size on disk and the free space on the blob volume. Each account row is those two pools, and a Total row sums each column on its own. `cache.usage` is the quick read and, with `measure`, the mail and file walk. The mail walk also stores `mailBodiesBytes`, `mailAttachmentsBytes`, and `mailAttachmentsCount`. `volumeFreeBytes` is optional. `PAVILLION_API_VERSION` stays 3. Help already describes this page. `make test` is 202 Unity tests, 2 Rust tests, and 363 Vitest tests, exit 0. Andrew's 2026-10-21 launch applied migration 2. He accepted the Limits layout on 2026-10-22, including a blank Document count until Check sizes has counted attachment parts.

**Lessons learned:**

- A whole-message octet total is not a storage-pool size. A mixed quota, including Stalwart's account-wide octets, stays in `serverBytes` and is not a column. Database size is decoded `textBody` plus `htmlBody`. Document size is decoded attachments plus file bytes that were reported. A part listed in both text and HTML is counted twice, on purpose. Sanitized HTML is a second local copy, so the Database cap needs room above the server text figure.
- The Database cap evicts cached message text only. Contacts and calendars are in that server total and are not evicted.
- Do not show a pool size, or the Document entry count, until the mail split for that pool is known. A file count of 0 is not "no documents" while attachment parts are uncounted. Quota and `Email/query` do not report that count. Check sizes counts the parts. An older `limits.server` value can hold the byte split and no `mailAttachmentsCount`. Refresh then shows the size without an entry count until Check sizes runs again. Andrew accepted that blank on 2026-10-22.
- The Total row sums each column by itself. A missing figure is left out. The two columns are not added together.
- The mail get asks for part lists, so that walk takes at most 4 windows in one request. Files stay at 8. A missing part list, or a part without an integer `size`, fails that mail walk and keeps the previous split. A message count of 0 is 0 for both parts, with no walk. A quota that already supplied the message size does not skip the walk.
- Do not paint the running whole-message sum into the Database cell during the walk. `cache.change` `bytes` is that sum.
- Free space is one platform call on the blob directory, or on the profile file when that directory is missing. The fake returns -1 until `pav_platform_fake_set_volume_free`. The same figure is shown beside both caps. Adding `volumeFreeBytes` does not bump `PAVILLION_API_VERSION`.
- The page sums the pools. The command returns the parts. The labels are Database and Document. The stored keys stay `bodiesMb` and `blobsMb`.
- Do not launch `make dev`. Andrew runs it and accepts the window.

**Handoff:** M-4 is next, step 56, in [M-SYNC.md](M-SYNC.md). Its entry gate is L-2 and E-7. Both are complete. Do not start M-4 in this sitting. The next session may start it. The phase stamp is `0.61.0`. `origin/main` is `2234482`, a README line-count commit on the L-2 merge `abba579`. Do not commit `PAVILLION_BUILD`. The agent does not commit.

Andrew accepted the startup window on 2026-10-22. That work is not L-2 and not M-4. It is uncommitted, together with this close. Commit it on its own before M-4 source edits, or leave it and do not describe it as M-4. The files are `ui/index.html`, `ui/public/styles/theme.css`, `ui/public/styles/light.css`, `ui/src/main.js`, `ui/src/shell.js`, `ui/src/mail/nav.js`, `ui/src/mail/nav.test.js`, `ui/src/shell/session.test.js`, and `ui/src/help/pages.js`.

Startup lessons, for the next session:

- The shell does not wait on the Font Awesome kit. The loader fetches `pro-plus.min.js` and runs it by inserting an inline script. The kit config says `asyncLoading.enabled` is false, and that loader does not read the flag. The script is inserted after the first paint. A multi-second gap before `mailbox.list` resumed lined up with the kit running during the first layout. That gap was not traced into the kit.
- The mail tree is built only when the Mail nav is showing. A Contacts-only restore does not call `mailbox.list`. `sync.state` that arrives early is applied when the nodes are built.
- The splash background is immediate. The version line fades in over 0.4 s. Fade-out waits until the restored tabs have painted and at least 2 s have passed, then takes 0.4 s. The cover does not wait for the kit.
- `cache.usage` at startup runs on a worker. The shell does not wait for it. The `headerTooltip` warning is the accounts table and stays with the paused Tabulator polish.

M-4 writes headers into `email`, `email_mailbox`, and `thread` through `pav_db_write`, one job per page. Do not edit `002_mail.sql`. Q-1 has not landed, so the header FTS call is a stub until that phase. Importing `tests/corpus/` stays in `tests/stalwart/seed.sh` and is an M-4 work item. The Check sizes walk is not the sync walk. After a finished sync, `pav_cache_evict` still drops the oldest opened message text and documents that are over the caps. Bodies refetch on open.

Do not touch the quit path. `lib.rs` stays a forwarder. Only `ui/src/transport/tauri.js` may import `@tauri-apps`. Tests install `pav_platform_fake()`. The Accounts table polish stays paused in [TABULATOR.md](TABULATOR.md). L-3 waits for R-5. `PAVILLION_API_VERSION` stays 3.

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

None. L-Q1 was answered on 2026-10-21: keep 2048 MB of bodies and 4096 MB of blobs.

## Working log

- 2026-10-16 Opened from plan §4. Decisions: profile is the source of account configuration and `account_state` replaces the `account` table; unix ms everywhere; embedded migrations; blobs sharded by blobId; cache limits and eviction; forwarded and junk columns; `local_marks` for bot markers; `rule_run`, `bot_key`, `cli_audit`, `reminder`, and `file_node` tables added.
- 2026-10-16 One store writer thread with a serial, batched write queue, plus a read pool of 2. A write path never sees `SQLITE_BUSY`. Added to L-1 with five tests.
- 2026-10-18 Prep only. L-1 stays not started. The entry gate now names V-5's Handoff and keeps this phase off the shell quit path. `001_accounts.sql` must repeat the I Lock's `activity` table, because a profile that already ran I-1 has that table at `user_version` 0. Activity persistence moves onto `pav_db_write` in the same phase. No source was edited for L-1.
- 2026-10-18 L-1's entry gate is J-9 accepted. Schema v3 sketches `task_list`, `task`, `task_fts`, `note`, `note_file`, and `note_fts`. Argent has no tables. L-1 was not started.
- 2026-10-18 GitHub has no tables. The sketch above was not extended. L-1 was not started.
- 2026-10-18 Prep only. L-1 stays not started. The work items now name the build list, the attach order, and the activity deadlock. `pav_store_open` sets no PRAGMAs. New sources have to be listed in `core/CMakeLists.txt` and in the Makefile `$(LIB)` prerequisites. Migrate runs after open and before `pav_activity_attach`, and a failure does not attach. The queued-row `DELETE` stays an every-open write, not a line in `001_accounts.sql`. `persist_finished` runs while `g_mu` is held, from the core thread and from the demo thread, so a wait on the core queue for `on_done` deadlocks. The live profile was read again: `user_version` 0, journal mode `delete`, table `activity`, 4 rows. Tests must not write that file. The next real launch after L-1 sets WAL and `user_version` 1 and must keep the rows. No source was edited for L-1. J-9's window is still open.
- 2026-10-18 L-1 complete, step 42. The store migrates on open to `user_version` 1. Andrew's `make dev` kept activity rows 1–4 and set WAL. `make test` is 81 Unity tests and 134 Vitest tests. The phase stamp is `0.46`. `PAVILLION_BUILD` is dirty at 169 from `make dev`; do not commit that bump. K-1 is next, step 43, in docs/K-ACCOUNTS.md. Do not start it in this sitting.
- 2026-10-19 Lock change for H-4: `log.db` is the one exception to the one-writer rule. The log thread owns it on its own connection, and the store writer never opens it ([H-LOGGING.md](H-LOGGING.md) Lock).
- 2026-10-21 L-2 in progress, step 55. Andrew kept the cache defaults at 2048 and 4096. Schema v2, the blob helpers, `preferences.cache`, eviction as `cache.evict`, and the runtime `open` directory are in. `make test` is 192 Unity tests, 2 Rust tests, and 357 Vitest tests, exit 0. The phase stays in progress until the next launch migrates the v1 profile store to `user_version` 2. The phase stamp stays `0.60.0`. Do not start M-4.
- 2026-10-21 L-2 still in progress. Andrew's launch applied migration 2 (schema version 1, binary 2) on the live profile, with two accounts idle and no cache eviction. Settings → Limits edits the two cache sizes. `make test` is 192 Unity tests, 2 Rust tests, and 361 Vitest tests, exit 0. The phase stamp stays `0.60.0`. Do not mark the row Done. Do not start M-4.
- 2026-10-21 L-2 still in progress. Settings → Limits shows on-disk bodies and attachments, and each account's server size from `Quota/get`, on the same Refresh. A missing quota is unreported. There is no mailbox scan and no separate button. `make test` is 195 Unity tests, 2 Rust tests, and 363 Vitest tests, exit 0. The phase stamp stays `0.60.0`. Do not mark the row Done. Do not start M-4.
- 2026-10-21 L-2 still in progress. Refresh now reads the quick server counts for mail, contacts, calendars, and files, and a combined quota only when every type is the same kind. A mixed quota, including Stalwart's whole-account octets, stays in the Server column. Check sizes walks mail and file sizes the quota does not split, and the last finished walk stays in `kv` `limits.server` with the time beside the button. A blank cell is a store that was not reported. IMAP, CalDAV, and CardDAV use the same fields later and stay blank in this phase. `make test` is 199 Unity tests, 2 Rust tests, and 363 Vitest tests, exit 0. The phase stamp stays `0.60.0`. Do not mark the row Done. Do not start M-4.
- 2026-10-21 L-2 still in progress. Check sizes was one request per 100 messages, so a mailbox of about 56,000 took about three minutes, and a refresh queued behind that walk painted over the result. The walk now uses the session page cap, at most 500, and up to 8 windows per request. The page shows a progress bar and Cancel while it runs. `cache.change` carries the account, the store, and the running sum. A store that did not finish keeps its previous size. Refresh during a check does not drop the result. `make test` is 201 Unity tests, 2 Rust tests, and 363 Vitest tests, exit 0. The phase stamp stays `0.60.0`. Do not mark the row Done. Do not start M-4.
- 2026-10-22 L-2 still in progress. The caps are labeled Database and Documents. The stored keys stay `bodiesMb` and `blobsMb`. Check sizes sums decoded message text and decoded attachments. Database on the server is that text plus contact and calendar sizes when they were reported. Documents is the attachment sizes plus file sizes when they were reported. A missing size is left out, and a pool total stays blank until the mail split is known. The page shows free space on the volume that holds the blobs. `make test` is 202 Unity tests, 2 Rust tests, and 363 Vitest tests, exit 0. The phase stamp stays `0.60.0`. Do not mark the row Done. Do not start M-4. The window has not been accepted.
- 2026-10-22 L-2 still in progress. Andrew rejected that layout. The caps are now Database and Document. Beside each cap is the on-disk size and the free space on the blob volume, on one line. The account table is two columns, Database (mail, contacts, calendars) and Document (attachments, files). Each cell is the entry count and the size. Check sizes also counts attachment parts. A pool size stays blank until the mail split is known, and so does the Document entry count. `make test` is 202 Unity tests, 2 Rust tests, and 363 Vitest tests, exit 0. The phase stamp stays `0.60.0`. Do not mark the row Done. Do not start M-4. The window has not been accepted.
- 2026-10-22 L-2 still in progress. The account table has a Total row. It sums the Database column on its own and the Document column on its own. A missing figure is left out of that sum. The two columns are not added together. `make test` is 202 Unity tests, 2 Rust tests, and 363 Vitest tests, exit 0. The phase stamp stays `0.60.0`. Do not mark the row Done. Do not start M-4. The window has not been accepted.
- 2026-10-22 L-2 still in progress. Andrew accepted the Limits layout, including a blank Document count until Check sizes has counted attachment parts. Every work item is checked. Accomplished, Lessons learned, and Handoff are written. `make test` is 202 Unity tests, 2 Rust tests, and 363 Vitest tests, exit 0. The tree is uncommitted, and `origin/main` is one commit ahead (`72514e5`). The row stays In progress until that push is green on `fedora`, `macos-core`, and `windows-core`. The phase stamp stays `0.60.0`. Do not mark the row Done. Do not start M-4.
- 2026-10-22 L-2 complete, step 55. Andrew asked to close it. CI on `abba579` (run 39797701301) is green for `fedora`, `macos-core`, and `windows-core`. `make test` on that close is 202 Unity tests, 2 Rust tests, and 363 Vitest tests, exit 0. The phase stamp is `0.61.0`. M-4 is next and is not started. The same sitting left an uncommitted startup change: the Font Awesome kit waits until after the first paint, and the mail tree is built only when Mail is showing. That window has not been accepted. Do not start M-4 until it is.
- 2026-10-22 Andrew accepted the startup window. The cover shows the version for at least two seconds, fades the line in over 0.4 s, and fades out over 0.4 s after the restored tabs have painted. The kit script is still inserted after the first paint, and the shell does not wait on it. The mail tree is built only when Mail is showing. That diff is uncommitted and is not M-4. M-4's entry gate is met. M-4 was not started. The last full `make test` is still the L-2 run. File-scoped Vitest covered the nav defer, the contacts-only restore, the theme sheets, and Help. CI has not seen this diff or the stamp bump.

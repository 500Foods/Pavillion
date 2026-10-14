# Pavillion — table of contents

Status: draft v0.4 (2026-10-14). Implementer: Grok Build. Reviewer: Andrew Simard.

**Resume:** B-2 is complete. B-3 is next.

This file is the map. A new session reads Resume, the sequence table, and then only the named phase. One phase per session. The next session starts fresh. Phases are not worked in parallel.

A phase id is a letter and a number. The letter is the document. The number starts at 1 inside that document. B-3 is the third phase of CONFIG. A-12 would be the twelfth phase of INIT. The letters are not worked as four stacks at once. The sequence below is the only order.

| Doc | File | What it decides |
| --- | --- | --- |
| A | [docs/A-INIT.md](docs/A-INIT.md) | The executable, the window, Unity, linking the C library |
| B | [docs/B-CONFIG.md](docs/B-CONFIG.md) | `Pavillion.json`, profiles, paths, the `engine` field |
| C | [docs/C-CORE.md](docs/C-CORE.md) | Threads, the loop, logging, the SQLite open |
| D | [docs/D-UI.md](docs/D-UI.md) | The shell, `api.js`, the first frame |

| | Phase | What | Kind |
| --- | --- | --- | --- |
| 1 | A-1 | C library skeleton and Makefile | Easy |
| 2 | A-2 | Unity harness | Easy |
| 3 | A-3 | Tauri and Vite window | Medium |
| 4 | A-6 | `--help` and `--version` | Easy |
| 5 | D-1 | Frame and theme variables | Easy |
| 6 | A-4 | Link the C library into the shell | Hard |
| 7 | D-2 | `api.js` and status text | Easy |
| 8 | C-1 | Log function | Easy |
| 9 | C-2 | Log thread and the line format | Medium |
| 10 | B-1 | Parse the index and a profile | Easy |
| 11 | B-2 | First-run files | Easy |
| 12 | B-3 | Scan, kind, and a broken active profile | Medium |
| 13 | B-4 | Paths and file modes | Medium |
| 14 | B-5 | An unknown engine is an error | Easy |
| 15 | C-3 | Core thread and the command queue | Medium |
| 16 | C-4 | SQLite open and close | Medium |
| 17 | C-5 | Stop | Easy |
| 18 | C-6 | Start loads a profile | Medium |
| 19 | A-5 | Working directory and a real start | Medium |
| 20 | D-3 | The error state | Easy |

**Easy** is one module and a test, or a static page. **Medium** crosses one boundary. **Hard** is the first join of the two build systems. That is A-4.

How a phase is closed, in the document that owns it: Status, Accomplished, Lessons learned, Handoff, then a Working log line. Then stop. A sitting that ends early marks that phase in progress and does not open the next one. The steps are restated at the top of each lettered document.

Recorded, and not in the sequence yet: a Settings screen (zoom first, then per-panel zoom, window placement, hidden chrome), an Activity view of the log, UPX on the release binary, and a dnf repository as the update path. Also waiting: CodeMirror 6 for HTML and Lua source, SunEditor as the mail composer, Font Awesome Pro (Duotone, Sharp, Thin), and a Lithium-style theme layer after the hello frame. Lua and a second database driver wait there too. §14 below is the product roadmap (P0 through P6). It is not this session list. It gets its own letters when A through D are done.

The feature sections under §3 onward stay the spec for mail and sync until those letters exist. Where a topic has moved, the lettered document wins.

## 1. Name, purpose, goals, non-goals

**Pavillion** is a personal Linux desktop app for mail, contacts and calendar. It replaces
Betterbird (mail), CardBook (contacts) and Omacal (calendar) for one user, Andrew Simard.

Goals:
- JMAP-first client for self-hosted Stalwart (tenants 500foods.com, 500courses.com, 500nodes.com).
- Handle ~20 accounts/identities comfortably: unified views, fast switching, per-account identities.
- Offline-capable local store (SQLite) with fast local search (FTS5).
- Safe HTML mail rendering (sandboxed, remote content blocked by default).
- One app, one process, one active profile. Wayland-friendly. 24h clock, America/Vancouver default.
- Small enough that every subsystem is understandable and testable.

Non-goals (do not build):
- Gmail and Microsoft OAuth, Exchange/EWS/Graph, POP3. Generic OpenID Connect
  (authorization code + PKCE, bearer token to the server) is in scope; see §2 and §3.
- Multi-user, sync of app settings across machines, cloud services of any kind.
- Plugin/add-on system (add-ons caused the CardBook mess; the app stays closed).
- RSS, chat, newsgroups, PGP/S-MIME (S/MIME and PGP may be an open question later).
- An in-app self-updater. Updates are a package the system already installs. Development builds stay local.
- Windows/macOS support.

## 2. Architecture

```
+--------------------------- Tauri 2 app ----------------------------+
| Frontend (WebView: WebKitGTK)                                       |
|   Vanilla JS, HTML, CSS. Vite. Tabulator where the screen is a table|
|   app UI  <--invoke/events-->  thin Rust shell                      |
|   message view: sandboxed <iframe srcdoc> (no bridge, no scripts)   |
+---------------------------------------------------------------------+
| Rust shell (Tauri only): commands, events, window, dialogs,         |
|   single-instance, mailto. No mail logic.                           |
+---------------------------------------------------------------------+
| C core (library, no Tauri)                                           |
|   accounts   jmap   imap/smtp   sync   store (SQLite+FTS5)           |
|   search   sender   contacts   calendar   files   notify   secrets   |
|   backends:  JmapBackend (full account)   ImapBackend (mail only)    |
+---------------------------------------------------------------------+
```

### 2.1 C core, thin Rust shell

`pavillion-core` is a C library. It has no Tauri dependency. Unity tests link it
directly. `pavillion-app` is the Tauri 2 binary: command handlers, event emitters,
and the plugins (`tauri-plugin-notification`, `tauri-plugin-deep-link` for mailto,
`tauri-plugin-dialog`, `tauri-plugin-opener`, `tauri-plugin-autostart` optional,
`tauri-plugin-single-instance`). The shell translates invoke calls into C calls and
emits events. Mail, sync, storage, and authentication do not live in Rust.

C modules: `accounts`, `secrets`, `jmap`, `imap`, `smtp`, `sync`, `store`, `search`,
`sender`, `mime`, `sanitize`, `contacts`, `calendar`, `files`, `backend`.

Libraries already used in Philement, kept because they are small and the build is fast:

- **Jansson** for every JSON document, including JMAP request and response bodies.
- **libcurl** for HTTPS, HTTP/2, the session, the API, upload, download, and EventSource.
- **SQLite** with FTS5 for the local store. Schema version is `user_version`, migrations
  hand-rolled. Link the system library; do not bring over Hydrogen's database-engine dlopen layer.
- **OpenSSL** for TLS through curl and SHA-256 for PKCE.
- **libuuid** for ids.

Added because Hydrogen does not do this job:

- **GMime** to parse and build MIME.
- **libical** for iCalendar and RRULE expansion when calendars land. Timezones come from
  the system zoneinfo data (`America/Vancouver` and the rest).
- **libsecret** for the Secret Service (gnome-keyring or KeePassXC).

Left behind on purpose: libmicrohttpd, libwebsockets, Lua, MagickWand, Brotli. Those
belong to the server. JMAP over HTTP does not need them.

Concurrency is the Hydrogen shape, not an async runtime: one worker thread per login,
a shared curl multi handle, a global cap on in-flight requests (default 6).

Build the core with CMake. `make dev`, `make build`, and `make test` at the repo root
drive both the C core and the Tauri shell. `make build` copies the release binary to
`dist/pavillion`. Do not pin library versions in this document.

### 2.2 Frontend: vanilla JS, Vite, Tabulator

ES modules, HTML, and CSS variables. No framework and no CSS library. Vite builds and
serves the UI, the same role it has in Lithium. Vitest covers JS modules that do not
need a window.

**Tabulator** is the table. Use it for the message list, contacts, files, and the
account and mailbox lists, with its virtual DOM so a list of 100k rows stays inside
the frame budget. The reading pane, the composer, and the calendar views are plain
DOM. They are not tables.

```
ui/
  index.html
  src/api.js          the only module that calls Tauri
  src/mail/           sidebar, message list, thread, composer
  src/contacts/
  src/calendar/       month, week, day, agenda, event editor
  src/files/
  src/common/         dialog, toast, shortcuts
  styles/             theme.css (variables), light.css, dark.css
```

### 2.3 Tauri bridge API
All commands are `async`, return `Result<T, ApiError>` where
`ApiError = { code: string, message: string, retryable: bool }`. IDs are local string IDs
(`acct:…`, server IDs kept as-is plus `account_id`).

Accounts:
| Command | Request | Response |
|---|---|---|
| `account_list` | `{}` | `Account[]` |
| `account_add` | `{ name, session_url, username, auth: "basic"\|"bearer", secret }` | `Account` |
| `account_update` | `{ id, patch: Partial<Account> }` | `Account` |
| `account_remove` | `{ id, purge_local: bool }` | `{}` |
| `account_test` | `{ session_url, username, auth, secret }` | `{ ok, capabilities: string[], error? }` |
| `identity_list` | `{ account_id? }` | `Identity[]` |

Mail:
| Command | Request | Response |
|---|---|---|
| `mailbox_list` | `{ account_id? }` | `Mailbox[]` (tree via `parent_id`) |
| `mailbox_create` / `_rename` / `_delete` | `{ account_id, name, parent_id? }` / `{ id, name }` / `{ id }` | `Mailbox` / `{}` |
| `message_list` | `{ view: {kind:"mailbox",id}\|{kind:"unified",role}\|{kind:"search",query}, offset, limit, threaded: bool }` | `{ total, items: MessageSummary[] }` |
| `thread_get` | `{ thread_id }` | `MessageSummary[]` |
| `message_get` | `{ id, allow_remote: bool }` | `MessageDetail` (`html_sanitized?`, `text?`, `attachments`, headers) |
| `message_set_flags` | `{ ids[], seen?, flagged?, answered?, keywords?: {add[],remove[]} }` | `{}` |
| `message_move` | `{ ids[], to_mailbox_id }` | `{}` |
| `message_archive` / `message_delete` | `{ ids[] }` / `{ ids[], permanent: bool }` | `{}` |
| `message_raw` | `{ id }` | `{ path }` (writes .eml to cache) |
| `attachment_save` | `{ message_id, part_id, dest_path? }` | `{ path }` |
| `attachment_open` | `{ message_id, part_id }` | `{}` (xdg-open via opener) |
| `search` | `{ query, scope: {account_id?, mailbox_id?}, server: bool, limit }` | `{ local: MessageSummary[], server_pending: bool }` |
| `sender_allow_remote` | `{ address\|domain, allow: bool }` | `{}` |
| `retention_get` / `retention_set` | `{ mailbox_id }` / `{ mailbox_id, keep_newest: number\|null, max_age_days?: number\|null }` | `RetentionRule` |

Compose:
| Command | Request | Response |
|---|---|---|
| `draft_new` | `{ mode: "new"\|"reply"\|"reply_all"\|"forward", source_id?, identity_id? }` | `Draft` |
| `draft_save` | `{ draft: Draft }` | `Draft` (synced to Drafts mailbox) |
| `draft_attach` | `{ draft_id, path, inline: bool }` | `{ part_id, cid? }` |
| `draft_send` | `{ draft_id, send_at?: iso8601 }` | `{ outbox_id }` |
| `draft_discard` | `{ draft_id }` | `{}` |
| `signature_list` / `signature_set` | `{ identity_id }` / `{ identity_id, html, text, inline_images: [{cid, path}] }` | `Signature` |

Contacts:
| `addressbook_list` | `{ account_id? }` | `AddressBook[]` |
|---|---|---|
| `contact_list` | `{ book_id?, query?, offset, limit }` | `{ total, items: ContactSummary[] }` |
| `contact_get` / `contact_save` / `contact_delete` | `{ id }` / `{ contact: Contact }` / `{ id }` | `Contact` / `Contact` / `{}` |
| `contact_autocomplete` | `{ prefix, limit }` | `{ name, email, source: "contact"\|"recent" }[]` |
| `contact_import` / `contact_export` | `{ book_id, path }` / `{ book_id\|ids, path, format: "vcard"\|"jscontact" }` | `{ count }` |

Calendar:
| `calendar_list` | `{ account_id? }` | `Calendar[]` |
|---|---|---|
| `event_range` | `{ calendar_ids[], start, end, tz }` | `EventOccurrence[]` (expanded) |
| `event_get` / `event_save` / `event_delete` | `{ id }` / `{ event, scope: "this"\|"following"\|"all", occurrence_start? }` / same scope | `Event` / `Event` / `{}` |
| `event_import` / `event_export` | `{ calendar_id, path }` / `{ ids\|calendar_id, path }` (.ics) | `{ count }` |

System:
| `settings_get` / `settings_set` | `{}` / `{ patch }` | `Settings` |
| `sync_now` | `{ account_id? }` | `{}` |
| `status` | `{}` | `{ accounts: [{id, state, last_sync, queue_len, error?}] }` |
| `open_external_link` | `{ url }` | `{}` (frontend confirms first) |

Events (backend → frontend, `emit`):
- `sync://state` `{ account_id, state: "idle"|"syncing"|"offline"|"error", detail? }`
- `mail://mailboxes-changed` `{ account_id, mailbox_ids[] }`
- `mail://messages-changed` `{ account_id, created[], updated[], destroyed[] }`
- `mail://new-mail` `{ account_id, mailbox_id, summaries: MessageSummary[] }` (also drives notifications)
- `outbox://update` `{ outbox_id, state: "queued"|"sending"|"sent"|"failed", error? }`
- `queue://conflict` `{ op_id, kind, detail }`
- `contacts://changed` `{ account_id, book_ids[] }`
- `calendar://changed` `{ account_id, calendar_ids[] }`
- `calendar://reminder` `{ event_id, occurrence_start, title, location? }`
- `app://mailto` `{ url }` (from deep-link handler)

### 2.4 Backend boundary

The runtime, the threads, and the store function table are in [docs/C-CORE.md](docs/C-CORE.md). The mail and calendar operations that used to be sketched here as a Rust trait (connect, sync, fetch, apply, submit, query, push) still describe the work. They will be a C function table in the sync document, with two implementations: `JmapBackend` for a full account, and `ImapBackend` for mail only. The UI talks to the core, and the core talks to that table. Nothing in the UI calls JMAP or IMAP itself.

## 3. Accounts & auth

The files, the profile schema, the account object, and the keyring entry are in [docs/B-CONFIG.md](docs/B-CONFIG.md). This section is the behavior of an account once it is loaded.

- Session discovery: GET `session_url` (follow `/.well-known/jmap` redirect), cache the Session
  object, use `apiUrl`, `downloadUrl`, `uploadUrl`, `eventSourceUrl`, `primaryAccounts`.
  A single login may expose several JMAP accounts (shared mailboxes/calendars): model each
  JMAP `accountId` under the login as a sub-account (`remote_account` table).
- Identities: `Identity/get` per account; stored locally; compose chooses identity by
  (1) the address the original was sent to, (2) mailbox/account default, (3) global default.
- Auth failure (401): mark account `error:auth`, stop that account's loops, show one banner with
  "Re-enter password". Never loop prompts (this is the Betterbird/iCloud failure mode).
- 20+ accounts: one worker thread per login, a shared curl multi handle (connection pooling
  per host), global concurrency cap (default 6 in-flight requests). See §2.1.

## 4. Data model (SQLite)

PRAGMAs: `journal_mode=WAL`, `foreign_keys=ON`, `synchronous=NORMAL`. Schema versioned with
`user_version`. All times stored as UTC ISO-8601 text or unix ms integers (pick one: unix ms).

```sql
CREATE TABLE account (id TEXT PRIMARY KEY, name TEXT, session_url TEXT, username TEXT,
  auth TEXT, session_json TEXT, session_state TEXT, enabled INT DEFAULT 1);
CREATE TABLE remote_account (id TEXT PRIMARY KEY, account_id TEXT REFERENCES account ON DELETE CASCADE,
  jmap_account_id TEXT, name TEXT, is_personal INT, caps TEXT);  -- caps: JSON list
CREATE TABLE identity (id TEXT, ra_id TEXT REFERENCES remote_account ON DELETE CASCADE,
  name TEXT, email TEXT, reply_to TEXT, bcc TEXT, sig_html TEXT, sig_text TEXT,
  PRIMARY KEY (ra_id, id));
CREATE TABLE mailbox (id TEXT, ra_id TEXT REFERENCES remote_account ON DELETE CASCADE,
  parent_id TEXT, name TEXT, role TEXT, sort_order INT, total INT, unread INT,
  total_threads INT, unread_threads INT, my_rights TEXT, is_subscribed INT,
  PRIMARY KEY (ra_id, id));
CREATE TABLE email (rowid INTEGER PRIMARY KEY, id TEXT, ra_id TEXT REFERENCES remote_account ON DELETE CASCADE,
  thread_id TEXT, blob_id TEXT, message_id TEXT, in_reply_to TEXT, refs TEXT,
  subject TEXT, from_json TEXT, to_json TEXT, cc_json TEXT, bcc_json TEXT, reply_to_json TEXT,
  sent_at INT, received_at INT, size INT, preview TEXT, has_attachment INT,
  seen INT, flagged INT, answered INT, draft INT, keywords TEXT,
  body_state TEXT DEFAULT 'none',   -- none|fetched|failed
  UNIQUE (ra_id, id));
CREATE INDEX email_thread ON email(ra_id, thread_id);
CREATE INDEX email_recv ON email(received_at DESC);
CREATE TABLE email_mailbox (email_rowid INT REFERENCES email ON DELETE CASCADE, mailbox_id TEXT,
  PRIMARY KEY (email_rowid, mailbox_id));
CREATE INDEX em_mbx ON email_mailbox(mailbox_id, email_rowid);
CREATE TABLE thread (ra_id TEXT, id TEXT, email_ids TEXT, PRIMARY KEY (ra_id, id));
CREATE TABLE body_cache (email_rowid INT PRIMARY KEY REFERENCES email ON DELETE CASCADE,
  text_plain TEXT, html_raw_path TEXT, html_sanitized TEXT, has_remote INT, fetched_at INT);
CREATE TABLE attachment (email_rowid INT REFERENCES email ON DELETE CASCADE, part_id TEXT,
  blob_id TEXT, name TEXT, mime TEXT, size INT, cid TEXT, disposition TEXT, local_path TEXT,
  PRIMARY KEY (email_rowid, part_id));
CREATE VIRTUAL TABLE email_fts USING fts5(subject, from_text, to_text, body,
  content='', tokenize='unicode61 remove_diacritics 2');  -- rowid = email.rowid
CREATE TABLE remote_allow (pattern TEXT PRIMARY KEY, kind TEXT);  -- address|domain
CREATE TABLE retention_rule (ra_id TEXT, mailbox_id TEXT, keep_newest INT, max_age_days INT,
  last_run INT, PRIMARY KEY (ra_id, mailbox_id));

CREATE TABLE addressbook (ra_id TEXT, id TEXT, name TEXT, is_default INT, my_rights TEXT,
  PRIMARY KEY (ra_id, id));
CREATE TABLE contact (rowid INTEGER PRIMARY KEY, ra_id TEXT, id TEXT, book_ids TEXT,
  uid TEXT, full_name TEXT, sort_name TEXT, emails_json TEXT, card_json TEXT, updated INT,
  UNIQUE (ra_id, id));
CREATE VIRTUAL TABLE contact_fts USING fts5(full_name, emails, org, notes, content='');
CREATE TABLE recent_address (email TEXT PRIMARY KEY, name TEXT, last_used INT, count INT);

CREATE TABLE calendar (ra_id TEXT, id TEXT, name TEXT, color TEXT, tz TEXT, is_visible INT,
  is_default INT, my_rights TEXT, PRIMARY KEY (ra_id, id));
CREATE TABLE event (rowid INTEGER PRIMARY KEY, ra_id TEXT, id TEXT, calendar_ids TEXT, uid TEXT,
  title TEXT, start_utc INT, end_utc INT, tz TEXT, all_day INT, rrule_json TEXT,
  overrides_json TEXT, alerts_json TEXT, raw_json TEXT, updated INT, UNIQUE (ra_id, id));
CREATE TABLE event_instance (event_rowid INT REFERENCES event ON DELETE CASCADE,
  start_utc INT, end_utc INT, PRIMARY KEY (event_rowid, start_utc));  -- expansion cache, ±18 months

CREATE TABLE sync_state (ra_id TEXT, type TEXT, state TEXT, updated INT,
  PRIMARY KEY (ra_id, type));   -- type: Mailbox|Email|Thread|Identity|AddressBook|ContactCard|Calendar|CalendarEvent
CREATE TABLE op_queue (id INTEGER PRIMARY KEY, ra_id TEXT, kind TEXT, payload TEXT,
  base_state TEXT, created INT, attempts INT DEFAULT 0, last_error TEXT, status TEXT);
CREATE TABLE outbox (id INTEGER PRIMARY KEY, ra_id TEXT, identity_id TEXT, draft_email_id TEXT,
  mime_path TEXT, envelope_json TEXT, send_at INT, status TEXT, attempts INT, last_error TEXT);
CREATE TABLE draft_local (id TEXT PRIMARY KEY, ra_id TEXT, json TEXT, server_email_id TEXT, updated INT);
CREATE TABLE settings (key TEXT PRIMARY KEY, value TEXT);
```
FTS5 is contentless: insert rows when bodies are fetched (headers-only rows first, body appended
on fetch via delete+reinsert). Rebuild command available in settings.

## 5. Sync engine

Per login, a supervisor task runs:
1. **Session**: fetch Session; if `state` changed since last run, refresh caps and remote accounts.
2. **Initial sync** (empty `sync_state`):
   - `Identity/get`, `Mailbox/get` (all).
   - `Email/query` per mailbox sorted `receivedAt desc`, limit 500 for first page, then backfill in
     pages of 500 in the background (newest first, Inbox and Sent first).
   - `Email/get` with header properties only (`id, threadId, mailboxIds, keywords, size, receivedAt,
     sentAt, messageId, inReplyTo, references, subject, from, to, cc, bcc, replyTo, preview,
     hasAttachment, blobId`). Use back-references (`#ids`) in one request.
   - Record `state` from each `/get` into `sync_state`.
3. **Delta sync**: `Mailbox/changes`, `Email/changes`, `Thread/changes` with `maxChanges=500`,
   loop while `hasMoreChanges`. Fetch created/updated headers; delete destroyed. If server returns
   `cannotCalculateChanges`, drop that type's local data for the account and re-run initial sync for it.
4. **Push**: open EventSource (`eventSourceUrl` with `types=*&closeafter=no&ping=60`). On `StateChange`
   for an account, run the matching `/changes`. Debounce 500 ms. Reconnect with exponential backoff
   1 s → 5 min, jitter ±20%. If push unavailable, poll every 2 min (configurable). Network change
   (NetworkManager D-Bus, optional) or resume-from-suspend triggers immediate delta sync.
5. **Bodies**: fetched lazily on open (`Email/get` with `bodyValues`, `fetchHTMLBodyValues`,
   `fetchTextBodyValues`, `maxBodyValueBytes` 2 MB) or prefetched for messages within
   `fetch_bodies` policy when idle. Full raw .eml via `downloadUrl` only on demand.
6. **Attachments**: downloaded only on save/open/inline display; stored in `blobs/` by blobId.
7. **Mutation queue** (`op_queue`): UI ops apply optimistically to the local store, then enqueue.
   Worker sends `Email/set` with `ifInState` = last known Email state where sensible.
   - `stateMismatch`: run delta sync, re-apply op if still meaningful (flags/moves are idempotent:
     patch keywords/mailboxIds rather than replacing), else drop and emit `queue://conflict`.
   - `notFound` on an email: drop silently (already deleted elsewhere).
   - Network errors: keep, retry with backoff; queue survives restarts.
   - Ordering per account is FIFO; coalesce consecutive flag ops on the same email.
8. **Sending**: outbox worker: upload MIME blob → `Email/import` into Drafts (or `Email/set` create),
   then `EmailSubmission/set` with `onSuccessUpdateEmail` moving to Sent and removing `$draft`.
   Status via `outbox://update`. Failed sends stay in outbox with visible error; never silently drop.
9. **Retention** (see 6.10).
10. **Contacts/Calendars**: same pattern with `AddressBook`, `ContactCard`, `Calendar`, `CalendarEvent`
    types — gated on capability presence in the Session (see 8, 9).

Bandwidth rules: headers first, bodies lazy, attachments on demand, batch method calls (≤ server
`maxCallsInRequest`, ≤ `maxObjectsInGet`), respect `maxSizeRequest`.

## 6. Mail features (MVP)

6.1 Views: Unified Inbox / Unread / Flagged / Drafts / Sent (role-based, across accounts) plus
per-account mailbox tree. Unread counts in sidebar. Accounts collapsible; order and hide in settings.
6.2 Mailboxes: create/rename/delete/move (`Mailbox/set`); subscribe toggle; role mapping from server.
6.3 Threading: server `threadId`; thread view shows conversation oldest→newest, collapsed read items.
Toggle threaded/flat per view.
6.4 Flags: read/unread, flagged, answered/forwarded (`$answered`, `$forwarded`), junk (`$junk`, move
to Junk role). Mark-read-on-open after 1 s (configurable).
6.5 Move / delete / archive: delete moves to Trash; delete in Trash = permanent (`Email/set destroy`)
with confirm. Archive = move to Archive role mailbox (create if missing, ask first).
Undo toast (5 s) for move/delete/archive before op is flushed.
6.6 Search: instant local FTS5 over headers+fetched bodies; "Search server" expands with `Email/query`
filters (`text`, `from`, `to`, `subject`, `before`, `after`, `hasAttachment`, `inMailbox`).
Query syntax: `from:x to:x subject:x has:attachment is:unread is:flagged before:2026-10-01 after:… in:mailbox`.
6.7 Compose: new/reply/reply-all/forward (inline or as attachment); identity picker (default by
rules in 3); To/Cc/Bcc with autocomplete; plain text or HTML (simple editor: bold/italic/link/list/
quote, contenteditable with sanitized output); quoted original with attribution line using 24h time.
6.8 Drafts: autosave locally every 5 s, to server every 30 s and on close (`Email/set` replace in Drafts).
6.9 Attachments: drag-drop or picker; inline images get `Content-ID` and `cid:` references;
size warning > 20 MB; open/save received attachments.
6.10 **Folder retention**: per mailbox rule "keep newest N" and/or "older than D days".
Enforced by the app via JMAP: `Email/query` on the mailbox sorted `receivedAt desc` with
`position=N`, then move results to Trash or destroy (rule option). Runs after each delta sync for that
mailbox at most once per hour. Clearly labeled "enforced while Pavillion is running" — it is
client-side logic over JMAP, not a server policy. (Use case: willard@500foods.com report folders.
If Stalwart gains/has a server-side per-folder retention feature, prefer it; verify.)
6.11 Signatures: per identity, HTML + text alternatives, inline images stored locally and attached as
`multipart/related` with `cid:` on send (Andrew's bots use cid images; must round-trip correctly).
6.12 Notifications: desktop notification per new message in Inbox (role inbox, or flagged mailboxes),
grouped when > 3 in 10 s; per-account mute; clicking focuses the message. Skip messages already seen.
6.13 Keyboard shortcuts (all remappable in settings later; defaults):

| Key | Action | Key | Action |
|---|---|---|---|
| `j` / `k` | next / previous message | `Enter` / `o` | open |
| `n` / `p` | next / previous thread item | `u` / `Esc` | back to list |
| `c` | compose | `r` | reply |
| `a` | reply all | `f` | forward |
| `e` | archive | `#` / `Del` | delete |
| `m` | move (picker) | `l` | label/keyword (later) |
| `s` / `*` | toggle flag | `Shift+U` / `Shift+I` | mark unread / read |
| `/` | search | `g i` / `g s` / `g d` | go inbox / sent / drafts |
| `g c` / `g a` | go calendar / contacts | `Ctrl+Enter` | send |
| `Ctrl+S` | save draft | `?` | shortcut help |
| `Ctrl+1/2/3` | mail / contacts / calendar | `F5` | sync now |

## 7. HTML mail safety

- Sanitize in the C core with an allowlist of tags and attributes for email layout (tables, inline
  styles via filtered `style`, `img`, fonts); strip `script`, `iframe`, `object`, `embed`, `form`,
  `input`, `base`, `meta refresh`, event handlers, `javascript:`/`data:` URLs except `data:image/*`.
- Rewrite remote resources (`img src`, CSS `url()`, `background`) to a placeholder unless the
  sender/domain is in `remote_allow` or the user clicks "Load remote content" for this message.
  Track `has_remote` to show the banner.
- `cid:` references rewritten to a Tauri custom protocol `pav-cid://<email_rowid>/<cid>` served from
  local blobs (protocol handler checks the cid belongs to that email).
- Render in `<iframe sandbox srcdoc=…>` with **no** `allow-scripts`, no `allow-same-origin`, no
  `allow-forms`, no `allow-popups`. Inject a CSP meta into srcdoc:
  `default-src 'none'; img-src pav-cid: data: [https: only when allowed]; style-src 'unsafe-inline'; font-src data:`.
- The iframe never gets the Tauri IPC; enforce with Tauri capability config (IPC only for the main
  window origin) and verify in tests that `window.__TAURI__` is undefined inside the frame.
- Links: the parent intercepts clicks via a transparent overlay technique or by rewriting `href` to
  `pav-link://` handled by the protocol handler, which emits an event; the UI shows a confirm dialog
  with the real URL (highlight when display text and target domain differ), then opens via opener.
- Plain-text view toggle always available; default to plain for messages from unknown senders
  optional setting.
- Never auto-open attachments; warn on executable types.

## 8. Contacts

- Requires JMAP for Contacts (RFC 9610, `urn:ietf:params:jmap:contacts`). **Verify that the running
  Stalwart version advertises this capability and which JSContact properties round-trip.** If absent,
  contacts stay local-only until P6 CardDAV adapter.
- Address books: list, create, rename, delete, default book per account.
- Contact CRUD with editor: name parts, nicknames, emails (typed), phones, addresses, org/title,
  notes, birthday, photo (blob). Store full JSContact in `card_json`; edit known fields, preserve unknown.
- Search: FTS over name/emails/org/notes.
- Compose autocomplete: contacts + `recent_address` (populated from sent mail), ranked by
  frequency × recency, shows account color.
- Import: `.vcf` (vCard 3/4, multi-card files), JSContact JSON. Export same. Map vCard with
  the C core (libical's vCard support, or a small parser in that module if the mapping is awkward).
- Migration: import CardBook/Betterbird exports (vCard) into a chosen book.

## 9. Calendar

- Requires JMAP Calendars (`urn:ietf:params:jmap:calendars`, IETF draft). **Verify Stalwart support and
  draft revision; property names may differ between revisions. Isolate mapping in one module.**
  If unsupported, calendars are local-only until the CalDAV adapter (P6), or move CalDAV earlier.
- Calendars: list, color, visibility toggle, default calendar; shared calendars from other accounts
  (e.g. work@/home@) shown when exposed as separate JMAP accounts or shared calendars.
- Views: month, week (default), day, agenda (next 30 days). Week starts Monday (setting).
  24h clock. Default tz `America/Vancouver`; per-event tz shown when different.
- Event CRUD: title, location, description, start/end or all-day, tz, calendar, alerts, recurrence.
  Drag to create/move/resize in week/day views.
- Recurrence: RRULE via libical, EXDATE, RDATE, overridden instances (`recurrenceOverrides`).
  Edit scope: this / this and following / all. Expansion cached in `event_instance` for a rolling
  window (−6 / +18 months), recomputed on change.
- Timezones: system zoneinfo via libical; store UTC + IANA tz; handle DST gaps/overlaps; floating times for all-day.
- Reminders: a scheduler in the C core wakes for the next alert, emits `calendar://reminder` + desktop notification;
  snooze 5/10/30 min. Only while app runs (autostart recommended).
- Import/export `.ics`. Migration from Omacal: import its .ics export or its data source (open question).
- Later (P6): iTIP invitations — show REQUEST in mail with Accept/Tentative/Decline, send REPLY via
  JMAP (or `CalendarEvent/participantReply` if supported); update attendee status.

## 10. UI / UX

The first window, `api.js`, and the rule that screens do not call Tauri themselves are in [docs/D-UI.md](docs/D-UI.md). The notes below are the later screens.

- Shell: left nav rail (Mail / Contacts / Calendar / Files / Settings), status dot per account.
  Settings is a screen with sections, not a popup. Activity sits on the rail footer and is always reachable.
  The first settings control is shell zoom (50–300%, steps of 25%). Each later panel stores its own zoom.
  Window placement and maximized state are restored on launch. Chrome may be the system frame or hidden,
  in which case the page header is the drag region.
- Mail: 3-pane (sidebar mailboxes | message list | reading pane); toggle to wide list + bottom pane.
  Message list row: account color bar, from, subject, preview, 24h time or date, flags, attachment icon.
- Contacts: books | list | detail. Calendar: mini-month + calendar list | main view.
- Theming: dark first. The hello frame in [docs/D-UI.md](docs/D-UI.md) keeps its five variables and does not read `preferences.theme`. The later theme layer follows Lithium (`docs/Li/LITHIUM-CSS.md`): semantic custom properties, dark defaults, and a theme is an override of those properties. No UI framework, no hardcoded colors in components, and no import of Lithium's stylesheets or fonts. Light and follow-system (`prefers-color-scheme`) are choices on that layer. Fonts: system UI font, monospace for the headers view. Density setting (compact/comfortable).
- Editors: CodeMirror 6 for HTML source and for Lua source, the generation Lithium already uses. SunEditor is the rich mail composer. Neither is a dependency until that screen exists.
- Icons: Font Awesome Duotone, Sharp, and Thin. Those families are Pro. They are not loaded from a CDN, and they are not added until a license or kit is in the repo.
- Times: 24h default everywhere (`HH:mm`), dates ISO-ish (`2026-10-14`) or short `Oct 14`; setting.
- Accessibility: full keyboard navigation, visible focus rings, ARIA roles on lists/trees/grid,
  contrast ≥ 4.5:1 in both themes, respect `prefers-reduced-motion`, font scaling via settings.
- Single instance. Window state is the profile's `preferences.window`. Tray icon optional (Wayland support varies).

## 11. Security

- Credentials only in the Secret Service keyring; never in a profile, SQLite, logs, crash output or
  frontend memory beyond the add-account form (cleared after submit). See [docs/B-CONFIG.md](docs/B-CONFIG.md).
- TLS required (libcurl with the system trust store); no "accept invalid cert" toggle.
  Optional per-account pinned CA path for internal endpoints.
- App CSP (tauri.conf): `default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline';
  img-src 'self' pav-cid: data:; frame-src 'self'; connect-src ipc: http://ipc.localhost`.
  No remote origins in the app window. Tauri capabilities: least privilege per window.
- No in-app updater and no telemetry. No outbound requests except configured JMAP endpoints and
  user-approved remote images/links. Package updates come from the system repo in §12.
- Logging: the core logger in [docs/C-CORE.md](docs/C-CORE.md). Redact `Authorization`
  headers, passwords, tokens, message bodies; log IDs and sizes, not content. Debug log level opt-in.
- SQLite file mode 0600; blobs dir 0700.
- Temp files for opened attachments in `$XDG_RUNTIME_DIR/pavillion/`, removed on exit.

## 12. Packaging

- One executable, `pavillion`. `make build` builds the release binary and copies it to `dist/pavillion`. That is the file you run. Cargo still writes `src-tauri/target/release/pavillion` on the way there. `build/` is the C library and the Unity test. `ui/dist/` is the page Tauri embeds. None of those three is the program.
- The binary links WebKitGTK from the system (`webkit2gtk-4.1`). It does not contain Chromium, and it does not contain WebKit. Tauri's AppImage copies those libraries into the image and grows from a few megabytes into the tens. `bundle.active` is false, so `make build` does not produce an AppImage, a deb, or an rpm. The rpm remains the Fedora package, and when it is built it will depend on the system WebKitGTK package. An AppImage is not a target while it would vendor that library. `.deb` stays out. Arch is a `PKGBUILD` that links the system library.
- Release compile is already LTO, opt-level 3, `panic = abort`, and strip. When `upx` is on the path, the release binary is packed afterwards. If the packed binary does not start, the unpacked binary ships and the pack failure is logged. UPX is not required to build. Packing does not swallow WebKitGTK, because WebKitGTK is not in the file.
- `cargo tauri build` embeds `ui/dist` in that binary, and the WebView loads it through Tauri's custom protocol. Dev keeps using the Vite URL and does not embed. The UI is not a Hydrogen payload. Hydrogen appends an encrypted archive after the executable because the C program serves Swagger. Pavillion has no HTTP server for its own window.
- `pavillion --help` and `pavillion --version` are A-6. They print the version and the release date
  without opening a window.
- Updates are a dnf repository fed by the rpm this build produces. That repo is a packaging priority
  once A through D are done, ahead of an in-app updater. The rpm depends on system WebKitGTK. An Arch
  `PKGBUILD` (Omarchy) builds from source and links the same system library. `.deb` is not required.
- Runtime deps: `webkit2gtk-4.1`, `libsecret`/a Secret Service provider (gnome-keyring or KeePassXC),
  `libayatana-appindicator` only if tray enabled.
- Desktop file `com.500nodes.Pavillion.desktop`: `MimeType=x-scheme-handler/mailto;text/calendar;text/vcard;`
  plus `Categories=Network;Email;Calendar;ContactManagement;`.
- mailto: register via `tauri-plugin-deep-link`; document `xdg-mime default com.500nodes.Pavillion.desktop
  x-scheme-handler/mailto`. Single-instance forwards `mailto:` to running app → opens composer.
- Autostart: optional via `tauri-plugin-autostart` (XDG autostart), off by default; start minimized option.
- Wayland: no X11-only APIs; test under GNOME (Fedora) and Hyprland (Omarchy). Set
  `WEBKIT_DISABLE_DMABUF_RENDERER=1` fallback documented if rendering glitches.
- `make dev`, `make build`, and `make test` at the repo root. `make build` publishes `dist/pavillion`. `make clean` removes `build/` and `dist/`.

## 13. Testing

- C unit tests use the Unity framework, the same ThrowTheSwitch Unity Hydrogen uses, vendored
  under `tests/` without Hydrogen's mock set. Cover MIME parse/build round-trip, sanitizer policy,
  query parser, RRULE expansion including DST transitions in America/Vancouver, identity selection,
  queue coalescing, and retention selection.
- JMAP fixtures in `tests/support/`: recorded Session, Mailbox/Email/Thread get/changes/query/set,
  `cannotCalculateChanges`, `stateMismatch`, 401, 429/503 with retry, and an EventSource stream
  with drops. The sync engine runs against those fixtures. A tiny local HTTP server is enough;
  do not take on libmicrohttpd for the product.
- Integration (opt-in, `PAVILLION_IT=1`): a dedicated test account on Stalwart (to be created by
  Andrew; e.g. a 500nodes test mailbox). Tests create/delete their own mailbox `pavillion-it-*`;
  never touch other folders. Covers send-to-self, drafts, move, flags, contacts/calendar if supported.
- Sample corpus `tests/corpus/`: plain, multipart/alternative, HTML newsletters, cid-image signatures
  (copy pattern from Andrew's bot reports), huge attachments, malformed MIME, non-UTF-8 charsets,
  and nasty HTML (script, `onerror`, CSS `url()` tracking, `<base>`, `<meta refresh>`, SVG with script,
  `javascript:` links, look-alike link text). Assertion: nothing executes, no network request made.
- Frontend: Vitest, as in Lithium, for modules that do not need a window. Playwright (optional) against `tauri dev` or a mock-API web build
  for keyboard flows, compose, iframe isolation check.
- Performance budgets: cold start < 1.5 s with 20 accounts cached; list scroll 60 fps at 100k messages;
  local search < 100 ms on 200k messages.

## 14. Phased plan

Effort is rough focused agent time with Andrew review; adjust after P1.

**P0 — Skeleton + login + mailbox list (2–3 days)**
- Tauri 2 shell, C core, Vite UI, config, libsecret, SQLite migrations, logging.
- Add account (Basic/bearer), session discovery, `Mailbox/get`, sidebar tree with counts.
- Accept: add 3 Stalwart accounts across 2 tenants; mailbox trees and unread counts match the server;
  password stored in keyring only (grep data dirs: no secret); bad password shows one error, no loop.

**P1 — Read mail (4–6 days)**
- Initial + delta sync of headers, message list (virtualized), thread view, lazy bodies, sanitizer,
  sandboxed iframe, remote-image blocking + allowlist, attachments save/open, flags, move/delete/archive
  (online), unified views.
- Accept: all ~20 accounts synced; reading a bot report with cid images renders inline images;
  corpus nasty-HTML suite passes; changes made in another client appear within one poll/push cycle.

**P2 — Compose, send, drafts (4–5 days)**
- Composer, identity selection, reply/forward quoting, attachments, inline images, HTML signatures,
  drafts autosave/server sync, outbox + EmailSubmission, mailto handler.
- Accept: send from 3 identities on different tenants; recipients receive correct From, threading
  headers, signature with cid image intact; draft survives app kill; failed send visible and retryable.

**P3 — Search, offline queue, push (4–5 days)**
- FTS5 index + query syntax, server search merge, op_queue with conflict handling, EventSource push
  with backoff, retention rules, notifications, keyboard shortcuts complete.
- Accept: offline: flag/move/delete 20 messages, reconnect, server matches; conflicting change from
  another client resolved without data loss; new mail notification < 5 s with push; retention keeps
  exactly N newest in a willard@ folder.

**P4 — Contacts (3–4 days)**
- Verify JMAP Contacts on Stalwart first. Address books, CRUD, search, autocomplete, vCard import/export.
- Accept: import a CardBook/Betterbird vCard export; edit on server reflects locally and vice versa;
  autocomplete ranks frequent recipients first.

**P5 — Calendar (8–12 days)**
- Verify JMAP Calendars on Stalwart first (go/no-go; else pull CalDAV adapter forward).
- Calendars, 4 views, CRUD, recurrence with exceptions, timezones, reminders, .ics import/export.
- Accept: weekly recurring event with one moved and one cancelled instance displays correctly across
  the Nov DST change; reminder fires on time; Omacal data imported.

**P6 — Polish (open-ended; 1–3 weeks)**
- iTIP invitations, shortcut remapping, density options, Betterbird mail import (mbox/Maildir)
  if wanted, an Arch package. AppImage stays out while it would vendor WebKitGTK (§12). IMAP/SMTP mail is a backend from the start (§2.1),
  not a P6 feature. CalDAV and CardDAV stay out until a later decision.
- Accept: per-feature, agreed with Andrew at the start of P6.

Total to daily-driver mail (P0–P3): ~3 weeks. Full replacement (P0–P5): ~5–6 weeks.

## 15. Open questions for Andrew

1. Which accounts first (P0/P1 set)? Andrew@500foods.com + which 500nodes/500courses accounts?
2. Exact JMAP session URLs per tenant, and does each tenant use its own Stalwart instance/hostname?
3. Which Stalwart version is running, and does it advertise `urn:ietf:params:jmap:contacts` and
   `urn:ietf:params:jmap:calendars`? (Decides P4/P5 vs pulling the DAV adapter forward.)
4. Shared calendars (work@ / home@): separate logins, or shared into one account? Needed in P5?
5. Notification style: every message, Inbox only, or per-folder opt-in? Quiet hours?
6. Migration: import existing mail history (or rely on server-side only)? CardBook/Betterbird
   vCard export available? How does Omacal store data (local .ics, CalDAV, other)?
7. Retention: move trimmed messages to Trash or destroy permanently? Same rule for all willard@ folders?
8. Should a server-side retention in Stalwart be preferred if available (the Willard watcher spec also
   proposes trimming)? Avoid two trimmers on the same folders.
9. App-password vs. main password per account; any accounts using bearer/API tokens?
10. Need PGP or S/MIME at any point?
11. Repo location and name (e.g. `/mnt/extra/Projects/Pavillion`, GitHub/origin remote)?
12. Fedora rpm only to start, or also Omarchy build from day one?

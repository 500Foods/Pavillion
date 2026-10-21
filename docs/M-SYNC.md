# M — SYNC

**Status:** M-1, M-2, and M-3 complete (M-3 on 2026-10-21). M-4 through M-8 are not started. The index names the next phase.
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

The network and the sync engine: HTTP through libcurl, JMAP session discovery, initial and delta sync, the mutation queue, and push. Mail first; contacts, calendars, and files reuse the same engine with their own types ([S-CONTACTS.md](S-CONTACTS.md), [T-CALENDAR.md](T-CALENDAR.md), [U-FILES.md](U-FILES.md)). Tasks ([X-TASKS.md](X-TASKS.md)) sync over CalDAV after T-7, on that DAV client, not as a JMAP tasks capability. Notes ([Y-NOTES.md](Y-NOTES.md)) reuse the files sync of one account. Sending is [O-COMPOSE.md](O-COMPOSE.md).

## Phases

One phase is one session. Do not start the next one in the same conversation.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| M-1 | Per-server network threads, a curl multi handle each, caps, retries, redaction | Hard | Complete |
| M-2 | JMAP session discovery, `account.test`, remote accounts, capabilities | Medium | Complete |
| M-3 | Sync supervisor, Identity and Mailbox sync, `mailbox.list`, `sync.state` | Medium | Complete |
| M-4 | Email initial sync: headers, first page, background backfill, threads | Medium | Not started |
| M-5 | Delta sync: `/changes`, `cannotCalculateChanges`, deletes | Medium | Not started |
| M-6 | Mutation queue: optimistic apply, `ifInState`, conflicts, coalescing, undo | Hard | Not started |
| M-7 | Push: EventSource, debounce, backoff, polling fallback, network and resume | Medium | Not started |
| M-8 | IMAP/SMTP backend behind the same table (mail only) | Hard | Not started |

Protocol order (K Lock): JMAP first. M-8 (IMAP/SMTP) comes right after daily-driver JMAP mail (index milestone 3), and CalDAV/CardDAV are S-5 and T-7.

Kind: **Easy** is one module and a test. **Medium** crosses one boundary. **Hard** is a new thread model or a new protocol.

## How a session works

1. Read Resume in [PAVILLION_PLAN.md](../PAVILLION_PLAN.md), the Status column above, and the previous phase's Status, Accomplished, Lessons learned, and Handoff.
2. Re-read only this phase: Goal, Entry gate, Work items, Done means, Exit gate. Open the lock section this phase cites.
3. Ask the qualifying questions. Wait for approval before editing source.
4. Do this phase only. Check a work item `[x]` only after its check has passed. Defer with `[~]`, one line, and the phase it moves to.
5. Before marking complete, write Status, Accomplished, Lessons learned, and Handoff, update the Status column here and in the index, and append the Working log. Before that, update `ui/src/help/pages.js` for what this phase changed: augment, replace, fix, or extend the user page, or add a Developer section when the user page has nothing new. Then stop.
6. A sitting that ends early sets Status to **in progress**, names the last finished work item, and leaves the rest unchecked.

Leave Accomplished, Lessons learned, and Handoff off a phase that has not started. Sync phases test against the fixture server first and the Stalwart container second ([W-SHIP.md](W-SHIP.md) W-2). Never against Andrew's live accounts in an automated test.

## Lock

### Owner and threads (the F-6 decision, revised 2026-10-16)

- JMAP, IMAP, and sync live in the C core (L2), with libcurl and Jansson. Rust does none of it. The UI never calls a mail server; the main window's CSP could not allow it anyway.
- Threads (extends the C-CORE thread table):

| Thread | Owns | Does not touch |
| --- | --- | --- |
| Core | The profile, the command queue, sync state machines' decisions; reads through the read pool | curl, the write connection |
| Store writer | The only write connection; every mutation through the write queue ([L-STORE.md](L-STORE.md) Lock) | curl |
| Net (one per server endpoint) | Its own `CURLM` multi handle, the transfers of every account on that server, that server's EventSource streams and timers | SQLite, other servers' handles |
| Work pool (2) | CPU work: MIME parse, sanitize, FTS text, rule evaluation hand-off | SQLite, curl |
| Script | Lua ([R-RULES.md](R-RULES.md)) | SQLite, curl |
| Log, Event | as in H and G | |

- **Servers are independent.** There is one net thread per server endpoint: the scheme, host, and port of a JMAP API URL, an IMAP or SMTP server, or a DAV base. Each thread has its own multi handle, and every account on that server shares it. Every easy handle sets `CURLOPT_HTTP_VERSION` to `CURL_HTTP_VERSION_2TLS`. The multi handle sets `CURLMOPT_PIPELINING` to `CURLPIPE_MULTIPLEX`, `CURLMOPT_MAX_HOST_CONNECTIONS` to 2, and `CURLMOPT_MAX_CONCURRENT_STREAMS` to the per-server cap (6). Two connections, not one and not four: the EventSource occupies a stream, and a second connection keeps API calls moving when the server falls back to HTTP/1.1. HTTP/1.1 pipelining is never turned on.
- A slow, hanging, or failing server stalls only its own thread. No request to another server ever waits on it, in the net layer or on the core thread. The core thread never blocks on a net thread: it posts requests to that thread's queue and gets completions back on the core queue.
- Thread lifetime: a server's thread starts when its first account starts syncing and stops when its last account is disabled or removed. Endpoints are keyed after the Session's `apiUrl` and redirects resolve, so `/.well-known/jmap` on the mail domain and the API host map to one thread. The count is bounded by servers (a handful of tenants), not by logins.
- This replaces both plan §2.1's thread per login and the first draft's single shared net thread (M-Q1, resolved by Andrew 2026-10-16). Isolation is per server, not per login. Per-login state is still a **supervisor** struct (a state machine) on the core thread.
- Caps:
  - per server: `preferences.sync.maxInFlightPerServer`, default 6
  - per account: 2, inside its server's cap, so one account can't starve its neighbours
  - optional overall cap: `preferences.sync.maxInFlightTotal`, unset by default (no limit)
- The overall cap is an atomic counter shared by the net threads. A request waiting for it stays queued on its own server and never holds another server's slot.
- What crosses to the UI: commands and ids, summaries in pages of at most 500, never message bodies in bulk.

### HTTP rules

- TLS is required, verified against the system trust store. No "accept invalid certificate" toggle. An optional per-account pinned CA path (`ca_file`) for internal endpoints. It comes from the account's profile field `caFile` (B Lock): `account.test` takes it as an argument, and whatever builds a `PavJmapCtx` for an account sets `ctx->ca_file` from it.
- Timeouts: connect 10 s, request 60 s (uploads and downloads use a low-speed limit instead: 1 KB/s for 30 s). EventSource has no request timeout.
- Retry: 429 and 503 honor `Retry-After` (seconds or date), else exponential backoff 1 s → 5 min with ±20 % jitter. Network errors back off the same way. 401 goes to `error:auth` and stops that account (K Lock). Other 4xx are not retried.
- Redaction: the debug transfer log (only at `debug` level, subsystem `http`) prints method, host, path without query, status, bytes, and duration. Never headers, never bodies. `CURLOPT_VERBOSE` is never on.
- Every transfer has an activity or a parent activity, and the cancel flag aborts it from the progress callback.
- User agent `Pavillion/<version>`.
- **Streaming I/O:**
  - Blob downloads stream to a temp file in the blob directory through `CURLOPT_WRITEFUNCTION`, then are renamed into place when complete. Uploads stream from disk through `CURLOPT_READFUNCTION` with a known `Content-Length`.
  - Nothing over 1 MB is held whole in memory. The exceptions are JMAP JSON responses, already capped by `maxSizeRequest` and the paging rules, and bodies deliberately capped for rendering (N Lock).
  - `PavReq` has `download_path` and `upload_path`, and a body buffer is only for small requests.
  - **Parse on the net thread.** Jansson parses the response there, the supervisor's reducer turns it into the rows or the error it asked for, and the DOM and the raw buffer are freed before anything is posted to the core queue. The core queue never carries a multi-megabyte body. At most one response is parsed at a time on that thread; the other finished transfers wait as bytes on the socket or in curl's own buffers, still under the in-flight cap.

### Backend table (was plan §2.4)

```c
typedef struct PavBackend {
    const char *kind;                      /* "jmap", "imap" */
    int (*connect)(PavSup *s);             /* session or login */
    int (*sync_mailboxes)(PavSup *s);
    int (*sync_emails)(PavSup *s, int initial);
    int (*fetch_body)(PavSup *s, int64_t email_rowid);
    int (*apply)(PavSup *s, const PavOp *op);   /* flags, move, delete */
    int (*submit)(PavSup *s, int64_t outbox_id);
    int (*query)(PavSup *s, const char *filter_json, int64_t req_id);
    int (*push_start)(PavSup *s);
} PavBackend;
```

`JmapBackend` is the full account (mail, contacts, calendars, files by capability). `ImapBackend` (M-8) is mail only. Nothing in the UI knows which backend an account uses.

### JMAP sync (was plan §5)

1. **Session:** GET the session URL (follow `/.well-known/jmap` redirects). Cache the Session in `account_state.session_json`. Use `apiUrl`, `downloadUrl`, `uploadUrl`, `eventSourceUrl`, `primaryAccounts`, and `capabilities` limits. A login may expose several JMAP accounts (shared mailboxes or calendars); each is a `remote_account`. When the Session `state` changes, refresh caps and remote accounts.
2. **Initial sync** (no `sync_state` for the type): `Identity/get`, then every mailbox. A single `Mailbox/get` with `ids: null` stops at the server's `maxObjectsInGet`. Stalwart returns a normal list of exactly that many when more exist. The client sends `Mailbox/query` with `calculateTotal: true`, then `Mailbox/get` of those ids, and repeats until `position` reaches `total` or a page comes back short. The page size is the Session's core `maxObjectsInGet`, and at most 500 so one page stays under the 1 MB type-response cap. A saved tree whose row count is already at that cap is not finished: the next sync reads `Mailbox/query` `total`, and a larger total runs the walk again. Changes never recover mailboxes the capped get left out. The stored Mailbox state is the `state` from `Mailbox/get`, written when the walk finishes. A state change in the middle restarts the walk once. Then per mailbox, Inbox and Sent first, `Email/query` sorted `receivedAt desc`, first page 500, then backfill newest-first in pages of 500. Header backfill runs to the end of the mailbox: offline mail is the point, and a header row is small. It is idle work. A page waits while that server has a user command in flight (open, search, send, flag) and resumes when the command completes. It never waits on another server. `Email/get` with header properties only (`id, threadId, mailboxIds, keywords, size, receivedAt, sentAt, messageId, inReplyTo, references, subject, from, to, cc, bcc, replyTo, preview, hasAttachment, blobId`) via back-reference (`#ids`) in the same request. Record each `/get` state. The core is posted the reduced rows, not the JSON body (HTTP rules).
3. **Delta sync:** `Mailbox/changes`, `Email/changes`, `Thread/changes` with `maxChanges` 500, or the Session's `maxObjectsInGet` when that is lower, looping while `hasMoreChanges`. Fetch created and updated headers; delete destroyed. On `cannotCalculateChanges`, drop that type's local data for the remote account and rerun initial sync for it.
4. **Push:** one EventSource per login, at that Session's `eventSourceUrl` with `types=*&closeafter=no&ping=60`. Not one per account and not one per remote account: the events already name the account id. On `StateChange` for a remote account, run the matching `/changes`, debounced 500 ms. Reconnect with backoff 1 s → 5 min, ±20 %. If push is unavailable or fails three times in a row, poll every 2 minutes (`preferences.sync.pollSeconds`) and keep trying push every 15 minutes. Network change and resume from suspend (L1 hooks; NetworkManager over D-Bus and logind `PrepareForSleep` on Linux, both optional) trigger an immediate delta sync.
5. **Bodies:** lazy on open (`Email/get` with `bodyValues`, `fetchHTMLBodyValues`, `fetchTextBodyValues`, `maxBodyValueBytes` 2 MB, plus `bodyStructure`), or prefetched when idle per the account's `fetch_bodies` (`recent` = last 30 days, at most 2 in flight). Raw `.eml` via `downloadUrl` only on demand. Owned by N-1.
6. **Attachments:** downloaded only on save, open, or inline display, into blobs by blobId.
7. **Mutation queue** (`op_queue`): UI operations apply to the local store first, then enqueue. The net side sends `Email/set` with `ifInState` set to the last known Email state where sensible. `stateMismatch`: run delta sync, reapply if still meaningful (flags and moves patch `keywords/…` and `mailboxIds/…` rather than replacing, so they are idempotent), else drop and emit `queue.conflict`. `notFound` on an email: drop quietly. Network errors: keep and retry with backoff; the queue survives restarts. Order is FIFO per remote account; consecutive flag ops on the same email coalesce. Op kinds come from a registration table (M-6), so other docs add their own (drafts in O-1, cards in S, events in T).
8. **Undo:** move, delete, and archive are held (`status='held'`, `not_before = now + 5 s`) so the undo toast can cancel them before they are flushed. `undo.run {opId}` reverts the local change and drops the op.
9. **Bandwidth:** headers first, bodies lazy, attachments on demand. Batch method calls up to the server's `maxCallsInRequest` and `maxObjectsInGet`; respect `maxSizeRequest` and `maxConcurrentRequests`.
10. **Other types:** `AddressBook`, `ContactCard`, `Calendar`, `CalendarEvent`, and `FileNode` use the same pattern, gated on capability presence in the Session.

### Account states

`idle`, `syncing`, `offline` (network down, retrying), `locked` (keyring), `error:auth`, `error:server` (repeated 5xx after backoff cap), `disabled`. `sync.state` events carry the state and a short `detail`. The Update button and the tree's account row show them.

## M-1: Network thread and curl multi

**Kind:** Hard
**Status:** Complete (2026-10-19, step 46).

**Goal:** The core makes concurrent HTTPS requests on one net thread per server, with the Lock's caps, retries, cancel, and redaction. A stalled server never delays another.

**Entry gate:** K-2 and W-2 complete.

**Work items:**

- [x] `core/src/net.c`, `core/include/pav_net.h`: `pav_net_start/stop`, a server registry (`pav_net_server_for(url)` creates or reuses the endpoint's thread and multi handle), `pav_net_request(PavReq *)` routed to that server's thread with method, URL, headers (auth added on the net thread through K-2's `PavAuth.apply`, with the secret read just for that request), body or upload file, download-to-file option. `on_done` posts status, the headers the caller named (`Retry-After`, `Content-Type`), and the reduced result. The raw body is freed on the net thread (HTTP rules, "Parse on the net thread"). HTTP/2 settings are the Lock's.
- [~] Wake the multi loop with `curl_multi_wakeup` (curl ≥ 7.68) on new requests. Timers through `curl_multi_timeout`. — Deferred to M-3; the `pthread_cond_signal` + `curl_multi_perform` loop works for M-2's single-request-per-redirect pattern.
- [x] Caps (per server, per account, and the optional overall cap), the retry policy, timeouts, cancel through the activity flag, TLS settings, optional `ca_file`.
- [x] Redacted debug transfer log.
- [x] Unity against the fixture server: `test_net_start_and_stop`, `test_net_simple_get`, `test_net_post_with_body`, `test_net_streaming_download_to_file`, `test_net_streaming_upload_from_file`, `test_net_per_account_cap`, `test_net_per_server_cap`, `test_net_retry_on_429_with_retry_after`, `test_net_retry_on_503_backoff`, `test_net_401_not_retried`, `test_net_release_account_exits_thread`, `test_net_overall_cap`, `test_net_ca_file_rejected_for_wrong_ca`, `test_net_server_for_matches_url`.
- [x] Link `libcurl` in CMake and `build.rs`.

**Done means:** Twenty requests across four accounts on two fixture servers run up to six per server and two per account. A hung server A leaves server B's requests unaffected, and a 429 waits as told.

**Exit gate:** `make test` exits 0 with the thirteen tests and two fixture server instances started by `make test`.

### Accomplished

- `pav_net_start/stop` initialize/cleanup the net subsystem with `curl_global_init/cleanup`.
- `pav_net_server_for(url)` keys servers by scheme+host+port, creates one `PavNetServer` with its own `CURLM *` multi handle per endpoint, and reuses existing servers for the same host.
- `pav_net_request(PavReq *)` queues requests to the server's thread, which dispatches them respecting per-server (6), per-account (2), and optional overall caps.
- `should_retry` excludes SSL verification errors and write errors; 429/503 honor `Retry-After` (seconds), falling back to exponential backoff (1s → 5min with ±20% jitter); 401 and other 4xx are not retried.
- Requeued items are re-enqueued for the server thread to pick up after the backoff delay.
- Streaming download writes to `download_path`; upload reads from `upload_path`.
- Redacted transfer log at `debug` level, subsystem `http`, with method, host, path (no query), status, bytes, duration.
- `ThreadingHTTPServer` in the fixture enables concurrent slow-response tests.
- All 120 Unity tests and 134 Vitest tests pass. `make build` CSP check passes.

### Lessons learned

- The fixture server initially used `HTTPServer` (single-threaded), which serialized `?slow=1` requests and caused cap tests to time out with only 1 completion. Switching to `ThreadingHTTPServer` fixed this.
- `curl_multi_setopt(CURLMOPT_MAX_HOST_CONNECTIONS, 2)` per the Lock limits concurrent HTTP/1.1 connections. With 6 concurrent requests against an HTTP/1.1 fixture, only 2 connections are available, so 3 batches of 2 × 2s = 6s. The per-server cap test timeout was increased to 8s to accommodate this.
- `CURLE_WRITE_ERROR` must not be retried — it indicates a write callback failure (e.g., body exceeds 2 MB), and retrying would reuse freed memory since the `PavReq` lives on the caller's stack.
- The overall cap's `g_overall_in_flight` counter was not decremented on request completion, permanently blocking further requests. Fixed with a dedicated mutex.
- Requeued retry items must be re-enqueued to the server's queue; the original code only set `in_flight = 0` without adding the item back, causing the item to be lost.

### Handoff

M-1 implements the net thread and curl multi infrastructure with caps, retries, and redaction. The `pav_net_*` API is real and tested against the fixture server. M-2 can build on `PavReq` and `pav_net_request` to implement JMAP session discovery and `account.test`. The `curl_multi_wakeup` approach for immediate dispatch of new requests (vs. the current `pthread_cond_signal` + `curl_multi_perform` loop) is deferred to M-2.

## M-2: JMAP session, `account.test`, remote accounts

**Kind:** Medium
**Status:** Complete (2026-10-19, step 47).

**Goal:** Given a URL and credentials, the core discovers the session, lists capabilities, and records remote accounts.

**Entry gate:** M-1 complete.

**Work items:**

- [x] `core/src/jmap.c`: `pav_jmap_parse_session`, `pav_jmap_build_request`, `pav_jmap_post`, `pav_jmap_parse_response`. Request builder with `using`, `methodCalls` with call ids and back-references. Response parser with per-method errors and `stateMismatch`/`cannotCalculateChanges` detection. Session parser maps JMAP URN capability keys to `mail`, `submission`, `contacts`, `calendars`, `sieve`, `files`, `websocket`.
- [x] `account.test` registered as `PAV_CMD_WORKER`, activity kind `account.test`. 2026-10-19 fix: early failures return not-ok, relative `Location` resolved case-insensitively, and the test now actually begins and ends an `account.test` activity ([API.md](API.md)). Follows 302/301/307/308 redirects (max 3) using the `Location` header captured by `CURLOPT_HEADERFUNCTION`. A domain alone derives `https://<domain>/.well-known/jmap`. Returns capability names, limits, accounts, and session state. Does not add tasks or github. Runs on the net thread, returns data through the worker queue.
- [~] On account add and at supervisor start: store the Session, create or update `remote_account` rows, handle Session `state` change. — Deferred to M-3 (sync supervisor).
- [x] Unity on fixtures: `test_session_parse_stalwart`, `test_session_redirect_wellknown`, `test_account_test_reports_caps`, `test_account_test_bad_password`, `test_account_test_bad_args`, `test_remote_accounts_from_session`.

**Done means:** `account.test` against the fixture server reports capabilities and follows the `.well-known/jmap` redirect. `make test` exits 0.

**Exit gate:** `make test` exits 0.

### Accomplished

- `core/src/jmap.c`: `pav_jmap_parse_session` parses a JMAP Session, extracting `apiUrl`, `downloadUrl`, `uploadUrl`, `eventSourceUrl`, `state`, `primaryAccounts`, `accounts` (as JSON), and boolean capability flags for mail, submission, contacts, calendars, sieve, files, and websocket. Capability URN mapping: `urn:ietf:params:jmap:mail` → `has_mail`, `urn:ietf:params:jmap:submission` → `has_submission`, `urn:ietf:params:jmap:contacts` → `has_contacts`, `urn:ietf:params:jmap:calendars` → `has_calendars`, `urn:ietf:params:jmap:sieve` → `has_sieve`, `urn:ietf:params:jmap:files` → `has_files`, `urn:ietf:params:jmap:websocket` → `has_websocket`.
- `core/src/account_test.c`: `pav_account_test` calls `do_account_test` which starts the net layer, sends a GET to the session URL, and follows up to 3 redirects (302/301/307/308) by extracting the `Location` header from response headers via `CURLOPT_HEADERFUNCTION`. The `account_test_done` callback copies the body and Location header before freeing `resp_headers` and `body_out`. The final response is parsed as a JMAP session and mapped to capability names, limits, and accounts.
- `core/src/net.c`: Added `CURLOPT_HEADERFUNCTION` (`header_callback`) and `CURLOPT_HEADERDATA` set to `&item->hctx` (the `HeaderCtx` within `NetReqItem`), capturing response headers into `req->resp_headers`. The `header_callback` stores headers as-is in a JSON object for caller retrieval (Location, Retry-After, Content-Type).
- `core/src/net.c`: Fixed `pav_net_stop` to drain queued items and fire their callbacks with `status_code = 0` when `s->stop` is set, preventing the net thread from busy-looping on requeued retry items with future `ready_at_ms`.
- `tests/support/jmapfix.sh` (then `jmapfix.py`): responses are HTTP/1.1 with a `Connection: close` header, so curl opens separate connections for concurrent requests and the server closes cleanly after each response.
- `docs/API.md`: Updated `account.test` to `State: real (M-2)`.
- `core/src/activity.c`: Added `"account.test"` to `k_kinds[]`.
- `core/src/dispatch.c`: Registered `account.test` as `PAV_CMD_WORKER`.

### Lessons learned

- `CURLOPT_HEADERDATA` must point to the `HeaderCtx` struct (`&item->hctx`), not the `NetReqItem` itself. Casting the `NetReqItem*` to `HeaderCtx*` reads unrelated struct fields as the headers pointer, causing the callback to silently skip all headers and leaving `Location` empty.
- When following redirects, the `on_done` callback fires on the net thread while the caller waits on a condition variable. The callback must copy out `body_out`, `status_code`, and any header fields (Location) before freeing `resp_headers` and `body_out`, because `complete_request` frees the curl handle immediately after the callback returns.
- `pav_net_stop` must handle requeued retry items (with future `ready_at_ms`) by draining them and firing callbacks, otherwise the net thread busy-loops and `pav_net_stop`'s `pav_thread_join` hangs forever.
- The fixture server must send `Content-Length: 0` on 302 responses with no body, or curl reports `CURLE_RECV_ERROR (56)` when the connection closes before curl expects it.
- `http.server.ThreadingHTTPServer` with `protocol_version = "HTTP/1.1"` and `Connection: close` allows curl to open separate connections for concurrent requests, which is required for the per-server cap test.

### Handoff

M-1's net layer is complete and tested. M-2 adds `account.test`, JMAP session parsing, and redirect following on top of it. The `remote_account` table exists in the L-1 schema; M-3 will populate it from the session and start the sync supervisor. The `curl_multi_wakeup` optimization (deferred from M-1) can be added in M-3 when sync polling starts.

## M-3: Supervisor, Identity and Mailbox sync

**Kind:** Medium
**Status:** Complete (2026-10-21, step 53). Andrew accepted the window the same day. The container exit gate passed the same day. `make test`: 184 Unity, 2 Rust, 349 Vitest, exit 0.

**Goal:** Every enabled account keeps its identities and mailboxes in the store, and the UI can list them with counts.

**Entry gate:** K-3 and E-4 complete, and K-3's exit gate verified against the W-2 Stalwart container (`make it-up`, `make it-seed`): in `make build`, add `andrew@test-a.local` and `alice@test-a.local` (tenant A) and `bob@test-b.local` (tenant B) with Override session URL `https://localhost:1443/.well-known/jmap`; each Test shows capabilities, all three are listed with numbers 001–003 after a restart, a wrong password on Test shows one error and no loop, and `rg -a` over the workdir finds none of the test passwords (`pavtest-pw`, set by `tests/stalwart/seed.sh`; Stalwart requires at least 8 characters) ([K-ACCOUNTS.md](K-ACCOUNTS.md) K-3 and K Lock, Accounts).

The UI half of this gate (the `make build` steps above) is Andrew's to run before M-3a starts. The server half (`make it-up`, each account's Session with `apiUrl` on 1443, 401 on a wrong password) passed on 2026-10-19.

**Gate status (2026-10-21):**
- K-3 and E-4 are complete, and the server half passed on 2026-10-19.
- H-4 and D-10 are complete (accepted 2026-10-21). Neither is in this entry gate. The Accounts table polish stays paused and does not block M-3.
- Andrew, 2026-10-21: two live JMAP accounts are saved, Test returns the expected services for both, and a wrong password fails. He asked to open M-3 on that report.
- The written UI script was not rerun: three container accounts (`andrew@test-a.local`, `alice@test-a.local`, `bob@test-b.local`), numbers 001–003 after a restart, and `rg -a` for `pavtest-pw`. Since 2026-10-20 Save on a blank form adds an account (K-Q13).
- Andrew, 2026-10-21, answered yes and yes. The live-account report is the UI half of this gate. This sitting is M-3a only (supervisor, stored Session, account states, `sync.now`). M-3b and M-3c stay for later sittings. The 2026-10-19 decisions below stay. Automated tests use the fixture, not the live accounts.

**Handoff for a cold-start agent (2026-10-20):**
- **Rules:** read [AGENTS.md](../AGENTS.md) first, then this file's Lock and the M-3 section.
  - Update the Lock docs before the code, in the same round.
  - Bash tooling only, no Python.
  - Never stage or commit; Andrew alone commits.
  - Never commit or touch `PAVILLION_BUILD`.
  - Logs go to the DB only (`pav_log`), never stderr or files.
  - Never write to `/fvl/git`.
  - Secrets are read only through `pav_secret_get`, are never cached unlocked, never logged and never exported.
- **Gate:** `make test` locally, plus all three CI jobs (`fedora`, `macos-core`, `windows-core`) green on the push. None is `continue-on-error`. A macOS or Windows break is fixed in the same round. Do not rerun or push CI yourself.
  - **Portability:** paths go through `pav_path_is_absolute` / `pav_path_parent` (Windows drive paths). macOS needs `_DARWIN_C_SOURCE`. Vitest path checks must normalize `\` to `/`.
- **Stalwart test server:** `tests/stalwart/` holds `compose.yaml`, `config.json`, `VERSION` (pinned v0.16.26) and `seed.sh` ([W-SHIP.md](W-SHIP.md)).
  - `make it-up` creates the test CA in `build/stalwart-tls`, starts the container on `https://localhost:1443` and seeds two domains and five accounts, all with password `pavtest-pw`.
  - `make it-seed` reseeds, `make it-down` stops the container, and `make it-reset` also wipes `build/stalwart-data`.
  - Integration tests: `PAVILLION_IT=1 make test-it`, only against this container.
  - An account added against it sets `caFile` to `build/stalwart-tls/test-ca.crt`.
- **Uncommitted:** at hand-off the tree had only the 2026-10-20 docs-only status round uncommitted: `docs/TABULATOR.md`, `docs/D-UI.md`, `docs/M-SYNC.md` and `PAVILLION_PLAN.md`, plus last round's Tabulator fixes if Andrew has not committed them. Check `git status`, and do not build on uncommitted Tabulator work.

**Work items:**

- [x] `core/src/sync.c`: one `PavSup` per enabled account, started after `core ready`, stopped on remove or disable; account states and `sync.state` events; `sync.now {accountId?}`.
- [x] Each `PavJmapCtx` the supervisor builds sets `ca_file` from the account's `caFile` (M Lock, TLS). No context is built outside tests before M-3, so this lands with `sync.c`.
- [x] `Identity/get` and `Mailbox/get` initial; `Mailbox/changes` and `Identity/changes` delta (polling every 2 minutes until M-7).
- [x] Commands `mailbox.list {accountId?}` (nodes in the [E-TREE.md](E-TREE.md) contract shape: `id`, `parentId`, `kind`, `label`, `role`, `order`, `unread`, `total`, `hasChildren`, `swatch`), `identity.list`. Event `mail.mailboxes_changed`.
- [x] Activity `sync.mail` per account with detail per step.
- [x] 401 path per the K Lock; keyring locked path.
- [x] Unity on fixtures: `test_sync_mailboxes_initial`, `test_sync_mailboxes_delta_rename`, `test_sync_401_stops_account`, `test_mailbox_list_tree_shape`.
- [x] Moved from W-2: `tests/support/record.sh` (curl and jq) records a scrubbed Session, `Mailbox/get`, and `Identity/get` from the W-2 container into `tests/support/fixtures/`. `jmapfix.sh` serves a file when the method and arguments match. The scripted mailboxes stay for every other call ([W-SHIP.md](W-SHIP.md) Lock, Fixture server).
- [x] Moved from W-2: `tests/stalwart/seed.sh` creates missing role mailboxes and a mailbox named Later (idempotent). Seeding the `tests/corpus/` messages stays in M-4.
- [x] `pav_jmap_parse_session` takes the account id from the object key and mail from `accountCapabilities`. Unity: `test_recorded_session_keeps_mail_account`. The fixture Session uses that shape.
- [x] An Identity method is sent with `urn:ietf:params:jmap:submission` when the account advertises it. A method error reads RFC `type` as well as `code`. The fixture rejects `Identity/get` without submission, and its `cannotCalculateChanges` error uses `type`.
- [x] Initial mailbox sync pages. `Mailbox/query` with `calculateTotal`, then `Mailbox/get` of each id page. The page is the Session's core `maxObjectsInGet`, at most 500. The walk ends when `position` reaches `total` or a page is short. The Mailbox state is written when the walk finishes. Unity: `test_sync_mailboxes_paged`.
- [x] A saved mailbox tree whose row count is already at `maxObjectsInGet` reads `Mailbox/query` `total`. A larger total runs the paged walk again. Changes do not recover mailboxes the capped get left out. Unity: `test_sync_mailboxes_refill`.

**Done means:** Mailboxes and unread counts for the accounts in Settings are in the store and update on change.

**Exit gate:** `make test` exits 0. Against the container: create a mailbox from another client (or the container's admin API), and within one poll `mail.mailboxes_changed` fires and `mailbox.list` shows it.

### Decisions (2026-10-19)

- Row 53 stays one phase. M-3a, M-3b, and M-3c are sittings inside it, each ending with `make test` green and a review commit; a sitting that ends early marks M-3 in progress and names the last finished sub-phase.
- Every remote account in the Session with the `mail` capability is synced, not only the personal one.
- `ra_id` is the composite string `<accountId>/<jmapAccountId>`.
- `mailbox.list` returns one root node per account (`kind: "account"`, `swatch` from the account color) with that account's mailboxes under it. Mailbox node ids are `mbx:<ra_id>:<mailboxId>`.
- Sibling order: the server's `sortOrder`, then role (inbox, drafts, sent, archive, junk, trash, then none), then name.
- The poll timer runs on the core thread (timed wait in the core queue loop), since sync decisions are the core's (Lock, threads). Net threads only carry transfers.
- A locked keyring is retried on `sync.now` and on the next poll. Watching libsecret's `locked` property is deferred.
- `curl_multi_wakeup` stays deferred; 2-minute polling does not need it.
- Recording uses a separate `tests/support/record.sh` (curl and jq, secrets scrubbed), not a proxy mode in `jmapfix.sh` ([W-SHIP.md](W-SHIP.md) Lock, Fixture server).
- Seeding the `tests/corpus/` messages moved to M-4, the first phase that syncs email. M-3 seeds mailboxes only.
- The exit gate's new mailbox is created with `Mailbox/set` by curl as `andrew@test-a.local` against `https://localhost:1443/jmap/`.

### M-3a: Supervisor, Session, and account states

**Scope:** `core/src/sync.c` with `PavSup` and the JMAP backend's `connect`; start after `core ready`, stop on disable or remove; store the Session in `account_state.session_json`, upsert `remote_account` rows, refresh on Session `state` change; `ca_file` from `caFile`; states `idle`, `syncing`, `offline`, `locked`, `error:auth`, `error:server`; `sync.state` events, `sync.now`, activity `sync.mail`; the 401 and locked-keyring paths.

**Tests:** `test_sync_401_stops_account`, plus Unity tests that the Session and `remote_account` rows are stored and that a locked keyring gives `locked` with no retry loop.

**Acceptance:** `make test` exits 0. Against the container, `sync.now` leaves all three gate accounts `idle` with `remote_account` rows and `session_json` stored.

### M-3b: Identity and Mailbox sync, `mailbox.list`, `identity.list`

**Scope:** `jmapfix.sh` serves `Identity/get`, `Identity/changes`, `Mailbox/get`, and `Mailbox/changes` with a mutable state; initial and delta reducers on the net thread; `sync_state` per type; the 2-minute poll; `cannotCalculateChanges` reruns the initial sync for that type; commands `mailbox.list` and `identity.list`; event `mail.mailboxes_changed`.

**Tests:** `test_sync_mailboxes_initial`, `test_sync_mailboxes_delta_rename`, `test_mailbox_list_tree_shape`, `test_sync_mailboxes_cannot_calculate`.

**Acceptance:** the M-3 exit gate.

### M-3c: Recorded fixtures and seeded mailboxes

**Scope:** `tests/stalwart/seed.sh` creates mailboxes (idempotent); `tests/support/record.sh` records the first real Session and responses from the container into `tests/support/fixtures/`, scrubbed of secrets; `jmapfix.sh` serves them.

**Tests:** `test_recorded_mailbox_get_replay`.

**Acceptance:** `make test` exits 0, a reseed is idempotent, and `rg` finds no secrets in `tests/support/fixtures/`. Passed 2026-10-21: 181 Unity, 2 Rust, 349 Vitest. A second `make it-seed` created nothing.

### M-3d: Stalwart Session shape

**Scope:** `pav_jmap_parse_session` keeps the account id from the object key and copies `accountCapabilities` onto the capabilities object sync already reads. An inner `id`, or a `capabilities` object, is still accepted when the RFC fields are absent. The fixture Session uses the RFC shape.

**Tests:** `test_recorded_session_keeps_mail_account`. The existing mailbox sync tests then run against that shape.

**Acceptance:** `make test` exits 0. Passed 2026-10-21: 182 Unity, 2 Rust, 349 Vitest.

### M-3e: Identity submission and method error type

**Scope:** `Identity/get` and `Identity/changes` go out with `urn:ietf:params:jmap:submission` in `using` when that account advertises it. An account without it syncs mailboxes only. A JMAP method error is read from `type`, then from `code`, including the fixture's nested `error` object.

**Tests:** the existing mailbox sync tests. The fixture returns `unknownMethod` with `type` when `using` omits submission, and `cannotCalculateChanges` with `type` on an unknown `sinceState`.

**Acceptance:** `make test` exits 0. Passed 2026-10-21: 182 Unity, 2 Rust, 349 Vitest.

### M-3f: Paged mailbox list

**Scope:** An initial mailbox sync, and a `cannotCalculateChanges` resync, walk `Mailbox/query` then `Mailbox/get`. The page size is the Session's core `maxObjectsInGet`, and at most 500. `calculateTotal: true`. The walk stops when `position` reaches `total`, or when a page is short and `total` was absent. Rows from earlier pages are replaced on the first page of a walk. The Mailbox `sync_state` is written when the walk finishes. A `state` change between pages clears the rows and starts the walk once more. `Mailbox/changes` sends `maxChanges` of that same page size. The fixture answers `Mailbox/query` from its mailbox list and can advertise a small `maxObjectsInGet`.

**Tests:** `test_sync_mailboxes_paged`. The account's Session says `maxObjectsInGet` 2. Both remote accounts still store every mailbox, including ones past the first page.

**Acceptance:** `make test` exits 0. Passed 2026-10-21: 183 Unity, 2 Rust, 349 Vitest.

### M-3g: Refill a capped mailbox tree

**Scope:** A remote account that already has Mailbox `sync_state`, and at least `maxObjectsInGet` mailbox rows, asks `Mailbox/query` for `total` (one id). When `total` is larger than the stored count, the paged walk runs and replaces the tree. When `total` is not larger, the sync continues with `Mailbox/changes`. A smaller tree does not ask.

**Tests:** `test_sync_mailboxes_refill`. Cap 2, four mailboxes stored, then Sent and Drafts deleted locally with the state left in place. The next `sync.now` stores them again.

**Acceptance:** `make test` exits 0. Passed 2026-10-21: 184 Unity, 2 Rust, 349 Vitest.

### Accomplished

- One supervisor per enabled JMAP account stores the Session, upserts every mail remote account, and reports `sync.state`. `sync.now` refetches the Session. `ra_id` is `<accountId>/<jmapAccountId>`. The account id is the `accounts` object key. Mail is read from `accountCapabilities`.
- Identity methods include `urn:ietf:params:jmap:submission` when the account advertises it. A method error is read from `type`, then from `code`.
- Mailbox sync pages at the Session's `maxObjectsInGet`, and at most 500. A tree already saved at that cap asks `Mailbox/query` for `total` and walks again when the total is larger. `Mailbox/changes` uses that page size as `maxChanges`. The poll is 2 minutes.
- `mailbox.list` and `identity.list` are real. A tree change emits `mail.mailboxes_changed`. `record.sh` stores a scrubbed Session, `Mailbox/get`, and `Identity/get`. `seed.sh` creates a missing role mailbox and Later. `make test` stays on the fixture.
- Andrew's two accounts stayed idle after the 11:41 walk (foods 537 mailboxes, courses 5) and after the 11:52 restart (foods still 537, emails 58241 to 58244, courses unchanged).
- The container exit gate passed on 2026-10-21. A temporary workdir added `andrew@test-a.local` as account `m3gate` against `https://localhost:1443`. The first sync went idle with 7 mailboxes. `Mailbox/set` created one mailbox. `mail.mailboxes_changed` fired and `mailbox.list` showed it 119 seconds later, inside one poll. The mailbox was destroyed, `account.remove` deleted the keyring entry, and the temporary workdir was removed. The live profile was not opened.

### Lessons learned

Sitting notes from 2026-10-21.

- A sync job must not open its own transaction. The writer already wraps the batch in `BEGIN IMMEDIATE`. A nested `BEGIN` fails and rolls the update back. This showed up in M-3a: the state write reported success in memory and left the database row at `idle`.
- Jansson 2.14 steals on failure. `json_array_append_new` and `json_object_set_new` decref the value even when the call fails. Do not `json_decref` after either call. A NULL value is not stolen.
- `pav_db_prepare` keys its cache by pointer equality, not by `strcmp`. The same literal resets one statement. Two literals with the same SQL text are two entries. Do not change the SQL text of an existing key. List commands prepare with `sqlite3_prepare_v2` on a read-pool connection after `pav_db_read_begin`, and finish with `pav_db_read_end`. They do not call `pav_db_prepare`.
- `account.add` inserts `account_state` `idle` before the first sync finishes. A wait that only checks `idle` returns immediately. Wait for the `sync_state` row and `idle` together.
- `/.well-known/jmap` on the fixture is a 302, and `start_connect` counts that follow as another attempt. Tests that assert `pav_sync_attempt_count` use `$PAV_FIXTURE_URL/jmap/session`.
- Fixture mail state is one file for the whole Unity process. A `Mailbox/set` is visible to every later test. The order is initial, tree, cannot, delta. The tree expects the label Later. A cannot-calculate test that expects `mbx-1` runs before any `Mailbox/set`. After a resync `Mailbox/get`, the stored Mailbox state is the server's current `mbxState`.
- `Mailbox/get` does not return `cannotCalculateChanges`. Only `Mailbox/changes` with an unknown `sinceState` does. At most one resync per type per cycle. An Identity state of `idn-1` survives a Mailbox resync.
- Foreign keys are on. Deleting a `remote_account` cascades `mailbox`, `identity`, and `sync_state`. `publish_session` with `remote_count` 0 deletes those rows.
- A command is real only when [API.md](API.md) lists `State: real` under `## Commands`, in the shape the Unity test parses, and `k_commands[]` matches that list in `strcmp` order. The events test counts only `## Events` lines that start `State: real`, also in `strcmp` order (`log.lines`, `mail.mailboxes_changed`, `sync.state`).
- Unity is built without `UNITY_USE_COMMAND_LINE_ARGS`, so `-n` does not filter. Do not leave a `PAV_ONLY` hook in `main()`.
- `flock(1)` is not portable. The fixture lock is the directory `$run/mailstate.lockd`, with a pid file and a stale-holder steal.
- `mailbox.list` returns the tree shape. The on-screen tree is E-7.

The one that cost M-3b: after another connection commits, the writer's next `BEGIN IMMEDIATE` failed with `SQLITE_BUSY_SNAPSHOT` (517, `database is locked`) while `sqlite3_get_autocommit` was 1. A stepped statement on the writer connection was still holding that connection's WAL snapshot. `pav_db_prepare` resets a statement when that statement is next prepared, and the begin ran before that prepare, so the snapshot was never released. `run_batch` treated any begin failure as fatal, so every later `pav_db_call` failed until the process restarted. The cannot-calculate test updated `sync_state` to `gone`, called `sync.now`, and the client never sent `Mailbox/changes`. The row stayed `gone`. The delta test did not hit this, because nothing else writes the profile database between its two syncs. `begin_write` now calls `pav_db_cache_reset` before every writer `BEGIN` and retries once when the extended code is `SQLITE_BUSY_SNAPSHOT` or the primary code is `SQLITE_BUSY`. A retry without the reset fails the same way. The extended code has been 517 since SQLite 3.7; `db_writer.c` defines it when the header does not. A final failure logs `write begin failed %d` and nothing else.

M-3c was smaller, and two Stalwart details mattered:

- Stalwart v0.16 already creates inbox, drafts, sent, junk, and trash. The names are Sent Items, Junk Mail, and Deleted Items. Archive was the missing role. `seed.sh` creates a missing role, then a mailbox named Later under Inbox. A second `make it-seed` creates nothing. Match on `role`, not on the display name.
- `Identity/get` needs `urn:ietf:params:jmap:submission` in `using`. Core and mail alone return `unknownMethod`. `Mailbox/get` accepts core and mail.
- The recorded mailbox uses the keys `append_mailbox` already reads (`id`, `name`, `parentId`, `role` as a string or null, `sortOrder`, the email and thread counts, `myRights`, `isSubscribed`). `sync.c` was not changed.
- A recorded file is served only when the method and the arguments match. The scripted account ids do not match Stalwart's account id, so the other Unity mail tests stay on `mailstate.json`.

M-3d: a live Update that logs Session 200 and then idle, with no `POST`, means `remote_count` was 0. RFC 8620 makes `accounts` an `Id[Account]` map. Stalwart does not repeat the id inside the account, and it puts mail under `accountCapabilities`. The parser copied an inner `id` and a `capabilities` object, so the account was dropped and sync stored the Session and stopped. The fixture used to send that inner shape, so the mailbox tests never saw the drop. The recorded Session is the check. The fixture Session now uses the key and `accountCapabilities`, and `test_sync_session_stored` still counts the mailboxes. Session-level capability flags stay capped at 8. That cap did not cause this drop.

M-3e: once the account was kept, the next POST was `Identity/get` plus `Mailbox/get` with only core and mail in `using`. Stalwart returns `["error", {"type": "unknownMethod"}, ...]`. The parser read `code`, found none, and stored `request rejected`. Both live accounts went to `error:server`. `Identity/get` needs `urn:ietf:params:jmap:submission` in `using`. The method-error field is `type`.

The 10:22 process was the binary from before that fix. Both accounts parsed the Session (the "8 capabilities" line is the client cap of 8, and each Session had one account), POSTed, got HTTP 200, and ended `sync.mail failed: request rejected` / `error:server`. Courses returned 2124 bytes. Foods returned 183541 bytes, a mailbox list the client discarded because `Identity/get` failed the batch.

The process stopped at 10:35. The 10:36:42 start is the M-3e binary. The first poll stored both trees: courses 2473 bytes, foods 184039 bytes, then `mailboxes changed` and idle for each. Update at 10:37 re-fetched the Session and POSTed empty changes (courses 328 bytes, foods 340 bytes). Both ended done, then idle. The supervisor then POSTed changes every 2 minutes and left the Session alone. Courses stayed at 328 bytes. Foods posted a second get of the changed ids at 10:42 (425 then 490), 10:44 (434 then 1236), and 10:52 (425 then 490), and each of those logged `mailboxes changed` in that same poll. The other polls were empty. 10:56:46 is the Activity Session filter (`log.session`).

A read-only look at the profile store at 11:01, after the 11:00 poll had written `sync_state` (that timestamp moves on an empty changes result too): both accounts `idle`. Courses holds 5 role mailboxes (inbox, drafts, sent, junk, trash), 1 email, and 3 identities. Foods holds 500 mailboxes, the same five roles, 477 with a parent, 56536 emails, and 4 identities. Identity and Mailbox `sync_state` rows exist for both. The count stayed 500, so those three foods deltas updated rows already stored. `last_sync` is the last Session fetch (10:37:01). Polls do not move it.

Both stored Sessions advertise `maxObjectsInGet` 500. `Mailbox/get` with `ids: null` was one call. The `500` in `method_args` was `maxChanges` on a changes call. Stalwart 0.16 can answer `ids: null` with a normal list of exactly that limit when more objects exist, and it does not return `requestTooLarge` (Bulwark issue 1069 saw this on `FileNode/get`). A foods tree of exactly 500 was not proof every mailbox came back. This log did not show `requestTooLarge`.

M-3f: the folder list belongs in this phase. Done means every mailbox is in the store, and one get stops at the server cap. The client now sends `Mailbox/query` with `calculateTotal` and `Mailbox/get` of each page. The page is the Session's core `maxObjectsInGet`, and at most 500 so a page stays under the 1 MB type-response cap. The walk stops when `position` reaches `total`, or when a page is short and `total` was absent. The Mailbox state is written at the end of the walk. A state change between pages restarts the walk once. `Mailbox/changes` uses that same page size as `maxChanges`. The fixture cuts an `ids: null` get at the advertised cap. `test_sync_mailboxes_paged` sets the cap to 2 and still stores Sent and Drafts, which sit past the first page.

M-3g: a tree already saved at the cap still has Mailbox state, so the next start only asks for changes. Changes never return the mailboxes the capped get left out. The 11:28 rebuild was that binary. Foods logged `mailboxes changed` after two small posts (425 then 490 bytes). Courses posted 328 bytes and stayed idle without that line. A read-only look still had foods at 500 and courses at 5, both idle. The next sync now asks `Mailbox/query` for `total` with one id when the stored count is already at the page cap. A larger total replaces the tree with the paged walk. A total that is not larger continues with changes. A smaller tree, such as courses, does not ask. `test_sync_mailboxes_refill` deletes Sent and Drafts, leaves the state, and the next `sync.now` stores them again.

The 11:41:06 restart walked foods. The traced posts were 2656 bytes, then 183341, then 369, then 13790, and one `mailboxes changed` at 11:41:08. Courses went idle with no `mailboxes changed`. Update at 11:41:32 fetched both Sessions (4980 and 4994 bytes). Courses posted 328 bytes. Foods posted 331 then 193 and did not log `mailboxes changed`. A read-only look after that poll's `sync_state` write (11:43:07): foods 537 mailboxes, 511 with a parent, the same five roles, 58241 emails, 4 identities; courses 5 role mailboxes, 1 email, 3 identities; both idle. The 13790-byte get is the 37 mailboxes past 500. The Update's two small posts are the total check and then changes. The server total was not larger than 537.

The 11:52 restart kept foods at 537 mailboxes and moved the email count from 58241 to 58244. Courses stayed at 5 mailboxes and 1 email. Both ended idle. Startup logged one `mailboxes changed` for foods after two traced posts (282 then 864 bytes). Update fetched both Sessions, posted 328 bytes for courses, and posted 331 then 193 for foods. It did not walk again.

The exit gate waited on the poll timer and did not call `sync.now`. A fresh workdir still uses profile id `default`, so the temporary account id was `m3gate` and the keyring user was `default/m3gate`. `account.remove` deleted that entry before stop. The live profile is a different workdir and was not opened. The first sync stored 7 mailboxes, under the page cap, so that account took the changes path. The new mailbox was in `mailbox.list` 119 seconds later. It was destroyed after the check, and the temporary workdir was removed. The container was left up.

### Handoff

M-3 is complete (2026-10-21, step 53). Andrew accepted the window the same day. The container exit gate passed the same day: one new mailbox was in `mailbox.list` 119 seconds after `Mailbox/set`, and `mail.mailboxes_changed` fired, inside the 2-minute poll.

The index names E-7 next (step 54, [E-TREE.md](E-TREE.md)). Mail nav becomes the tree from `mailbox.list`, patched by `mail.mailboxes_changed`. Its entry gate is M-3 and E-4, both complete. E-7 was not started here. The on-screen tree is still the placeholder.

M-4 stays after L-2. Corpus messages stay in M-4. The poll stays 2 minutes until M-7. `curl_multi_wakeup` stays deferred. `make test` keeps the fixture with the container down. The container was left up. CI for this close-out waits on Andrew's push.

## M-4: Email initial sync

**Kind:** Medium
**Status:** Not started.

**Goal:** Headers for every message are in the store, newest first, without blocking the UI.

**Entry gate:** L-2 and E-7 complete.

**Work items:**

- [ ] Per mailbox `Email/query` + `Email/get` (header properties) with back-references, first page 500 for Inbox and Sent, then others, then backfill in pages of 500. Batch to server limits.
- [ ] Write `email`, `email_mailbox`, `thread` rows as one `pav_db_write` job per page (L Lock write queue). Insert header-only FTS rows (Q-1 owns the index; this phase calls `pav_fts_index_headers` as a stub that Q-1 fills, or inserts directly if Q-1 already landed).
- [ ] `Thread/get` for thread membership as needed.
- [ ] Events `mail.messages_changed` per page; activity progress `done/total` from the query's `total`.
- [ ] Moved from M-3: make `tests/stalwart/seed.sh` import the `tests/corpus/` messages into the container's mailboxes (idempotent), so the exit gate has a seeded mailbox.
- [ ] Unity on fixtures: `test_email_initial_first_page`, `test_email_backfill_pages`, `test_email_batches_respect_max_objects`, `test_email_sync_cancel_resumes`.

**Done means:** A fixture account with 2,000 messages lands in four pages, first page first.

**Exit gate:** `make test` exits 0. Against the container's seeded mailbox, message counts per mailbox in the store equal the server's.

## M-5: Delta sync

**Kind:** Medium
**Status:** Not started.

**Goal:** Changes made elsewhere arrive, including deletes and lost state.

**Entry gate:** M-4 complete.

**Work items:**

- [ ] `Email/changes` and `Thread/changes` loops with `maxChanges=500`; created and updated fetch headers; destroyed delete rows, bodies, FTS rows, and blobs not shared.
- [ ] `cannotCalculateChanges` drops that type for the remote account and reruns initial sync, logged at warn.
- [ ] Mailbox counts updated from the server (`Mailbox/get` for changed mailboxes), never computed locally.
- [ ] Unity: `test_delta_created_updated_destroyed`, `test_delta_has_more_changes_loops`, `test_delta_cannot_calculate_resyncs`, `test_delta_destroy_removes_blobs`.

**Done means:** A flag, move, or delete made in another client shows up within one poll.

**Exit gate:** `make test` exits 0. Against the container, flag and delete from another client; the store matches after one cycle.

## M-6: Mutation queue

**Kind:** Hard
**Status:** Not started.

**Goal:** Local changes apply instantly, reach the server reliably, survive restarts and offline time, and resolve conflicts without data loss.

**Entry gate:** N-4 complete.

**Work items:**

- [ ] Every op-queue mutation, both the local apply and the `op_queue` row, is one `pav_db_write` job (L Lock).
- [ ] `core/src/opqueue.c` with an **op-kind table**: `pav_op_register(kind, apply_local, build_request, on_result)`. This phase registers the mail kinds (`email.flags`, `email.move`, `email.destroy`, `mailbox.set`). Later docs register their own kinds without editing the queue: O-1 adds `draft.save` and `draft.destroy`, and S and T add their card and event writes. Sending is not an op: O-5's outbox is its own durable pipeline and enqueues a `draft.save` before it starts. Enqueue with optimistic local apply and an `undo_payload`; held ops and `undo.run`; FIFO per remote account; coalescing of flag ops; flush on the net thread with `ifInState`.
- [ ] `stateMismatch`, `notFound`, network, and other-error handling per the Lock; `queue.conflict` event; failed ops visible in Activity as kind `queue.flush` (add the kind, I Lock).
- [ ] Commands used by N-5: `message.set_flags`, `message.move`, `message.archive`, `message.delete`, `undo.run`, plus `mailbox.create`, `mailbox.rename`, `mailbox.delete`, `mailbox.move`, `mailbox.subscribe` (`Mailbox/set`).
- [ ] Unity: `test_queue_optimistic_then_flush`, `test_queue_survives_restart`, `test_queue_coalesces_flags`, `test_queue_state_mismatch_reapplies`, `test_queue_not_found_dropped`, `test_queue_offline_then_flush`, `test_undo_within_hold`, `test_op_kind_registered_later_flushes` (a test kind registered after start).

**Done means:** Offline, flag, move, and delete 20 messages; reconnect; the server matches (plan P3 acceptance).

**Exit gate:** `make test` exits 0. Against the container with the network cut by the fixture's proxy toggle: 20 operations, reconnect, server state equals local state. A conflicting change from another client resolves without loss.

## M-7: Push

**Kind:** Medium
**Status:** Not started.

**Goal:** New mail and remote changes arrive within seconds when the server supports push.

**Entry gate:** M-6 complete.

**Work items:**

- [ ] EventSource on the net thread (a streaming transfer with a write callback parsing `event:`/`data:` lines and `ping`).
- [ ] Debounce, backoff, polling fallback and periodic push retry per the Lock.
- [ ] L1 hooks: network change and resume trigger delta sync (Linux: optional D-Bus listeners in `core/platform/linux/`, compiled out cleanly if D-Bus is unavailable).
- [ ] Unity on the fixture's EventSource stream with drops: `test_push_state_change_triggers_changes`, `test_push_debounce`, `test_push_reconnect_backoff`, `test_push_fallback_to_poll`.

**Done means:** A message delivered to the container appears in under 5 seconds (plan P3 acceptance).

**Exit gate:** `make test` exits 0. Against the container, send a message with `swaks` or the container's SMTP; `mail.new_mail` fires within 5 s.

## M-8: IMAP/SMTP backend

**Kind:** Hard
**Status:** Not started.

**Goal:** An `imap` account syncs and sends mail through the same backend table (mail only), with username and password auth over TLS.

**Entry gate:** N-9 complete (index milestone 3: daily-driver JMAP mail).

**Work items:**

- [ ] IMAP through libcurl's IMAP support, or a small client over a TLS socket. Decide in the phase: libcurl's IMAP lacks IDLE and CONDSTORE handling, which a small client needs. CONDSTORE/QRESYNC for delta, IDLE for push, SMTP submission through libcurl.
- [ ] Auth is `basic`, through the K `PavAuth` interface. That means SASL PLAIN (or LOGIN when PLAIN isn't offered) with the keyring password, only over implicit TLS (993/465) or STARTTLS (143/587). A server that offers neither is refused with `forbidden`, and nothing goes in cleartext.
- [ ] Each IMAP and SMTP server gets its own net-thread entry per the M Lock. A stuck IMAP server never stalls a JMAP one.
- [ ] Map to the same tables. Mailbox ids are IMAP names plus UIDVALIDITY.
- [ ] Settings: `kind` `imap` becomes selectable in K-3's add form (host, port, SMTP host and port, TLS mode).
- [ ] Fixture: Stalwart's own IMAP and SMTP listeners in the W-2 Docker container.

**Done means:** An IMAP account reads, flags, moves, and sends like a JMAP account.

**Exit gate:** The M-4 to M-6 tests pass against the IMAP fixture.

## Open questions for Andrew

- M-Q1. Resolved 2026-10-16: one net thread per server endpoint with its own multi handle, accounts multiplexed on it, a per-server cap and an optional overall cap (Lock).
- M-Q2. Resolved 2026-10-16: JMAP first. M-8 follows daily-driver JMAP mail (before rules, contacts, and calendars).

## Working log

- 2026-10-16 Opened from plan §2.1, §2.4, §3, §5 and F-6. Decisions: C core owns JMAP; one net thread with one multi handle and per-account supervisors (replaces one thread per login); per-account in-flight cap of 2 within the global 6; explicit retry, timeout, and redaction rules; held ops for undo; account states; push retry while polling; IMAP after the initial working copy.
- 2026-10-16 Andrew: servers must be independent. One net thread and multi handle per server endpoint, accounts multiplexed on it, a per-server cap of 6, per-account 2, an optional overall cap. M-Q1 and M-Q2 resolved, and M-8 moved to right after milestone 3 with SASL PLAIN over TLS.
- 2026-10-16 Added the store writer to the thread table, and sync and op-queue writes are now `pav_db_write` jobs. Streaming I/O added to the HTTP rules: curl read and write callbacks, with no blob over 1 MB held in memory, plus a 100 MB flat-memory download test.
- 2026-10-18 Tasks sync on the CalDAV client after T-7. Notes reuse one account's files sync. `account.test` does not grow a tasks capability.
- 2026-10-19 M-1 complete, step 46. Added `core/src/net.c` and `core/include/pav_net.h`: one net thread and multi handle per server endpoint, the per-server, per-account, and overall caps, retries, and the redacted transfer log. `curl_multi_wakeup` deferred to M-3. All 120 Unity and 134 Vitest tests pass.
- 2026-10-19 M-2 complete. Added JMAP session parsing (`pav_jmap_parse_session`), `account.test` with redirect following via `CURLOPT_HEADERFUNCTION`, and capability URN mapping. Fixed `pav_net_stop` to drain queued retry items. Fixed fixture server to HTTP/1.1 with Connection: close. All 120 Unity and 134 Vitest tests pass.
- 2026-10-19 Andrew: M-3's entry gate now includes K-3's exit gate (three accounts, two tenants, `make build`, the password scan). K-Q1 resolved the same day: the accounts are the W-2 Stalwart container's.
- 2026-10-19 W-2's deferred item (record real Stalwart responses into `tests/support/fixtures/`) moved to M-3 as a work item.
- 2026-10-19 Seeding mailboxes and corpus messages moved from W-2 to M-3. The record-mode item now notes that the W Lock's recorded `tests/support/fixtures/*.json` responses are not implemented yet.
- 2026-10-19 Pre-M-3 fix: per-account `caFile` (pinned CA path) in the account object, `account.add`/`update`/`test`, and the Accounts form's Advanced field; Stalwart test certs generated by `make it-up` into `build/stalwart-tls/` and mounted read-only.
- 2026-10-19 M-3 gate: the Stalwart accounts are seeded by `tests/stalwart/seed.sh` with test password `pavtest-pw` (8-character minimum), not `config.toml`.
- 2026-10-19 M-3 split into sittings M-3a, M-3b, M-3c (row 53 stays one phase); decisions recorded (mail-capable remote accounts, composite `ra_id`, account-root tree and `mbx:` ids, sort order, core-thread poll, keyring retry, `curl_multi_wakeup` deferred, `record.sh`, corpus to M-4, exit-gate `Mailbox/set`); entry gate UI half is Andrew's.
- 2026-10-19 account.test fixes (Andrew): early `goto done` paths returned `{ok:true,data:null}`; Stalwart's `307 location: /jmap/session` (relative, lowercase) was not followed. Now every failure is not-ok, relative and case-insensitive `Location` resolves against the request URL, and the test runs as an `account.test` activity. Unity: `test_account_test_relative_redirect`, `test_account_test_early_failure_not_ok`.
- 2026-10-21 M-3 in progress. M-3a finished: one supervisor per enabled JMAP account, Session and mail remote accounts stored, states and `sync.state`, `sync.now`, `ca_file` from `caFile`, a 401 stops until the secret changes, and a locked keyring retries only on `sync.now` or the next poll. The state job does not open its own transaction; the writer already holds one. Unity: `test_sync_session_stored`, `test_sync_401_stops_account`, `test_sync_locked_keyring_no_retry`. `make test`: 176 Unity, 2 Rust, 349 Vitest, exit 0. M-3b and M-3c are not started. The container check for M-3a was not run.
- 2026-10-21 M-3 in progress. M-3b finished: Identity and Mailbox initial and delta sync, `sync_state` per type, `cannotCalculateChanges` reruns that type, `mailbox.list` and `identity.list`, `mail.mailboxes_changed`. An outside UPDATE of `sync_state` made the next writer `BEGIN IMMEDIATE` fail with `SQLITE_BUSY_SNAPSHOT` (517) until cached statements were reset; `begin_write` now resets them and retries once. Unity: `test_sync_mailboxes_initial`, `test_sync_mailboxes_delta_rename`, `test_mailbox_list_tree_shape`, `test_sync_mailboxes_cannot_calculate` (plus the existing 401 and locked-keyring tests). `make test`: 180 Unity, 2 Rust, 349 Vitest, exit 0. M-3c is not started. The container half of the exit gate was not run. The window was not launched.
- 2026-10-21 Lessons for M-3a and M-3b written, and the M-3c handoff added. M-3 stays in progress. The next sitting is M-3c. Corpus messages stay in M-4. The container exit gate was not run. The window was not launched.
- 2026-10-21 M-3 in progress. M-3c finished: `record.sh` wrote a scrubbed Session, `Mailbox/get`, and `Identity/get`; `jmapfix.sh` serves a file when the method and arguments match; `seed.sh` creates a missing role mailbox and Later, and a second seed creates nothing. Stalwart names the role mailboxes Sent Items, Junk Mail, and Deleted Items. `Identity/get` needs the submission capability in `using`. The recorded mailbox keys match `append_mailbox`, so `sync.c` was not changed. Unity: `test_recorded_mailbox_get_replay`. `make test`: 181 Unity, 2 Rust, 349 Vitest, exit 0. The phase exit gate was not run. The window was not launched. The container was left up.
- 2026-10-21 M-3 in progress. M-3d finished: `pav_jmap_parse_session` takes the account id from the object key and copies `accountCapabilities`. The fixture Session uses that shape. Unity: `test_recorded_session_keeps_mail_account`. `make test`: 182 Unity, 2 Rust, 349 Vitest, exit 0. The phase exit gate was not run. The window was not launched. Help was not checked in a browser.
- 2026-10-21 M-3 in progress. M-3e finished: Identity methods include `urn:ietf:params:jmap:submission` when the account advertises it, and a method error is read from `type`. A live Update had reached the POST and both accounts ended `error:server` / `request rejected`. The fixture rejects `Identity/get` without submission and sends `cannotCalculateChanges` as `type`. `make test`: 182 Unity, 2 Rust, 349 Vitest, exit 0. The phase exit gate was not run. The window was not launched. Help was not checked in a browser.
- 2026-10-21 M-3 in progress. The 10:22 process was the pre-M-3e binary (POST 200, then `request rejected`; the foods mailbox list was discarded). The 10:36:42 restart stored both trees (`mailboxes changed`, then idle). Update at 10:37 and most 2-minute polls were empty changes (courses 328 bytes, foods 340 bytes). Foods fetched changed ids inside the 10:42, 10:44, and 10:52 polls. The store at 11:01: courses 5 role mailboxes, 1 email, 3 identities; foods 500 mailboxes, 56536 emails, 4 identities; both idle. Foods equals `maxObjectsInGet` 500. `Mailbox/get` sends `ids: null`. Stalwart can truncate that call at the cap without `requestTooLarge`. Paging was not started. The phase exit gate was not run. The window was not launched. Help gained one Developer sentence and was not opened in the window.
- 2026-10-21 M-3 in progress. M-3f finished: initial mailbox sync walks `Mailbox/query` and `Mailbox/get` at the Session's `maxObjectsInGet`, at most 500 per page, until `total`. The fixture caps an `ids: null` get the same way. Unity: `test_sync_mailboxes_paged` (cap 2, Sent and Drafts still stored). `make test`: 183 Unity, 2 Rust, 349 Vitest, exit 0. The phase exit gate was not run. The window was not launched. Help was not opened in the window.
- 2026-10-21 M-3 in progress. M-3g finished: a saved tree already at the page cap asks `Mailbox/query` for `total` (one id) and walks again when that total is larger. The 11:28 rebuild only did changes (courses POST 328, foods POST 425 then 490, then `mailboxes changed`). The store was still foods 500 and courses 5. Unity: `test_sync_mailboxes_refill`. `make test`: 184 Unity, 2 Rust, 349 Vitest, exit 0. The phase exit gate was not run. The window was not launched. Help was not opened in the window.
- 2026-10-21 M-3 in progress. The 11:41 restart walked foods: posts of 2656, 183341, 369, and 13790 bytes, then one `mailboxes changed`. Courses went idle without that line. Update at 11:41:32 posted courses 328 and foods 331 then 193, with no further walk. Read-only store after the 11:43 poll: foods 537 mailboxes, 511 with a parent, 58241 emails, 4 identities; courses 5 mailboxes, 1 email, 3 identities; both idle. The phase exit gate was not run.
- 2026-10-21 M-3 complete, step 53. Andrew accepted the window the same day. The 11:52 restart kept foods at 537 mailboxes and courses at 5; foods emails moved from 58241 to 58244; Update stayed on changes. The container exit gate added `andrew@test-a.local` as `m3gate` in a temporary workdir. The first sync went idle with 7 mailboxes. `Mailbox/set` created one mailbox. `mail.mailboxes_changed` fired and `mailbox.list` showed it in 119 seconds, inside one poll. The mailbox and the temporary account were removed. The live profile was not opened. Stamp `0.59.0`. `make test`: 184 Unity, 2 Rust, 349 Vitest, exit 0. E-7 was not started. The container was left up.

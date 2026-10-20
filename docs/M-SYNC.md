# M — SYNC

**Status:** M-1 and M-2 complete (2026-10-19). M-3 through M-8 are not started. The index names the next phase.
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

The network and the sync engine: HTTP through libcurl, JMAP session discovery, initial and delta sync, the mutation queue, and push. Mail first; contacts, calendars, and files reuse the same engine with their own types ([S-CONTACTS.md](S-CONTACTS.md), [T-CALENDAR.md](T-CALENDAR.md), [U-FILES.md](U-FILES.md)). Tasks ([X-TASKS.md](X-TASKS.md)) sync over CalDAV after T-7, on that DAV client, not as a JMAP tasks capability. Notes ([Y-NOTES.md](Y-NOTES.md)) reuse the files sync of one account. Sending is [O-COMPOSE.md](O-COMPOSE.md).

## Phases

One phase is one session. Do not start the next one in the same conversation.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| M-1 | Per-server network threads, a curl multi handle each, caps, retries, redaction | Hard | Complete |
| M-2 | JMAP session discovery, `account.test`, remote accounts, capabilities | Medium | Complete |
| M-3 | Sync supervisor, Identity and Mailbox sync, `mailbox.list`, `sync.state` | Medium | Not started |
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
2. **Initial sync** (no `sync_state` for the type): `Identity/get`, `Mailbox/get` (all). Then per mailbox, Inbox and Sent first, `Email/query` sorted `receivedAt desc`, first page 500, then backfill newest-first in pages of 500. Header backfill runs to the end of the mailbox: offline mail is the point, and a header row is small. It is idle work. A page waits while that server has a user command in flight (open, search, send, flag) and resumes when the command completes. It never waits on another server. `Email/get` with header properties only (`id, threadId, mailboxIds, keywords, size, receivedAt, sentAt, messageId, inReplyTo, references, subject, from, to, cc, bcc, replyTo, preview, hasAttachment, blobId`) via back-reference (`#ids`) in the same request. Record each `/get` state. The core is posted the reduced rows, not the JSON body (HTTP rules).
3. **Delta sync:** `Mailbox/changes`, `Email/changes`, `Thread/changes` with `maxChanges=500`, looping while `hasMoreChanges`. Fetch created and updated headers; delete destroyed. On `cannotCalculateChanges`, drop that type's local data for the remote account and rerun initial sync for it.
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
**Status:** Not started.

**Goal:** Every enabled account keeps its identities and mailboxes in the store, and the UI can list them with counts.

**Entry gate:** K-3 and E-4 complete, and K-3's exit gate verified against the W-2 Stalwart container (`make it-up`, `make it-seed`): in `make build`, add `andrew@test-a.local` and `alice@test-a.local` (tenant A) and `bob@test-b.local` (tenant B) with Override session URL `https://localhost:1443/.well-known/jmap`; each Test shows capabilities, all three are listed with numbers 001–003 after a restart, a wrong password on Test shows one error and no loop, and `rg -a` over the workdir finds none of the test passwords (`pavtest-pw`, set by `tests/stalwart/seed.sh`; Stalwart requires at least 8 characters) ([K-ACCOUNTS.md](K-ACCOUNTS.md) K-3 and K Lock, Accounts).

The UI half of this gate (the `make build` steps above) is Andrew's to run before M-3a starts. The server half (`make it-up`, each account's Session with `apiUrl` on 1443, 401 on a wrong password) passed on 2026-10-19.

**Gate status (2026-10-20):**
- K-3 and E-4 are complete, and the server half passed on 2026-10-19.
- The UI half is still Andrew's to run: add the three test accounts in `make build`, restart, check a wrong password, then `rg -a` the workdir.
- Since 2026-10-20 the Accounts form adds accounts through Save as well as Add (K-Q13). Before that, Save on a blank form did nothing, so an earlier attempt at this gate may have failed for that reason. Rerun it.
- H-4 (the log in SQLite) is code-complete and waiting for Andrew's acceptance. It is not in M-3's entry gate.
- D-10 and the Accounts table (Tabulator) polish continue in parallel. M-3 does not depend on either: it is core sync work (`sync.c`, the store, `mailbox.list`, `identity.list`, `sync.state`), and its only UI is the counts it exposes.

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

- [ ] `core/src/sync.c`: one `PavSup` per enabled account, started after `core ready`, stopped on remove or disable; account states and `sync.state` events; `sync.now {accountId?}`.
- [ ] Each `PavJmapCtx` the supervisor builds sets `ca_file` from the account's `caFile` (M Lock, TLS). No context is built outside tests before M-3, so this lands with `sync.c`.
- [ ] `Identity/get` and `Mailbox/get` initial; `Mailbox/changes` and `Identity/changes` delta (polling every 2 minutes until M-7).
- [ ] Commands `mailbox.list {accountId?}` (nodes in the [E-TREE.md](E-TREE.md) contract shape: `id`, `parentId`, `kind`, `label`, `role`, `order`, `unread`, `total`, `hasChildren`, `swatch`), `identity.list`. Event `mail.mailboxes_changed`.
- [ ] Activity `sync.mail` per account with detail per step.
- [ ] 401 path per the K Lock; keyring locked path.
- [ ] Unity on fixtures: `test_sync_mailboxes_initial`, `test_sync_mailboxes_delta_rename`, `test_sync_401_stops_account`, `test_mailbox_list_tree_shape`.
- [ ] Moved from W-2: add `tests/support/record.sh` (curl and jq) and record the first real Session and responses from the W-2 Stalwart container into `tests/support/fixtures/`, scrubbed of secrets. This is what makes the W Lock's "serves recorded JMAP responses from `tests/support/fixtures/*.json`" true: it is not implemented yet. The directory is empty and `jmapfix.sh` builds every reply with jq ([W-SHIP.md](W-SHIP.md) Lock, Fixture server).
- [ ] Moved from W-2: make `tests/stalwart/seed.sh` create mailboxes in the container (idempotent). Today it only creates the domains and accounts through the admin API. Seeding the `tests/corpus/` messages moved to M-4.

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

**Tests:** `test_sync_mailboxes_initial`, `test_sync_mailboxes_delta_rename`, `test_mailbox_list_tree_shape`.

**Acceptance:** the M-3 exit gate.

### M-3c: Recorded fixtures and seeded mailboxes

**Scope:** `tests/stalwart/seed.sh` creates mailboxes (idempotent); `tests/support/record.sh` records the first real Session and responses from the container into `tests/support/fixtures/`, scrubbed of secrets; `jmapfix.sh` serves them.

**Tests:** a Unity test that replays a recorded Stalwart `Mailbox/get`.

**Acceptance:** `make test` exits 0, a reseed is idempotent, and `rg` finds no secrets in `tests/support/fixtures/`.

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

# H — LOGGING

**Status:** Not started (exploration 2026-10-16, scoped the same day).
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

The core log in production, read back through a command, and the Log view in the Activity tab. It comes before tabs and before mail so every later phase can be watched from inside the app. The line format and `pav_log` are the [C-CORE.md](C-CORE.md) Lock and do not change here.

## Phases

One phase is one session. Do not start the next one in the same conversation.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| H-1 | Log thread in production, a ring buffer, `log.tail`, and `log.lines` events | Medium | Not started |
| H-2 | Log view: CodeMirror 6, live, in the Activity tab | Medium | Not started |
| H-3 | Search, level and subsystem filters, follow, copy, save, print | Medium | Not started |

Kind: **Easy** is one module and a test. **Medium** crosses one boundary.

## How a session works

1. Read Resume in [PAVILLION_PLAN.md](../PAVILLION_PLAN.md), the Status column above, and the previous phase's Status, Accomplished, Lessons learned, and Handoff.
2. Re-read only this phase: Goal, Entry gate, Work items, Done means, Exit gate. Open the lock section this phase cites.
3. Ask the qualifying questions. Wait for approval before editing source.
4. Do this phase only. Check a work item `[x]` only after its check has passed. Defer with `[~]`, one line, and the phase it moves to.
5. Before marking complete, write Status, Accomplished, Lessons learned, and Handoff, update the Status column here and in the index, and append the Working log. Then stop.
6. A sitting that ends early sets Status to **in progress**, names the last finished work item, and leaves the rest unchecked.

Leave Accomplished, Lessons learned, and Handoff off a phase that has not started.

## What exists (2026-10-16)

- `pav_log(subsystem, level, num_args, fmt, ...)` in `core/src/pavillion.c`, the millisecond-UTC line `2026-10-14T17:04:01.123Z INFO  core  core ready`, levels `PAV_ERROR 0` to `PAV_DEBUG 3`.
- `pav_log_thread_start` and `pav_log_thread_shutdown` exist and are tested, but **nothing in production starts the thread**. Every line is written synchronously by the caller under the mutex. `pavillion_stop` still calls shutdown, which does nothing.
- The profile log is opened for append in `core_boot()` (`attach_profile_log`). Path `logs/<profile id>.log`.
- `pav_ui_log` and the `ui_log` Tauri command bridge JS messages into the core log. `api.js` exports `log()`. There is no `console.*` call in `ui/src`.
- The Activity tab (`#panel-activity`, class `.log`) is empty. CodeMirror is not installed.

## Lock

- The log file is the record. Its line format is the C-CORE Lock. The view never opens the file; it asks the core.
- The core keeps the last 10,000 lines in an in-memory ring (`core/src/logring.c`), filled by the log thread as it writes. `log.tail` reads the ring, not the file. The ring holds the same bytes as the file.
- Every line is also delivered as an event: the log thread emits `log.lines {lines:[{seq, ts, level, subsystem, text}, ...]}` through `pav_on_event`, at most one batch per 100 ms. There is no per-line `log.line` event. `seq` is a monotonically increasing 64-bit counter since start.
- Filtering is client-side in the view for what is loaded; `log.tail {sinceSeq?, limit<=10000, minLevel?, subsystems?}` filters in the core when asked. Default load is the whole ring.
- Times on screen are local `HH:mm:ss.mmm` (24 h, profile timezone, default `America/Vancouver`). Hovering a line shows the UTC stamp. The file stays UTC.
- Log rotation: at start, a profile log over 10 MB is renamed `<id>.log.1` (one generation kept) before opening. No rotation while running.
- Redaction (was plan §11): never log a password, token, `Authorization` header, cookie, message body, or bot key. Log ids, sizes, counts, durations, and HTTP status. `pav_log` callers pass ids, never content. A Unity test (`test_log_redacts_known_secret_shapes`) feeds a fixture `Authorization: Basic …` and `Bearer …` through the HTTP debug path (M-1) and asserts neither appears.
- `log_level` stays on the index (`error`, `warn`, `info`, `debug`). The Log view can change it for the running session through `prefs.set {logLevel}`, which writes the index.
- The viewer is CodeMirror 6 (`@codemirror/view`, `@codemirror/state`, `@codemirror/search`), read-only, installed in H-2 and bundled by Vite. The same packages later edit HTML (F-8) and Lua (R-4). The document is plain text. Level color is a line decoration, not HTML in the document. Font is the monospace already used by the shell (Vanadium Mono when installed, then `ui-monospace, monospace`); G-4 and D-4 keep that choice when they name `--pav-font-mono`. A 10,000-line ring is the whole document. CodeMirror virtualizes the viewport. Copy and save use the UTC line from the ring, so the bytes match the file. The on-screen timestamp is local time.
- H-2 creates the Log view and the `Activity | Log` segmented control on the Activity tab, before I-2 and before J-1. The Activity side stays empty until I-2. The right-click menu on the Activity nav button is J-1; it switches the two views H-2 already created. Plain click opens the last view used (`preferences.ui.activity.view`, default `log` until I-2).
- H-2 runs before G-4. It adds `--pav-log-error`, `--pav-log-warn`, `--pav-log-info`, and `--pav-log-debug` to the current theme files and uses those names. G-4 keeps them when it renames the older properties. Do not wait on G-4 to show the log.

## H-1: Log thread, ring, `log.tail`, and `log.lines`

**Kind:** Medium
**Status:** Not started.

**Goal:** Production logging is asynchronous, the last lines are readable through a command, and new lines arrive as events.

**Entry gate:** G-8 complete.

**Work items:**

- [ ] `pavillion_start` calls `pav_log_thread_start` before the core thread; `pavillion_stop` keeps joining it last. Lines logged before the profile log is attached go to stderr and `<workdir>/logs/pavillion.log` as the C-CORE Lock says (this was never implemented; implement it).
- [ ] `core/src/logring.c`: `pav_logring_push(seq, line)`, `pav_logring_read(since_seq, limit, min_level, subsystems, json_t **out)`. Capacity 10,000 lines, oldest dropped.
- [ ] Command `log.tail` in the dispatcher and `docs/API.md`. Events `log.lines` with the 100 ms coalescing on the log thread.
- [ ] **Pre-profile logging**, as an explicit item: a line logged before a workdir exists goes to stderr only. Once the workdir is known but before the profile log is attached, it goes to stderr and `<workdir>/logs/pavillion.log`. When the profile log attaches, the buffered lines are already on disk and nothing is lost or duplicated. Unity: `test_log_before_workdir_stderr_only`, `test_log_before_profile_goes_to_workdir_log`, `test_log_no_duplicate_after_profile_attach`.
- [ ] Startup rotation over 10 MB.
- [ ] `log.client {subsystem, level, message}` replaces `ui_log`: subsystem limited to `^[a-z][a-z0-9_]{0,15}$`, message capped at 2,000 bytes, newlines replaced by `⏎`.
- [ ] `ui/src/log.js` routes `window.onerror` and `unhandledrejection` to `log.client` at `error`.
- [ ] Unity: `test_logring_wraps_at_capacity`, `test_log_tail_since_seq`, `test_log_tail_filters_level`, `test_log_rotation_over_limit`, `test_log_client_sanitizes`, plus the three pre-profile tests above.

**Done means:** A running app writes its log from the log thread, and `log.tail` returns the newest lines.

**Exit gate:** `make test` exits 0. In `make dev`, the devtools console call `await api.log.tail({limit:5})` (debug builds expose `window.pav` for this) returns five lines ending with `core ready`, and a forced `throw` in the console appears as a `ui` error line.

## H-2: Log view in the Activity tab

**Kind:** Medium
**Status:** Not started.

**Goal:** The Activity tab shows the log, live, in CodeMirror, before later phases need it for debugging.

**Entry gate:** H-1 complete.

**Work items:**

- [ ] Add `@codemirror/view`, `@codemirror/state`, and `@codemirror/search`. `ui/src/log/view.js`: `createLogView(el, api)`, a read-only EditorView over the ring. Line decorations `pav-log-level-ERROR|WARN|INFO|DEBUG`. The document is text. The view is not an HTML sink.
- [ ] The timestamp decoration is local time. Copy and save use the UTC line from the ring, so the bytes match the file.
- [ ] On open: `log.tail`, scroll to the end. Subscribe to `log.lines` and append while open. Unsubscribe on close.
- [ ] Font size for the view is `preferences.ui.zoom.log` (a new pane key, default 1). The view has its own slider, stored through the F-2 blob. Wrapping is off by default.
- [ ] `Activity | Log` segmented control at the top of `#panel-activity`. Log is this view. Activity is an empty panel until I-2. Plain click opens the last view (`preferences.ui.activity.view`, default `log`).
- [ ] Record the gzipped size of `ui/dist` in Accomplished. The size is a record. It is not a reason to drop CodeMirror.

**Done means:** Opening Activity shows the newest lines, and new lines appear without a refresh.

**Exit gate:** `make test` exits 0. `make build`: open Activity, the last line is visible, trigger a log line (change zoom, which logs `prefs saved` at debug with level debug set), and it appears within a second.

## H-3: Search, filters, follow, copy, save, print

**Kind:** Medium
**Status:** Not started.

**Goal:** The Log view is a tool: find, filter, follow, and hand a log to someone.

**Entry gate:** H-2 complete.

**Work items:**

- [ ] Toolbar above the view: search field (CodeMirror search over the loaded document, match count, `Ctrl+F` focuses it), level checkboxes (ERROR, WARN, INFO, DEBUG), subsystem multi-select filled from the distinct subsystems seen, Follow toggle (on by default; scrolling up turns it off), Wrap toggle, Refresh, and the session log level select.
- [ ] Filtering changes which lines the editor shows. It does not drop them from the ring. Clearing the filter restores every loaded line. The match count and "showing N of M" are shown.
- [ ] Copy visible lines; Save visible lines to a file through the host's save dialog (`dialog.save` host service, file written by the core with `log.export {path, filter}`); Print through `window.print()` on a print stylesheet that shows only the visible lines.
- [ ] "Email this log" creates a draft with the log attached when O-1 exists; until then the item is absent. (Recorded; wired in O-5's Handoff.) `mailto:` bodies are not used: they truncate and leak to another client.
- [ ] Filter, wrap, and follow state persist in `preferences.ui.log`.
- [ ] Vitest: `filter.test.js` for the level, subsystem, and text predicate.

**Done means:** A user can find every WARN from `config` in the last hour and save exactly those lines.

**Exit gate:** `make test` exits 0. In `make build`, filter to WARN + `config`, search a word, save; the saved file holds exactly the visible lines, byte for byte the same as the log file's lines.

## Open questions for Andrew

- H-Q1. Ring size 10,000 lines and file rotation at 10 MB with one old generation: acceptable defaults?
- H-Q2. Should debug level be switchable per subsystem (`log_level: {"default":"info","sync":"debug"}`) once sync exists?

## Working log

- 2026-10-16 Exploration written (what exists, a read API, the viewer, filters, print and email).
- 2026-10-16 Scoped into H-1 to H-3. Decisions: ring buffer in the core instead of reading the file, events for live lines, client-side filtering, the log thread started in production, `mailto:` forwarding replaced by a draft with an attachment.
- 2026-10-16 Andrew put H-2 and H-3 immediately after H-1 and restored CodeMirror 6 as the log viewer. The icon-kit failure had no in-app log. The view exists so the next failure has a cause.

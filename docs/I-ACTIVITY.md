# I — ACTIVITY

**Status:** I-1 complete (2026-10-16). I-2 has not started. Its entry gate is met (2026-10-17).
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

The user-facing feed of work in progress: syncing a mailbox, fetching bodies, downloading an attachment, sending, indexing, importing, running rules. Each has a status, progress, an estimate, and a cancel button. The registry lives in the core; every long operation in M, N, O, Q, R, S, T, and U registers with it. The Update button's spin reads it.

## Phases

One phase is one session. Do not start the next one in the same conversation.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| I-1 | Activity registry in the core, `activity.*` commands and events, cooperative cancel | Medium | Complete |
| I-2 | Activity view, history toggle, the Update button bound to it | Medium | Not started |

Kind: **Easy** is one module and a test. **Medium** crosses one boundary.

## How a session works

1. Read Resume in [PAVILLION_PLAN.md](../PAVILLION_PLAN.md), the Status column above, and the previous phase's Status, Accomplished, Lessons learned, and Handoff.
2. Re-read only this phase: Goal, Entry gate, Work items, Done means, Exit gate. Open the lock section this phase cites.
3. Ask the qualifying questions. Wait for approval before editing source.
4. Do this phase only. Check a work item `[x]` only after its check has passed. Defer with `[~]`, one line, and the phase it moves to.
5. Before marking complete, write Status, Accomplished, Lessons learned, and Handoff, update the Status column here and in the index, and append the Working log. Then stop.
6. A sitting that ends early sets Status to **in progress**, names the last finished work item, and leaves the rest unchecked.

Leave Accomplished, Lessons learned, and Handoff off a phase that has not started.

## Lock

- The registry is in the core (`core/src/activity.c`), not a UI abstraction. The UI never invents an activity, so the CLI's `status` and the window agree.
- An activity:

```json
{ "id": "act-42", "kind": "sync.mail", "accountId": "a-500foods-andrew",
  "title": "Sync andrew@500foods.com", "detail": "Inbox: headers 300 of 1,200",
  "state": "running", "startedMs": 1760630581452, "endedMs": null,
  "progress": { "done": 300, "total": 1200, "unit": "messages" },
  "etaMs": 4200, "cancellable": true, "error": null, "parentId": null }
```

- `state` is `queued`, `running`, `done`, `failed`, or `cancelled`. `kind` is a closed list kept in `docs/API.md`. The kinds below are the **initial set**. The doc that owns a feature extends it, adding the kind to `docs/API.md` in the phase that first uses it, and a kind not in that list is rejected by `pav_activity_begin`. Initial set: `sync.mail`, `sync.contacts`, `sync.calendar`, `sync.files`, `body.fetch`, `attachment.download`, `files.upload`, `files.download`, `send`, `index.rebuild`, `rules.run`, `import`, `export`, `retention`. Added by owning docs: `cache.evict` (L-2), `search.server` (Q-2), `queue.flush` (M-6, failed and pending ops), and `account.test` (M-2).
- C API, callable from any thread:

```c
int64_t pav_activity_begin(const char *kind, const char *account_id, const char *title, int cancellable, int64_t parent);
void    pav_activity_progress(int64_t id, int64_t done, int64_t total, const char *unit, const char *detail);
void    pav_activity_end(int64_t id, int state, const char *error);   /* PAV_ACT_DONE, FAILED, CANCELLED */
int     pav_activity_cancelled(int64_t id);   /* the operation polls this between steps */
```

- `pav_activity_begin` returns a positive id, or 0 when it rejects the call. `parent` 0 means no parent. `pav_activity_progress` treats `total` below 0 as unknown, and the ETA stays null. `pav_activity_end` ignores an id that is already finished.
- Cancel is cooperative: `activity.cancel {id}` sets a flag; the operation checks `pav_activity_cancelled` between requests or rows and ends itself as `cancelled`. curl transfers in M-1 use the flag in their progress callback to abort. Cancel never kills a thread. The public id is `act-<n>`.
- ETA is computed in the core from the progress rate over the last 10 seconds (exponential moving average), and is null until two samples exist or when `total` is unknown.
- Events: `activity.changed {activity}` on begin, end, and dismiss, and at most every 250 ms per activity on progress. Begin, end, and dismiss are not throttled.
- History is the profile SQLite table `activity` (I-Q1, 2026-10-16: finished activities persist; the log file stays the human record). `pav_store_open` still creates no tables. After that open, the registry runs the statement below. It does not set `PRAGMA user_version`. L-1's first migration repeats it and keeps the rows. Until L-1 this connection is the only writer of the table. L-1 moves the writes onto `pav_db_write`.

```sql
CREATE TABLE IF NOT EXISTS activity (
  id INTEGER PRIMARY KEY,
  kind TEXT NOT NULL,
  account_id TEXT,
  title TEXT NOT NULL,
  detail TEXT,
  state TEXT NOT NULL,
  started_ms INTEGER NOT NULL,
  ended_ms INTEGER,
  prog_done INTEGER,
  prog_total INTEGER,
  prog_unit TEXT,
  eta_ms INTEGER,
  cancellable INTEGER NOT NULL,
  error TEXT,
  parent_id INTEGER,
  dismissed INTEGER NOT NULL DEFAULT 0
);
```

- Running and queued activities stay in memory. A stop or a crash does not resume them. On the next open, a `queued` or `running` row is deleted. `pav_activity_end` inserts the finished row. Begin and end also log under subsystem `activity`.
- The table keeps the newest 500 finished activities that are not an undismissed failure, ordered by `ended_ms` then `id`. An undismissed failure stays until `activity.dismiss`, even past 500. Dismiss sets `dismissed` to 1, and that row then ages out with the cap. At most 256 activities are running or queued. `pav_activity_begin` returns 0 when that list is full, the kind is unknown, the title is empty, or the parent is not an activity.
- `activity.list` reads memory. The registry loads the table when the store opens.
- The view is a table of text (no HTML from outside): Started (local `HH:mm:ss`), Kind (icon + label), Account (number), Title and Detail, Progress (bar + `300/1,200`), ETA (`in 5s`, `2m`), State, Cancel. Columns use `textContent`. Child activities fold under their parent.
- Default shows running and queued, plus failed not dismissed. A toggle shows history.
- The Update button (bottom nav) runs `sync.now` and spins while any `sync.*` activity is running. Its tooltip names the count.

## I-1: Activity registry in the core

**Kind:** Medium
**Status:** Complete (2026-10-16). I-Q1 is persistence in SQLite. This sitting did not push.

**Goal:** Any core thread can register, update, finish, and cancel an activity, and the UI is told.

**Entry gate:** H-1 complete.

**Work items:**

- [x] `core/src/activity.c` and `core/include/pav_activity.h` with the Lock's API, a mutex, the 500-entry history, and the `activity` table. `user_version` stays unset.
- [x] Commands `activity.list {includeHistory}`, `activity.cancel {id}`, `activity.dismiss {id}`; event `activity.changed` with the 250 ms throttle.
- [x] A test-only command `debug.activity_demo {steps, ms}` (compiled in debug builds) that runs on a worker, reports progress, and honors cancel. I-2 uses it.
- [x] Unity: `test_activity_begin_progress_end`, `test_activity_cancel_is_cooperative`, `test_activity_eta_after_two_samples`, `test_activity_history_capped`, `test_activity_events_throttled`.

**Done means:** The demo activity can be started, watched, and cancelled through commands.

**Exit gate:** `make test` exits 0 with the five tests.

**Accomplished:** `pav_activity_begin` returns `act-<n>`'s number, or 0 for an unknown kind, an empty title, a missing parent, or a full list of 256 running activities. Progress stores done, total, unit, and detail. `total` below 0 is unknown. The ETA is a discrete exponential moving average with a 10 second time constant, and it stays null until two samples have different timestamps. End writes `done`, `failed`, or `cancelled`. Cancel sets a flag and leaves the state running until the operation ends itself. Begin, end, and dismiss post `activity.changed`. Progress posts only when 250 ms have passed for that activity. Begin and end also log under subsystem `activity`. Finished rows are inserted into table `activity` on the profile database. The newest 500 finished rows stay, except an undismissed failure, which stays until dismiss. A stop deletes nothing that already finished. The next open drops any `queued` or `running` row and loads the rest. `PRAGMA user_version` is not set. `pav_store_open` still creates no tables. `activity.list`, `activity.cancel`, and `activity.dismiss` are real. `debug.activity_demo` is real in this build and is left out when `NDEBUG` is set. It is the first `worker` command: the handler runs off the core thread, begins a cancellable `sync.mail` activity titled `Demo activity`, and returns before the steps sleep. The public id is `act-<n>`. `PAVILLION_API_VERSION` stays 3. `src-tauri/src/lib.rs` is unchanged. There is no UI in this phase, so the Activity segment stays empty and there was no window check. `make test` is 65 Unity tests and 23 Vitest tests, exit 0. The five new tests are the ones named above. The live profile was not opened.

**Lessons learned:** Hand a worker call to its thread only after releasing `g_core_mu`, and drain that thread before clearing `g_core_alive`. The caller frees the `PavCall` once `done` is set or the core is no longer alive. `bsearch` over an array of `char *` needs a comparator that dereferences the element; `strcmp` on the element pointer does not compare the names. A second progress call in the same millisecond updates the counts and does not count as the second ETA sample. The cap orders by `ended_ms` then `id`, so a burst in one millisecond still drops the lowest id. Dismiss puts a failure back into that set, and the oldest row past 500 disappears in that call. The profile connection is used from more than one thread, always under the activity mutex, so the statements do not overlap. L-1 moves those writes onto `pav_db_write`.

**Handoff:** F-4 is next. Do not start it in this sitting. Its entry gate is G-8, which has landed. Prove the pane in `make build`, not `make dev`. I-2 stays at step 37 and still waits on J-1. The Activity segment stays empty until I-2. I-2 fills it and does not rebuild the log. Do not bump `PAVILLION_API_VERSION`. Do not add a `### log.lines` heading. `lib.rs` stays a forwarder at 199 lines. A new Tauri plugin has to fit on the existing builder line. Only `ui/src/transport/tauri.js` may import `@tauri-apps`. Secrets stay `-1` until K-1. `macos-core` stays `continue-on-error` until G-6. H-Q2 stays open until M-1. "Email this log" is O-5. L-1 repeats the I Lock's `CREATE TABLE IF NOT EXISTS activity` and keeps the rows. It does not treat `user_version` 0 as an empty database if that table is present. A release build that defines `NDEBUG` does not register `debug.activity_demo`. Do not drive the window. The live profile was not opened.

## I-2: Activity view and the Update button

**Kind:** Medium
**Status:** Not started. Entry gate met 2026-10-17 (I-1 and J-1 complete). Do not start it in the J-1 sitting.

**Goal:** The Activity tab shows live work with cancel, and the Update button reflects it.

**Entry gate:** I-1 and J-1 complete. H-2 and H-3 have already landed. This phase does not build a log.

**Work items:**

- [ ] `ui/src/activity/view.js`: a plain DOM table (a few hundred rows, no table library), patched by id on `activity.changed`, never rebuilt. This is the activity registry, not the message list.
- [ ] Status colors from `--pav-status-running`, `-done`, `-failed`, `-cancelled` (G-4 has landed). Progress bar is a `<progress>` element. Cancel is a button with an accessible name `Cancel <title>`.
- [ ] History toggle and Dismiss on failed rows; state in `preferences.ui.activity`.
- [ ] Fill the Activity side of the `Activity | Log` control H-2 created. Do not replace the CodeMirror log and do not add a `<pre>` tail. The J-1 right-click menu already switches the two views. Plain click on the Activity nav button opens the last view (`preferences.ui.activity.view`).
- [ ] The Update button calls `sync.now` (a no-op returning `ok` until M-3) and spins while a `sync.*` activity runs, replacing the preview's 1.5 s fake spin. `prefers-reduced-motion` stops the spin and shows a static badge instead.
- [ ] Debug-only Help item `Run demo activity` calls `debug.activity_demo`.

**Done means:** A demo activity shows progress and an ETA, cancels from its button, and moves to history.

**Exit gate:** `make test` exits 0. In `make dev`: start the demo, watch the bar, cancel it, toggle history, see it as cancelled.

## Open questions for Andrew

Resolved 2026-10-16: I-Q1. Finished activities persist in the profile SQLite table `activity`. The log file remains the human record. Running work is not resumed after a restart.

## Working log

- 2026-10-16 Exploration written (columns, behavior, a data model, push versus poll).
- 2026-10-16 Scoped into I-1 and I-2. Decisions: registry in the core, push events with throttling, cooperative cancel, ETA in the core, closed kind list, session-only history, the Update button bound to sync activities.
- 2026-10-16 H-2 now precedes I-2 and owns the CodeMirror log. I-2 fills the Activity side of that control and does not ship a log tail.
- 2026-10-16 I-Q1: finished activities persist in the profile SQLite table `activity`. Running work is not resumed. `user_version` stays unset.
- 2026-10-16 I-1: registry, cancel, dismiss, `activity.changed`, and `debug.activity_demo` on a worker. `make test` is 65 Unity tests and 23 Vitest tests.
- 2026-10-17 J-1 is complete, so I-2's entry gate is met. I-2 has not started. It fills the Activity side and does not rebuild the log. The J-1 menu items are Activity and Logs. `--pav-status-running`, `--pav-status-done`, `--pav-status-failed`, and `--pav-status-cancelled` are not in the theme files yet.

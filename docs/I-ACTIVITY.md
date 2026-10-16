# I — ACTIVITY

**Status:** Not started (exploration 2026-10-16, scoped the same day).
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

The user-facing feed of work in progress: syncing a mailbox, fetching bodies, downloading an attachment, sending, indexing, importing, running rules. Each has a status, progress, an estimate, and a cancel button. The registry lives in the core; every long operation in M, N, O, Q, R, S, T, and U registers with it. The Update button's spin reads it.

## Phases

One phase is one session. Do not start the next one in the same conversation.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| I-1 | Activity registry in the core, `activity.*` commands and events, cooperative cancel | Medium | Not started |
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

- Cancel is cooperative: `activity.cancel {id}` sets a flag; the operation checks `pav_activity_cancelled` between requests or rows and ends itself as `cancelled`. curl transfers in M-1 use the flag in their progress callback to abort. Cancel never kills a thread.
- ETA is computed in the core from the progress rate over the last 10 seconds (exponential moving average), and is null until two samples exist or when `total` is unknown.
- Events: `activity.changed {activity}` on begin, end, and at most every 250 ms per activity on progress.
- History: the last 500 finished activities are kept in memory for the session. Failed ones stay until dismissed. Nothing is persisted across restarts; the log file is the permanent record (each begin and end is also a log line under subsystem `activity`).
- The view is a table of text (no HTML from outside): Started (local `HH:mm:ss`), Kind (icon + label), Account (number), Title and Detail, Progress (bar + `300/1,200`), ETA (`in 5s`, `2m`), State, Cancel. Columns use `textContent`. Child activities fold under their parent.
- Default shows running and queued, plus failed not dismissed. A toggle shows history.
- The Update button (bottom nav) runs `sync.now` and spins while any `sync.*` activity is running. Its tooltip names the count.

## I-1: Activity registry in the core

**Kind:** Medium
**Status:** Not started.

**Goal:** Any core thread can register, update, finish, and cancel an activity, and the UI is told.

**Entry gate:** H-1 complete.

**Work items:**

- [ ] `core/src/activity.c` and `core/include/pav_activity.h` with the Lock's API, a mutex, and the 500-entry history.
- [ ] Commands `activity.list {includeHistory}`, `activity.cancel {id}`, `activity.dismiss {id}`; event `activity.changed` with the 250 ms throttle.
- [ ] A test-only command `debug.activity_demo {steps, ms}` (compiled in debug builds) that runs on a worker, reports progress, and honors cancel. I-2 uses it.
- [ ] Unity: `test_activity_begin_progress_end`, `test_activity_cancel_is_cooperative`, `test_activity_eta_after_two_samples`, `test_activity_history_capped`, `test_activity_events_throttled`.

**Done means:** The demo activity can be started, watched, and cancelled through commands.

**Exit gate:** `make test` exits 0 with the five tests.

## I-2: Activity view and the Update button

**Kind:** Medium
**Status:** Not started.

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

- I-Q1. Should finished activities persist across restarts (a small SQLite table), or is the session plus the log file enough? The Lock says session only.

## Working log

- 2026-10-16 Exploration written (columns, behavior, a data model, push versus poll).
- 2026-10-16 Scoped into I-1 and I-2. Decisions: registry in the core, push events with throttling, cooperative cancel, ETA in the core, closed kind list, session-only history, the Update button bound to sync activities.
- 2026-10-16 H-2 now precedes I-2 and owns the CodeMirror log. I-2 fills the Activity side of that control and does not ship a log tail.

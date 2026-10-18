# X — TASKS

**Status:** Not started (scoped 2026-10-18). The icon and the empty home are J-9. This file is the screens and the sync.
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

Tasks are to-dos. A task group is a list. A task has a title, a plain description, a status, a priority, and an optional due date. The store is CalDAV VTODO, not JMAP. JMAP Calendars ([T-CALENDAR.md](T-CALENDAR.md)) covers events and does not define VTODO. `draft-ietf-jmap-tasks` expired in 2023 and Stalwart does not implement it. Stalwart's management `x:Task` is a server job, not a person's to-do. Do not call it.

## Phases

One phase is one session. Do not start the next one in the same conversation. The index is the order. X-1 and X-2 sit after T-7 and before milestone 4. X-3 sits after V-4.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| X-1 | Verify CalDAV VTODO; sync task lists and tasks | Medium | Not started |
| X-2 | Tasks screen, editor, and the Grok context type `task` | Medium | Not started |
| X-3 | CLI task commands | Medium | Not started |

Kind: **Easy** is one module and a test. **Medium** crosses one boundary.

## How a session works

1. Read Resume in [PAVILLION_PLAN.md](../PAVILLION_PLAN.md), the Status column above, and the previous phase's Status, Accomplished, Lessons learned, and Handoff.
2. Re-read only this phase: Goal, Entry gate, Work items, Done means, Exit gate. Open the lock section this phase cites.
3. Ask the qualifying questions. Wait for approval before editing source.
4. Do this phase only. Check a work item `[x]` only after its check has passed. Defer with `[~]`, one line, and the phase it moves to.
5. Before marking complete, write Status, Accomplished, Lessons learned, and Handoff, update the Status column here and in the index, and append the Working log. Before that, update `ui/src/help/pages.js` for what this phase changed: augment, replace, fix, or extend the user page, or add a Developer section when the user page has nothing new. Then stop.
6. A sitting that ends early sets Status to **in progress**, names the last finished work item, and leaves the rest unchecked.

Leave Accomplished, Lessons learned, and Handoff off a phase that has not started.

## Lock

- **Protocol.** VTODO over CalDAV, RFC 5545, through T-7's shared `dav.c`. Stalwart 0.14 and later advertise `supported-calendar-component-set` (shipped in 0.14.0). Festival production is Stalwart v0.16.24. X-1 reads that set. `account.test` does not grow a JMAP tasks capability. T-Q1 (whether JMAP Calendars is advertised) does not gate this letter. Tasks wait on T-7.
- **Which collection is which.** A collection that allows only VTODO is a task group and is hidden from Calendars. A collection that allows only VEVENT is a calendar and is hidden from Tasks. A collection that allows both is listed in both, the same `(ra_id, id)` in `calendar` and in `task_list`. Creating a group from Tasks sets VTODO only (`MKCALENDAR` at X-1, once T-7's client can do it). Creating a calendar from Calendars sets VEVENT only when the protocol allows it (T-7). If a JMAP calendar cannot name components, it stays in Calendars and also appears in Tasks when it accepts VTODO.
- **Tables** are L-3's `task_list`, `task`, and `task_fts`. They are not the event tables. `sync_state` types `TaskList` and `Task` are written here. X adds `sync_tasks` on the account object in the B Lock. K-3 does not build a Tasks heading. X-2 adds Settings → Accounts → Tasks.
- **An IMAP account** has no CalDAV VTODO. Its Tasks nav is an empty list.
- **Home** is every open task across accounts. The nav is groups by account. Open means status is not completed and not cancelled.
- **v1 fields.** `SUMMARY` is the title. `DESCRIPTION` is plain text. Status `NEEDS-ACTION`, `IN-PROCESS`, `COMPLETED`, and `CANCELLED` are shown as Not started, In progress, Done, and Cancelled. Priority `1`, `5`, `9`, and `0` are shown as High, Normal, Low, and None. Any other priority integer is kept as stored until the person changes it, and the editor then writes `1`, `5`, `9`, or `0`. Due is optional, a date in `America/Vancouver` unless the task names a zone. libical, already planned in T-2, reads and writes the VTODO.
- **Out of v1.** Subtasks, assignees, reminders, repeating tasks, and drawing due tasks on the calendar. Those wait for a later talk. Do not add them in X-1 or X-2.
- **Screen.** The list is the shared Tabulator from N-3. The editor is plain fields, not SunEditor. Cells stay text.
- **Grok.** X-2 registers context type `task` on the open task, the same pattern as `event` in T-3. No open task, no context. X-3 adds the CLI commands. They are not part of V-4. V-4 stays contacts and calendars.
- **Icon.** Sharp Duotone Solid `fa-list-check`, theme key `--pav-icon-tasks`. J-9 places the button. This letter fills it.

## X-1: Verify and sync

**Kind:** Medium
**Status:** Not started.

**Goal:** Task groups and tasks from CalDAV are in the store.

**Entry gate:** T-7 complete. L-3 has already created the task tables.

**Work items:**

- [ ] On Andrew's Stalwart and the container, read `supported-calendar-component-set`. Record the version and which collections allow VTODO, VEVENT, or both. Do not add a tasks name to `account.test`.
- [ ] Sync VTODO collections into `task_list` and VTODO components into `task`, through `dav.c`. A VEVENT-only collection is not a task list. A both-component collection is both rows. Activity `sync.tasks`.
- [ ] `task.list` and `task.get`. `task.changed`.
- [ ] Creating a group sends VTODO only.
- [ ] Unity on fixtures: `test_vtodo_list_sync`, `test_vevent_only_hidden_from_tasks`, `test_both_components_listed_twice`, `test_priority_other_than_1590_preserved`.

**Done means:** Open tasks from a VTODO collection are in the store, and a VEVENT-only calendar is not.

**Exit gate:** `make test` exits 0. The container's VTODO collection is in `task_list`.

## X-2: Screen and editor

**Kind:** Medium
**Status:** Not started.

**Goal:** Andrew can see his tasks and edit one.

**Entry gate:** X-1 complete.

**Work items:**

- [ ] Tasks home: all open tasks. Nav: groups by account, with color. An IMAP account shows an empty list. The empty copy from J-9 is replaced.
- [ ] Editor: title, plain description, status, priority, optional due. Saving writes a VTODO. A priority that is not 1, 5, 9, or 0 is shown and kept until the control is changed.
- [ ] Settings → Accounts gains a Tasks heading for accounts that have VTODO. K-3 does not grow that heading.
- [ ] The Grok composer from V-8 reports the open task's id (`context.type` `task`). No open task, no context.
- [ ] Help: replace the Tasks stub with the fields and the chords.

**Done means:** A task can be added, set in progress, and marked done, and it round-trips to CalDAV.

**Exit gate:** `make test` exits 0. In `make build` the list and the editor work against the container.

## X-3: CLI

**Kind:** Medium
**Status:** Not started.

**Goal:** `pavillion task` can list and change tasks through the running app.

**Entry gate:** V-4 complete, and X-2 complete. V-4 is not extended to cover this.

**Work items:**

- [ ] `task list`, `task get`, `task set` (status, priority, due, title, description). JSON fields match the store. No prompt.
- [ ] A bot key needs a scope named in the V Lock before it can mutate. Read is the default key. Record the scope name in the V Lock in this phase.
- [ ] Unity or the CLI fixture: list, set status, get.

**Done means:** `pavillion task list --json` prints the open tasks, and `task set` changes one.

**Exit gate:** `make test` exits 0.

## Open questions for Andrew

None. The component-set rule above is the decision. T-Q1 stays on T and does not move here.

## Working log

- 2026-10-18 Opened. Tasks are CalDAV VTODO, own tables, after T-7. JMAP tasks and Stalwart `x:Task` are out. Both-component collections show in Calendars and in Tasks. Priority integers other than 1, 5, 9, and 0 are preserved until edited. The icon is J-9. X-3 follows V-4 and does not widen V-4.

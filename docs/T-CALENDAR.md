# T — CALENDAR

**Status:** Not started (scoped 2026-10-16 from old plan §9, §10, §13).
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

Calendars and events over JMAP Calendars (`urn:ietf:params:jmap:calendars`), replacing Omacal. Recurrence and time zones through libical. The sync pattern is [M-SYNC.md](M-SYNC.md).

## Phases

One phase is one session. Do not start the next one in the same conversation.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| T-1 | Verify Stalwart's JMAP Calendars (go/no-go); sync Calendar and CalendarEvent | Medium | Not started |
| T-2 | Recurrence and time zones with libical; the instance cache; DST tests | Hard | Not started |
| T-3 | Views: week, day, month, agenda; the mini-calendar nav | Medium | Not started |
| T-4 | Event editor: create, edit, move, resize, delete, with recurrence scope | Medium | Not started |
| T-5 | Reminders, snooze, `.ics` import and export | Medium | Not started |
| T-6 | iTIP invitations in mail: accept, tentative, decline | Medium | Not started |
| T-7 | CalDAV adapter on the shared DAV client | Hard | Not started |

T-6 comes after the initial working copy.

Kind: **Easy** is one module and a test. **Medium** crosses one boundary. **Hard** is time-zone and recurrence correctness.

## How a session works

1. Read Resume in [PAVILLION_PLAN.md](../PAVILLION_PLAN.md), the Status column above, and the previous phase's Status, Accomplished, Lessons learned, and Handoff.
2. Re-read only this phase: Goal, Entry gate, Work items, Done means, Exit gate. Open the lock section this phase cites.
3. Ask the qualifying questions. Wait for approval before editing source.
4. Do this phase only. Check a work item `[x]` only after its check has passed. Defer with `[~]`, one line, and the phase it moves to.
5. Before marking complete, write Status, Accomplished, Lessons learned, and Handoff, update the Status column here and in the index, and append the Working log. Then stop.
6. A sitting that ends early sets Status to **in progress**, names the last finished work item, and leaves the rest unchecked.

Leave Accomplished, Lessons learned, and Handoff off a phase that has not started.

## Lock

- Requires `urn:ietf:params:jmap:calendars` (an IETF draft). T-1 verifies Stalwart's support and the draft revision; property names may differ between revisions, so all mapping is isolated in `core/src/jscal_map.c`. If unsupported, calendars are local-only and the go/no-go decision (CalDAV earlier, or wait) goes to Andrew.
- Calendars: list, color, visibility toggle, default calendar; shared calendars from other accounts (work@, home@) appear when exposed as separate JMAP accounts or shared calendars.
- Views: week (the Calendars home tab and default), day, month, agenda (next 30 days). Week starts Monday (`preferences.week_start`). 24 h clock. Default time zone `America/Vancouver` (`preferences.timezone`); an event's own zone is shown when it differs. (Plan §10 said the home tab is the month and §9 said week is the default view; week wins and month is one click.)
- Nav (old plan §10): a continuous mini-calendar scroller opening on the week before the current month through the week after, then calendars grouped by account with a color swatch and a visibility toggle.
- Event fields: title, location, description (text, or the F-4 pane if HTML), start and end or all-day, time zone, calendar, alerts, recurrence, participants (read in T-1, editable in T-6).
- Recurrence: RRULE via libical, EXDATE, RDATE, overridden instances (`recurrenceOverrides`). Edit scope: this, this and following, all. Expansion cached in `event_instance` for a rolling window of −6 to +18 months, recomputed on change and when the window moves (daily).
- Time zones: system zoneinfo through libical; store UTC plus IANA zone; handle DST gaps (a 02:30 that does not exist moves forward) and overlaps (first occurrence); all-day and floating events are dates, not instants.
- Reminders: the core schedules the next alert (a timer on the core thread over `reminder`), emits `calendar.reminder` and a desktop notification through the host; snooze 5, 10, or 30 minutes. Only while the app runs (autostart recommended, [W-SHIP.md](W-SHIP.md)).
- Drag to create, move, and resize in week and day views, in 15-minute steps (`ui.calendar.snapMinutes`).
- Import and export `.ics`. Migration from Omacal: its `.ics` export or its data source (open question T-Q3).

## T-1: Verify and sync

**Kind:** Medium
**Status:** Not started.

**Goal:** Go/no-go on JMAP Calendars, then events in the store.

**Entry gate:** S-5 complete.

**Work items:**

- [ ] Check the capability on Andrew's Stalwart and the container; record the version, the draft revision, and property names. No-go stops here with a decision request.
- [ ] `Calendar/get|changes`, `CalendarEvent/get|changes|query` in `core/src/calendar_sync.c`; `calendar.list`, `event.get`; `calendar.changed`; activity `sync.calendar`.
- [ ] Unity on fixtures: `test_calendar_initial`, `test_event_delta`, `test_jscal_map_roundtrip`.

**Done means:** Events mirror the server.

**Exit gate:** `make test` exits 0; container events are in the store.

## T-2: Recurrence and time zones

**Kind:** Hard
**Status:** Not started.

**Goal:** Every occurrence is computed correctly, including across DST.

**Entry gate:** T-1 complete.

**Work items:**

- [ ] Add libical (`libical-devel`); `core/src/recur.c` expanding JSCalendar recurrence (via conversion to libical structures) into `event_instance`.
- [ ] `event.range {calendarIds, start, end, tz}` returning occurrences with overrides applied.
- [ ] Window maintenance daily.
- [ ] Unity: weekly event across the November 2026 DST change in America/Vancouver, a monthly "last Friday", an EXDATE, an RDATE, a moved override, a cancelled instance, a gap time, an overlap time, an all-day multi-day event, a floating event.

**Done means:** The occurrence list matches hand-computed expectations for every case.

**Exit gate:** `make test` exits 0 with the DST suite.

## T-3: Calendar views

**Kind:** Medium
**Status:** Not started.

**Goal:** Andrew sees his week.

**Entry gate:** T-2 complete.

**Work items:**

- [ ] `ui/src/calendar/`: week (default home tab), day, month, agenda in plain DOM (not Tabulator), 24 h axis, now line, all-day row, overlap layout.
- [ ] Mini-calendar nav and the calendars list with swatches and visibility.
- [ ] Keyboard: `t` today, `w`/`d`/`m`/`a` views, arrows move by period.
- [ ] Event popover with text fields; open in a sub-tab.
- [ ] The Grok composer from V-8 reports the open event's id (`context.type` `event`). No open event, no context.

**Done means:** A weekly recurring event with one moved and one cancelled instance displays correctly across the November DST change (plan P5 acceptance, display half).

**Exit gate:** `make test` exits 0; in `make build` the case above displays correctly.

## T-4: Event editor

**Kind:** Medium
**Status:** Not started.

**Goal:** Events can be created and changed, including recurring ones.

**Entry gate:** T-3 complete.

**Work items:**

- [ ] Editor tab with the Lock's fields, a recurrence builder (daily, weekly by day, monthly by date or weekday, yearly, until or count), alerts.
- [ ] Drag create, move, resize in week and day; scope prompt for recurring events.
- [ ] `event.save {event, scope, occurrenceStart?}`, `event.delete` through the op queue (`CalendarEvent/set`).
- [ ] Unity: `test_scope_this_creates_override`, `test_scope_following_splits_series`, `test_scope_all_updates_master`.

**Done means:** Each scope does what it says, and the server agrees.

**Exit gate:** `make test` exits 0; against the container each scope round-trips.

## T-5: Reminders and `.ics`

**Kind:** Medium
**Status:** Not started.

**Goal:** Reminders fire on time, and Omacal data comes in.

**Entry gate:** T-4 complete.

**Work items:**

- [ ] `reminder` scheduling, `calendar.reminder`, host notification with Snooze 5/10/30 and Open; `reminder.snooze`; missed reminders while closed show once at start (within 24 h).
- [ ] `event.import {calendarId, path}`, `event.export {ids|calendarId, path}` with libical.
- [ ] Unity: `test_reminder_next_fire`, `test_reminder_snooze`, `test_ics_roundtrip`.

**Done means:** A reminder fires on time and Omacal data is imported (plan P5 acceptance).

**Exit gate:** `make test` exits 0; a reminder set 2 minutes ahead fires within 5 s of its time; Andrew's Omacal export imports.

## T-6: iTIP invitations

**Kind:** Medium
**Status:** Not started.

**Goal:** Invitations received by mail can be answered.

**Entry gate:** The initial working copy is complete (index milestone 4).

**Work items:**

- [ ] Detect `text/calendar; method=REQUEST` parts; show Accept, Tentative, Decline above the message (parent-drawn, not in the pane).
- [ ] Reply with `CalendarEvent/participantReply` when supported, else an iTIP REPLY by mail through the outbox; update attendee status.
- [ ] Unity on fixture invitations.

**Done means:** Accepting an invitation updates the organizer and the local event.

**Exit gate:** `make test` exits 0; accept an invitation sent between two container accounts.

## T-7: CalDAV adapter

**Kind:** Hard
**Status:** Not started.

**Goal:** A CalDAV calendar syncs both ways into the same calendar tables as JMAP Calendars. It reuses S-5's DAV client, and iCalendar maps through T-2's libical path.

**Entry gate:** T-5 complete. (S-5, which provides `dav.c`, is earlier in the sequence.)

**Work items:**

- [ ] `core/src/caldav_sync.c`:
  - discovery through `/.well-known/caldav` and `calendar-home-set`
  - `sync-collection` with a ctag/ETag fallback
  - `calendar-query` with a time range for the initial window
  - iCalendar to JSCalendar through `jscal_map.c` (T Lock), with unknown properties preserved
- [ ] Writes: PUT with `If-Match`, DELETE, and recurrence-scope edits written as the full VCALENDAR object. A 412 is a conflict (refetch, then re-apply).
- [ ] Reminders (T-5) work the same way for CalDAV events.
- [ ] Unity: `test_caldav_discovery`, `test_caldav_time_range_initial`, `test_caldav_recurring_edit_this_and_following`, `test_caldav_put_conflict`.

**Done means:** A CalDAV calendar's events show in every view and edits round-trip.

**Exit gate:** `make test` exits 0 with the four tests. Against the Docker Stalwart, a recurring event made on the server shows correctly in the week view, and a moved instance round-trips.

## Open questions for Andrew

- T-Q1. Does Stalwart advertise `urn:ietf:params:jmap:calendars`, and which draft revision? (plan Q3)
- T-Q2. Shared calendars (work@, home@): separate logins or shared into one account? Needed in the first round? (plan Q4)
- T-Q3. How does Omacal store data (local `.ics`, CalDAV, other)? (plan Q6)

## Working log

- 2026-10-16 Opened from old plan §9, §10, §13. Decisions: mapping isolated in `jscal_map.c`, week as the home tab, DST gap and overlap rules, daily window maintenance, 15-minute snap, missed reminders shown once, iTIP after the initial copy.
- 2026-10-16 Added T-7, a CalDAV adapter on S-5's DAV client, after the JMAP calendar phases. T-1's entry gate is now S-5.

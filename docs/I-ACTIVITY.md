# Activity Feed — Design Exploration

**Status:** Exploration. Covers the user-facing activity feed view that shows in-progress
operations (mailbox checks, file copies, downloads) with status, progress, ETA, and cancel
buttons, plus a history toggle.

---

## What exists
The Activity button (`ui/index.html:63-65`, `shell.js:168` with `nav: false`) opens the
`#panel-activity` div (`index.html:212`), which is styled with `.log` CSS (`theme.css:355-362`)
but is currently empty. There is no activity tracking infrastructure in the C core, Rust bridge,
or JavaScript.

---

## Activity feed requirements

### Columns
| Column | Description | Example |
|---|---|---|
| Started | When the activity began | `14:23:01.452` |
| Type | The kind of activity | `Retrieve mail`, `Copy files`, `Download attachment` |
| Status | Current state | `In progress`, `Complete`, `Failed`, `Cancelled` |
| ETA | Projected completion time or duration | `in 5s`, `2m remaining`, `done` |
| Description | What the activity is doing | `Fetching message 3 of 17 from imap.example.com` |
| Progress | Current step | `3/17` or `42%` |
| Cancel | Button (only for in-progress activities) | `⨯` button |

### Behavior
- Shows in-progress activities by default.
- Toggle to show historical actions (completed, failed, cancelled).
- Failed activities remain visible in the list (not auto-dismissed).
- Cancel button sends a cancel signal to the C core operation.

---

## Data model (UI-side)

```javascript
type Activity = {
  id: string,           // unique identifier
  started: string,      // timestamp
  type: string,         // "Retrieve mail" | "Copy files" | etc.
  status: "in-progress" | "complete" | "failed" | "cancelled",
  eta?: string,         // "in 5s" | "2m remaining" | undefined
  description: string,  // current action description
  progress: {         // step-based progress
    current: number,
    total: number | null,
  },
  cancellable: boolean,
}
```

---

## C-core integration

The C core would need:
- A registry of active operations (each with an ID, type, status, progress, ETA).
- Functions to register/unregister activities as they start/stop.
- A cancel mechanism: `pavillion_activity_cancel(activity_id)`.
- A read API: `pavillion_activities_get(ActivityInfo **out, int *count)` returning the current
  snapshot.

### Tier impact:
- **C core**: Major — new activity registry module with start/stop/cancel/get APIs.
- **Rust bridge**: New `#[tauri::command]` for `activities_get` and `activity_cancel`, plus
  event bridge if using push model.
- **UI**: New `ui/src/activity.js` module, table rendering in `shell.js`, CSS for the activity
  table and status badges (using `--pav-*` variables).

---

## Data flow options

**Option A — Polling.** The UI polls `activities_get` every 500ms (or a debounce interval).
Simpler to implement but wastes cycles when nothing is happening.

**Option B — Push via Event.** The C core uses a condition variable / event mechanism that
signals the Rust bridge, which then emits a Tauri event to the frontend. More efficient but
requires cross-thread signalling infrastructure.

**Option C — WebSocket bridge.** If the C core supports async command completion via callbacks,
expose those as Tauri events.

---

## CSS/design

- Status badges: `--pav-status-in-progress` (blue), `--pav-status-complete` (green),
  `--pav-status-failed` (red), `--pav-status-cancelled` (gray).
- Progress bar or progress text in the Progress column.
- Cancel button: small `⨯` or `fa-x` icon button, only visible for `in-progress` + `cancellable`.
- History toggle: a checkbox or segmented control at the top of the Activity view.

---

## Tier impact summary

| Component | Changes |
|---|---|
| C core | Activity registry, `pavillion_activity_cancel`, `pavillion_activities_get` |
| Rust bridge | New commands `activities_get`, `activity_cancel`, event bridge if using push |
| UI | New `ui/src/activity.js`, table rendering in `shell.js`, activity CSS |

---

## Open questions

1. **C-core integration depth**: Should the C core track activities per-mailbox sync, per-copy
   job, etc.? Or should this be a UI-side abstraction that wraps lower-level progress events?
2. **Cancellation granularity**: Can any activity be cancelled, or only certain types (network
   fetches, copies)? How does cancel propagate — SIGINT-equivalent to a thread, or a flag the
   operation polls?
3. **History retention**: How long are completed/failed activities kept? Until the app restarts,
   or persisted across sessions?
4. **ETA calculation**: Does the C core provide an ETA, or does the UI estimate based on elapsed
   time and progress fraction?
5. **Push vs poll**: Which approach for the C core → UI data flow? Polling is simpler but less
   responsive for fast-changing progress bars.
6. **Activity types**: Should we enumerate known types (retrieve mail, copy files, download
   attachment, compaction, indexing) or let any subsystem register with a free-form type string?
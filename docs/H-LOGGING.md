# Logging and Log Viewer — Design Exploration

**Status:** Exploration. Covers the C-level app log, JS log bridging, the CodeMirror-based log
viewer in the Activity panel, and the right-click menu that splits Activity vs Log views.

---

## What exists

**C-level logging** (`core/src/pavillion.c:904`):
- `pav_log(subsystem, level, num_args, fmt, ...)` — main logging function, thread-safe via
  `g_log_mu` mutex. Formats a line with millisecond UTC timestamp, level name, subsystem, and
  message.
- Log line format (`C-CORE.md:79`):
  `2026-10-14T17:04:01.123Z INFO  core  core ready`
- Levels (`pavillion.h:84-87`): `PAV_ERROR (0)`, `PAV_WARN (1)`, `PAV_INFO (2)`, `PAV_DEBUG (3)`.
- Dedicated log thread (`pav_log_thread_start/shutdown` at lines 830/849) drains an enqueue
  queue. Thread is NOT started from the Rust bridge currently.
- Output: file via `attach_profile_log` at line 866, opened in append mode during `core_boot()`
  after config loads. Path: `logs/default.log` in the working directory (configurable in profile).
- `pav_ui_log(subsystem, level, message)` at line 936 bridges JS errors into the C core log.

**Rust bridge** (`src-tauri/src/lib.rs`):
- `pav_ui_log` FFI declared at line 67, bridging to C at line 684.
- Registered Tauri command `ui_log` (`build.rs:56-63`, `lib.rs:632`).
- No `log` crate or `env_logger` — Rust uses `eprintln!` for diagnostics only (16 call sites).

**JavaScript** (`ui/src/api.js:23-24`):
- `log(subsystem, level, message)` calls `invoke("ui_log", { subsystem, level, message })`.
- Called from `main.js` (lines 56-66, 84) and `shell.js` (lines 85, 92 for window error handlers).
- Level mapping: `log("ui", 0, ...)` = `PAV_ERROR`, `log("ui", 2, ...)` = `PAV_INFO`.
- No `console.log/warn/error` anywhere in `ui/src/`.

**Activity panel** (`ui/index.html:212`):
- `<div class="panel log" id="panel-activity" role="tabpanel" hidden>` — exists but is empty.
- CSS `.log` class already styled (`theme.css:355-362`):
  ```css
  .log {
    flex: 1;
    margin: 0;
    padding: 0.75rem;
    overflow: auto;
    font-family: ui-monospace, monospace;
    font-size: 0.85rem;
  }
  ```
- Activity nav button at `index.html:63-65` with `data-section="activity"`, `nav: false`
  in shell.js:168.

**No log-reading API exists.** The design doc (`D-UI.md:82`) says "The view asks the core for the
text. It does not open the log file itself." But there is no FFI function to read the log file.
The C core writes to a `FILE*` and does not expose a read-back mechanism.

**CodeMirror**: Not installed anywhere. Mentioned only in docs
(`PAVILLION_PLAN.md:61`, `PAVILLION_PLAN.md:505`, `D-UI.md:84`, `G-LAYERS.md:71`) as "not a
dependency until that screen exists." The Lithium project has CodeMirror extensions, but they
are not available in this codebase.

---

## 1. C-level log API (new function needed)

A new FFI function to read the log file contents is required. Following the existing C API
pattern:

```
// pavillion.h (new declaration)
int pavillion_log_read(char **out_text, char *err, size_t err_len);

// Semantics:
// - Reads the log file (logs/default.log or pavillion.log if no profile loaded)
// - Allocates and returns contents via *out_text (caller frees with pavillion_free)
// - Returns 0 on success, -1 on error (with err buffer populated)
// - Optional: accept a severity filter or component filter as parameters
// - Format: returns the same line format as written:
//   2026-10-14T17:04:01.123Z INFO  core  core ready
```

The log thread does not need to be started for read-back. Reading the file directly is simpler
and avoids race conditions with the queue. The file is written sequentially (append mode with
`fputs`), so reads can use `fread` or `getline` into an allocated buffer.

### Tier impact:
- **C core**: New function in `pavillion.h` + implementation in `pavillion.c`.
- **Rust bridge**: New `#[tauri::command] fn log_read()` in `lib.rs`, registered in `build.rs`.
- **UI**: New `api.js` function calling the Tauri command.

---

## 2. Log viewer as an Activity tab option

The Activity button should present a context menu on right-click (or a dropdown) with two options:
- **Activity** — the activity feed view (see [I-ACTIVITY.md](I-ACTIVITY.md)).
- **Log** — the raw C-core log viewer.

When "Log" is selected, the `#panel-activity` div is populated with the CodeMirror editor.

### Right-click menu on Activity button

Right-clicking the Activity nav button shows a small menu:
```
Activity
Log
```
- "Activity" opens the activity feed view.
- "Log" opens the log viewer in the same panel.

This replaces the current plain click behavior of `openSection("activity")` with a choice, or
adds a modifier (e.g., Ctrl+click = Log, plain click = Activity).

### Tier impact: UI only — `shell.js` event handler on the Activity button.

---

## 3. CodeMirror integration

**Installation decision**: CodeMirror 6 is planned for HTML/Lua source editing
(`PAVILLION_PLAN.md:61`). Install it now for the log viewer as well.

**Editor configuration:**
- Font: Vanadium Mono. The log panel CSS uses `ui-monospace`, but CodeMirror can be configured
  with `fontFamily: 'Vanadium Mono, monospace'`.
- Line numbers: enabled.
- ReadOnly: true.
- Line wrapping: configurable.
- Theme: match existing dark theme variables (`--pav-*` from `G-LAYERS.md` L4).

**Custom syntax highlighting:**
A simple CodeMirror language mode that parses each line:
`<timestamp> <LEVEL> <subsystem> <message>`

Token classes:
- `.cm-log-ts` — timestamp (dimmed)
- `.cm-log-level-INFO` / `.cm-log-level-WARN` / `.cm-log-level-ERROR` / `.cm-log-level-DEBUG`
- `.cm-log-subsystem` — the subsystem tag
- `.cm-log-message` — the rest

**Lithium extensions**: The task mentions "extensions to CodeMirror that we might want to include
from the Lithium project." Since no Lithium code is present in this repo, this will require:
- Identifying which extensions are useful (line highlighting? search match count? column guide?)
- Either vendoring them or importing from a shared package.

### Tier impact:
- **UI**: Install CodeMirror 6 via npm, create `ui/src/log-viewer.js` module, CSS for dark theme.
- **Dependencies**: Add `@codemirror/view`, `@codemirror/state`, `@codemirror/lang-*` packages.

---

## 4. Search and filter UI

At the top of the log viewer (above the CodeMirror editor):
- **Search input**: text field that filters lines containing the search string. Uses CodeMirror's
  `search` extension + a custom filter that highlights matches.
- **Severity filter**: multi-select dropdown with INFO, WARN, ERROR, DEBUG checkboxes. Default: all
  checked.
- **Component/subsystem filter**: multi-select dropdown populated from the distinct subsystem
  tags in the log (core, config, ui, etc.). Default: all checked.
- **Refresh button**: re-reads the log file from the C core.
- **Auto-follow toggle**: when enabled, the viewer scrolls to the newest line (like `tail -f`).

**Filtering approach**: Rather than modifying the loaded text, use CodeMirror's `panel` or `gutter`
markers, or maintain a folded-lines set. Simpler initial approach: load all lines, then use a
custom `TextMarker` or `Decoration` to dim non-matching lines.

### Tier impact: UI only — `shell.js` event handlers, new CSS classes for filters.

---

## 5. Print and email forwarding

- **Print**: Standard browser print on the CodeMirror container, or use CodeMirror's print
  extension. Print the filtered view (current filter applied).
- **Email forwarding**: Use Tauri's `invoke` to call a Rust function that opens the system email
  client with a `mailto:` URI pre-populated with the log (or filtered log) as the body.
  Alternatively, use the OS opener with `data:text/plain;base64,...` as a fallback.

### Tier impact:
- **UI**: `shell.js` handler for the print/email buttons.
- **Rust**: New `invoke` command to open mailto or write to a temp file.
- **C core**: None.

---

## Tier impact summary

| Component | Changes |
|---|---|
| C core | New `pavillion_log_read` function |
| Rust bridge | New `log_read` command, optionally `print_log` / `email_log` |
| UI | New `ui/src/log-viewer.js`, CodeMirror install, filter/search UI, right-click menu on Activity button |
| CSS | Dark theme matching for CodeMirror, filter bar styling |
| npm | `@codemirror/*` packages as dependencies |

---

## Open questions

1. **Log file location**: Should the Rust bridge read the file path from the profile config
   (like `attach_profile_log` does), or should the C function take a path parameter?
2. **Filtering**: Server-side (C returns filtered lines) or client-side (C returns all, UI filters
   in CodeMirror)? Server-side is simpler for large logs but client-side is more responsive.
3. **Auto-follow**: Should the log viewer auto-scroll to the newest line on open, or stay at the
   top? D-UI.md:82 says "scroll to the newest line on open" — confirm this is the desired behavior.
4. **Log thread**: Should we start the log thread from Rust at app launch (it currently isn't
   started)? Or is file-based reading sufficient?
5. **Line limit**: For very large log files, should there be a tail limit (e.g., last 10,000 lines)
   to avoid memory issues in the browser?
6. **Lithium extensions**: Which specific CodeMirror extensions from the Lithium project should
   be included? Need access to the Lithium source to evaluate.
7. **Email method**: `mailto:` URI (simple, limited to ~2KB body) or temporary file with
   `tauri::api::process::Command` to call `xdg-email`?
8. **Right-click menu vs modifier key**: Right-click context menu (as proposed) or keyboard
   modifier (Ctrl+click for Log, Shift+click for Activity)?
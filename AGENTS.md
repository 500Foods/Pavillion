# Pavillion — agent notes

Working rules pulled from the locks on 2026-10-18. A Lock wins if this file drifts. Fix this file. [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) is the map. [PAVILLION_PLAN.md](PAVILLION_PLAN.md) is the only order of work. The document that owns a topic wins a conflict; fix the other page.

## What it is

A Linux desktop for one person, one process, one active profile: mail, contacts, calendars, and files on a self-hosted Stalwart server (JMAP), plus a few ordinary IMAP mailboxes. Offline SQLite. HTML mail stays sandboxed. Passwords stay in the keyring. Fedora is the v1 platform. The identifier is `com.500nodes.pavillion`.

Portable C11 core, a thin Tauri 2 shell, vanilla JS. The core will later link into Hydrogen. The page will later mount in Lithium. Do not write logic that would have to be rewritten for that.

## Read before editing

1. The Resume and the sequence row in [PAVILLION_PLAN.md](PAVILLION_PLAN.md).
2. Only the named phase: Goal, Entry gate, Work items, Done means, Exit gate, plus the previous phase's Status, Accomplished, Lessons learned, and Handoff.
3. The Lock that phase cites. Not the rest of the plan.

One phase per session. Do not open the next phase. Do not work two phases at once. Ask the qualifying questions and wait before editing source. A Lock change is asked first, written first, then the code. Check a work item `[x]` only after its check passed. Defer with `[~]`, one line, and the phase it moves to. A sitting that ends early marks the phase **in progress** and names the last finished work item.

Close-out, in the owning document, before stopping: Status, Accomplished, Lessons learned, Handoff, that document's phase row, a Working log line, and the Status column and Resume in the index. Before that writing, update `ui/src/help/pages.js` for what changed. If the user page has nothing new, add or extend a Developer section. A phase is not complete with that pass skipped. A UI exit gate is the window, not a screenshot in the repo. Asking whether the phase is wrapped up, or whether lessons and the next phase are written, is not acceptance. Acceptance is Andrew saying the window is accepted. Until then leave Status in progress, do not write Accomplished, Lessons learned, or Handoff, and do not prep the next phase.

## Where the work stands

K-3 (step 48) is complete. The Settings → Accounts panel with add/test/save/remove is real, backed by mock transport and 10 Vitest tests. M-2 (account.test) is complete. E-1 (step 49) is complete: tree tokens, skeleton, and row regions render a placeholder Mail tree scaled by shell and nav-body zoom, with 16 Vitest tests. The phase stamp is `0.50`. `PAVILLION_BUILD` is dirty at 180 from `make dev`; do not commit that bump. `PAVILLION_API_VERSION` stays 3. E-2 (step 50) is next.

Tasks, Notes, and Argent are icons and empty homes. GitHub is an icon. Do not build those screens, tables, event pulls, or a ledger. Do not add a Projects field or a contact link for GitHub. CalDAV and CardDAV are later and write the same tables JMAP will write. They are not a second PIM.

## Layers

| Layer | Path | Owns | Does not |
| --- | --- | --- | --- |
| L4 | `ui/` | Screens, tabs, theme CSS | Servers, `invoke`, untrusted HTML in the main document |
| L3 | `src-tauri/` | Window, webview, dialogs, plugins, one `core` forward, one event emit, scheme forwarding | Command logic, prefs, profiles, URL parsing |
| L2 | `core/` | Dispatcher, store, sync, mail, auth, sanitizer, rules, CLI socket | Tauri types, WebView assumptions |
| L1 | `core/platform/` | Dirs, secrets, file modes, clock, wake hooks | Notifications, tray, single-instance (those are L3) |

- The UI talks only through `ui/src/api.js`. Only `ui/src/transport/tauri.js` may import `@tauri-apps`.
- Rust forwards. Do not add logic to `src-tauri/src/lib.rs`. Do not shrink it back to a forwarder, and do not touch the quit path (SIGINT block, counting `sigaction`, `WEBKIT_DISABLE_COMPOSITING_MODE`) except in a phase that owns it.
- OS calls go through `pav_platform()`. `#ifdef`, libsecret, and XDG names stay under `core/platform/`. Join paths with `pav_path_join`. No `/` literals and no `~` expansion.
- Tests install `pav_platform_fake()`. They never touch the real keyring.
- The core's public C surface is `core/include/pavillion.h`: `pavillion_start`, `pavillion_stop`, `pavillion_call`, `pavillion_free`, `pav_on_event`, `pav_protocol`, `pav_cli_main`, `pavillion_api_version`. Free every buffer the core hands out with `pavillion_free`.
- Commands are `noun.verb`. Events are `noun.change`. Wire fields are camelCase. C stays snake_case. A command is not real until `docs/API.md` lists it as `State: real` under `## Commands`, in the same shape the Unity test parses. Planned headings are not callable.
- Bulk data does not cross as JSON over 2 MB. Use a file path or `pav-cid` / `pav-blob` / `pav-link`.
- One store writer will own the only write connection (L-1, not started). Until then the core thread is the only database opener. A worker never holds `sqlite3 *`. Every thread Pavillion creates uses a 512 KB stack.
- `pav_log(subsystem, level, num_args, fmt, ...)`. `num_args` must match the format string. Never log a password, token, `Authorization` header, or message body.

## Security

- Outside data is `textContent` or the sandboxed pane (`ui/src/common/htmlpane.js`). That includes subjects, addresses, notes, descriptions, file names, server errors, log lines, and settings values. `innerHTML`, `outerHTML`, `insertAdjacentHTML`, and `document.write` need a `// html-ok:` comment naming the static string. A test greps for those names.
- The message pane is `<iframe sandbox>` with no scripts, no same-origin, no forms, no popups, no top navigation. Remote images stay off until allowed for that message. No IPC in a child frame.
- Do not widen `app.security.csp` in `src-tauri/tauri.conf.json` without changing the F Lock first. No `https:` on `img-src`, `frame-src`, or `navigate-to`. No `blob:`. JMAP is libcurl in the core, so `connect-src` never names a mail server. CSP failures show up in `make build`, not `make dev`.
- Font Awesome Pro is the kit `45801522ef` only. Do not vendor Pro SVGs, CSS, or the loader. Do not self-host it to fix a CSP problem.
- No password, token, `secret`, or `refresh_token` field in a profile. The keyring service is `pavillion`, user `<profile id>/<account id>`.
- Index, profile, log, and database files are `0600`. Directories that hold them are `0700`. Writes are atomic (`name.tmp`, then rename).

## UI

No framework and no CSS library. Semantic `--pav-*` variables. Widgets scope under `.pav-root`. Times are 24 h. Default zone is `America/Vancouver`.

Allowed libraries, installed by the phase that first needs them: Tabulator for mail, contacts, and files lists; CodeMirror 6 for the log and later HTML/Lua source; SunEditor inside the sandboxed composer, plus a plain-text mode. The mailbox tree, the activity table, and calendar grids stay plain DOM. Another UI library needs a Lock change first.

The page does not read `Pavillion.json`. Layout the user can change lives in the opaque `preferences.ui` blob (64 KB). The core does not interpret it, except the icon-library allowlist when that phase lands. The page clamps. Zoom writes at most every 300 ms and on `pagehide`.

Served sheets are `ui/public/styles`. `ui/styles` stays unlinked. A new theme variable ships in `dark.css`, `light.css`, and both starter `*.theme` files in the same change.

The Font Awesome kit replaces `<i class="fa-...">` with `<svg>`. A grip or other drag handle must wrap that icon. Set `draggable="false"`, `pointer-events: none`, and `-webkit-user-drag: none` on the icon, and call `preventDefault` on the row `pointerdown`, or the browser starts an image drag. Drag surfaces set `user-select: none` and `-webkit-user-select: none`: feature rows, nav buttons, tabs, and resize handles. A settings help icon (`[data-help-page]`, optional `[data-help-section]`) opens that Help topic through `helpView.open`. It does not open a popup. Hide it when Help is off.

## Build and test

`make` is the entry point. Fedora packages are the names in `scripts/fedora-deps.txt`. Do not pin versions there.

| Command | What |
| --- | --- |
| `make test` | Unity (`build/core_test`), then Vitest (`npm --prefix ui test`). Does not bump the build count. A file-scoped `npm --prefix ui test -- <file>` is a check while editing. It is not this gate. The count in a phase Status is the last full `make test`. |
| `make dev` | Library, bump, Vite, window. Optional working directory: `make dev DEV_DIR=...` |
| `make build` | Release binary at `dist/pavillion`. Also the CSP check. |
| `make mini` | Size build at `dist/pavillion-mini`. Needs `upx`, `strip`, and ImageMagick. Not the rpm. |
| `make clean` | Removes `build/` and `dist/`. |

No argument uses `~/.local/share/pavillion`. One argument is the working directory. A second argument is an error. `~` is expanded by the invoking shell, not by Pavillion.

Version is `0.phase.build`. The phase stamp is `0.phase.0` in `src-tauri/Cargo.toml` and `src-tauri/tauri.conf.json`. The count is the `PAVILLION_BUILD` file. `make dev`, `make build`, and `make mini` add one before they compile and dirty that file. Do not commit that bump unless the sitting means to. Accepting a phase moves the middle number and the release date in `src-tauri/build.rs`, and leaves the count. `PAVILLION_API_VERSION` bumps only for an incompatible C surface or command-contract change. Adding a command or an optional field does not bump it.

`make build` does not produce an AppImage, deb, or rpm (`bundle.active` is false). WebKit bytes stay out of the ELF. The one binary dynamically needs system WebKit. Dynamic link only for curl, SQLite, Jansson, and the other system libraries. Do not static-link them. Lua 5.5 and lexbor are the later vendored exceptions. Do not UPX `dist/pavillion`. CI is `.github/workflows/ci.yml`: Fedora runs `make test` and `make build`.

## Do not build

Gmail or Microsoft OAuth, Exchange, POP3, multi-user, settings sync, telemetry, an in-app updater, a plugin gallery, RSS, chat, newsgroups, PGP/S/MIME, an MCP server. Generic OpenID Connect with PKCE is K-4, not now. There is no HTTP server in Pavillion. Python is not used. Do not add a field the owning Lock does not name.

## Where to look

| Topic | File |
| --- | --- |
| Layers, threads, data flow | `docs/ARCHITECTURE.md` |
| Order of work | `PAVILLION_PLAN.md` |
| Command contract | `docs/API.md` |
| Paths, profile, secrets | `docs/B-CONFIG.md` |
| Threads, `pav_log` | `docs/C-CORE.md` |
| Theme, UI rules | `docs/D-UI.md` |
| CSP, pane, FFI, icons | `docs/F-HARDEN.md` |
| Platform and ABI | `docs/G-LAYERS.md` |
| Shell, tabs, rail | `docs/J-TABS.md` |
| Store | `docs/L-STORE.md` |
| Accounts | `docs/K-ACCOUNTS.md` |
| Sync | `docs/M-SYNC.md` |
| Mail, compose, search, rules | `docs/N-MAIL.md`, `O-COMPOSE.md`, `Q-SEARCH.md`, `R-RULES.md` |
| CLI and Grok Bots | `docs/V-CLI.md` |
| CI and packaging | `docs/W-SHIP.md` |
| Stubs | `docs/X-TASKS.md`, `Y-NOTES.md`, `Z-ARGENT.md`, `GH-GITHUB.md` |

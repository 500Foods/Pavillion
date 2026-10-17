# Pavillion — index

Status: v0.7 (2026-10-16). Implementer: Grok Build. Reviewer: Andrew Simard.

**Resume:** The hello-world sequence (steps 1–20) is complete, and the shell preview is in the tree. F-1 landed 2026-10-16: `pavillion_api_version()` returns `PAVILLION_API_VERSION` (1), `build.rs` copies that macro into the shell, and a mismatch prints `API version mismatch: core=<n> shell=<m>` to stderr and exits 1 before the window. It does not call `pav_log` and it does not create the working directory. v0.7 puts the CodeMirror log (H-2, H-3) immediately after H-1, and restores Tabulator and SunEditor in the phases that first need them. WebKit bytes stay out of both ELFs; only `pavillion-gui` dynamically needs the system library. W-1 is complete. The Fedora job is green ([run 38961249214](https://github.com/500Foods/Pavillion/actions/runs/38961249214)), a deliberate Unity failure turned it red ([run 38962441480](https://github.com/500Foods/Pavillion/actions/runs/38962441480)), and the revert is green ([run 38962706169](https://github.com/500Foods/Pavillion/actions/runs/38962706169)). F-5 landed 2026-10-16: config parse, preferences, and ensure/prepare sit on `pav_cfg.h`, split into `config.c`, `config_prefs.c`, and `config_fs.c` (1,414 lines together, under the old 1,493). `PAVILLION_API_VERSION` stayed 1 through F-5. F-2 landed 2026-10-16: `preferences.ui` is an opaque blob (`pavillion_ui_get` / `pavillion_ui_set`, 64 KB, no forbidden key). Zoom, nav, splits, icons, kit, and fonts live there. `PavShellPrefs` is the window only, so `PAVILLION_API_VERSION` is 2. An old profile is rewritten once on load and the schema stays 1. The page clamps in `ui/src/prefs.js` and writes at most every 300 ms and on `pagehide`. Fonts persist in a commit that touches only `ui/`. `make test` was 35 tests after F-2. G-1 landed 2026-10-16: OS file calls go through `pav_platform()`. Tests install the fake. Profile writes are atomic (`name.tmp` then rename). `PAVILLION_API_VERSION` stays 2. `make test` is 37 tests. The `macos-core` job is in `.github/workflows/ci.yml` with `continue-on-error` and has not been observed on a runner. G-2 landed 2026-10-16: `pavillion_call` and `pav_on_event` are the JSON command path. The dispatcher serves `system.status`, `prefs.get`, `prefs.set`, `prefs.ui.get`, `prefs.ui.set`, and `log.client`. [docs/API.md](docs/API.md) lists those as real and the seed as planned. `PAVILLION_API_VERSION` stays 2. `make test` is 43 tests. G-3 landed 2026-10-16: the shell has one `core` command over `pavillion_call`, `ui/src/api.js` is `createApi(transport)`, and `make test` runs Vitest (43 Unity tests and 8 Vitest tests). The old C functions stayed in the header until G-8. G-8 landed 2026-10-16: window state, `pav_protocol`, and workdir creation are C. `src-tauri/src/lib.rs` is 199 lines and only forwards. `PavShellPrefs` and `PavillionStatus` are gone from the header, so `PAVILLION_API_VERSION` is 3. `make test` is 48 Unity tests and 8 Vitest tests. H-1 landed 2026-10-16: `pavillion_start` starts the log thread, the ring holds 10,000 lines, `log.tail` reads it, and the log thread emits `log.lines`. `PAVILLION_API_VERSION` stays 3. `make test` is 57 Unity tests and 12 Vitest tests. H-2 landed 2026-10-16: the Activity tab shows the ring in a read-only CodeMirror 6 view. The document is the UTC file line. On screen the stamp is local time, and the level and subsystem are fixed-width columns with two spaces between fields. `preferences.ui.zoom.log` is the log font size, and its slider is on Settings → Zoom. The index `log_level` is applied, and a successful `preferences.ui` write logs `prefs saved` at debug. `PAVILLION_API_VERSION` stays 3. `make test` is 58 Unity tests and 16 Vitest tests. Gzip -9 of `ui/dist` was 167961 bytes after H-2. H-3 landed 2026-10-16: the log view filters by severity and subsystem, hides lines that miss the search, and copies, saves, and prints the visible lines. Save asks the host for a path and the core writes it with `log.export`. `preferences.ui.log` keeps the filter, follow, and wrap. The session log level is `prefs.set {logLevel}` and writes the index. The top bar is Activity and Logs, Severity, Subsystem, search, and a gear. Copy, Save, and Print sit on the bottom bar. Settings calls the shared zoom "Button bars" and the stored key stays `navButtons`. The main tab strip is one of those bars. "Email this log" waits for O-5. `PAVILLION_API_VERSION` stays 3. `src-tauri/src/lib.rs` is still 199 lines; the dialog plugin is chained on the existing builder line. `make test` is 60 Unity tests and 23 Vitest tests. Gzip -9 of `ui/dist` is 171516 bytes. I-1 landed 2026-10-16: the core registry begins, updates, finishes, and cancels activities. Andrew chose SQLite persistence. Finished rows persist in the profile SQLite table `activity` and reload on the next start. Running rows do not. `user_version` stays unset. `activity.list`, `activity.cancel`, `activity.dismiss`, and `activity.changed` are real. `debug.activity_demo` is a worker command and is omitted when `NDEBUG` is set. `PAVILLION_API_VERSION` stays 3. `src-tauri/src/lib.rs` is unchanged. `make test` is 65 Unity tests and 23 Vitest tests. F-4 landed 2026-10-16: outside HTML is a sandboxed srcdoc iframe. `pav-link://<paneId>/<n>` emits `link.clicked` and returns 204. The parent dialog shows the real URL and does not open it. `PAVILLION_API_VERSION` stays 3. `src-tauri/src/lib.rs` is 199 lines. `make test` is 67 Unity tests and 30 Vitest tests. G-4 is complete (2026-10-16, step 34): the UI mounts into an element, the served sheets use `--pav-*` under `.pav-root`, and the harness shows the shell inside a foreign box. Andrew confirmed `make dev` and `make build` look unchanged. After that check, Shell zoom is 50%–200% and the other zoom rows stay 5%–500%. Toolbar follow-ons closed 2026-10-17: a session zoom stack, a shared bar strut, icons on `--pav-bar-zoom`, and toolbar rules held at 1px. `make test` is 67 Unity tests and 34 Vitest tests. `PAVILLION_API_VERSION` stays 3. `src-tauri/src/lib.rs` is 199 lines. D-4 is complete (2026-10-17), in [docs/D-UI.md](docs/D-UI.md). Andrew accepted the look: the themes look right and the switcher is fine. The same day he pulled D-6 (step 41, theme catalog) and D-7 (step 106, theme skin) forward, ahead of J-1. He accepted that window on 2026-10-17, so D-6 and D-7 are complete. J-1 is complete (2026-10-17), step 36, in [docs/J-TABS.md](docs/J-TABS.md). Andrew accepted the window. `make test` is 68 Unity tests and 59 Vitest tests. I-2 landed 2026-10-17: the Activity side is a plain DOM table patched by id. The column is Action and the core field stays `kind`. Account is a number, an icon, and a name. No account shows `000`, the mail icon, and `Pavillion`. Activity hides the log severity menu, subsystem menu, search, and gear, and shows All actions, All accounts, and Options (Show history, Stop all, Clear all, Expand all, Collapse all). Show history is `preferences.ui.activity.history`. Stop all cancels each cancellable row. Clear all dismisses failures and hides finished rows. It does not delete them. Pause and Retry are not in the menu. `dismissed` is on the activity object. `sync.now` returns `{}` until M-3. Update spins while a `sync.*` activity is running, and reduced motion shows the count. Help's `Run demo activity` is debug-only. Andrew accepted the window on 2026-10-17. `PAVILLION_API_VERSION` stays 3. `make test` is 69 Unity tests and 69 Vitest tests. J-2 is complete (2026-10-17), step 38, in [docs/J-TABS.md](docs/J-TABS.md). Andrew accepted the window. Every tab is closable, pointer drag reorders the strip, a single click leaves the nav, and a nav button or a double-click brings that section's home tab forward and switches the nav. `make test` is 69 Unity tests and 78 Vitest tests. J-3 is complete (2026-10-17), step 39, in that same file. Andrew accepted the window, including shortcuts after a calendar click and Ctrl+wheel pane zoom. `make test` is 69 Unity tests and 95 Vitest tests. J-8 is complete (2026-10-17), step 123, in that same file. Andrew accepted the window, including Help zoom and the draggable split. J-6 is next, step 40. Do not start it in this sitting. J-4 stays step 107. After the profile log attaches, a terminal sees warn and error only. `core ready` stays in `logs/<id>.log` and the Activity log. `PAVILLION_API_VERSION` stays 3. `src-tauri/src/lib.rs` is 199 lines; `pav-theme` is on the existing scheme list. Served sheets are `ui/public/styles`. `ui/styles` was left as the unlinked copy. The host page still starts on `color-scheme: dark` and `background: #000`; `data-pav-theme` follows the resolved theme. Leave the bar strut and the 1px toolbar rules in place.

**Read first:** [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), one page with diagrams of the layers, the threads, and the data flow.

## What Pavillion is

Pavillion is a personal desktop app for mail, contacts, calendars, and files, for one user, Andrew Simard. It replaces Betterbird (mail), CardBook (contacts), and Omacal (calendar). It is a JMAP-first client for self-hosted Stalwart, then IMAP/SMTP and CalDAV/CardDAV, all with username and password over TLS and the password kept in the keyring (tenants 500foods.com, 500courses.com, 500nodes.com) and handles about 20 accounts and identities comfortably: unified views, fast switching, per-account identities. It keeps an offline-capable SQLite store with fast local search, renders HTML mail safely (sandboxed, remote content blocked by default), and runs as one app, one process, one active profile, Wayland-friendly, with a 24 h clock and `America/Vancouver` as the default zone. It is a Tauri 2 app: a portable C11 core library (all mail, sync, storage, and auth), a minimal Rust/Tauri shell that does only Tauri work (window, webview, dialogs, plugins, one forwarding command and event) so the app can later be built into Hydrogen (C), and a vanilla JS/Vite UI that can later be mounted inside Lithium. Lua 5.5 is embedded for mail rules and automation. Fedora Linux is the v1 platform; macOS, Windows, and iPad are deferred, not deleted. Every subsystem should stay small enough to understand and test. Pavillion is open source. An optional fifth section, Grok Bots, stays hidden until Settings → Features turns it on. It wakes a Grok Bot by webhook, and the bot acts through the CLI (V).

**Non-goals (do not build):** Gmail and Microsoft OAuth, Exchange/EWS/Graph, POP3 (generic OpenID Connect with PKCE is in scope, K-4); multi-user; sync of app settings across machines; cloud services of any kind; a plugin or add-on system (add-ons caused the CardBook mess; Lua is a fixed API, not plugins); RSS, a person-to-person chat, newsgroups; PGP and S/MIME unless Andrew decides otherwise (N-Q5); an in-app self-updater (updates are a system package); telemetry. CalDAV and CardDAV are not a second PIM: JMAP writes the tables, and the adapters (S-5, T-7) write the same tables. WebDAV for files is U. The Grok Bots transcript (V-7) is a status log of bot requests, not a chat product. There is no MCP server.

## How sessions work

- One phase per session. A new session reads this Resume, the sequence below, and then only the named phase in its document. Phases are not worked in parallel. The sequence is the only order. Letters group phases by topic. The numbered sequence is the only order, and a phase may depend on a phase in another letter as long as that phase comes earlier in the sequence.
- A phase id is a letter and a number. The letter is the document; the number counts inside it. Numbers are never reused (G-5 and G-7 moved to R-1 and R-3 and are not reused in G). There is no letter P, so phase ids never collide with the Lua `P` table or the old roadmap's P0–P6.
- In the session: read the previous phase's Status, Accomplished, Lessons learned, and Handoff; re-read only this phase and the Lock it cites; ask the qualifying questions and wait for approval before editing source; do this phase only; check a work item `[x]` only after its check passed; defer with `[~]`, one line, and the phase it moves to.
- Close-out, in the document that owns the phase: Status, Accomplished, Lessons learned, Handoff, the phase's row in that document's table, a Working log line, and the Status column and Resume here. Then stop. Before that writing, read `ui/src/help/pages.js` against what the phase changed. Augment, replace, fix, or extend the user page. If the user page has nothing new to say, add or extend a Developer section. A phase is not complete with that pass skipped. A sitting that ends early marks the phase **in progress**, names the last finished work item, and does not open the next phase.
- `make test` passes at the end of every phase. A UI exit gate is the window. Anything touching the CSP, the kit, or isolation is checked in `make build`, not `make dev`.
- A Lock is binding. A phase that needs to change a Lock stops and asks, then changes the Lock first and the code second. Where two documents disagree, the one that owns the topic (the doc table below) wins, and the conflict is fixed in the other.
- **Kinds:** **Easy** is one module and a test, or a static page. **Medium** crosses one boundary (the window, the filesystem, a thread, the FFI, the network). **Hard** is a first join (two build systems, two processes, a new thread model, a new protocol) or a security boundary built from scratch.

## Documents

| Doc | File | Owns |
| --- | --- | --- |
| A | [docs/A-INIT.md](docs/A-INIT.md) | The executable, the window, Unity, linking the C library, the Fedora package list (complete) |
| B | [docs/B-CONFIG.md](docs/B-CONFIG.md) | `Pavillion.json`, profiles, paths, file modes, the account object, the engine field (complete; later phases add fields to its Lock) |
| C | [docs/C-CORE.md](docs/C-CORE.md) | Threads, the command loop, `pav_log`, the store vtable (complete) |
| — | [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | **Read first.** Layers, threads, and data flow on one page (mermaid) |
| D | [docs/D-UI.md](docs/D-UI.md) | The first frame, the shell preview, UI rules, the theme layer |
| E | [docs/E-TREE.md](docs/E-TREE.md) | The tree engine behind the section navs |
| F | [docs/F-HARDEN.md](docs/F-HARDEN.md) | The API version check, UI prefs blob, `config.c` helpers, the CSP, icon libraries and the kit, the icon sanitizer, registry, and icon settings, the outside-HTML rule, the sandboxed pane |
| G | [docs/G-LAYERS.md](docs/G-LAYERS.md) | Layers L1–L4, `pav_platform.h`, the JSON command ABI and `docs/API.md`, the host adapter, the mountable UI, multiplatform |
| H | [docs/H-LOGGING.md](docs/H-LOGGING.md) | The log in production, `log.tail`, the Log view, redaction |
| I | [docs/I-ACTIVITY.md](docs/I-ACTIVITY.md) | The activity registry, cancel, the Activity view, the Update button |
| J | [docs/J-TABS.md](docs/J-TABS.md) | The shell layout, tabs, shortcuts registry, common widgets, session restore, browse tabs |
| K | [docs/K-ACCOUNTS.md](docs/K-ACCOUNTS.md) | Secrets, account numbers, account commands, Settings → Accounts, OIDC |
| L | [docs/L-STORE.md](docs/L-STORE.md) | SQLite schema, migrations, blobs, cache eviction |
| M | [docs/M-SYNC.md](docs/M-SYNC.md) | Network thread, JMAP session and sync, op queue, push, the backend table, IMAP |
| N | [docs/N-MAIL.md](docs/N-MAIL.md) | MIME, the sanitizer, message list, reading pane, actions, attachments, notifications, shortcuts, retention |
| O | [docs/O-COMPOSE.md](docs/O-COMPOSE.md) | Drafts, the composer frame, identities, quoting, signatures, outbox, `mailto:` |
| Q | [docs/Q-SEARCH.md](docs/Q-SEARCH.md) | FTS5, the query syntax, server search, saved searches |
| R | [docs/R-RULES.md](docs/R-RULES.md) | Embedded Lua, the `P` API, mail rules, Sieve export |
| S | [docs/S-CONTACTS.md](docs/S-CONTACTS.md) | Address books, contacts, vCard, autocomplete |
| T | [docs/T-CALENDAR.md](docs/T-CALENDAR.md) | Calendars, events, recurrence, time zones, reminders, `.ics`, iTIP |
| U | [docs/U-FILES.md](docs/U-FILES.md) | The Files section: protocol, browse, transfer |
| V | [docs/V-CLI.md](docs/V-CLI.md) | The command line for AI agents: socket, keys, audit, and the Grok Bots section |
| W | [docs/W-SHIP.md](docs/W-SHIP.md) | CI, the fixture server and Stalwart container, testing rules, rpm, dnf, Arch |

[docs/API.md](docs/API.md) is the command contract (G-2). `docs/LUA_API.md` is written by R-2.

## Implementation sequence

| # | Phase | What | Kind | Status |
| --- | --- | --- | --- | --- |
| 1 | A-1 | C library skeleton and Makefile | Easy | Done |
| 2 | A-2 | Unity harness | Easy | Done |
| 3 | A-3 | Tauri and Vite window | Medium | Done |
| 4 | A-6 | `--help` and `--version` | Easy | Done |
| 5 | D-1 | Frame and theme variables | Easy | Done |
| 6 | A-4 | Link the C library into the shell | Hard | Done |
| 7 | D-2 | `api.js` and status text | Easy | Done |
| 8 | C-1 | Log function | Easy | Done |
| 9 | C-2 | Log thread and the line format | Medium | Done |
| 10 | B-1 | Parse the index and a profile | Easy | Done |
| 11 | B-2 | First-run files | Easy | Done |
| 12 | B-3 | Scan, kind, and a broken active profile | Medium | Done |
| 13 | B-4 | Paths and file modes | Medium | Done |
| 14 | B-5 | An unknown engine is an error | Easy | Done |
| 15 | C-3 | Core thread and the command queue | Medium | Done |
| 16 | C-4 | SQLite open and close | Medium | Done |
| 17 | C-5 | Stop | Easy | Done |
| 18 | C-6 | Start loads a profile | Medium | Done |
| 19 | A-5 | Working directory and a real start | Medium | Done |
| 20 | D-3 | The error state | Easy | Done |
| | | *Milestone 0: hello world, plus the shell preview (not a phase)* | | Done |
| 21 | F-1 | API version check (`pavillion_api_version`) | Medium | Done |
| 22 | W-1 | CI on GitHub Actions: `make test` and `make build` on Fedora | Medium | Done |
| 23 | F-5 | Trim `config.c` with a helper layer | Medium | Done |
| 24 | F-2 | UI preferences as an opaque JSON blob (Fonts persisted) | Medium | Done |
| 25 | G-1 | Platform layer and a test fake; macOS core CI job | Medium | Done |
| 26 | G-2 | JSON command dispatcher, `pav_on_event`, `docs/API.md` | Medium | Done |
| 27 | G-3 | Generic Tauri `core` command; transport-agnostic `api.js`; Vitest on `make test` | Medium | Done |
| 28 | G-8 | Move the remaining `lib.rs` logic into C (window state, `pav_protocol`, workdir); Rust only does Tauri work | Medium | Done |
| 29 | H-1 | Log thread in production, ring, `log.tail`, `log.lines` events | Medium | Done |
| 30 | H-2 | Log view: CodeMirror, live, in the Activity tab | Medium | Done |
| 31 | H-3 | Log search, filters, follow, copy, save, print | Medium | Done |
| 32 | I-1 | Activity registry in the core, cancel, events | Medium | Done |
| 33 | F-4 | Message pane isolation, proven in `make build` | Medium | Done |
| 34 | G-4 | Mountable UI for Lithium, `--pav-*`, harness | Medium | Done |
| 35 | D-4 | Theme switch: semantic variables, dark, light, follow system | Medium | Done |
| 36 | J-1 | Common widgets: menu, tooltip, toast, dialog | Medium | Done |
| 37 | I-2 | Activity view and the Update button | Medium | Done |
| 38 | J-2 | Tab registry, icons and close, sub-tabs | Medium | Done |
| 39 | J-3 | Shortcut registry, tab menu, pinned tabs | Medium | Done |
| 123 | J-8 | Help index, search, and topic pages | Medium | Done |
| 40 | J-6 | Session restore | Medium | |
| 41 | D-6 | Theme catalog: files, the list, and fallback | Medium | Done |
| | | *Milestone 1: foundation — layers, log, activity, tabs* | | |
| 116 | V-5 | Features toggles and the Grok Bots section shell | Medium | |
| 42 | L-1 | Migration runner, statement helpers, schema v1 | Medium | |
| 43 | K-1 | Secrets through the platform (libsecret) | Medium | |
| 44 | K-2 | Account model and commands, account numbers | Medium | |
| 45 | W-2 | JMAP fixture server and the Stalwart container | Hard | |
| 46 | M-1 | Per-server network threads (own curl multi each), caps, retries, redaction | Hard | |
| 47 | M-2 | JMAP session discovery, `account.test`, remote accounts | Medium | |
| 48 | K-3 | Settings → Accounts panel | Medium | |
| 49 | E-1 | Tree tokens, skeleton, row regions | Easy | |
| 50 | E-2 | Tree model, view state, three collapse behaviors | Medium | |
| 51 | E-3 | Tree renderer states, badges, hover, menus | Medium | |
| 52 | E-4 | Tree animation and the height test | Easy | |
| 53 | M-3 | Sync supervisor, Identity and Mailbox sync, `mailbox.list` | Medium | |
| 54 | E-7 | Real mailbox tree from `mailbox.list` | Hard | |
| | | *Milestone 2: real accounts and mailbox trees (old P0)* | | |
| 55 | L-2 | Mail schema and the blob store | Medium | |
| 56 | M-4 | Email initial sync | Medium | |
| 57 | M-5 | Delta sync | Medium | |
| 58 | N-1 | Body fetch and MIME (GMime) | Medium | |
| 59 | N-2 | The HTML sanitizer and the hostile corpus | Hard | |
| 60 | Q-1 | FTS5 indexing and local search | Medium | |
| 61 | N-3 | Message list (Tabulator) | Medium | |
| 62 | Q-2 | Search UI and server search | Medium | |
| 63 | N-4 | Reading pane: sandbox, remote content, `cid:`, links | Medium | |
| 64 | M-6 | Mutation queue, conflicts, undo | Hard | |
| 65 | N-5 | Flags, move, delete, archive, mailbox management | Medium | |
| 66 | N-6 | Threads, unified views, message tabs | Medium | |
| 67 | N-7 | Attachments | Medium | |
| 68 | M-7 | Push (EventSource), polling fallback | Medium | |
| 69 | O-1 | Draft model | Medium | |
| 70 | O-2 | Composer frame (SunEditor) | Hard | |
| 71 | O-3 | Identities, reply, forward, autocomplete | Medium | |
| 72 | O-4 | Attachments, inline images, signatures | Medium | |
| 73 | O-5 | Outbox and sending | Hard | |
| 74 | O-6 | `mailto:` and single instance | Medium | |
| 75 | N-8 | Notifications and mail shortcuts | Medium | |
| 76 | N-9 | Folder retention | Medium | |
| | | *Milestone 3: daily-driver mail (old P1–P3)* | | |
| 77 | W-3 | rpm, dnf repository, desktop integration | Medium | |
| 78 | W-4 | Arch `PKGBUILD` | Easy | |
| 79 | V-1 | CLI socket, dispatch, `status`, `help --json` | Hard | |
| 80 | V-2 | Bot keys, pause, audit | Medium | |
| 117 | V-6 | Grok Bot webhook, hello, and request states | Medium | |
| 118 | V-7 | Bot transcript, prod, and the Help instruction | Medium | |
| 81 | V-3 | CLI mail commands | Medium | |
| 119 | V-8 | Contextual Grok composer on the open item | Medium | |
| 82 | M-8 | IMAP/SMTP backend (SASL PLAIN over TLS) | Hard | |
| 83 | R-1 | Lua 5.5 in the core, sandboxed | Medium | |
| 84 | R-2 | The `P` API and `docs/LUA_API.md` | Medium | |
| 85 | R-3 | Mail rules engine | Hard | |
| 86 | R-4 | Rules editor, dry run, run log | Medium | |
| 87 | R-5 | Sieve export | Medium | |
| 121 | V-10 | Tell a Grok Bot when mail arrives or a mailbox changes | Medium | |
| 88 | L-3 | Contacts, calendars, files, rules, CLI tables | Medium | |
| 89 | S-1 | Verify JMAP Contacts; sync | Medium | |
| 90 | S-2 | Contacts screen | Medium | |
| 91 | S-3 | Contact editor | Medium | |
| 92 | S-4 | vCard import and export; autocomplete | Medium | |
| 93 | S-5 | CardDAV adapter and the shared DAV client | Hard | |
| 94 | T-1 | Verify JMAP Calendars (go/no-go); sync | Medium | |
| 95 | T-2 | Recurrence and time zones (libical) | Hard | |
| 96 | T-3 | Calendar views | Medium | |
| 97 | T-4 | Event editor | Medium | |
| 98 | T-5 | Reminders and `.ics` | Medium | |
| 99 | T-7 | CalDAV adapter | Hard | |
| 100 | U-1 | Files: protocol and browse | Medium | |
| 101 | U-2 | Files: transfer and manage | Medium | |
| 120 | V-9 | CLI file commands | Medium | |
| 122 | V-11 | Tell a Grok Bot when a files folder changes | Medium | |
| | | *Milestone 4: the initial working copy (old P4–P5 plus rules and files)* | | |
| 102 | V-4 | `watch`, `config`, contacts and calendar commands | Medium | |
| | | *Milestone 5: the CLI covers mail, files, contacts, and calendars* | | |
| 103 | F-3 | Icon library sources: typed https URLs, allowlist in the CSP, FA and Material presets | Medium | |
| 104 | F-7 | Shared icon HTML sanitizer, icon registry, overrides in the UI blob | Medium | |
| 105 | F-8 | Icon settings: icon tree and the three-preview editor | Medium | |
| 106 | D-7 | Theme skin: fonts, icons, mark, and HTML in CSS | Medium | Done |
| 107 | J-4 | Keyboard reorder, overflow, widths, file drop | Medium | |
| 108 | J-5 | Vertical tabs and section colors | Medium | |
| 109 | E-5 | Tree presets | Medium | |
| 110 | E-6 | Tree drag and drop | Hard | |
| 111 | J-7 | The `+` tab: sandboxed browsing | Hard | |
| 112 | K-4 | OpenID Connect sign-in | Hard | |
| 113 | T-6 | iTIP invitations | Medium | |
| 114 | D-5 | Theme extras: density, gradients, contrast audit | Medium | |
| 115 | G-6 | Second-platform smoke build | Hard | |
| | | *Milestone 6: polish (old P6); further items are agreed with Andrew at that point* | | |

Why this order: the command ABI (G-2, G-3, G-8) is in place before new commands proliferate. H-1, H-2, and H-3 land before the activity view and before accounts, so later phases can be watched from inside the app. The icon-kit failure was invisible without that view: the only report possible was that no icons were visible. The icon editor and tab polish stay at milestone 6 and are not on the path to mail. Andrew pulled the theme catalog (D-6, step 41) and the theme skin (D-7, step 106) forward on 2026-10-17, before J-1 and before that icon editor. D-7 reads icon HTML from theme variables now. F-8 still replaces the icon editor later, and that rewrite meets these variables. He accepted the D-6 and D-7 window on 2026-10-17. J-1 is complete (2026-10-17). I-2 is complete (2026-10-17) and Andrew accepted that window. J-2 is complete (2026-10-17) and Andrew accepted that window. J-3 is complete (2026-10-17). Andrew accepted that window. J-8 is complete (2026-10-17), step 123. Andrew accepted the window, including Help zoom and the draggable split. J-6 is next, step 40. Do not start it in this sitting. J-4 stays at step 107. Tabs, menus, toasts, and dialogs (through J-3 and J-6) come before accounts because every screen after them uses them. The fixture server comes before the first network phase. Isolation (F-4) and the sanitizer (N-2) come before any real message is shown. Search (Q) comes as soon as bodies exist, before the composer. The rpm and the mail CLI follow milestone 3, so the Betterbird replacement is installable before calendars and files, and neither packaging nor `pavillion status` waits on U. IMAP/SMTP (M-8) follows that shippable mail client. Rules follow IMAP because they hook into a working sync path. CardDAV (S-5) and CalDAV (T-7) follow their JMAP phases. A phase whose entry gate names a phase later in this table is an error in the docs; fix the docs before starting it. Grok Bots were scoped on 2026-10-17 and are not started. V-5 is the fifth nav cell and Features visibility, after J-6 and before accounts. The webhook, the transcript, and the composer follow V-2. File commands follow U-2. Triggers follow the mail rules (V-10) and the files store (V-11). They are not part of R.

## Review notes (2026-10-17, Grok Bots)

Andrew scoped a fifth section. The decisions are the V Lock, under "Grok Bots (2026-10-17)". Nothing in that block is started. Completed phases were not rewritten. J-2 was left alone.

- **Section.** Grok Bots is a top-rail cell, hidden until Settings → Features turns it on. The other four top cells can be hidden there too. The bottom rail stays. V-5 builds the empty section after J-6.
- **Two directions.** Pavillion POSTs to the bot's webhook. The bot acts, and reports back, through the CLI. HTTP 200 means accepted. The states are Sending, Sent, Working, Completed, Failed, and No reply. A quiet request can be prodded on the same id. The token stays in `PAVILLION_TOKEN`.
- **Where it sits.** V-6 and V-7 follow V-2. V-8 follows mail commands, and sends the open item's id with the sentence. V-9 adds file commands after U-2. Contacts, calendars, and files register their context in S-2, T-3, and U-2.
- **Help.** V-7 writes the note Andrew pastes to a bot: env var, `help --json`, ack then the task then done, and no MCP.
- **Triggers.** "Tell Charlie when new mail arrives" and "Tell Betty when this folder is updated" are saved phrases (V-10, V-11). Each match is a bot request. Initial sync does not fire them. They are not mail rules and they do not export to Sieve.
- **Later talks.** At the start of V-10, and not before: how far queue limits go (V-Q2), and how much of the behavior is a suite of Lua tools and examples instead of more C (V-Q3, R-Q4). The two-in-flight cap and `P.bot.tell` stay as written until that conversation.

## Review notes (2026-10-16, v0.7)

Andrew put three libraries back the same day v0.6 had taken them out, and moved the log view back next to H-1. The icon-kit failure had no in-app log, so the only report possible was that no icons were visible. Logging early is how the next failure gets a cause instead of a symptom. The decisions live in the owning Lock; this list is the map.

- **Log view.** H-2 and H-3 are steps 30 and 31, immediately after H-1 and before I-1. H-2 installs CodeMirror 6, shows the live log, and creates the Activity|Log control. I-2 fills the Activity side. It does not ship a `<pre>` tail for H-2 to replace.
- **Lists and composer.** N-3 uses Tabulator for mail. Contacts (S) and files (U) share that table. O-2 uses SunEditor inside the existing sandbox, with a plain-text mode. None of the three waits on a smaller substitute failing. The mailbox tree (E) and the activity table (I-2) stay plain DOM. Calendar grids stay plain DOM.
- **WebKit.** Neither ELF contains WebKit bytes. No static link, no vendored WebKit, no AppImage. Only `pavillion-gui` has a dynamic `NEEDED` entry for system `libwebkit2gtk`. The `pavillion` CLI does not link it. The rpm depends on the system package, so the GUI process maps that copy.
- **Size.** `ui/dist` gzipped size is recorded when H-2, N-3, and O-2 land. That record is not a reason to drop Tabulator, CodeMirror, or SunEditor. A further UI library needs a Lock change. No UPX.

## Review notes (2026-10-16, v0.6)

A review of the v0.5 plan against the code and the locks. Still in force except where the v0.7 notes above say otherwise.

- **Sequence.** Icon libraries and the icon editor (F-3, F-7, F-8) and tab chrome (J-4, J-5) are milestone 6. They do not sit in front of the platform layer. v0.6 also parked H-2 and H-3 there; v0.7 moved the log view back to steps 30 and 31. Search sits beside the message list. The rpm, the Arch package, and the mail CLI sit immediately after daily-driver mail.
- **Store (L).** 16 MB page cache, 64 MB mmap, short read transactions, one checkpoint retry. `email_mailbox` carries `ra_id` and `received_at` so a folder listing does not sort the whole account. Keyword columns have one writer. Bodies and sanitized HTML are files under the cache budget, not SQLite text. Blob ids are encoded before they become paths.
- **Network (M).** HTTP/2 is requested, with two connections per server so the EventSource does not block API calls. One EventSource per login, not per account. Responses are reduced on the net thread; the core queue never carries a multi-megabyte body. Header backfill stays complete (offline mail is the point) and yields while the user is waiting on a command.
- **Threads (C, R).** Every thread Pavillion creates uses a 512 KB stack. The core thread never blocks on the script thread; a message stays unannounced until rules finish or the page budget expires.
- **UI libraries (N, H, O, W).** Superseded by the v0.7 notes. v0.6 had banned Tabulator, a CodeMirror log, and a default SunEditor, and had capped `ui/dist` at 500 KB gzipped to keep them out. Andrew reversed that the same day. No UPX remains. The CLI binary must not map WebKitGTK (V), and neither ELF may contain WebKit bytes (W).
- **Corrected contradictions.** CalDAV and CardDAV are in the sequence, not in the non-goals. F-6's "one curl multi, store on the core thread" was already superseded by M. The event name is `log.lines`. B and C no longer name a finished phase as next. F-1's test count is 27.

Recorded and waiting beyond milestone 6: shortcut remapping UI (the J-3 registry is data-ready), rule-based tree presets, Betterbird mail import (mbox/Maildir, N-Q4), a headless `pavillion daemon` (V Lock), per-account status dots in the nav, the spacious density, pause-bots-on-lock, Windows and iPad builds.

## Open questions for Andrew

Resolved 2026-10-16 by Andrew (each written into its Lock):
- F-1: an API version (`pavillion_api_version()` / `PAVILLION_API_VERSION`), with no struct-size check and no bindgen. G-8 deletes the structs (F Lock, FFI).
- Rust minimized for Hydrogen (G L3 Lock, G-8).
- M-Q1: one net thread per server (M Lock).
- M-Q2: M-8 right after milestone 3.
- K-Q2: username and password over TLS, with the password in the keyring. OIDC later (K Lock, Authentication).
- K-Q3: discovery from `.well-known/jmap`, with an override.
- R-Q1: settled by a metric (2 ms per message, R-3).
- W-Q3: Docker, with Stalwart v0.16.26 (checked 2026-10-16).
- H-Q1: a ring of 10,000 lines, and a profile log over 10 MB renamed to `<id>.log.1` at start (one generation, no rotation while running).
- J-Q1 (2026-10-17): a restart restores every open tab that can still be rebuilt, including sub-tabs (J Lock, session restore). A missing context is dropped.

Still open, owned by the named document:

- K-Q4. Before K-1, how Stronghold's snapshot password is supplied. 1Password service accounts (token, cached vault list, cached username and password, one refresh, then the Activity control) are recorded in [docs/K-ACCOUNTS.md](docs/K-ACCOUNTS.md). Not part of J-1.

Each is owned by the document named; the answer is written into that document's Lock.

| Id | Question | Needed before |
| --- | --- | --- |
| K-Q1 | Which two or three accounts to add first (two tenants), with their hostnames? | K-3 |
| K-Q4 | How is the Stronghold snapshot password supplied without a second prompt? | K-1 |
| H-Q2 | Per-subsystem debug level once sync exists? | M-1 |
| I-Q1 | The I lock keeps finished activities in memory for the session and treats the log file as the record. Confirm, or ask for a SQLite history, before I-1. | I-1 |
| J-Q2 | Single click shows in the reading pane and only Ctrl+Enter or double-click opens a sub-tab? | J-2 |
| J-Q3 | A cap on open tabs? | J-2 |
| J-Q4 | Should section colors tint the nav button too? | J-5 |
| L-Q1 | Cache limits 2 GB bodies and 4 GB attachments: right size? | L-2 |
| N-Q1 | Notifications: every message, Inbox only, or per-folder opt-in? Quiet hours? | N-8 |
| N-Q2 | Retention: Trash or destroy? Same rule for all willard@ folders? | N-9 |
| N-Q3 | Prefer a server-side retention in Stalwart if available? Never two trimmers on one folder. | N-9 |
| N-Q4 | Import existing mail history from Betterbird, or rely on the server? | after M6 |
| N-Q5 | PGP or S/MIME ever? | after M6 |
| O-Q1 | Undo-send delay: 10 s default, or off? | O-5 |
| O-Q2 | Default compose format: HTML or plain text? | O-2 |
| R-Q2 | Does Stalwart advertise `urn:ietf:params:jmap:sieve`; simple rules default to server or client? | R-5 |
| R-Q3 | Existing Sieve scripts on these accounts to coexist with? | R-5 |
| S-Q1 | Stalwart version and `urn:ietf:params:jmap:contacts`? | S-1 |
| S-Q2 | A CardBook/Betterbird vCard export available? | S-4 |
| T-Q1 | `urn:ietf:params:jmap:calendars` advertised, and which draft revision? | T-1 |
| T-Q2 | Shared calendars (work@, home@): separate logins or shared into one account? Needed in the first round? | T-1 |
| T-Q3 | How does Omacal store data (local `.ics`, CalDAV, other)? | T-5 |
| U-Q1 | Which file storage on Stalwart today (WebDAV, JMAP FileNode), and for which accounts? | U-1 |
| V-Q1 | Which Authorization header does one saved Grok Bot routine show? Copy it into the V Lock at the start of V-6. | V-6 |
| V-Q2 | How far do bot queue limits go? Two in flight is the stand-in. Talk at the start of V-10. | V-10 |
| V-Q3 | How much of "tell a bot" is a Lua tool suite on `P.bot.tell`, and how much is C? Same talk as R-Q4. | V-10 |
| R-Q4 | Lua tools and examples versus more core C, for bot triggers. Same talk as V-Q3. | V-10 |
| G-Q1 | How does Lithium host the core (native module, WASM, Hydrogen-like process, or a socket to a running Pavillion)? | after G-4 |
| G-Q4 | Windows: MSVC or MinGW? | G-6 |
| W-Q1 | Fedora rpm only to start, or Omarchy from day one? Where is the dnf repo hosted? | W-3 |
| W-Q2 | Vanadium Sans and Mono: package the fonts or rely on a system install? | W-3 |

Answered: the repo is [github.com/500Foods/Pavillion](https://github.com/500Foods/Pavillion) (old Q11). Browsing is in scope (J-7). Lua is in (R). The kit stays the only way to load Font Awesome and is the default icon library; other libraries are typed https URLs on a build-time allowlist, and icons are sanitized HTML snippets (F, 2026-10-16).

## Where the old plan went

The 823-line plan (v0.4, §1–§17) is absorbed. Its text is in git history before this commit.

| Old section | Now in |
| --- | --- |
| §1 Purpose, goals, non-goals | This index |
| §2 Architecture, libraries, frontend, bridge API, backend boundary | G Lock (layers, API seed), M Lock (threads, backend table), D Lock (frontend rules), A (package list), N, O, S, T (their libraries) |
| §3 Accounts and auth | K Lock, M Lock |
| §4 Data model | L Lock |
| §5 Sync engine | M Lock |
| §6 Mail features | N Lock; §6.6 Q; §6.7–§6.11 O; §6.13 N and J |
| §7 HTML mail safety | F Lock (pane, CSP) and N Lock (sanitizer) |
| §8 Contacts | S |
| §9 Calendar | T |
| §10 UI/UX | J Lock (shell layout), D-4, D-6, D-7, and D-5 (theme), K Lock (Accounts panel), E, F Lock (icons, kit) |
| §11 Security | F Lock (CSP, kit, HTML), K Lock (credentials), H Lock (redaction), L Lock (modes, runtime dir), V Lock (bot keys, socket) |
| §12 Packaging | W Lock |
| §13 Testing | W Lock and each phase's tests |
| §14 Roadmap P0–P6 | Milestones 2–6 in the sequence; acceptance criteria became exit gates (E-7, N-4, N-6, M-6, M-7, N-9, O-5, S-3, S-4, T-3, T-5) |
| §15 Open questions | The table above |
| §16 AI and command line | V |
| §17 Multiplatform | G Lock |

## Glossary

- **Workdir:** the directory Pavillion runs in (default `~/.local/share/pavillion`); holds the index, profiles, logs, the database, blobs, and cache.
- **Index / profile:** `Pavillion.json` names the active profile; a profile is one user configuration loaded for the life of the process (B).
- **Account / remote account (`ra`):** an account is a login in the profile, numbered `001`…; a remote account is one JMAP `accountId` under that login (shared mailboxes or calendars).
- **Command / event:** the JSON contract between the UI (or CLI) and the core, `noun.verb` and `noun.change`, listed in `docs/API.md` (G).
- **L1–L4:** platform, core, host adapter, UI (G).
- **Supervisor:** the per-account sync state machine on the core thread (M).
- **Op queue / held op:** local changes waiting to reach the server; a held op waits 5 s for undo (M).
- **Activity:** a registered unit of long work with progress and cancel (I).
- **Pane:** the sandboxed `srcdoc` iframe for outside HTML (F-4).
- **Kit:** the Font Awesome Pro Plus kit `45801522ef`, loaded only from Font Awesome's hosts (F).
- **`P`:** the Lua host table, modeled on Hydrogen's `H` (R).
- **Intent:** what a rule asks for (move, keyword), applied by the core through the op queue (R).
- **Fixture server / container:** the recorded JMAP server in `tests/support` and the disposable Stalwart in `tests/stalwart` (W).
- **IT:** integration tests, `PAVILLION_IT=1 make test-it`, only against the container or the dedicated test mailbox.

# G — LAYERS

**Status:** G-2 complete (2026-10-16). The index names the next phase. Lua and mail rules moved to [R-RULES.md](R-RULES.md) (old G-5 is R-1, old G-7 is R-3).
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

Pavillion runs on Fedora Linux first, then macOS and Windows, maybe iPad one day. Its UI can be lifted into Lithium. Both come down to clear layers with one written contract between each pair. This document owns the layers, the platform table, the command ABI, the host adapter, the mountable UI, and the multiplatform strategy.

## Phases

One phase is one session. Do not start the next one in the same conversation.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| G-1 | `pav_platform.h`, the Linux backend, a test fake; dirs and file modes behind it | Medium | Complete |
| G-2 | JSON command dispatcher, `pav_on_event`, and `docs/API.md` | Medium | Complete |
| G-3 | Generic Tauri `core` command; `api.js` takes a transport | Medium | Not started |
| G-8 | Move the remaining `lib.rs` logic (662 lines) into C behind the dispatcher | Medium | Not started |
| G-4 | Mountable UI: `mount(el, api)`, `--pav-*` variables, scoped styles, bare harness | Medium | Not started |
| G-6 | Second-platform smoke build of the core and `make test` | Hard | Not started |

G-5 and G-7 are now R-1 and R-3. The ids are not reused, so the `lib.rs` migration is G-8 and runs right after G-3.

Kind: **Easy** is one module and a test. **Medium** crosses one boundary. **Hard** is a new toolchain or platform.

## How a session works

1. Read Resume in [PAVILLION_PLAN.md](../PAVILLION_PLAN.md), the Status column above, and the previous phase's Status, Accomplished, Lessons learned, and Handoff.
2. Re-read only this phase: Goal, Entry gate, Work items, Done means, Exit gate. Open the lock section this phase cites.
3. Ask the qualifying questions. Wait for approval before editing source.
4. Do this phase only. Check a work item `[x]` only after its check has passed. Defer with `[~]`, one line, and the phase it moves to.
5. Before marking complete, write Status, Accomplished, Lessons learned, and Handoff, update the Status column here and in the index, and append the Working log. Then stop.
6. A sitting that ends early sets Status to **in progress**, names the last finished work item, and leaves the rest unchecked.

Leave Accomplished, Lessons learned, and Handoff off a phase that has not started.

## Layers

```
+------------------------------------------------------------+
| L4  UI (ui/)        vanilla JS, HTML, CSS. Host-agnostic.   |
|       talks only to the Pavillion API object (api.js)       |
+------------------------------------------------------------+
| L3  Host adapter    Tauri today (src-tauri). Lithium later. |
|       forwards commands and events; window, dialogs, opener |
+------------------------------------------------------------+
| L2  Core (core/)    portable C11: config, store, sync,      |
|       JMAP, MIME, sanitize, search, log, activity, Lua      |
+------------------------------------------------------------+
| L1  Platform (core/platform/<os>/)  one function table:     |
|       dirs, secrets, fs modes, time, notify hook,           |
|       single-instance, network-change, power, open          |
+------------------------------------------------------------+
```

## Lock

### L1 Platform

- The core includes `pav_platform.h` and nothing OS-specific. `#ifdef __linux__`, `<sys/stat.h>`, `libsecret`, and XDG names appear only under `core/platform/`.
- `core/platform/posix/` holds what Linux and macOS share (pthreads, `open`/`fchmod`, `clock_gettime`). `core/platform/linux/` holds libsecret and XDG dirs. `macos/`, `windows/`, and `ios/` come later (Keychain, Credential Manager, `%APPDATA%`, Win32 threads or a pthreads shim).
- The table, installed once before the core thread starts:

```c
typedef struct PavPlatform {
    const char *name;                                   /* "linux", "fake" */
    int  (*dir)(int which, char *out, size_t len);      /* PAV_DIR_DATA, CONFIG, CACHE, LOG, RUNTIME */
    int  (*mkdir_private)(const char *path);            /* 0700, or the platform equivalent */
    int  (*write_private)(const char *path, const void *buf, size_t n); /* 0600, atomic rename */
    int  (*secret_get)(const char *service, const char *user, char **out);
    int  (*secret_set)(const char *service, const char *user, const char *label, const char *val);
    int  (*secret_delete)(const char *service, const char *user);
    int64_t (*now_ms)(void);                            /* UTC unix ms */
    void (*on_network_change)(void (*cb)(void *), void *user);
    void (*on_resume)(void (*cb)(void *), void *user);
} PavPlatform;
void pav_platform_install(const PavPlatform *p);
const PavPlatform *pav_platform(void);
```

- Paths are joined only through `pav_path_join(out, len, a, b)`. Code never contains `/` literals for joining or `~` expansion.
- The B-4 file-mode code (0600, 0700) moves behind `mkdir_private` and `write_private`. On Windows it becomes an ACL or a no-op.
- Tests install `pav_platform_fake()`: an in-memory secret store, a temp root for every dir, a settable clock. Tests never touch the real keyring.
- Notifications, tray, autostart, deep links, and single-instance are host work (L3, Tauri plugins), not L1. L1 only exposes the hooks the core needs.

### L2 Core and its ABI

- The core's public C surface is `core/include/pavillion.h`. Everything else the UI can do is a **command**. There is one free function, `pavillion_free(void *)`, for every buffer the core hands out (`result_json` and `PavBytes.body`). The name `pav_bytes_free` is retired.

`PAVILLION_API_VERSION` (from F-1) bumps when the command contract in `docs/API.md` or this C surface changes incompatibly. The rule is in the F Lock (FFI). The shell refuses to start on a mismatch.

| Surface | Functions |
| --- | --- |
| Today (F-1) | `pavillion_start`, `pavillion_stop`, `pavillion_status` (`PavillionStatus`), `pavillion_prefs_get/apply` (`PavShellPrefs`), `pavillion_icons_get/apply`, `pavillion_kit_get/apply`, `pav_ui_log`, `pavillion_api_version` |
| After F-2 | `pavillion_start`, `pavillion_stop`, `pavillion_status` (`PavillionStatus`), `pavillion_prefs_get/apply` (`PavShellPrefs`, window and chrome only), `pavillion_ui_get/set`, `pavillion_icons_get/apply`, `pavillion_kit_get/apply`, `pav_ui_log`, `pavillion_api_version` (2) |
| After G-3 | `pavillion_start`, `pavillion_stop`, `pavillion_call`, `pavillion_free`, `pav_on_event`, `pavillion_api_version`, and the two structs until G-8 (the per-command entry points become commands in G-2, and their callers are gone after G-3) |
| After G-8 (target) | `pavillion_start`, `pavillion_stop`, `pavillion_call`, `pavillion_free`, `pav_on_event`, `pav_protocol`, `pav_cli_main`, `pavillion_api_version`. No struct except `PavBytes`. |

```c
/* Runs one command. args_json and the result are UTF-8 JSON. Blocks until the
   core thread replies. *result_json is {"ok":true,"data":...} or
   {"ok":false,"error":{"code":"...","message":"...","retryable":false}}.
   Free with pavillion_free. Returns 0 when a reply was produced. */
int  pavillion_call(const char *cmd, const char *args_json, char **result_json);
void pavillion_free(void *p);

/* One callback for every event. name is "sync.state", "log.lines", ...;
   json is the payload. Called on a core-owned thread; the host only queues. */
typedef void (*pav_event_cb)(const char *name, const char *json, void *user);
void pav_on_event(pav_event_cb cb, void *user);

/* URI scheme handlers (pav-cid, pav-blob, pav-link). The host forwards every
   request unparsed; C decides everything and fills *out. */
typedef struct {
  int         status;        /* 200, 204, 206, 403, 404, ... */
  char       *headers_json;  /* {"Content-Type":"...","Content-Length":"...",...} */
  void       *body;          /* may be NULL (204) */
  size_t      body_len;
} PavBytes;
/* req_json: {"scheme":"pav-blob","url":"pav-blob://...","method":"GET","range":"bytes=0-"}
   Returns 0 when *out was filled (even for 4xx). Free headers_json and body
   with pavillion_free. */
int pav_protocol(const char *req_json, PavBytes *out);
```

- Scheme-handler contract. Rust only forwards the request and copies the status, headers, and body into the Tauri response, so it never parses the URL. Ownership checks, MIME type, `Range`, and `Cache-Control: no-store` are all decided in C, and an unknown scheme or URL returns 404.
  - `pav-cid://<emailRowid>/<cid>`: the inline part, only if that cid belongs to that email (N-4).
  - `pav-blob://<raId>/<blobId>`: a stored blob, such as a contact photo or an attachment preview (N-7, S-2).
  - `pav-link://<paneId>/<n>`: emits `link.clicked {paneId, n}` and returns 204 (F-4).

- Command names are `noun.verb` in lowercase dots (`mailbox.list`, `message.get`). Event names are `noun.change` (`mail.messages_changed`). Wire field names are camelCase. C structs stay snake_case. The §2.3 names in the old plan map one to one: `mailbox_list` becomes `mailbox.list`, `mail://messages-changed` becomes `mail.messages_changed`.
- `ApiError` codes are a closed list in `docs/API.md`: `bad_args`, `not_found`, `not_ready`, `auth`, `network`, `server`, `conflict`, `forbidden`, `too_large`, `cancelled`, `internal`. `retryable` is true only for `network` and some `server`.
- `docs/API.md` is the contract the UI and every host share. A command or event is not real until it is listed there with its args, result, errors, and the phase that added it. A Unity test (`test_api_md_lists_every_command`) compares the dispatcher's table to the headings in that file.
- Commands run on the core thread unless the table marks them `worker` (long work goes to a worker and replies through the activity registry, [I-ACTIVITY.md](I-ACTIVITY.md)). Bulk data (message bodies, attachments) never crosses as JSON larger than 2 MB; it goes through a file path or the `pav-cid` and `pav-blob` schemes.
- The core has no Tauri or Rust types, no WebView assumptions, and no UI strings beyond error text.

### L3 Host adapter (Rust is minimal)

- Rust and Tauri do only what needs Tauri, because Pavillion will later be built into Hydrogen, which is C. Any logic written in Rust would have to be rewritten for Hydrogen. Rust owns:
  - the window and the webview
  - native dialogs and the opener
  - the deep-link, single-instance, and notification plugins (just the plugin calls)
  - one generic command, `core(cmd, args)`, which forwards to `pavillion_call` on `spawn_blocking`
  - one listener, which forwards `pav_on_event` to `app.emit("pav", {name, payload})`
- Everything else is C:
  - command handling
  - preferences, defaults, clamps, and validation
  - profile logic
  - working-directory creation
  - window-state persistence: when to save, the debounce, clamping to the monitors the host reports, and the restored rectangle
  - every decision a URI scheme makes
- Window state: the host sends raw facts to the core with `window.report {x, y, w, h, maximized, monitors}` on move, resize, and maximize. Before showing the window it asks `window.initial`, then applies the rectangle the core returns. Rust holds no saved-state struct and no timer.
- URI schemes (`pav-cid`, `pav-blob`, `pav-link`): Rust forwards each request to `pav_protocol` under the L2 scheme-handler contract and never parses the URL, checks ownership, or picks a MIME type.
- The CLI socket listener and the CLI client are both C ([V-CLI.md](V-CLI.md)). From V-1 the `pavillion` ELF is that C client and execs `pavillion-gui` only when there is no subcommand. `main.rs` does not grow a subcommand parser. G-8 may call a `pav_cli_main` stub from `main.rs` until V-1 moves the entry point. WebKit bytes are never inside either ELF. Only `pavillion-gui` dynamically needs system `libwebkit2gtk`. The CLI ELF does not link it.
- Review rule: a Rust change that is not a Tauri API call, a plugin registration, or plain forwarding needs a reason recorded in this Lock. Target after G-8: `lib.rs` under 200 lines, with no `serde` structs other than the forwarded `Value`.
- Per-command Rust handlers (`status`, `prefs`, `save_layout`, `save_icons`, `save_kit`, `ui_log`) are deleted in G-3. G-8 moves the rest of today's `lib.rs` logic (window state, prefs struct conversion, workdir) into C. The capability file then lists `allow-core` and the plugin permissions only.
- Lithium (and later Hydrogen) is another host. It loads the same `ui/` and supplies its own transport. Hydrogen links the C core directly and needs nothing from `src-tauri`. Nothing in `ui/` may import `@tauri-apps/*` except `ui/src/transport/tauri.js`.

### L4 UI

- `ui/src/api.js` is `createApi(transport)`. A transport is `{ call(cmd, args) -> Promise, on(handler) -> unsubscribe }`. Screens call `api.mailbox.list(...)` style methods and `api.on("mail.messages_changed", fn)`; they never see the transport.
- The UI is one ES module root with no global singletons. `mount(el, api, { hostServices })` renders into `el` and leaves `document` and `window` alone outside it, except keyboard shortcuts registered through the mount's own listener. `hostServices` is the optional window chrome (drag region, window controls) that only the Tauri host provides.
- CSS custom properties are namespaced `--pav-*`, and every rule is scoped under `.pav-root`. Theme files assign `--pav-*` names only.
- Icon libraries (F-3) are loaded by the host page, not the UI module; the UI draws icons only through the registry (F-7).
- Font Awesome loading belongs to the host page, not the UI module. Lithium may already load its own kit; the UI only expects `fa-*` classes to resolve. See the [F-HARDEN.md](F-HARDEN.md) Lock.

### Multiplatform (was plan §17)

- v1 ships on Fedora Linux only (rpm and a dnf repo). Windows, macOS, and iPad are deferred, not deleted. Code written today must not box them out.
- Same everywhere: the UI bundle, the minimal Rust shell (Tauri hosts only), the C core and its libraries (Jansson, libcurl, SQLite with FTS5, OpenSSL, libuuid, GMime, libical, Lua), the SQLite schema and wire format (UTC unix ms, JMAP JSON), and the concurrency model.
- Per platform (the edge):

| Concern | Linux (today) | Windows | macOS | iPad (later) |
| --- | --- | --- | --- | --- |
| WebView | WebKitGTK 4.1 (system) | WebView2 | WKWebView | WKWebView (Tauri mobile) |
| Secrets | libsecret / Secret Service | Credential Manager | Keychain | Keychain |
| Notifications | Tauri notification plugin | toast | Notification Center | APNs, different model |
| Data dir | `$XDG_DATA_HOME/pavillion` | `%APPDATA%\Pavillion` | `~/Library/Application Support/Pavillion` | app container |
| Autostart | XDG autostart | Run key | launch agent | n/a |
| mailto | deep-link plugin + xdg-mime | protocol registration | `CFBundleURLTypes` | custom scheme |
| Single instance | plugin (D-Bus/socket) | named mutex | plugin | n/a |
| Packaging | rpm + dnf repo, Arch PKGBUILD | MSI | notarized .app | App Store |

- The core must not: call X11, Wayland, or libayatana; use a `dlopen` database layer; take on libmicrohttpd, libwebsockets, MagickWand, or Brotli; assume `/` separators or `~`.
- Windows and macOS get no updater; manual downloads until a separate decision. The app never vendors a browser engine (that is also why AppImage is rejected, [W-SHIP.md](W-SHIP.md)).
- iPad is the furthest out: the split-pane UI needs a touch layout and iOS background sync is a different domain from EventSource. Revisit after the initial working copy if there is demand.

### Seed for `docs/API.md` (was plan §2.3)

G-2 writes `docs/API.md` from this list. The planned header line is `API version: <PAVILLION_API_VERSION>`, bumped on any incompatible change to a command, an event, or the C surface. Each later phase moves its rows from "planned" to "real". Names here are the new dotted form.

- System: `system.status`, `system.version`, `prefs.get`, `prefs.set`, `prefs.ui.get`, `prefs.ui.set`, `log.tail`, `log.client` (was `ui_log`), `activity.list`, `activity.cancel`, `sync.now {accountId?}`, `link.open {url}` (after the UI confirm).
- Accounts: `account.list`, `account.add {name, kind, sessionUrl, username, auth, secret}`, `account.update {id, patch}`, `account.remove {id, purgeLocal}`, `account.test {sessionUrl, username, auth, secret} -> {ok, capabilities[], error?}`, `account.set_secret {id, secret}`, `identity.list {accountId?}`.
- Mail: `mailbox.list`, `mailbox.create`, `mailbox.rename`, `mailbox.delete`, `mailbox.move`, `mailbox.subscribe`, `message.list {view:{kind:"mailbox",id}|{kind:"unified",role}|{kind:"search",query}, offset, limit, threaded}`, `thread.get`, `message.get {id, allowRemote}`, `message.set_flags {ids, seen?, flagged?, answered?, keywords?:{add,remove}}`, `message.move`, `message.archive`, `message.delete {ids, permanent}`, `message.raw`, `attachment.save`, `attachment.open`, `search.run {query, scope, server, limit}`, `sender.allow_remote`, `retention.get`, `retention.set`, `undo.run {opId}`.
- Compose: `draft.new {mode, sourceId?, identityId?}`, `draft.save`, `draft.attach {draftId, path, inline}`, `draft.send {draftId, sendAt?}`, `draft.discard`, `signature.list`, `signature.set`, `outbox.list`, `outbox.retry`.
- Contacts: `addressbook.list`, `contact.list`, `contact.get`, `contact.save`, `contact.delete`, `contact.autocomplete {prefix, limit}`, `contact.import`, `contact.export {format:"vcard"|"jscontact"}`.
- Calendar: `calendar.list`, `calendar.set`, `event.range {calendarIds, start, end, tz}`, `event.get`, `event.save {event, scope:"this"|"following"|"all", occurrenceStart?}`, `event.delete`, `event.import`, `event.export`, `reminder.snooze`.
- Files: `files.list`, `files.get`, `files.download`, `files.upload`, `files.mkdir`, `files.move`, `files.delete`.
- Rules and scripts: `rule.list`, `rule.save`, `rule.delete`, `rule.order`, `rule.run {mailboxId, dryRun}`, `rule.compile_preview`, `rule.export_sieve`, `script.list`, `script.save`, `script.run`.
- Events: `core.state`, `log.lines`, `activity.changed`, `sync.state {accountId, state:"idle"|"syncing"|"offline"|"error", detail?}`, `mail.mailboxes_changed`, `mail.messages_changed {accountId, created, updated, destroyed}`, `mail.new_mail {accountId, mailboxId, summaries}`, `outbox.update {outboxId, state:"queued"|"sending"|"sent"|"failed", error?}`, `queue.conflict {opId, kind, detail}`, `contacts.changed`, `calendar.changed`, `calendar.reminder {eventId, occurrenceStart, title, location?}`, `files.changed`, `rule.disabled {ruleId, reason}`, `app.mailto {url}`, `link.clicked`.

## G-1: Platform layer and a test fake

**Kind:** Medium
**Status:** Complete (2026-10-16). Variance: the `macos-core` job is in the workflow and has not been observed on a runner. This sitting did not push.

**Goal:** Every OS call in the core goes through `pav_platform()`. Tests run on a fake platform.

**Entry gate:** F-2 complete. G-1 does not need the icon sanitizer. It needs the config split and the UI blob so profile writes go through the helpers and, from this phase, through `write_private`.

**Work items:**

- [x] Add `core/include/pav_platform.h` with the Lock's table, `PAV_DIR_*`, and `pav_path_join`.
- [x] `core/platform/posix/posix.c` (threads helpers, `mkdir_private`, `write_private` with temp file, `fchmod`, `fsync`, `rename`), `core/platform/linux/linux.c` (XDG dirs; secrets stubbed with `-1 "not yet"` until K-1), `core/platform/fake/fake.c`.
- [x] Move every `mkdir`, `open(...0600)`, `fchmod`, `stat`, and `getenv("HOME")` in `config.c`, `config_fs.c`, `store.c`, and `pavillion.c` behind the table. `rg 'sys/stat|fchmod|getenv' core/src` returns nothing.
- [x] `pavillion_start` installs the Linux table unless a test installed one first. CMake builds `posix` and `linux` on Linux, and the fake into the test binary only.
- [x] Profile writes become atomic (`write_private` writes `name.tmp` then renames), so a crash mid-write cannot leave a half profile. Unity test `test_profile_write_is_atomic`.
- [x] Add the macOS core-only CI job to `.github/workflows/ci.yml` (W-1 adds the file): `cmake` + Unity on `macos-latest` with the fake platform. It may be `continue-on-error: true` until G-6.

**Done means:** The core contains no OS-specific call outside `core/platform/`, and every Unity test runs on the fake.

**Exit gate:** `make test` exits 0. The `rg` check is empty. `make dev` on a fresh `DEV_DIR` creates the same files with the same modes as before. The macOS job runs.

**Accomplished:** `pav_platform.h` holds the Lock's table. `posix.c` does `mkdir_private` (parents included, new directories `0700`, an existing directory keeps its mode), `write_private` (`path.tmp`, `fchmod 0600`, `fsync`, `rename`), the clock, directory listing, and `pav_thread_create` / `pav_thread_join`. `linux.c` resolves XDG dirs and appends `pavillion`. `PAV_DIR_LOG` is `XDG_STATE_HOME` (default `$HOME/.local/state`). `PAV_DIR_RUNTIME` fails when `XDG_RUNTIME_DIR` is unset. Secret calls return `-1` until K-1. The fake keeps secrets in memory, one temp root per dir kind, and a settable clock. `pav_platform_install` keeps the first table. `pavillion_start` calls `pav_platform_install_host`, which installs Linux only when a test has not installed the fake. On Linux the fake is linked into the test binary only. A non-Linux core links the fake as its host, because there is no macOS backend until G-6. `config.c`, `config_fs.c`, and `store.c` no longer include `sys/stat.h`. `rg 'sys/stat|fchmod|getenv' core/src` is empty. Profile stores go through `write_private`. `make test` is 37 tests, 0 failures, including `test_profile_write_is_atomic` and `test_platform_fake_dirs_secrets_and_clock`. Every test's `setUp` requires the fake. A fresh `DEV_DIR` (`/tmp/pav-g1-aNIqBT`) from `make dev` created `Pavillion.json`, `profiles/default.json`, `logs/default.log`, and `default.sqlite` at `0600`, and the workdir, `profiles`, `logs`, `blobs`, `blobs/default`, `cache`, and `cache/default` at `0700`. The real profile `~/.local/share/pavillion/profiles/default.json` was not rewritten. Its sha256 stayed `d551dd41c45a987e74c4af687c74f513eb68425bbd5ebedaeffe47103f339e04`. `PAVILLION_API_VERSION` stays 2. `.github/workflows/ci.yml` has job `macos-core` (`macos-latest`, `continue-on-error: true`, `make test` with the Homebrew include path). It has not been executed on a runner.

**Lessons learned:** The first install wins, so the test installs the fake in `main` before `pavillion_start`. `write_private` replaces. `pav_config_ensure` still refuses to overwrite a profile that exists when the index does not, which is what `O_EXCL` did. An existing log is chmod'd to `0600` and not truncated; a missing log is an empty `write_private`. `json_dumps` has no trailing newline, so `pav_profile_store` adds one before the write. `tearDown` clears the fake clock and the abort flag, or the next test would keep them. `pthread.h` stays in `pavillion.c`. The C-3 mutexes are static initializers, and a byte-array stand-in would be the wrong size on macOS. The thread helpers are for new code. `fopen` stays; it is ISO C. Helpers that are not table slots (`pav_exists`, `pav_is_dir`, `pav_file_private`, `pav_dir_each`, `pav_path_parent`, `pav_path_is_absolute`, `pav_now_ms`, `pav_format_utc`) exist so `core/src` does not include `sys/stat.h` or walk `/` itself. The Lock table was not changed. Killing `make dev` after the files exist makes `make` report Terminated. The files were already written.

**Handoff:** G-2 is next. Do not bump `PAVILLION_API_VERSION`. Tests install the fake before Unity and `setUp` rejects any other name. `pavillion_start` must keep the host install, or a production launch has no platform and profile writes fail. Secrets stay `-1` until K-1. `dir()` is unused by the shell; G-8 moves workdir creation. The core queue still uses `pthread` directly. `macos-core` stays `continue-on-error` until G-6. G-Q1 and G-Q4 stay open; this phase did not need them.

## G-2: JSON command dispatcher and `docs/API.md`

**Kind:** Medium
**Status:** Complete (2026-10-16). No variances. This sitting did not push.

**Goal:** One generic entry point, `pavillion_call`, and one event callback, `pav_on_event`, carry everything after this phase.

**Entry gate:** G-1 complete.

**Work items:**

- [x] `core/src/dispatch.c`: a sorted table of `{ name, handler, flags }`, `handler(json_t *args, json_t **data, PavError *err)`. Unknown command is `bad_args` with the name. Args that are not an object are `bad_args`.
- [x] `pavillion_call` posts a `CORE_CMD_CALL` to the core queue and waits on that command's reply (the C-3 pattern). Grow `CORE_QUEUE_CAP` from 8 to 256 and make a full queue an `internal` error, not a hang.
- [x] `pav_on_event`: an event queue drained by a small event thread that calls the callback, so a slow host never blocks the core thread. Events posted with no callback are dropped and counted.
- [x] Port the existing surface: `system.status`, `prefs.get`, `prefs.set` (window and core-read preferences), `prefs.ui.get`, `prefs.ui.set`, `log.client`. The old C entry points stay until G-3 removes their callers.
- [x] Write `docs/API.md`. Its header states the current `PAVILLION_API_VERSION` and the bump rule (F Lock). Then: conventions, error codes, every real command, every planned one from the Lock's seed marked `planned (phase)`.
- [x] Unity tests: `test_call_unknown_command`, `test_call_bad_args`, `test_call_status_roundtrip`, `test_event_delivered_off_core_thread`, `test_api_md_lists_every_command`.

**Done means:** Every current UI need is reachable through `pavillion_call`, and `docs/API.md` lists it.

**Exit gate:** `make test` exits 0 with the five new tests. `docs/API.md` exists.

**Accomplished:** `core/src/dispatch.c` holds a sorted table of six commands. `pavillion_call` parses a JSON object, posts `CORE_CMD_CALL`, and waits for that call's reply. The queue cap is 256. A full queue returns `{"ok":false,"error":{"code":"internal",...}}` immediately. `pav_on_event` is served by an event thread started from `pavillion_start` and joined from `pavillion_stop` before the log thread. A post with no callback, or before that thread is running, increments `pav_event_dropped` and is not queued. The real commands are `system.status`, `prefs.get`, `prefs.set`, `prefs.ui.get`, `prefs.ui.set`, and `log.client`. `prefs.set` writes the window and the core-read preference strings (`timezone`, `weekStart`, `timeFormat`, `theme`, `density`). `prefs.ui.set` replaces the blob. `log.client` checks the subsystem pattern, the level name, and a 2000-byte cap, and replaces newlines with U+23CE. The old C entry points are unchanged. `docs/API.md` states API version 2, the bump rule, the error codes, the six real commands, and every seed command and event as `planned (phase)`. `make test` is 43 tests, 0 failures: the five named above plus `test_call_prefs_ui_and_log`. `PAVILLION_API_VERSION` stays 2. `pav_thread_create` sets a 512 KB stack, which is what the event thread uses.

**Lessons learned:** A handler that runs on the core thread must not call `pavillion_prefs_apply` or `pavillion_ui_set`. Those wait for a queue reply, and the core thread would wait for itself. The handlers write the profile directly. `pavillion_call` returns 0 whenever it produced a reply, including `bad_args` and `not_ready`. It returns -1 only when there is no buffer to put a reply in. `prefs.ui.set` replaces the object; it does not merge. `log.client` already sanitizes, so H-1 does not change that command's meaning. `pav_log` still copies the formatted line into a 1024-byte buffer. The event callback must not call `pavillion_stop`: stop joins the event thread. `strdup` in `dispatch.c` needs `_POSIX_C_SOURCE` before the includes. `test_api_md_lists_every_command` reads `docs/API.md` from the repo root and treats every `###` heading under `## Commands` as a command. A `State: real` heading that is not in the sorted table fails the test. The queue-full branch returns `internal` without waiting; the tests do not fill all 256 slots. G-Q1 and G-Q4 stayed open.

**Handoff:** G-3 is next. Point the shell at `pavillion_call` and delete the per-command Tauri handlers. Do not bump `PAVILLION_API_VERSION`. The old C functions stay until their callers are gone. G-8's removal bump is 3. A new command is a row in the sorted table and a `State: real` heading, in the same sitting. The event thread starts inside `pavillion_start` and stops inside `pavillion_stop` before the log thread. Stop clears the callback before it drains, so a late event is not delivered. The callback only queues. Secrets stay `-1` until K-1. `macos-core` stays `continue-on-error` until G-6. Tests install the fake before Unity.

## G-3: Generic host command and a transport-agnostic `api.js`

**Kind:** Medium
**Status:** Not started.

**Goal:** The Tauri shell has one `core` command and one event forwarder. The UI never imports Tauri outside its transport.

**Entry gate:** G-2 complete.

**Work items:**

- [ ] `src-tauri/src/lib.rs`: `#[tauri::command] async fn core(cmd: String, args: serde_json::Value) -> Result<Value, ApiError>` over `spawn_blocking` and `pavillion_call`. Register `pav_on_event` once; the callback pushes into a channel and a task emits `pav`.
- [ ] Delete `status`, `prefs`, `save_layout`, `save_icons`, `save_kit`, `ui_log` and their permissions. Capability: `allow-core` plus plugin permissions.
- [ ] `ui/src/transport/tauri.js` (the only `@tauri-apps` import) and `ui/src/transport/mock.js` (in-memory, for Vitest and the G-4 harness).
- [ ] Add Vitest (`ui/package.json`) and make `make test` run `npm --prefix ui test`. This is the first phase that does; F-3 and later add tests, they do not add the runner again.
- [ ] `ui/src/api.js` becomes `createApi(transport)` with namespaced methods for the real commands. `main.js` builds it with the Tauri transport.
- [ ] Vitest: `api.test.js` runs the API over the mock transport; `no-tauri-imports.test.js` asserts only `transport/tauri.js` imports `@tauri-apps`.

**Done means:** The window behaves as before, through one command.

**Exit gate:** `make test` exits 0. `make dev` restores layout and saves it. `make build` shows the icons. `rg '@tauri-apps' ui/src` lists one file.

## G-8: Move `lib.rs` logic into C

**Kind:** Medium
**Status:** Not started.

**Goal:** `src-tauri` holds only Tauri work, per the L3 Lock. Window state, the prefs conversion, the workdir, and the scheme decisions are C behind the dispatcher. No C struct crosses the FFI.

**Entry gate:** G-3 complete.

**Work items:**

- [ ] Inventory `src-tauri/src/lib.rs` (447 lines after F-2; 662 was the count when this phase was written). The inventory goes in Accomplished, listing each function with "stays (Tauri)" or "moves to C".
- [ ] Commands `window.initial` and `window.report` in `core/src/window_state.c`. These replace `read_prefs`, `write_prefs`, `seed_restored`, `note_window`, `schedule_window_save`, `save_window_now`, and the clamp in `apply_saved_window`. The 500 ms debounce, the monitor clamp, the maximized flag, and the write are all core-side, with a timer on the core thread.
- [ ] Rust keeps three things: reading the logical rectangle and monitors from Tauri, sending `window.report`, and applying the `window.initial` reply before `show()`.
- [ ] `ensure_workdir` moves into `pavillion_start` (mode `0700`, through G-1's platform layer). `start_core` reduces to one call.
- [ ] `pav_protocol` (L2 Lock contract) in `core/src/protocol.c`, freed with `pavillion_free`, with a table of handlers per scheme. Its first entry is a stub `pav-blob` that returns 404. N-4 and N-7 fill it in, and F-4 adds `pav-link`. Rust gets one generic scheme handler that registers each scheme against `pav_protocol`.
- [ ] Delete `PavShellPrefs` and `PavillionStatus` from the FFI. Status becomes the `system.status` command. This removes functions, so `PAVILLION_API_VERSION` goes to 3. F-2 already used 2 when `PavShellPrefs` shrank to the window.
- [ ] `pav_cli_main` stub in C (prints `pavillion: no subcommands yet` for an unknown subcommand). `main.rs` calls it and keeps no flag logic of its own beyond what A-6 needs before the core.
- [ ] Unity: `test_window_initial_default_1200x800`, `test_window_report_debounced_write`, `test_window_clamped_to_monitor`, `test_protocol_unknown_scheme_404`, `test_start_creates_workdir_0700`.

**Done means:** `lib.rs` is under 200 lines. Every line in it is a Tauri call, a plugin registration, or forwarding, and window behavior is unchanged.

**Exit gate:** `make test` exits 0 with the five tests. In `make dev`: move and resize the window, maximize it, quit, and relaunch. The window comes back the same with no 1200×800 flash. `wc -l src-tauri/src/lib.rs` is under 200. `rg 'serde::Deserialize|struct Pav' src-tauri/src` finds nothing outside the forwarded `Value`.

## G-4: Mountable UI for Lithium

**Kind:** Medium
**Status:** Not started.

**Goal:** The UI mounts into any element with any transport. A bare HTML page mounts it with the mock transport and no Tauri.

**Entry gate:** G-8 complete.

**Work items:**

- [ ] `ui/src/index.js` exports `mount(el, api, opts)` and `unmount()`. `main.js` is the Tauri host page: it loads the kit, builds the API, and calls `mount(document.querySelector("#app"), api, { hostServices })`.
- [ ] Move the static shell markup from `ui/index.html` into `ui/src/shell/template.js` built with DOM calls (no `innerHTML` of outside data; static markup may use a `// html-ok` template).
- [ ] Rename every custom property to `--pav-*` and scope every rule under `.pav-root`. `dark.css` assigns only `--pav-*`. Keep the `--pav-log-*` properties H-2 added. The rename covers the older names. It does not drop the log colors.
- [ ] Global listeners (`keydown`, `resize`, `pointerup`) attach to the mount root or are removed on `unmount`.
- [ ] `ui/harness.html` (not in the release bundle): mounts with the mock transport into a `div` inside a page that has its own conflicting `--bg` and `button` styles.
- [ ] Vitest with jsdom: `mount.test.js` mounts, unmounts, and asserts no listeners or nodes remain outside `el`.

**Done means:** The harness renders the shell next to foreign CSS with no bleed in either direction.

**Exit gate:** `make test` exits 0. `npx vite` serving `harness.html` shows the shell inside the box. `make dev` and `make build` look unchanged.

## G-6: Second-platform smoke build

**Kind:** Hard
**Status:** Not started.

**Goal:** The core and `make test` build and pass on macOS (or Windows). No packaging, no window.

**Entry gate:** The initial working copy is complete (index milestone 4) and a second machine or a CI runner is available.

**Work items:**

- [ ] `core/platform/macos/` with Keychain secrets (`SecItem*`) and Application Support dirs, or `windows/` per the toolchain answer (open question G-Q4).
- [ ] Homebrew (or vcpkg) dependency list in [W-SHIP.md](W-SHIP.md).
- [ ] The CI macOS job loses `continue-on-error`.

**Done means:** `make test` passes on the second platform.

**Exit gate:** A green CI run on that platform, linked from Accomplished.

## Open questions for Andrew

- G-Q1. Lithium host: does Lithium run the C core itself (native module, WASM, or a Hydrogen-like process), or does Pavillion's UI talk to a running Pavillion over a socket or HTTP? This decides what `lithiumTransport` is. G-4 does not depend on the answer.
- G-Q4. Windows: MSVC or MinGW for the core? It affects the pthreads choice in L1.

## Working log

- 2026-10-15 Opened from a design conversation: platform split, Lithium embedding, Lua. Not a phase.
- 2026-10-16 Scoped for implementation: the platform table, `pavillion_call`/`pav_on_event` signatures, naming and error codes, the API seed from the old §2.3, plan §17 absorbed. Lua and rules moved to R-RULES (G-5 to R-1, G-7 to R-3). Lua open questions moved there.
- 2026-10-16 Andrew: minimize Rust ahead of the Hydrogen (C) integration. Rewrote the L3 Lock so Rust does only Tauri work, moved window-state persistence and scheme decisions to C (`window.report`/`window.initial`, `pav_protocol`), and added G-8 to migrate today's `lib.rs` logic right after G-3.
- 2026-10-16 Review fixes: the `pav_protocol` signature, `PavBytes`, and the scheme-handler contract are now in the L2 Lock. One free function, `pavillion_free` (`pav_bytes_free` retired). Added a table of the FFI surface by stage. `system.status` and `log.lines` names aligned.
- 2026-10-16 G-1: platform table, Linux XDG dirs, and a test fake. Profile writes are `path.tmp` then rename. `make test` is 37 tests. `rg 'sys/stat|fchmod|getenv' core/src` is empty. A fresh `make dev` workdir has the index, profile, log, and sqlite at `0600` and the directories at `0700`. API version stays 2. `macos-core` is in `ci.yml` with `continue-on-error` and was not pushed.
- 2026-10-16 G-2: `pavillion_call`, `pav_on_event`, and `docs/API.md`. Six real commands. Queue cap 256. `make test` is 43 tests. API version stays 2. Not pushed.

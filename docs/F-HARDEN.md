# F — HARDEN

**Status:** F-1, F-2, F-4, and F-5 complete (2026-10-16). F-6 is superseded. The index names the next phase.
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

Structural fixes found after D-3. They go in before mail is real, because each gets more expensive once JMAP, accounts, and credentials sit on top of it. This document owns the main-window security rules: the CSP, the Font Awesome kit, and the rule that outside HTML never enters the main DOM.

## Phases

One phase is one session. Do not start the next one in the same conversation.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| F-1 | API version check (`pavillion_api_version`) | Medium | Complete |
| F-5 | Trim `config.c` with a helper layer | Medium | Complete |
| F-2 | UI preferences as an opaque JSON blob | Medium | Complete |
| F-4 | Message pane isolation, proven before mail | Medium | Complete |
| F-3 | Icon library sources: typed https URLs on an allowlist, presets | Medium | Not started |
| F-7 | Shared icon HTML sanitizer, the icon registry, overrides in the UI blob | Medium | Not started |
| F-8 | Icon settings: the icon tree and the three-preview editor | Medium | Not started |
| F-6 | Decide where JMAP and sync live | Easy (decision) | Superseded (2026-10-16) |

The table is in sequence order, not number order. F-5 runs before F-2 so the blob work lands on the new helpers. F-4 is index step 33. It follows G-8 and does not wait on the icon phases. F-3, F-7, and F-8 are milestone 6: no phase before F-3 adds a settings field that accepts icon HTML or a new script URL. The preview's existing kit field stays as it is until F-3 migrates it. Exception, Andrew on 2026-10-17: D-7 reads `--pav-icon-*` from a theme file before F-7 exists. A value may be a class string, one `<i class='...'>`, or an image filename. It does not add a script URL. F-7 still adds the shared sanitizer. F-8 still replaces this painting with the icon editor.

Kind: **Easy** is one module and a test, or a static page. **Medium** crosses one boundary (the window, the filesystem, a thread, the FFI). **Hard** joins two build systems or two processes for the first time.

## How a session works

1. Read Resume in [PAVILLION_PLAN.md](../PAVILLION_PLAN.md), the Status column above, and the previous phase's Status, Accomplished, Lessons learned, and Handoff.
2. Re-read only this phase: Goal, Entry gate, Work items, Done means, Exit gate. Open the lock section this phase cites.
3. Ask the qualifying questions. Wait for approval before editing source.
4. Do this phase only. Check a work item `[x]` only after its check has passed. Defer with `[~]`, one line, and the phase it moves to.
5. Before marking complete, write Status, Accomplished, Lessons learned, and Handoff, update the Status column here and in the index, and append the Working log. Before that, update `ui/src/help/pages.js` for what this phase changed: augment, replace, fix, or extend the user page, or add a Developer section when the user page has nothing new. Then stop.
6. A sitting that ends early sets Status to **in progress**, names the last finished work item, and leaves the rest unchecked.

Leave Accomplished, Lessons learned, and Handoff off a phase that has not started. `make test` passes at the end of every phase. A UI exit gate is the window, and for anything touching the CSP it is the `make build` window, not `make dev`.

## Lock

### Font Awesome stays a kit

- Pinned kit version: 7.3.1 (Pro Plus, SVG/JS)
- Font Awesome Pro is loaded **only** through the official kit (`45801522ef`, Pro Plus 7.3.1, SVG/JS method, `https://kit.fontawesome.com/45801522ef.js`). Pavillion is open source, and the kit is the only distribution the license allows. Do not copy Pro SVGs, CSS, or font files into the tree, and do not vendor the kit loader. Do not "fix" a CSP problem by self-hosting. Copying SVG icons in by hand is also a license violation.
- Families in use: Duotone, Sharp, and Thin. Mail keeps the roof mark (`ui/public/mark.png`).
- The kit loader fetches `pro-plus.min.js` from `ka-p.fontawesome.com` with `fetch()` and runs it as an **inline** `<script>`. That is why `script-src` carries `'unsafe-inline'` in the release build. `make dev` never shows the problem, because the Vite page is not served with the Tauri CSP.
- `'unsafe-inline'` is effective only because `dangerousDisableAssetCspModification` is `true`. If Tauri adds a hash or a nonce to `script-src`, browsers ignore `'unsafe-inline'` and the icons vanish in `make build` only. Keep the flag, and keep a comment beside it in the docs (JSON has no comments).
- `font-src 'self' data: https://ka-p.fontawesome.com pav-theme:` covers the kit's v4 and v5 font-face shims and a theme file's local font. `kit-uploads.fontawesome.com` is not allowed. `blob:` stays out.
- The kit's domain list stays open. `make dev` is `http://localhost:1420` and the packaged window is `http://tauri.localhost`, which a domain-limited kit rejects and will not let you add. Loads count as pageviews on that subscription. Revisit if the app is widely used or the embed is misused.
- **Pin the kit version.** In the Font Awesome kit settings, set the version to `7.3.1` rather than "latest 7.x", so icon names do not shift between builds. Record the pinned version here when it changes. A version bump is its own commit with a visual check of every icon in the shell.

### The release CSP

This is the exact `app.security.csp` in `src-tauri/tauri.conf.json`. A change to it is a change to this lock first.

```
default-src 'self';
script-src 'self' 'unsafe-inline' https://kit.fontawesome.com https://ka-p.fontawesome.com;
font-src 'self' data: https://ka-p.fontawesome.com https://fonts.gstatic.com pav-theme:;
style-src 'self' 'unsafe-inline' https://fonts.googleapis.com;
img-src 'self' pav-cid: data: pav-theme:;
frame-src 'self' pav-link:;
connect-src ipc: http://ipc.localhost https://ka-p.fontawesome.com ws://localhost:1420
```

`ws://localhost:1420` is for the dev server only and is harmless in release. `fonts.googleapis.com` (stylesheet) and `fonts.gstatic.com` (font files) are there for the Material Symbols preset (F-3) and are not in `tauri.conf.json` until F-3. `pav-theme:` is on `img-src` and `font-src` as of D-6 (2026-10-17). The file today is this fence without the two Google font hosts. `blob:` stays out. Do not add `https:` to `img-src`, `frame-src`, or `navigate-to`. No other host is added to the main window's CSP except through the icon-library allowlist rule below. JMAP traffic is made by the C core with libcurl, not by the page, so `connect-src` never names a mail server.

### Outside HTML never enters the main DOM

- The main window can run inline script. So nothing untrusted may ever reach the main document as HTML. Mail bodies, subjects, addresses, contact notes, calendar descriptions, file names and previews, server error text, log lines, and settings values are inserted with `textContent` or rendered in the sandboxed pane. Tabulator formatters return text or set `textContent`. The CodeMirror log is an editor of text, not an HTML sink. SunEditor runs only inside the sandboxed composer frame.
- `innerHTML`, `outerHTML`, `insertAdjacentHTML`, and `document.write` with any outside data are a review failure. A test greps `ui/src` for those names; each use carries a `// html-ok: <reason>` comment naming the static string it inserts.
- Icon libraries are typed https URLs on an allowlist, never free HTML. Icon overrides are HTML snippets that always pass the shared icon sanitizer (`ui/src/common/iconsan.js`) before they reach the DOM. See "Icons" below. (This replaces the earlier rule that the kit field takes an id only and icons are class strings.)

### Icons (Andrew, 2026-10-16; replaces the kit-id-only rule)

**Icon library sources.**

- Settings holds a list of icon libraries, `preferences.ui.iconLibraries`: `[{ "id", "name", "type": "script"|"stylesheet", "url", "enabled" }]`. Each entry is a URL, never HTML. The page builds the element itself (`document.createElement("script")` with `src`, or `<link rel="stylesheet">` with `href`); nothing from the entry is parsed as markup.
- Rules, checked in the UI and again in the core when `prefs.ui.set` stores the list: `type` is `script` or `stylesheet`; the URL parses, is `https:`, has no credentials, and its host is on the **library host allowlist**. A failing entry is an error under the field, never a silent fallback.
- The allowlist (`core/src/icon_hosts.c` and `ui/src/common/iconhosts.js`, one generated list, with a test that both match and that every host is in the CSP): scripts from `kit.fontawesome.com` (whose loader then pulls from `ka-p.fontawesome.com`); stylesheets from `fonts.googleapis.com` (fonts then from `fonts.gstatic.com`). A test parses `tauri.conf.json` and fails if an allowlisted host is missing from the matching CSP directive.
- **The Tauri CSP is fixed at build time.** It is baked into the binary from `tauri.conf.json`. Adding a library from a new host means a change to this Lock, the allowlist, and the CSP, then a rebuild. A user cannot add a new host from Settings; the field explains that.
- Presets: **Font Awesome kit** (`script`, `https://kit.fontawesome.com/45801522ef.js`, enabled, the default) and **Material Symbols** (`stylesheet`, `https://fonts.googleapis.com/css2?family=Material+Symbols+Sharp:opsz,wght,FILL,GRAD@20..48,100..700,0..1,-50..200`, disabled). The FA kit rules above (kit only, no vendoring, `'unsafe-inline'`) still hold; Material Symbols is Apache-2.0 and loaded from Google's hosts the same way, not vendored.
- Load order: enabled libraries load in list order at startup; a change applies live (add the element, or remove it and reload the page for a removed script, which cannot be unloaded).

**Per-icon HTML.**

- Every icon in the UI is an HTML snippet so it can be layered or transformed: `fa-layers` stacks, `fa-stack`, `fa-rotate-*`, `fa-flip-*`, `data-fa-transform`, a Material ligature `<span class="material-symbols-sharp">inbox</span>`, or a small inline SVG.
- Because the main window allows inline script, every snippet passes the shared sanitizer before it reaches the main DOM, every time it is rendered (defaults included, so one path exists): `sanitizeIconHtml(html) -> DocumentFragment | error`. It parses with `DOMParser` (`text/html`, the SVG subtree as parsed), walks the tree, and rebuilds a new fragment from allowed nodes only (never reinserts the parsed nodes or a string).
- Allowed elements: `i`, `span`, `svg`, `path`, `g`, `circle`, `rect`, `title`. Anything else is dropped with its content (text inside `i`, `span`, and `title` is kept as text nodes).
- Allowed attributes: `class`, `style` (filtered), `data-fa-*`, `aria-*`, `viewBox`, `d`, `fill`, `transform`. Geometry attributes on the shapes (`cx`, `cy`, `r`, `x`, `y`, `width`, `height`) are not on the list; shapes size through `viewBox`/`d`/`transform` or Andrew adds them to this Lock. No `on*`, no `href`, `xlink:href`, `src`, `srcset`, `id`, `style` with `url(`, `expression(`, `@import`, `\`, or `javascript:`; no attribute value containing a URL scheme.
- `style` filter: declarations only from `color`, `--fa-*` custom properties, `font-size`, `opacity`, `transform`, `rotate`, `scale`, `vertical-align`, `font-variation-settings`; values matching `^[#a-zA-Z0-9 .,%()+-]{1,120}$`.
- Limits: 8 KB per snippet, depth 6, 64 elements. Over a limit is an error.
- The sanitizer has a hostile-input test suite (`ui/test/iconsan.test.js`): `<script>`, `<img onerror>`, `<svg onload>`, `<svg><script>`, `<a href=javascript:>`, `<use href>`, `<foreignObject>`, `<style>`, `style="background:url(...)"`, entity-encoded `javascript:`, `<math>` and `<template>` mutation tricks, `<iframe>`, unclosed and nested comments, a 100 KB input, and deep nesting. Each must produce only allowed nodes and attributes (a checker walks the output).
- The registry caches the sanitized fragment keyed by the snippet text. A list does not reparse the same icon on every row. An override change drops that key.

**Icon registry and overrides.**

- `ui/src/icons/registry.js` lists every icon the UI draws: `{ key, section, label, default }`, where `key` is stable (`nav.mail`, `toolbar.mail.archive`, `list.flag`, `tab.kind.message`), `section` groups the settings tree (Nav buttons, Tabs, Mail toolbar, Mail list, Reader, Composer, Contacts, Calendars, Files, Activity, Settings, Tree), and `default` is HTML. Code renders icons only through `icon(key)`, never inline markup, and a test fails if a key used in `ui/src` is missing from the registry.
- Only overrides persist: `preferences.ui.icons` is `{ key: html }`. A key not in the registry is kept but ignored. Reverting deletes the key. The old `preferences.ui.icons` (nav id to class string) migrates on load to `nav.<id>` HTML snippets.
- An override that fails the sanitizer at render time falls back to the default and logs a warn once.

### Message pane

- All outside HTML renders in `<iframe sandbox srcdoc=…>` with no `allow-scripts`, no `allow-same-origin`, no `allow-forms`, no `allow-popups`, and no `allow-top-navigation`. The srcdoc carries its own CSP meta: `default-src 'none'; img-src pav-cid: data:` plus `https:` only when remote content is allowed for that message; `style-src 'unsafe-inline'; font-src data:; frame-src pav-link:`. It never gains the Font Awesome hosts or any script source. WebKit on Fedora ignores `navigate-to` and reports it as an unrecognized directive, so the pane does not send that directive. The sandbox itself blocks a meta refresh. `frame-src pav-link:` is how a link click reaches `pav_protocol`. The main-window `frame-src` includes `pav-link:` for the same reason, because a srcdoc inherits that policy. It does not include `https:`. `allowRemote` adds `https:` to `img-src` only. The inherited parent `img-src` does not include `https:`, so a remote image stays blocked until that lock changes.
- A srcdoc document inherits the embedder's CSP as well as its own. The sandbox without `allow-scripts` is what stops script; the inner CSP is what stops network loads. Both are required.
- A click navigates that frame to `pav-link`. The handler returns 204 with an empty body. WebKit on Fedora still replaces the srcdoc, and that document has no URL to reload. `createHtmlPane` keeps the last HTML and writes the same srcdoc back when the frame loads a navigation the pane did not just write.
- The frame has no Tauri IPC. Capabilities grant IPC to the main window only (`"windows": ["main"]`, no `remote` URLs).
- Icons and controls for a message (remote-content banner, link confirm) are drawn by the parent around the frame, never inside it.
- The same pane component (`ui/src/common/htmlpane.js`) is used for mail, calendar descriptions, contact notes with HTML, and file previews of HTML.

### FFI

- **Decision (2026-10-16): an API version, not a struct check and not bindgen.**
  - The core exports `int pavillion_api_version(void)`, which returns `PAVILLION_API_VERSION` from `pavillion.h`, starting at 1. The macro is one bare decimal integer on its own line. `build.rs` is the only reader. It rejects a leading zero, a second definition, and a value below 1.
  - The shell compiles in the value it was built against. After the A-6 flags and before `pavillion_start`, it compares `pavillion_api_version()` with that number. On a mismatch it prints `API version mismatch: core=<n> shell=<m>` to stderr and exits 1 before the window. It does not call `pav_log`, and it does not create the working directory. `--help` and `--version` do not call the core.
- **Why the size check was dropped:**
  - Its only targets, `PavillionStatus` and `PavShellPrefs`, are deleted in G-8. A size check for them would be built, tested, and then thrown away.
  - Sizes miss reordered or retyped fields of the same size anyway.
  - After G-8, nothing crosses the FFI but strings, ints, and `PavBytes`. What can drift is the command contract, and only a version number captures that.
  - bindgen was rejected earlier: libclang for two dying structs, and Hydrogen links the C core directly.
- **When to bump:** `PAVILLION_API_VERSION` goes up when the command contract (`docs/API.md`) or the C surface (G L2 Lock) changes incompatibly: a function removed or changed, or a command or event removed, renamed, or given a different meaning. Adding a command or an optional field doesn't bump it.
- Until G-8, the two structs are kept in step by hand, in one commit that touches both sides. The FFI surface is in the [G-LAYERS.md](G-LAYERS.md) L2 Lock, with one free function, `pavillion_free`. A new FFI struct needs a reason recorded in this Lock.

### Preferences split

- The core interprets only what it acts on: `timezone`, `week_start`, `time_format`, `theme`, `window`, `cache` (L-2's limits, enforced by the core, so never under `ui`), `sync` (M-1's caps), and anything a later core feature reads. `preferences.ui` is an opaque object the core stores for the page, validated only as an object under 64 KB with no forbidden key (B lock). Zoom, splits, nav width, fonts, icons, kit, tab state, and colors live there.

## F-1: API version check

**Kind:** Medium
**Status:** Complete (2026-10-16). Variance: the mismatch is not passed to `pav_log`. Andrew chose the named stderr line only, and no working-directory creation, because the profile log is not open before start.

**Goal:** A shell and a core built from different contract versions refuse to start together, with a named error, instead of misbehaving.

**Entry gate:** D-3 complete. `make test` passes (27 Unity tests at the 2026-10-16 count).

**Work items:**

- [x] `#define PAVILLION_API_VERSION 1` and `int pavillion_api_version(void);` in `core/include/pavillion.h`, implemented in `core/src/pavillion.c`.
- [x] Shell: before `pavillion_start` (after the A-6 flags), compare `pavillion_api_version()` with `PAVILLION_API_VERSION` as read from this header by `build.rs`. On a mismatch, print `API version mismatch: core=<n> shell=<m>` to stderr and exit 1 before the window. Do not call `pav_log`. Do not create the working directory. `--help` and `--version` do not reach this check. The comparison in `lib.rs` is the only new shell logic.
- [x] Unity: `test_api_version_matches_header` (the function returns `PAVILLION_API_VERSION`, and it's ≥ 1).
- [x] Do not add bindgen, libclang, or struct-size checks (FFI Lock).

**Done means:** A core whose version differs from the shell's stops the launch with a named error.

**Exit gate:** `make test` exits 0 (28 tests). A scratch `return 2`, with the macro left at 1, printed `API version mismatch: core=2 shell=1` and exited 1. The path argument was not created. `--help` and `--version` on that binary still exited 0. The scratch was reverted and both shells were rebuilt. `make build` still shows the icons: the release window drew the roof mark and the Sharp Thin rail icons.

**Accomplished:** `pavillion_api_version` returns the header macro and takes no lock, so it is safe before `pavillion_start`. `src-tauri/build.rs` copies that macro into the shell. `require_api_version` runs at the top of `start_core`, before `ensure_workdir`. The number is written only in `pavillion.h`.

**Lessons learned:** Before start, `pav_log` can only reach stderr. The log thread is not started in production until H-1, and the profile log is attached inside start, so a log call here would not have been a record. The shell's number has to come from the macro. If it came from the function body, a same-tree build could not fail the scratch test. Exact equality does not notice two builds that both say version 1 and differ by an added command. That remains a review rule.

**Handoff:** W-1 is next. Do not bump `PAVILLION_API_VERSION` for an added command or an optional field. Bump it when a function, command, or event is removed, renamed, or given a different meaning. `build.rs` rejects a leading zero, a second definition, and a value below 1. The check does not run for `--help` or `--version`, and it does not call `pav_log`.

## F-5: Trim `config.c` with a helper layer

**Kind:** Medium
**Status:** Complete (2026-10-16). No behavior change. `PAVILLION_API_VERSION` stays 1.

**Goal:** `core/src/config.c` (1,493 lines after the preview) gets a small helper layer before accounts, rules, and mail settings are added to it. No behavior change.

**Entry gate:** F-1 complete.

**Work items:**

- [x] Measure the repetition: count jansson get, type check, range clamp, and default blocks. Write the count in Accomplished.
- [x] Add `core/src/cfg.c` and `core/include/pav_cfg.h`: `cfg_obj(json_t*, key, create)`, `cfg_str(obj, key, def, out, len)`, `cfg_num_range(obj, key, def, min, max, step)`, `cfg_int_range`, `cfg_bool`, `cfg_enum(obj, key, const char *const *allowed, def)`, `cfg_set_*` writers, and a path helper `cfg_at(root, "preferences.ui.nav")`.
- [x] Move the existing parse, ensure, read, and write functions onto the helpers. Split the file: `config.c` (index, profile, load, scan), `config_prefs.c` (preferences), `config_fs.c` (ensure and prepare; G-1 moves its OS calls behind the platform).
- [x] Add Unity tests for each helper (`test_cfg_num_range_clamps_and_steps`, `test_cfg_enum_rejects_unknown`, and so on).

**Done means:** `make test` passes with every old test unchanged, and adding a profile field is one helper call plus a test.

**Exit gate:** `make test` exits 0 with the old test names all present. `wc -l core/src/config*.c` is reported in Accomplished and the three files together are smaller than 1,493 lines. `make dev` restores the saved layout as before.

**Accomplished:** The 1,493-line `config.c` had `json_object_get` 42 times, `json_is_*` type checks 37 times, `set_err` messages beginning `missing` 45 times (of 100 `set_err` calls), `copy_string` 18, `need_number` 12, `read_number` 8, `clamp_double` 7, `clamp_int` 3, and `json_object_set_new` 8. Those paths now go through `core/include/pav_cfg.h` and `core/src/cfg.c`: `cfg_err`, `cfg_obj`, `cfg_at`, `cfg_str`, `cfg_num_range`, `cfg_int_range`, `cfg_bool`, `cfg_enum`, and the `cfg_set_*` writers. The split is `config.c` 485 lines (index, profile, load, scan), `config_prefs.c` 584 (preferences), and `config_fs.c` 345 (ensure and prepare): 1,414 lines together. `cfg.c` is 429 lines and is not in that count. `make test` is 34 tests, 0 failures. The previous 28 names are unchanged. The new names are `test_cfg_str_defaults_and_rejects_wrong_type`, `test_cfg_num_range_clamps_and_steps`, `test_cfg_int_range_clamps_and_steps`, `test_cfg_bool_defaults_and_rejects_wrong_type`, `test_cfg_enum_rejects_unknown`, and `test_cfg_obj_and_at_do_not_replace_or_create`. `make dev` against a copy of the saved profile restored the window geometry `1900x1309+797+496` and the zoom labels 160%, 170%, 90%, 145%, and 90%. The real profile `~/.local/share/pavillion/profiles/default.json` was not rewritten. Its sha256 stayed `b9bbab0e7be74fcc06536ac624d873f501bf91c73df2d0596f9054e55944bcf8`.

**Lessons learned:** A missing key uses the default. A present value of the wrong type is still an error. `step == 0` does not snap. `cfg_enum` on the chrome write would have changed `chrome must be system or hidden` into `chrome must be one of: system, hidden`. The Unity check only searches for `chrome`, so that change would have passed. The write path compares the two names first, then `cfg_set_enum` stores the value. Window size and position stay errors, not clamps. A pixel width still has to be a whole number before the pane clamp. Zoom shell is stored as written and clamped when the shell prefs are read. `json_object_set_new` already releases its value when the insert fails, so the icon writer does not release the object again. Scan stays in `config.c`. The ensure and prepare OS calls stay in `config_fs.c` for G-1. Which section is selected on launch was not part of this gate. The restored window showed Settings and the saved zoom sliders, and no UI file changed.

**Handoff:** F-2 is next. Do not bump `PAVILLION_API_VERSION` for a helper or a new profile field. Adding a field is one helper call plus a test. Parse of theme, week start, time format, density, log level, and chrome still uses `cfg_str`, so an unknown string still loads. `cfg_at` only walks a dotted path and does not create objects. `cfg_obj` with create inserts one missing object and does not replace a value of the wrong type. G-1 still moves `mkdir`, `open`, `fchmod`, `stat`, and `getenv` out of `config.c` and `config_fs.c`.

## F-2: UI preferences as an opaque JSON blob

**Kind:** Medium
**Status:** Complete (2026-10-16). Variance: `PAVILLION_API_VERSION` is 2. Shrinking `PavShellPrefs` changes the C struct, so the FFI Lock requires the bump. Adding `pavillion_ui_get` and `pavillion_ui_set` alone would not have.

**Goal:** A new UI knob (a slider, a split, a font, a tab setting) touches only `ui/` and does not change a C header, a Rust struct, or a `PAV_PREFS_*` mask.

**Entry gate:** F-5 complete.

**Work items:**

- [x] Core: `int pavillion_ui_get(char **json_out, char *err, size_t len)` and `int pavillion_ui_set(const char *json, char *err, size_t len)`. Set validates an object, at most 64 KB serialized, no forbidden key, and writes the profile on the core thread. Get returns the stored object. (G-2 turns these into the `prefs.ui.get` and `prefs.ui.set` commands.)
- [x] Move zoom (`preferences.zoom` becomes `preferences.ui.zoom`), nav width, the three splits and their limits, icons, kit, and fonts under `preferences.ui`. `PavShellPrefs` shrinks to the window rectangle and `chrome`, which the shell applies before the page loads.
- [x] Migration on load: a profile with the old `preferences.zoom` or fields outside `ui` is rewritten once into the new shape; schema stays 1. Unity test `test_profile_migrates_old_zoom_into_ui` reads a fixture copied from today's `default.json`.
- [x] The page owns defaults and clamps: `ui/src/prefs.js` with one `DEFAULT_UI` object and `clampUi()`. Zoom range is `0.05`–`5` step `0.05` (B lock); the preview's `0.5`–`3` clamp is replaced.
- [x] Persist the Fonts setting (`ui.fonts.body`, `ui.fonts.mono`), which today resets on launch.
- [x] Debounce: the page writes at most every 300 ms and on `pagehide`.

**Done means:** The Fonts setting is persisted with no C or Rust change in that commit, and an old profile loads with its zoom and splits intact.

**Exit gate:** `make test` exits 0 including the migration test. In `make dev`: change the font, the nav width, and a pane zoom; quit; relaunch; all three are restored. `git diff` of the Fonts commit touches only `ui/`.

**Accomplished:** `pavillion_ui_get` and `pavillion_ui_set` queue `CORE_CMD_UI` on the core thread. Set rejects a non-object, a forbidden key, and a payload over `PAV_UI_JSON_MAX` (65536) both as text and as compact JSON. Get of a missing `ui` returns `{}` and does not write the file. The core stores the object and does not clamp or invent keys inside it. On load, `zoom`, `nav`, `mail`, `contacts`, `files`, `icons`, `fontawesome_kit`, and `fonts` move from `preferences` into `preferences.ui` when absent there; if both exist, `ui` wins and the outside key is deleted. Schema stays 1. `PavShellPrefs` is the window rectangle and `chrome`. `PAV_PREFS_ZOOM` and `PAV_PREFS_UI` are gone. `PAV_PREFS_WINDOW` stays `2u`. `PAVILLION_API_VERSION` is 2. The page commands are `ui_get` and `ui_set`. `ui/src/prefs.js` clamps zoom to `0.05`–`5` in steps of `0.05`, nav width into its stored limits, and the three splits the same way. Writes are a leading 300 ms throttle, a flush on `pagehide`, and an immediate flush before an icon reload. `config.c` is 478 lines, `config_prefs.c` 412, `config_fs.c` 345: 1,235 together. `make test` is 35 tests, 0 failures, including `test_profile_migrates_old_zoom_into_ui` on `tests/fixtures/default-2026-10-16.json`. In `make dev` the body font was set to System UI, the mono font to Liberation Mono, the nav width from 354 to 400, and the mail-list zoom from 145% to 160%. After quit and relaunch the window had `nav` `400px`, body `system-ui, sans-serif`, and mail list `160%`. The Fonts commit `9be9a8b` touches only `ui/`. The trial values were then removed: `~/.local/share/pavillion/profiles/default.json` was restored from the snapshot taken before that launch. Its sha256 is `d551dd41c45a987e74c4af687c74f513eb68425bbd5ebedaeffe47103f339e04`. The next real launch migrates that file once.

**Lessons learned:** A new command does not bump the API version. A smaller `PavShellPrefs` does, because a shell still at version 1 would read the window fields at the old offsets. The core must not invent pane defaults. The page writes them on the next change, and a load that filled them in would hide a missing key from the migration test. `json_object_set_new` steals the reference. The 300 ms rule is a leading throttle: the first dirty event starts the timer, later events in that window are included, and `pagehide` flushes. A trailing debounce can miss the quit. The window gate's save had already landed from the timer before quit, so this sitting did not prove that the `pagehide` invoke finishes before the webview dies. Served CSS is `ui/public/styles`. A font stack with an unquoted multi-word family is two families. The Vanadium option value is `"Vanadium Sans", system-ui, sans-serif`. `clampUi` did not invent `fonts` until the Fonts commit, so the earlier layout save could not persist a font and spoil that commit's diff.

**Handoff:** G-1 is next. `PAVILLION_API_VERSION` is 2. G-8 removes functions, so that bump goes to 3, not 2. Do not bump for a helper or a new optional field. The core does not clamp `preferences.ui` and does not invent zoom or pane keys. The page does, in `ui/src/prefs.js`. `pavillion_icons_get`, `pavillion_icons_apply`, `pavillion_kit_get`, and `pavillion_kit_apply` stay for the Unity tests. The page does not call them. G-1 still moves `mkdir`, `open`, `fchmod`, `stat`, and `getenv` out of `config.c` and `config_fs.c`. Schema stays 1. The real profile was put back after the window gate and will be migrated on the next launch.

## F-3: Icon library sources

**Kind:** Medium
**Status:** Not started.

**Goal:** Settings holds typed icon-library URLs on an allowlist that the CSP carries, with the FA kit and Material Symbols presets. No free HTML reaches the page from these settings.

**Entry gate:** F-2 complete.

**Work items:**

- [ ] `preferences.ui.iconLibraries` with the Lock's shape; migrate today's `ui.fontawesome_kit` (an id, or the raw-HTML forms the preview accepted, reduced to the id when one can be found, else the default) into the FA preset entry.
- [ ] The allowlist in `core/src/icon_hosts.c` and `ui/src/common/iconhosts.js`, generated from one source (`ui/src/common/iconhosts.json`, embedded into C by CMake). `prefs.ui.set` validates entries (the one interpreted part of the opaque blob besides size and forbidden keys).
- [ ] Add `https://fonts.googleapis.com` to `style-src` and `https://fonts.gstatic.com` to `font-src` in `tauri.conf.json` and this Lock.
- [ ] Remove the raw-HTML branch of `loadFontAwesomeKit` in `ui/src/main.js` and the help text offering a `<script>` tag; replace with `loadIconLibraries(list)` that builds elements from URLs.
- [ ] Settings → Icons gains a Libraries list: add (type, URL), enable, reorder, remove, presets; the note that a new host needs a rebuild.
- [ ] Vitest is already on `make test` (G-3). Add `iconhosts.test.js` (both lists equal; each host in the CSP from `tauri.conf.json`) and `iconlibs.test.js` (http refused, credentials refused, unknown host refused, `javascript:` refused, wrong type refused).
- [ ] Add the HTML-sink grep test from the Lock as `ui/test/no-html-sinks.test.js`.
- [ ] Unity: `test_icon_library_validation`.

**Done means:** Pasting `<script>alert(1)</script>` or an `http:` or off-list URL into the Libraries list shows an error and changes nothing; enabling Material Symbols renders a ligature icon.

**Exit gate:** `make test` exits 0. In `make build`: FA icons render by default; enable Material Symbols and a test `<span class="material-symbols-sharp">inbox</span>` renders; a URL on another host is refused with the rebuild note.

## F-7: Icon HTML sanitizer, registry, and overrides

**Kind:** Medium
**Status:** Not started.

**Goal:** Every icon in the UI is drawn from the registry through one shared sanitizer, and overrides are stored as key-to-HTML in the UI blob.

**Entry gate:** F-3 complete.

**Work items:**

- [ ] `ui/src/common/iconsan.js`: `sanitizeIconHtml` per the Lock (allowlists, style filter, limits, rebuild into a fresh fragment).
- [ ] `ui/test/iconsan.test.js`: the Lock's hostile inputs plus valid layered FA (`fa-layers` with `data-fa-transform`), a Material ligature, and a small SVG; an output checker shared by the tests.
- [ ] `ui/src/icons/registry.js` with every icon the shell draws today (8 nav buttons, settings headings, tab kinds as J-2 will use, Update spin) and `icon(key)` that applies the override or default through the sanitizer.
- [ ] Replace `applyIconOverride` and every inline icon markup in `ui/index.html` and `ui/src` with `icon(key)`; a test that every key used is registered.
- [ ] Migrate the old `preferences.ui.icons` shape to `nav.<id>` HTML; Vitest for the migration.
- [ ] The F-3 Settings field for raw icon HTML (if any remains from the preview) is removed; F-8 brings the editor.

**Done means:** All icons come from the registry through the sanitizer, and an override containing `<img src=x onerror=…>` renders as nothing but the allowed parts.

**Exit gate:** `make test` exits 0 with the hostile suite. In `make build` the shell looks the same as before, and a hand-edited profile override with a script renders the default and logs one warn.

## F-4: Message pane isolation, proven before mail

**Kind:** Medium
**Status:** Complete (2026-10-16). Variance: `--selftest` may be followed by one workdir. This WebKit ignores `navigate-to`, so the pane does not send it. A 204 still replaces the srcdoc, and the pane writes that document back. The dialog does not open the URL.

**Goal:** The sandboxed pane exists and is tested with hostile HTML in the release build before any real message is shown.

**Entry gate:** G-8 complete (`pav-link` lives in the C `pav_protocol` table). F-4 does not need the icon sanitizer.

**Work items:**

- [x] `ui/src/common/htmlpane.js`: `createHtmlPane(el)` returns `{ show(html, { allowRemote }), clear() }`. It builds the iframe with the Lock's attributes, wraps the body with the inner CSP meta and a base stylesheet (`color-scheme`, font, `max-width:100%` on images).
- [x] Put one pane in the Mail detail placeholder, fed by a fixture behind a Help-menu item `Show isolation test` (debug builds and `--selftest` only). Andrew confirmed the button is hidden without the flag and that the fixture loads with it.
- [x] Fixture `tests/corpus/hostile.html`: `<script>`, `onerror`, `javascript:` link, `<meta http-equiv=refresh>`, `<base href>`, remote `img` and CSS `url()`, `<form>`, SVG with script, `<iframe>`, `<object>`.
- [x] In the release build, from the frame: `window.__TAURI__`, `window.__TAURI_INTERNALS__`, and `parent.__TAURI__` are unreachable (no script runs, so the test is that nothing in the fixture changes the page title the parent watches, and that devtools' network panel shows zero requests). Record the steps in Accomplished.
- [x] `src-tauri/capabilities/default.json` keeps `"windows": ["main"]` and gains no `remote` entry. Add a Vitest test (`capabilities.test.js`) that parses the file and asserts that. This is not a Rust test, per the G L3 Lock.
- [x] Link clicks: the parent cannot see clicks in a script-free sandbox. N-2's core sanitizer will rewrite every `href` to `pav-link://<id>/<n>`. That URL shape is fixed here, and N-2 depends on it. This phase uses a hand-written fixture page that already carries such links; this phase adds `pav-link` to the C `pav_protocol` table under the scheme contract in the [G-LAYERS.md](G-LAYERS.md) L2 Lock. C emits `link.clicked` with the index and returns 204, and Rust only forwards, and the parent shows a plain `<dialog>`. J-1 replaces that dialog with the shared one. Prove it with a fixture link here.

**Done means:** The fixture renders as inert text and layout in `make build`, makes no network request, and a link click raises the parent's confirm with the real URL.

**Exit gate:** `make build` and run `dist/pavillion`: the isolation test shows the fixture, the dialog appears on link click, the network panel is empty, and `make test` exits 0.

**Accomplished:** `createHtmlPane` builds an `<iframe sandbox="" referrerpolicy="no-referrer" title="Message">` and sets `srcdoc`. The inner CSP is `default-src 'none'; img-src pav-cid: data:; style-src 'unsafe-inline'; font-src data:; frame-src pav-link:`. `allowRemote` adds `https:` to that `img-src` only. The base style sets `color-scheme: dark`, a 16px system font, and `max-width: 100%` on images. The fixture is `tests/corpus/hostile.html`. The Help panel button Show isolation test is shown when `system.status` reports `debug` or `selftest`. A debug build sets `PAVILLION_DEBUG=1` from Rust `debug_assertions`. `--selftest` sets `PAVILLION_SELFTEST=1` and may be followed by one workdir. A release build clears an inherited debug value. Both are read once in `pavillion_start`. Any other flag still prints help on stderr and exits 1. `--help` and `--version` still exit before the core. `pav-link://<paneId>/<n>` is parsed in `protocol.c`. `paneId` is 1 to 64 characters from `A-Za-z0-9_-`. `n` is a decimal integer from 0 through 2^53−1. The handler emits `link.clicked` as `{"paneId","n"}` and returns 204 with an empty body. Any other URL is 404 and emits nothing. `src-tauri/src/lib.rs` is 199 lines and only forwards. The parent list for pane `isolation` has one URL, `https://example.com/pavillion-isolation-test`. The dialog says "This link wants to open:" and that URL. Close dismisses it. `link.open` is not called. Capabilities stay `"windows": ["main"]` with no `remote` entry. The main `frame-src` is `'self' pav-link:`. `PAVILLION_API_VERSION` stays 3. `make test` is 67 Unity tests and 30 Vitest tests. `make build` produced `dist/pavillion`. Andrew ran that binary with `--selftest`. The button was hidden without the flag. The fixture showed. Example link raised the dialog with the example.com URL. The network panel was empty. No page loaded. The console showed the fixture script blocked, the `example.invalid` image, frame, object, and CSS loads refused, and the meta refresh blocked by the sandbox. That block printed again when the pane wrote the srcdoc back. The script that would have changed the title is the one the console blocked.

**Lessons learned:** This WebKit ignores `navigate-to` and logs it as unrecognized, so the pane does not send it. The sandbox already blocks a meta refresh. A srcdoc inherits the parent CSP, so `frame-src 'self'` refused `pav-link://isolation/0` until the main policy listed `pav-link:`. A 204 with an empty body still replaces the srcdoc, and that document has no URL to reload. `createHtmlPane` writes the same HTML back when the frame loads a navigation the pane did not just write. The attribute does not navigate when the new text equals the old text, so each write adds a comment. The same console block can print a second time. That is the restore. The parent `img-src` is still `'self' pav-cid: data:`, so `allowRemote` cannot load an https image until that lock changes. cmake does not define `NDEBUG`, so the debug signal is the shell's `debug_assertions`. Edition 2024 makes `std::env::set_var` unsafe, and `main` is the only thread there. A-6 said a flag mixed with any other argument is unknown. `--selftest` plus one workdir is the exception, so a release check can leave the live profile untouched. The dialog does not open the URL.

**Handoff:** G-4 is next, in [G-LAYERS.md](G-LAYERS.md). Do not bump `PAVILLION_API_VERSION`. `lib.rs` stays a forwarder at 199 lines. Do not add `navigate-to`. Do not add `https:` to the main `img-src` or `frame-src`. N-4 owns remote images and `link.open`. J-1 replaces the plain `#link-confirm` dialog. N-2 rewrites real mail hrefs to this `pav-link` shape. The Help button stays behind `debug` or `selftest`. Secrets stay `-1` until K-1. `macos-core` stays `continue-on-error` until G-6. G-4's entry gate is G-8, which is already met.

## F-8: Icon settings: tree and editor

**Kind:** Medium
**Status:** Not started.

**Goal:** Andrew can find any icon by UI section, edit its HTML, compare it with the saved and default versions, and apply, revert, or cancel.

**Entry gate:** J-1 and E-7 complete (menus, dialogs, toasts, and the tree engine).

**Work items:**

- [ ] Settings → Icons: the E tree (E-7 has landed) of every registry entry grouped by `section` and labeled by `label`, with the current icon beside each label.
- [ ] Picking an icon opens an editor: CodeMirror 6 in HTML mode (H-2 already installed it) with the HTML, and three side-by-side previews labeled **Current** (saved), **Default**, and **Preview** (the edited HTML), each at three sizes (1rem, 2rem, 4rem) on the panel surface. The editor's document is text. Preview goes through the icon sanitizer before it reaches the DOM.
- [ ] Preview refreshes only when the user clicks **Preview**, never on keystrokes. A sanitizer error shows under the field and the Preview box says it was rejected; what the sanitizer removed is listed as text (e.g. `removed: onerror on img; img`).
- [ ] Buttons: **Preview**; **Apply/Save** (sanitizes, stores the override through `prefs.ui.set`, updates every use of that icon live, toast with Undo); **Revert to default** (deletes the override, confirm); **Cancel** (discards edits, back to Current). Leaving with unsaved edits asks through the dialog.
- [ ] Search box filtering the tree by label or key; a badge on overridden icons; "Revert all" with confirm.
- [ ] Vitest: preview not refreshed on input; apply stores only the key; revert removes the key; cancel leaves the blob unchanged.

**Done means:** Change the Mail nav icon to an `fa-layers` stack, preview it, apply it, see it live in the nav, revert it.

**Exit gate:** `make test` exits 0. In `make build`: the flow above works; pasting the hostile suite's worst input previews as rejected and Apply refuses it; restart keeps an applied override.

## F-6: Decide where JMAP and sync live

**Kind:** Easy (a written decision)
**Status:** Superseded (2026-10-16). The decision is the [M-SYNC.md](M-SYNC.md) Lock as revised the same day: the C core owns JMAP and sync with libcurl and Jansson; one net thread and its own curl multi per server endpoint; the store writer owns the only write connection. The earlier wording (one shared multi handle, the store on the core thread) is not the decision. Andrew can reopen it from the index's open questions.

## Working log

- 2026-10-15 Opened from a project review. Release-build icons were fixed by adding `'unsafe-inline'` to `script-src` and `font-src` for the Font Awesome hosts. Not a phase.
- 2026-10-16 Andrew's icon requirement replaced the kit-id-only rule: icon libraries as typed https URLs on an allowlist that the CSP carries (FA kit default, Material Symbols preset, Google Fonts hosts added), per-icon HTML through a shared sanitizer with a hostile suite, an icon registry, and the icon settings tree and three-preview editor. F-3 rewritten; F-7 and F-8 added.
- 2026-10-16 Scoped for implementation: entry and exit gates, the exact CSP, the HTML-sink rule and test, kit pinning, the preferences split, link handling through `pav-link`. F-5 moved ahead of F-2. F-6 superseded by the M-SYNC Lock.
- 2026-10-16 F-1 decided: the ABI check, not bindgen. With Rust minimized and G-8 deleting both structs, bindgen's libclang dependency isn't worth it. `pav-link` moves into C's `pav_protocol`.
- 2026-10-16 Review fixes: added a standalone pinned-kit-version line; `preferences.cache` and `sync` listed as core-read; FFI and scheme contracts point to the G L2 Lock; F-4 no longer depends on N-2 code (it fixes the `pav-link` URL shape); capabilities check is Vitest, not Rust.
- 2026-10-16 F-1 is now an API version check (`pavillion_api_version()` and `PAVILLION_API_VERSION`). The struct-size check was dropped because G-8 deletes both structs, and the bump rule is in the FFI Lock.
- 2026-10-16 F-1: `build.rs` reads the macro. A mismatch prints `API version mismatch: core=<n> shell=<m>` and exits 1. No `pav_log`, no working directory. Scratch `return 2` proved it. `make test` is 28 tests. The release window still showed the rail icons.
- 2026-10-16 F-5: helpers in `pav_cfg.h` and `cfg.c`. `config.c` split into `config.c` (485), `config_prefs.c` (584), and `config_fs.c` (345), 1,414 lines together. `make test` is 34 tests. `make dev` restored `1900x1309+797+496` and the saved zoom labels. API version stays 1.
- 2026-10-16 F-2: `preferences.ui` is an opaque blob. `PavShellPrefs` is the window only, so `PAVILLION_API_VERSION` is 2. An old profile migrates once, schema stays 1. `make test` is 35 tests. Fonts persist in `9be9a8b`, which touches only `ui/`. The window restored System UI, nav 400, and mail list 160% after quit. The real profile was then restored.
- 2026-10-16 F-4 in progress. `htmlpane`, `tests/corpus/hostile.html`, `pav-link` (204 and `link.clicked`), and the capabilities Vitest are in. The inner CSP gained `navigate-to pav-link:`. `PAVILLION_API_VERSION` stays 3. `make test` is 67 Unity tests and 28 Vitest tests. `make build` produced `dist/pavillion`. The release window check is still open.
- 2026-10-16 F-4 window check: WebKit ignores `navigate-to` and the sandbox blocks meta refresh. The click was refused because inherited `frame-src 'self'` does not include `pav-link:`. The main CSP and the pane CSP now use `frame-src` for that scheme. `navigate-to` is gone. The dialog is still unconfirmed.
- 2026-10-16 F-4 rebuild: `make test` is 67 Unity tests and 29 Vitest tests. `make build` produced `dist/pavillion` with `frame-src 'self' pav-link:` and no `navigate-to`. The dialog is still unconfirmed. Do not start G-4.
- 2026-10-16 F-4 window check: the dialog showed `https://example.com/pavillion-isolation-test`. The frame then stayed empty. WebKit replaces the srcdoc on the 204, and that document has no URL to reload. `createHtmlPane` writes the same HTML back on that load. `make test` is 67 Unity tests and 30 Vitest tests. `make build` produced `dist/pavillion` with that restore. The restored frame is still unconfirmed.
- 2026-10-16 F-4 complete. Andrew confirmed the dialog, an empty network panel, and no page load. The console block is the sandbox and the CSP, and it printed again when the srcdoc was written back. `make test` is 67 Unity tests and 30 Vitest tests. `PAVILLION_API_VERSION` stays 3. `lib.rs` is 199 lines. G-4 is next.
- 2026-10-17 D-6 added `pav-theme:` to `img-src` and `font-src` only. `blob:` stays out. `https:` was not added to `img-src`. D-7 may put one `<i class>` from a theme file into the nav before F-7's sanitizer exists. F-7 and F-8 are unchanged.

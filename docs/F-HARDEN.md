# F — HARDEN

**Status:** Not started (opened 2026-10-15 from a project review; scoped 2026-10-16). F-6 is superseded.
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

Structural fixes found after D-3. They go in before mail is real, because each gets more expensive once JMAP, accounts, and credentials sit on top of it. This document owns the main-window security rules: the CSP, the Font Awesome kit, and the rule that outside HTML never enters the main DOM.

## Phases

One phase is one session. Do not start the next one in the same conversation.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| F-1 | API version check (`pavillion_api_version`) | Medium | Not started |
| F-5 | Trim `config.c` with a helper layer | Medium | Not started |
| F-2 | UI preferences as an opaque JSON blob | Medium | Not started |
| F-3 | Icon library sources: typed https URLs on an allowlist, presets | Medium | Not started |
| F-7 | Shared icon HTML sanitizer, the icon registry, overrides in the UI blob | Medium | Not started |
| F-8 | Icon settings: the icon tree and the three-preview editor | Medium | Not started |
| F-4 | Message pane isolation, proven before mail | Medium | Not started |
| F-6 | Decide where JMAP and sync live | Easy (decision) | Superseded (2026-10-16) |

The table is in sequence order, not number order. F-5 runs before F-2 so the blob work lands on the new helpers. F-7 follows F-3 directly; F-8 waits for J-1's widgets.

Kind: **Easy** is one module and a test, or a static page. **Medium** crosses one boundary (the window, the filesystem, a thread, the FFI). **Hard** joins two build systems or two processes for the first time.

## How a session works

1. Read Resume in [PAVILLION_PLAN.md](../PAVILLION_PLAN.md), the Status column above, and the previous phase's Status, Accomplished, Lessons learned, and Handoff.
2. Re-read only this phase: Goal, Entry gate, Work items, Done means, Exit gate. Open the lock section this phase cites.
3. Ask the qualifying questions. Wait for approval before editing source.
4. Do this phase only. Check a work item `[x]` only after its check has passed. Defer with `[~]`, one line, and the phase it moves to.
5. Before marking complete, write Status, Accomplished, Lessons learned, and Handoff, update the Status column here and in the index, and append the Working log. Then stop.
6. A sitting that ends early sets Status to **in progress**, names the last finished work item, and leaves the rest unchecked.

Leave Accomplished, Lessons learned, and Handoff off a phase that has not started. `make test` passes at the end of every phase. A UI exit gate is the window, and for anything touching the CSP it is the `make build` window, not `make dev`.

## Lock

### Font Awesome stays a kit

- Pinned kit version: 7.3.1 (Pro Plus, SVG/JS)
- Font Awesome Pro is loaded **only** through the official kit (`45801522ef`, Pro Plus 7.3.1, SVG/JS method, `https://kit.fontawesome.com/45801522ef.js`). Pavillion is open source, and the kit is the only distribution the license allows. Do not copy Pro SVGs, CSS, or font files into the tree, and do not vendor the kit loader. Do not "fix" a CSP problem by self-hosting. Copying SVG icons in by hand is also a license violation.
- Families in use: Duotone, Sharp, and Thin. Mail keeps the roof mark (`ui/public/mark.png`).
- The kit loader fetches `pro-plus.min.js` from `ka-p.fontawesome.com` with `fetch()` and runs it as an **inline** `<script>`. That is why `script-src` carries `'unsafe-inline'` in the release build. `make dev` never shows the problem, because the Vite page is not served with the Tauri CSP.
- `'unsafe-inline'` is effective only because `dangerousDisableAssetCspModification` is `true`. If Tauri adds a hash or a nonce to `script-src`, browsers ignore `'unsafe-inline'` and the icons vanish in `make build` only. Keep the flag, and keep a comment beside it in the docs (JSON has no comments).
- `font-src 'self' data: https://ka-p.fontawesome.com` covers the kit's v4 and v5 font-face shims. `kit-uploads.fontawesome.com` is not allowed.
- The kit's domain list stays open. `make dev` is `http://localhost:1420` and the packaged window is `http://tauri.localhost`, which a domain-limited kit rejects and will not let you add. Loads count as pageviews on that subscription. Revisit if the app is widely used or the embed is misused.
- **Pin the kit version.** In the Font Awesome kit settings, set the version to `7.3.1` rather than "latest 7.x", so icon names do not shift between builds. Record the pinned version here when it changes. A version bump is its own commit with a visual check of every icon in the shell.

### The release CSP

This is the exact `app.security.csp` in `src-tauri/tauri.conf.json`. A change to it is a change to this lock first.

```
default-src 'self';
script-src 'self' 'unsafe-inline' https://kit.fontawesome.com https://ka-p.fontawesome.com;
font-src 'self' data: https://ka-p.fontawesome.com https://fonts.gstatic.com;
style-src 'self' 'unsafe-inline' https://fonts.googleapis.com;
img-src 'self' pav-cid: data:;
frame-src 'self';
connect-src ipc: http://ipc.localhost https://ka-p.fontawesome.com ws://localhost:1420
```

`ws://localhost:1420` is for the dev server only and is harmless in release. `fonts.googleapis.com` (stylesheet) and `fonts.gstatic.com` (font files) are there for the Material Symbols preset (F-3). No other host is added to the main window's CSP except through the icon-library allowlist rule below. JMAP traffic is made by the C core with libcurl, not by the page, so `connect-src` never names a mail server.

### Outside HTML never enters the main DOM

- The main window can run inline script. So nothing untrusted may ever reach the main document as HTML. Mail bodies, subjects, addresses, contact notes, calendar descriptions, file names and previews, server error text, log lines, and settings values are inserted with `textContent` (or a library that renders text nodes, such as Tabulator formatters that return text, or CodeMirror) or rendered in the sandboxed pane.
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

**Icon registry and overrides.**

- `ui/src/icons/registry.js` lists every icon the UI draws: `{ key, section, label, default }`, where `key` is stable (`nav.mail`, `toolbar.mail.archive`, `list.flag`, `tab.kind.message`), `section` groups the settings tree (Nav buttons, Tabs, Mail toolbar, Mail list, Reader, Composer, Contacts, Calendars, Files, Activity, Settings, Tree), and `default` is HTML. Code renders icons only through `icon(key)`, never inline markup, and a test fails if a key used in `ui/src` is missing from the registry.
- Only overrides persist: `preferences.ui.icons` is `{ key: html }`. A key not in the registry is kept but ignored. Reverting deletes the key. The old `preferences.ui.icons` (nav id to class string) migrates on load to `nav.<id>` HTML snippets.
- An override that fails the sanitizer at render time falls back to the default and logs a warn once.

### Message pane

- All outside HTML renders in `<iframe sandbox srcdoc=…>` with no `allow-scripts`, no `allow-same-origin`, no `allow-forms`, no `allow-popups`, and no `allow-top-navigation`. The srcdoc carries its own CSP meta: `default-src 'none'; img-src pav-cid: data:` plus `https:` only when remote content is allowed for that message; `style-src 'unsafe-inline'; font-src data:`. It never gains the Font Awesome hosts or any script source.
- A srcdoc document inherits the embedder's CSP as well as its own. The sandbox without `allow-scripts` is what stops script; the inner CSP is what stops network loads. Both are required.
- The frame has no Tauri IPC. Capabilities grant IPC to the main window only (`"windows": ["main"]`, no `remote` URLs).
- Icons and controls for a message (remote-content banner, link confirm) are drawn by the parent around the frame, never inside it.
- The same pane component (`ui/src/common/htmlpane.js`) is used for mail, calendar descriptions, contact notes with HTML, and file previews of HTML.

### FFI

- **Decision (2026-10-16): an API version, not a struct check and not bindgen.**
  - The core exports `int pavillion_api_version(void)`, which returns `PAVILLION_API_VERSION` from `pavillion.h`, starting at 1.
  - The shell compiles in the value it was built against. At startup, before `pavillion_start`, it compares the two. On a mismatch it prints `API version mismatch: core=<n> shell=<m>` to stderr and exits 1 before the window.
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
**Status:** Not started.

**Goal:** A shell and a core built from different contract versions refuse to start together, with a named error, instead of misbehaving.

**Entry gate:** D-3 complete. `make test` passes (23 tests at the time of writing).

**Work items:**

- [ ] `#define PAVILLION_API_VERSION 1` and `int pavillion_api_version(void);` in `core/include/pavillion.h`, implemented in `core/src/pavillion.c`.
- [ ] Shell: before `pavillion_start` (after the A-6 flags), compare `pavillion_api_version()` with the compiled-in `PAVILLION_API_VERSION`. On a mismatch, print `API version mismatch: core=<n> shell=<m>` to stderr, log it, and exit 1 before the window. This is the only new Rust, one comparison.
- [ ] Unity: `test_api_version_matches_header` (the function returns `PAVILLION_API_VERSION`, and it's ≥ 1).
- [ ] Do not add bindgen, libclang, or struct-size checks (FFI Lock).

**Done means:** A core whose version differs from the shell's stops the launch with a named error.

**Exit gate:** `make test` exits 0. In a scratch change, set the core's return value to 2 without rebuilding the shell's constant: the launch prints the mismatch and exits 1. Revert it. `make build` still shows the icons.

## F-5: Trim `config.c` with a helper layer

**Kind:** Medium
**Status:** Not started.

**Goal:** `core/src/config.c` (1,493 lines after the preview) gets a small helper layer before accounts, rules, and mail settings are added to it. No behavior change.

**Entry gate:** F-1 complete.

**Work items:**

- [ ] Measure the repetition: count jansson get, type check, range clamp, and default blocks. Write the count in Accomplished.
- [ ] Add `core/src/cfg.c` and `core/include/pav_cfg.h`: `cfg_obj(json_t*, key, create)`, `cfg_str(obj, key, def, out, len)`, `cfg_num_range(obj, key, def, min, max, step)`, `cfg_int_range`, `cfg_bool`, `cfg_enum(obj, key, const char *const *allowed, def)`, `cfg_set_*` writers, and a path helper `cfg_at(root, "preferences.ui.nav")`.
- [ ] Move the existing parse, ensure, read, and write functions onto the helpers. Split the file: `config.c` (index, profile, load, scan), `config_prefs.c` (preferences), `config_fs.c` (ensure and prepare; G-1 moves its OS calls behind the platform).
- [ ] Add Unity tests for each helper (`test_cfg_num_range_clamps_and_steps`, `test_cfg_enum_rejects_unknown`, and so on).

**Done means:** `make test` passes with every old test unchanged, and adding a profile field is one helper call plus a test.

**Exit gate:** `make test` exits 0 with the old test names all present. `wc -l core/src/config*.c` is reported in Accomplished and the three files together are smaller than 1,493 lines. `make dev` restores the saved layout as before.

## F-2: UI preferences as an opaque JSON blob

**Kind:** Medium
**Status:** Not started.

**Goal:** A new UI knob (a slider, a split, a font, a tab setting) touches only `ui/` and does not change a C header, a Rust struct, or a `PAV_PREFS_*` mask.

**Entry gate:** F-5 complete.

**Work items:**

- [ ] Core: `int pavillion_ui_get(char **json_out, char *err, size_t len)` and `int pavillion_ui_set(const char *json, char *err, size_t len)`. Set validates an object, at most 64 KB serialized, no forbidden key, and writes the profile on the core thread. Get returns the stored object. (G-2 turns these into the `prefs.ui.get` and `prefs.ui.set` commands.)
- [ ] Move zoom (`preferences.zoom` becomes `preferences.ui.zoom`), nav width, the three splits and their limits, icons, kit, and fonts under `preferences.ui`. `PavShellPrefs` shrinks to the window rectangle and `chrome`, which the shell applies before the page loads.
- [ ] Migration on load: a profile with the old `preferences.zoom` or fields outside `ui` is rewritten once into the new shape; schema stays 1. Unity test `test_profile_migrates_old_zoom_into_ui` reads a fixture copied from today's `default.json`.
- [ ] The page owns defaults and clamps: `ui/src/prefs.js` with one `DEFAULT_UI` object and `clampUi()`. Zoom range is `0.05`–`5` step `0.05` (B lock); the preview's `0.5`–`3` clamp is replaced.
- [ ] Persist the Fonts setting (`ui.fonts.body`, `ui.fonts.mono`), which today resets on launch.
- [ ] Debounce: the page writes at most every 300 ms and on `pagehide`.

**Done means:** The Fonts setting is persisted with no C or Rust change in that commit, and an old profile loads with its zoom and splits intact.

**Exit gate:** `make test` exits 0 including the migration test. In `make dev`: change the font, the nav width, and a pane zoom; quit; relaunch; all three are restored. `git diff` of the Fonts commit touches only `ui/`.

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
- [ ] Add Vitest to `ui/package.json`; `make test` runs `npm --prefix ui test`. Tests: `iconhosts.test.js` (both lists equal; each host in the CSP from `tauri.conf.json`), `iconlibs.test.js` (http refused, credentials refused, unknown host refused, `javascript:` refused, wrong type refused).
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
**Status:** Not started.

**Goal:** The sandboxed pane exists and is tested with hostile HTML in the release build before any real message is shown.

**Entry gate:** F-7 and G-8 complete (`pav-link` lives in the C `pav_protocol` table).

**Work items:**

- [ ] `ui/src/common/htmlpane.js`: `createHtmlPane(el)` returns `{ show(html, { allowRemote }), clear() }`. It builds the iframe with the Lock's attributes, wraps the body with the inner CSP meta and a base stylesheet (`color-scheme`, font, `max-width:100%` on images).
- [ ] Put one pane in the Mail detail placeholder, fed by a fixture behind a Help-menu item `Show isolation test` (debug builds and `--selftest` only).
- [ ] Fixture `tests/corpus/hostile.html`: `<script>`, `onerror`, `javascript:` link, `<meta http-equiv=refresh>`, `<base href>`, remote `img` and CSS `url()`, `<form>`, SVG with script, `<iframe>`, `<object>`.
- [ ] In the release build, from the frame: `window.__TAURI__`, `window.__TAURI_INTERNALS__`, and `parent.__TAURI__` are unreachable (no script runs, so the test is that nothing in the fixture changes the page title the parent watches, and that devtools' network panel shows zero requests). Record the steps in Accomplished.
- [ ] `src-tauri/capabilities/default.json` keeps `"windows": ["main"]` and gains no `remote` entry. Add a Vitest test (`capabilities.test.js`) that parses the file and asserts that. This is not a Rust test, per the G L3 Lock.
- [ ] Link clicks: the parent cannot see clicks in a script-free sandbox. N-2's core sanitizer will rewrite every `href` to `pav-link://<id>/<n>`. That URL shape is fixed here, and N-2 depends on it. This phase uses a hand-written fixture page that already carries such links; this phase adds `pav-link` to the C `pav_protocol` table under the scheme contract in the [G-LAYERS.md](G-LAYERS.md) L2 Lock. C emits `link.clicked` with the index and returns 204, and Rust only forwards, and the parent shows a confirm dialog. Prove it with a fixture link here.

**Done means:** The fixture renders as inert text and layout in `make build`, makes no network request, and a link click raises the parent's confirm with the real URL.

**Exit gate:** `make build` and run `dist/pavillion`: the isolation test shows the fixture, the dialog appears on link click, the network panel is empty, and `make test` exits 0.

## F-8: Icon settings: tree and editor

**Kind:** Medium
**Status:** Not started.

**Goal:** Andrew can find any icon by UI section, edit its HTML, compare it with the saved and default versions, and apply, revert, or cancel.

**Entry gate:** J-1 complete (menus, dialogs, toasts).

**Work items:**

- [ ] Settings → Icons: a tree of every registry entry grouped by `section` and labeled by `label`, with the current icon beside each label. A plain nested list with `role="tree"` now; it moves onto the E engine in E-7.
- [ ] Picking an icon opens an editor: a `<textarea>` with the HTML (monospace; CodeMirror HTML mode if H-2 has already brought CodeMirror in), and three side-by-side previews labeled **Current** (saved), **Default**, and **Preview** (the edited HTML), each at three sizes (1rem, 2rem, 4rem) on the panel surface.
- [ ] Preview refreshes only when the user clicks **Preview**, never on keystrokes. A sanitizer error shows under the field and the Preview box says it was rejected; what the sanitizer removed is listed as text (e.g. `removed: onerror on img; img`).
- [ ] Buttons: **Preview**; **Apply/Save** (sanitizes, stores the override through `prefs.ui.set`, updates every use of that icon live, toast with Undo); **Revert to default** (deletes the override, confirm); **Cancel** (discards edits, back to Current). Leaving with unsaved edits asks through the dialog.
- [ ] Search box filtering the tree by label or key; a badge on overridden icons; "Revert all" with confirm.
- [ ] Vitest: preview not refreshed on input; apply stores only the key; revert removes the key; cancel leaves the blob unchanged.

**Done means:** Change the Mail nav icon to an `fa-layers` stack, preview it, apply it, see it live in the nav, revert it.

**Exit gate:** `make test` exits 0. In `make build`: the flow above works; pasting the hostile suite's worst input previews as rejected and Apply refuses it; restart keeps an applied override.

## F-6: Decide where JMAP and sync live

**Kind:** Easy (a written decision)
**Status:** Superseded (2026-10-16). The decision is recorded in the [M-SYNC.md](M-SYNC.md) Lock: the C core owns JMAP and sync with libcurl and Jansson, on a network thread driving one curl multi handle, with the store on the core thread. Andrew can reopen it from the index's open questions.

## Working log

- 2026-10-15 Opened from a project review. Release-build icons were fixed by adding `'unsafe-inline'` to `script-src` and `font-src` for the Font Awesome hosts. Not a phase.
- 2026-10-16 Andrew's icon requirement replaced the kit-id-only rule: icon libraries as typed https URLs on an allowlist that the CSP carries (FA kit default, Material Symbols preset, Google Fonts hosts added), per-icon HTML through a shared sanitizer with a hostile suite, an icon registry, and the icon settings tree and three-preview editor. F-3 rewritten; F-7 and F-8 added.
- 2026-10-16 Scoped for implementation: entry and exit gates, the exact CSP, the HTML-sink rule and test, kit pinning, the preferences split, link handling through `pav-link`. F-5 moved ahead of F-2. F-6 superseded by the M-SYNC Lock.
- 2026-10-16 F-1 decided: the ABI check, not bindgen. With Rust minimized and G-8 deleting both structs, bindgen's libclang dependency isn't worth it. `pav-link` moves into C's `pav_protocol`.
- 2026-10-16 Review fixes: added a standalone pinned-kit-version line; `preferences.cache` and `sync` listed as core-read; FFI and scheme contracts point to the G L2 Lock; F-4 no longer depends on N-2 code (it fixes the `pav-link` URL shape); capabilities check is Vitest, not Rust.
- 2026-10-16 F-1 is now an API version check (`pavillion_api_version()` and `PAVILLION_API_VERSION`). The struct-size check was dropped because G-8 deletes both structs, and the bump rule is in the FFI Lock.

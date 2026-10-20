# D — UI

**Status:** D-3 complete (2026-10-14). D-4, D-6, and D-7 complete (2026-10-17). D-5 is not started. The shell preview below is not a phase.
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

The first frame, the shell preview, and the theme layer. The shell layout (nav, buttons, home tabs, window) is the [J-TABS.md](J-TABS.md) Lock. Security rules for the main window are the [F-HARDEN.md](F-HARDEN.md) Lock. The tree behind the mail and files navs is [E-TREE.md](E-TREE.md). Mail, contacts, calendars, tasks, files, and notes have their own letters (N, O, S, T, X, U, Y). Argent is Z and is an icon only.

## Phases

One phase is one session. Do not start the next one in the same conversation.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| D-1 | Frame and theme variables | Easy | Complete |
| D-2 | `api.js` and status text | Easy | Complete |
| D-3 | The error state | Easy | Complete |
| D-4 | Theme switch: semantic variables, dark, light, follow system | Medium | Complete |
| D-6 | Theme catalog: files, the list, and fallback | Medium | Complete |
| D-7 | Theme skin: fonts, icons, mark, and HTML in CSS | Medium | Complete |
| D-5 | Theme extras: density, gradient tuning, the contrast audit | Medium | Not started |
| D-8 | Settings → Formats: app-wide display formats through Luxon (before N-3) | Medium | Not started |
| D-9 | Icon Wizard (DRAFT spec, docs only): up to 3 layered icons or images per icon, stored as an HTML snippet | Medium | Not started |
| D-10 | Module top bars (DRAFT spec, docs only): Accounts Clone and search, the Help bar with search and Back | Medium | Not started |

Kind: **Easy** is the static frame or one function in `api.js`. **Medium** crosses one boundary. **Hard** is unused.

## How a session works

1. Read Resume in [PAVILLION_PLAN.md](../PAVILLION_PLAN.md), the Status column above, and the previous phase's Status, Accomplished, Lessons learned, and Handoff.
2. Re-read only this phase: Goal, Work items, Done means, Exit gate. Open the lock below when the phase cites it.
3. Ask the qualifying questions. Wait for approval before editing source.
4. Do this phase only. Check a work item `[x]` only after its check has passed. Defer with `[~]`, one line, and the phase it moves to.
5. Before marking complete, write Status, Accomplished, Lessons learned, and Handoff, and append the Working log. Before that, update `ui/src/help/pages.js` for what this phase changed: augment, replace, fix, or extend the user page, or add a Developer section when the user page has nothing new. Then stop.
6. A sitting that ends early sets Status to **in progress**, names the last finished work item, and leaves the rest unchecked.

Leave Accomplished, Lessons learned, and Handoff off a phase that has not started.

A UI exit gate is the window, not a screenshot stored in the repo. `make test` must still pass if the phase touched the shell.

The frame diagram below is the hello-world page. The shell preview replaced that page on the same day. The layout is the [J-TABS.md](J-TABS.md) Lock.

## Lock

- ES modules, HTML, and CSS variables. Vite serves them. No framework and no CSS library.
- `ui/src/api.js` is the only module that imports Tauri until G-3, which moves that import to `ui/src/transport/tauri.js`. A screen calls functions in `api.js` and does not call `invoke`.
- The UI does not read `Pavillion.json`. Status comes from `pavillion_status`.
- Mail, contacts, and files lists use Tabulator, themed on `--pav-*`, with text formatters (N-3, and the same module in S and U). The mailbox tree is hand-built (E). The activity table is a plain DOM table (I-2). Calendar grids stay plain DOM. No other table library.
- The message pane, when it exists, is a sandboxed iframe with no script and no bridge. The shell does not put IPC on `window` inside a child frame.
- One window. (A browse tab in [J-TABS.md](J-TABS.md) J-7 is a child webview inside it, not a second window.)
- Times are 24 h (`HH:mm`) by default everywhere, dates `2026-10-14` or `Oct 14` (`preferences.time_format`). Default time zone `America/Vancouver`.
- Accessibility: full keyboard navigation, visible focus rings, ARIA roles on lists, trees, and grids, contrast at least 4.5:1 in every theme, `prefers-reduced-motion` respected, font scaling through the zoom settings.
- CodeMirror 6 is the log viewer ([H-LOGGING.md](H-LOGGING.md) H-2) and the source editor for HTML and Lua (F-8, R-4). The composer is SunEditor in the sandboxed frame ([O-COMPOSE.md](O-COMPOSE.md) O-2), plus a plain-text mode. H-2, N-3, and O-2 install those libraries. This document does not.
- The main window runs the Font Awesome kit, which needs `'unsafe-inline'` scripts. Outside HTML never goes into the main DOM; use `textContent` or the message pane. Icon libraries are typed https URLs on an allowlist, and every icon snippet passes the shared icon sanitizer. See the Icons section of the [F-HARDEN.md](F-HARDEN.md) Lock.

### Frame

```
+--------------------------------------------------+
| Pavillion                          Default       |
+--------------------------------------------------+
|                                                  |
|  ready                                           |
|                                                  |
+--------------------------------------------------+
```

The title row is the word Pavillion and the profile `name`. The body is the state. On `error`, the body is the error string and the profile name is blank if none loaded.

```css
:root {
  --bg: #1c1c1c;
  --fg: #e8e8e8;
  --muted: #9a9a9a;
  --accent: #2a7;
  --border: #333;
  --font: system-ui, sans-serif;
}
```

Dark only, until a preferences screen exists. The OS window title is `Pavillion`. Contrast of `--fg` on `--bg` is at least 4.5:1.

### api.js

Hello world exports `status()`, invoking the Tauri command `status` and returning `{ profileName, profileId, state, error }`. Wire names are camelCase. The C struct stays snake_case. Later functions are added to this module one at a time. A screen does not register its own Tauri listener. Event names already listed in the plan stay the names, and none are emitted in this document.

The shell layout is in the [J-TABS.md](J-TABS.md) Lock. Changing the active profile is a restart, as [B-CONFIG.md](B-CONFIG.md) says. J-9 adds `--pav-icon-tasks`, `--pav-icon-notes`, `--pav-icon-argent`, and `--pav-icon-github` beside the keys D-7 shipped. D-7's accomplished list of eight keys is the set that phase shipped. The living set is the J Lock.

### Theme files

D-6 and D-7, pulled forward on 2026-10-17. A theme file is CSS. The live shell reads `color-scheme`, `--pav-*`, and an uncommented `@font-face` whose `url()` is a local file. Other rules stay in the file and are not copied onto the window.

Built-in Dark and Light stay embedded. System follows `prefers-color-scheme`. When that query is unavailable, the resolved theme is Dark. A missing saved choice, a missing file, or a file the core cannot use paints System, then the desktop scheme, then Dark. The saved id stays until the user picks a row.

`*.theme` files sit in the working directory beside `Pavillion.json`, or in the single folder named by `themes_folder` on the index (see the [B-CONFIG.md](B-CONFIG.md) Lock). The folder is one level deep. Every profile in that working directory shares it. The page does not read the index. The commands are `themes.list`, `themes.ensure`, and `themes.folder.set`. The list is read at startup, when Settings → Theme opens, and from Refresh. There is no watcher. A missing or unreadable folder shows the built-ins, does not fail startup, and does not change the current theme. The folder field is blank when `themes_folder` is unset. Its placeholder is `Default:` plus the working directory from `themes.list`'s `fallback`. `chosen` is the stored string, or empty. The field shows `chosen`, not the resolved `folder`. Selecting a theme or Refresh repaints colors, fonts, icons, and the mark in place. Open tabs stay. A selected file's icons win while that file is selected, including when Icons source is Override. Light, Dark, and System keep the profile icons while Override is on. Default, Undo, and Redo on the Theme page walk theme choices only and do not change zoom. The bottom bar starts with Refresh.

`Pavillion Dark.theme` and `Pavillion Light.theme` are written when that name is absent, including after a delete. A file that is already there is left as it is. Those two names are hidden when the parsed `--pav-*` and `color-scheme` values match the built-in text. Comments and spacing do not count as a difference. A value change of either name is listed with ` Custom` after the title. Any other filename is listed even when the values match a built-in. After an update, an old starter that no longer matches appears as Custom. Deleting it writes the new template.

The header is one CSS comment. `Title:`, `Author:`, `Version:`, and `Released:` are the only keys, one per line, inside the first 10 lines. Display text is clipped at 20 characters and tags are stripped. Hover shows the full text. A missing title uses the filename. Any other missing field reads `Not found`, so every row stays three lines. The row name is the title. Clip the title first, append ` Custom` when that starter was edited, then append ` (filename.theme)` when that visible label is duplicated. Pinned Light, Dark, and System have no filename, so a file titled Light becomes `Light (Other.theme)` and the pinned row stays `Light`.

`preferences.theme` stays `dark`, `light`, or `system`. The core field is 16 characters and cannot hold a filename. A file choice is `preferences.ui.themeChoice`, shaped `file:Name.theme`. Choosing a file writes core theme `dark` and sets `data-theme="file"` on `.pav-root`, so `light.css` does not cover the file. The host attribute is `light` or `dark`, from the first ident of the file's `color-scheme`. An unknown stored theme word stays `system`. A `file:` id is not passed through that normalizer.

The list order is Light, Dark, System, a divider that is not a choice, then files sorted by title. The divider stays when the file section is empty. The whole row is the radio. The preview is a 1024×768 poster in an iframe with an empty `sandbox`, scaled so its height matches the three lines. It draws nav, tabs, list, and detail from the theme's own variables. It does not run the icon kit, so a Font Awesome glyph shows on the real window and not in the poster. The System poster follows the desktop scheme.

The two starter files are commented templates of every variable the shell reads, with the public kit id `45801522ef`. A new variable ships in both files in the same change. `@font-face` in those files is a comment until someone wants a real face. Icon values may be HTML (`<i class='...'>` in a double-quoted custom property), a Font Awesome class string, or an image filename. The shell accepts one `i` (class only) or one `img` whose source is `pav-theme:`, `data:image`, or a basename rewritten to `pav-theme:`. The roof mark is a filename in `--pav-mark`, or `none`. The shell owns `<img class="nav-mark">` and shows it when the value names a file.

Fonts and Icons each have Source `Theme` or `Override`. Theme paints from the file and removes the inline font properties. Override keeps `preferences.ui.fonts`, `preferences.ui.icons`, and the kit. A profile that already customized fonts or icons and has no source key is treated as Override.

Sibling images and fonts load through `pav-theme:`. That scheme is on `img-src` and `font-src` only. Theme text is capped at 256 KB. An asset is capped at 2 MB. The name is one basename.

### Later, not a phase in this document

Settings is a screen, not a popup. Its button sits on the bottom of the nav, with Activity, Update, and Help. The first control is zoom: one scale for the shell, stored as `preferences.zoom.shell`. (Superseded 2026-10-16: Shell is 50%–200% in steps of 5%. The other rows are 5%–500% in steps of 5%, per the B Lock. F-2 moved zoom under `preferences.ui.zoom`.) Each later panel gets its own key and its own scale. The window applies `preferences.window` on startup and writes it back on move, resize, and quit. `chrome: "hidden"` turns off the system decorations. The empty part of the tab strip is the drag region, and the icon buttons stay clickable. Default remains `system` until that setting exists. Layout the user can change is `preferences.ui` in the [B-CONFIG.md](B-CONFIG.md) Lock.

Activity is a tab opened from the bottom of the nav, including while Settings is open. It is a read-only view of the core log: refresh, scroll to the newest line on open, monospace, and a font size for that view. Times on screen are local `HH:mm:ss.mmm`. The file format stays the millisecond UTC line in [C-CORE.md](C-CORE.md). The view asks the core for the text. It does not open the log file itself.

CodeMirror 6 is the log viewer (H-2) and the source editor for HTML and for Lua (F-8, R-4). The composer is SunEditor in the sandboxed frame (O-2), plus a plain-text mode. None of the three is installed in this document. Icons are the Font Awesome Pro Plus 7.3.1 SVG kit (`https://kit.fontawesome.com/45801522ef.js`). Duotone, Sharp, and Thin are the families to use. Mail keeps the roof mark. The kit hosts are allowed by the CSP in the [F-HARDEN.md](F-HARDEN.md) Lock. The kit is used as-is, and Pro files are not vendored. The shell preview loads the kit. This phase did not.

D-1 keeps the five variables in the lock above. A later theme phase follows Lithium: dark-first semantic custom properties, and a theme is an override of those properties. `preferences.theme` names `dark.css` or `light.css`. The control is the Theme heading under Settings. The shell has no theme icon. Gradients live in those files. `preferences.ui` does not store backgrounds. Pavillion does not import Lithium's stylesheets or fonts. The shell preview links `dark.css` directly and does not read `preferences.theme`.

## D-1: Frame and theme variables

**Kind:** Easy
**Status:** Complete (2026-10-14). No variances. The profile slot and the body are empty. The word `ready` is not in the page. D-2 fills both from `status()`.

**Goal:** The window from A-3 shows the lock's frame, still with static text.

**Entry gate:** A-3 complete.

**Work items:**

- [x] Add `ui/styles/theme.css` with the lock's variables.
- [x] Mark up the title row and the body. Static text may read Pavillion and a placeholder body.
- [x] Use `rem` and the system UI font. No second stylesheet.

**Done means:** `make dev` shows the frame, the word Pavillion, and the dark background.

**Exit gate:** `make dev` opens that window. Close it and the process exits. Passed. The window was 1200 by 800, dark, with Pavillion in the title row and a line under that row. The body was empty. Closing it with Alt+F4 made `make dev` exit 0.

**Accomplished:** `ui/styles/theme.css` holds the lock's variables. `ui/index.html` is the title row and an empty body, and it links that one stylesheet. Padding and type use `rem`. The font is `system-ui`.

**Lessons learned:** `--accent` is defined and not painted yet. The hairline under the title is `1px`. A normal close exits 0. Destroying the X window from outside can make GTK report `BadWindow`, and then `make dev` exits non-zero.

**Handoff:** D-2 writes `profileName` into `#profile-name` and `state` into `#status`, once, from `api.js`. Do not put the word `ready` in the HTML. A-4 provides the command those elements display. The flag check in `main.rs` stays in front of the window.

## D-2: api.js and status text

**Kind:** Easy
**Status:** Complete (2026-10-14). No variances.

**Goal:** The body and the profile name come from `status()`, once.

**Entry gate:** A-4 and D-1 complete.

**Work items:**

- [x] `api.js` exports `status()` and is the only file that imports Tauri.
- [x] `main.js` calls it once after load and writes `profileName` and `state` into the frame.
- [x] There is no poll loop.

**Done means:** With the A-4 stub, the body shows `ready` and the name is blank.

**Exit gate:** `make dev` shows that. `make test` exits 0. Passed. The title row showed Pavillion and no profile name. The body showed `ready`. `make test` printed `OK`. Closing the window made `make dev` exit 0.

**Accomplished:** `ui/src/api.js` is the only module that imports `@tauri-apps/api`. `ui/src/main.js` calls `status()` once and writes `profileName` into `#profile-name` and `state` into `#status`.

**Lessons learned:** The stub's empty profile name leaves the right side of the title row blank. That is the D-2 result, not a missing element.

**Handoff:** D-3, when its entry gate is met, fills the body with `error` and clears the name when `state` is `error`. The next phase in the sequence is C-1. Do not add a poll. Do not call `pavillion_start` from the page.

## D-3: The error state

**Kind:** Easy
**Status:** Complete (2026-10-14). No variances.

**Goal:** A failed start shows the error string and a blank profile name.

**Entry gate:** A-5 complete.

**Work items:**

- [x] When `state` is `error`, the body is `error` and the profile name is cleared.
- [x] When `state` is `ready`, D-2's rendering is unchanged.
- [x] Confirm by pointing `make dev` at a working-directory path that is a file, which the core rejects.

**Done means:** That launch shows the core's error text and does not show `ready`.

**Exit gate:** `make dev DEV_DIR=/tmp/pav-file-DnKIGI` showed Pavillion, a blank profile name, and `error`. The word ready was not on the page. The same build on `/tmp/pav-dev-W5Ep5Q` showed Default and ready. `make test` exits 0 (21 tests). Closing the window exited 0. The file used as a directory was left unchanged.

**Accomplished:** `main.js` still calls `status()` once. When `state` is `error`, `#profile-name` is cleared and `#status` shows that state. The word `ready` is not in the HTML, CSS, or JS.

**Lessons learned:** The body shows the state word from the core. The longer message, such as `cannot create .../profiles`, stays in the status `error` field and is not drawn. A ready launch still shows the profile name.

**Handoff:** The hello-world sequence is complete. The page this phase drew was replaced the same day by the shell preview below. That preview is not a phase.

## D-4: Theme switch

**Kind:** Medium
**Status:** Complete (2026-10-17). Andrew accepted the look the same day: the themes look right and the switcher is fine. The catalog and the skin he asked for next are D-6 and D-7.

**Goal:** Every surface reads semantic `--pav-*` variables, and `preferences.theme` switches dark, light, and system live. The shell preview already has a Theme setting and a `dark.css`, and screens built after G-4 should be built on variables from the start.

**Entry gate:** G-4 complete (variables namespaced `--pav-*`, styles scoped).

**Lock for this phase (was old plan §10 Theming):**

- Dark first. The theme layer follows Lithium (`docs/Li/LITHIUM-CSS.md`): semantic custom properties (`--pav-surface-*`, `--pav-text-*`, `--pav-accent`, `--pav-border`, `--pav-status-*`), dark defaults, and a theme is an override of those properties. No UI framework, no hard-coded colors in components, no import of Lithium's stylesheets or fonts.
- `preferences.theme` names `dark`, `light`, or `system`. `system` follows `prefers-color-scheme`. The control is the Theme heading under Settings; the shell has no theme icon.
- `ui/styles/theme.css` holds the rules and only references variables. `dark.css` and `light.css` assign the same names. A surface may be a gradient: one angle, black in one corner, a very dark red in the other, taken from the roof mark; panels and buttons differ by how far the red reaches. Active and inactive are two variables. `preferences.ui` stores layout only, never backgrounds.
- Density `compact` or `comfortable` (`preferences.density`) is a handful of custom properties (row height, padding, icon size). E-TREE's third density, spacious, is recorded and not promised.
- Fonts: the system UI font by default, Vanadium Sans when chosen; monospace (Vanadium Mono) for the headers view and the log.

**Work items:**

- [x] Audit every color literal in `ui/styles` and `ui/src`; move each to a semantic variable. Keep `--pav-log-*` from H-2, and give light mode its own values for them.
- [x] `light.css` assigning the same names (a first pass. D-5 tunes it).
- [x] `preferences.theme` (`dark`, `light`, `system`) through `prefs.set`. The Theme setting in Settings now persists. `system` follows `prefers-color-scheme` live.
- [x] Vitest: `theme-vars.test.js` checks that `dark.css` and `light.css` define the same variable names.

**Done means:** Switching dark, light, and system changes every surface live, with no hard-coded color left.

**Exit gate:** `make test` exits 0. `rg '#[0-9a-fA-F]{3,6}' ui/src` finds none outside theme files. In the window, all three modes switch live and survive a restart. Passed. Andrew accepted that look on 2026-10-17.

**Accomplished:** Served sheets are `ui/public/styles`. `dark.css` stays on `.pav-root`. `light.css` overrides `.pav-root[data-theme="light"]` and `html[data-pav-theme="light"]`. `theme.css` only references variables. Names are `--pav-text`, `--pav-text-muted`, `--pav-surface-canvas`, `--pav-surface-scrim`, and `--pav-status-error`, `--pav-status-warn`, `--pav-status-ok`. Surfaces, accent, border, swatches, icon glow, and `--pav-log-*` stayed. Light is warm paper (`#f7f4f2`, `#1c1212`, `#6b5e5e`, `#d9cfcf`) with the same 160° roof wash and accent `#2a7`. `preferences.theme` is `dark`, `light`, or `system`. `system` follows `prefers-color-scheme` live. An unknown stored word is `system`. The host page sets `data-pav-theme` to the resolved `light` or `dark`. Mount does not restyle `documentElement`. The isolation frame stays `color-scheme: dark`. The first paint can be dark until `prefs.get` returns. `PAVILLION_API_VERSION` stays 3. D-6 replaced the Theme select with the radio list in the same sitting.

**Lessons learned:** Custom properties inherit, so a declaration on `.pav-root` beats a harness `:root` color. `color-scheme` on `.pav-root[data-theme="light"]` beats the one on `.pav-root`. A Vitest `matchMedia` that answers every query makes CodeMirror's print listener throw. Answer only `(prefers-color-scheme: light)`. The color audit that matters is `ui/public/styles` plus `rg '#[0-9a-fA-F]{3,6}' ui/src`. `ui/styles` is the unlinked copy and was left alone. The bar strut, `--pav-bar-zoom`, and the 1px toolbar rules stayed.

**Handoff:** D-6 and D-7 are in progress in this file. He pulled both forward on 2026-10-17. Do not start J-1 until he accepts that window. The Theme control is the list, not the select. Leave the strut and the 1px rules. Do not add `https:` to `img-src` or `navigate-to`. `lib.rs` stays a 199-line forwarder. `pav-theme` is already on its scheme list.

## D-6: Theme catalog

**Kind:** Medium
**Status:** Complete (2026-10-17). Andrew accepted the window the same day. Pulled forward ahead of J-1. The sequence number is step 41, after J-6 and before milestone 1.

**Goal:** Settings → Theme lists built-in Light, Dark, and System, then the `*.theme` files for this working directory, and applying a row paints and saves that choice.

**Entry gate:** D-4 complete.

**Work items:**

- [x] `themes.list`, `themes.ensure`, and `themes.folder.set`, with `State: real` in [API.md](API.md). `PAVILLION_API_VERSION` stays 3.
- [x] `themes_folder` on the index. Empty means the working directory. A missing folder returns `missing: true` and does not fail startup.
- [x] Write `Pavillion Dark.theme` and `Pavillion Light.theme` when absent. Hide those two names when the parsed values match. Show ` Custom` when they do not. List every other filename.
- [x] Header keys in the first 10 lines. The row name is the title. A duplicate label gains the filename in brackets. Clip at 20 characters. Strip tags. Missing fields read `Not found`.
- [x] Radio rows, a divider, and a sandboxed 1024×768 poster. Folder field and Refresh. Rescan on startup and when Theme opens.
- [x] `preferences.ui.themeChoice` holds `file:Name.theme`. Core `preferences.theme` stays `dark`, `light`, or `system`. A file selection underlays `dark` and sets `data-theme="file"`.
- [x] Andrew's window check.

**Done means:** The list matches the files on disk, a choice survives a restart, and a missing file paints System without erasing the saved id.

**Exit gate:** `make test` exits 0. In the window: Light, Dark, and System still switch and the notes read `Light is in use.`, `Following the system: dark.`, and `Following the system: light.`. A copied theme appears under the divider, selecting it paints the window, and Refresh after a delete returns to the built-ins. Andrew accepted the window on 2026-10-17.

**Accomplished:** `themes.ensure`, `themes.folder.set`, and `themes.list` are real. `PAVILLION_API_VERSION` stays 3. `themes.list` returns `folder`, `chosen`, `fallback`, `themes`, and `missing` when the folder is gone. `chosen` is the stored string. `fallback` is the working directory. The folder field is blank when `themes_folder` is unset, and its placeholder is `Default:` plus `fallback`. Browse uses `host.dialog.open({ directory: true })`. Cancel leaves the field. An empty field clears `themes_folder`. A path that is not a directory is `bad_args` and the message contains `not a directory`. `Pavillion Dark.theme` and `Pavillion Light.theme` are written only when that name is absent. Those two names hide when the parsed `--pav-*` and `color-scheme` values match, and a real edit shows with ` Custom`. Any other filename is listed even when the values match. The row title comes from `Title:` in the first 10 lines, clipped at 20, tags stripped, then ` Custom`, then the filename in brackets when that visible label is duplicated. A missing title uses the filename. Any other missing field reads `Not found`. The list is Light, Dark, System, a divider, then files by title. The poster is a 1024×768 iframe with an empty sandbox. `preferences.ui.themeChoice` holds `file:Name.theme`. Choosing a file writes core theme `dark` and sets `data-theme="file"`. A missing file paints System and keeps the saved id. The list is read at startup, when Theme opens, from Refresh, and after a successful choice. There is no watcher. Theme Default selects System. Undo and Redo walk theme choices and do not change zoom. `make test` is 68 Unity tests and 44 Vitest tests.

**Lessons learned:** The field shows `chosen`, not the absolute `folder`. `Default:` and the path have no space between them. `clampUi` drops a key that is listed in `KNOWN` and not copied onto `out`, so `themeChoice`, `fontSource`, and `iconSource` are assigned only when valid and a missing key stays missing. Setting `checked` in script does not fire `change`. A jsdom click on a disabled button does not fire. `flushUiSave` chains on one promise and skips the write when `stopShell` has replaced the api. A failed write does not cancel the next one. The choice loaded at startup is not pushed onto the undo stack. When `prefs.set` fails, the previous choice and both stacks go back.

**Handoff:** D-7 closed in the same sitting. The next phase is J-1 in [J-TABS.md](J-TABS.md). Do not start it here. D-5 stays at step 114. Leave the D-4 handoff as written. `lib.rs` stays 199 lines. Do not add `https:` to `img-src` or `navigate-to`.

## D-7: Theme skin

**Kind:** Medium
**Status:** Complete (2026-10-17). Andrew accepted the window the same day. Pulled forward the same day. The sequence number is step 106, after F-8. F-8 still owns the icon editor and will replace this painting later.

**Goal:** A theme file is the skin: colors, fonts, the roof mark, and the eight nav icons, including an HTML icon in a custom property. Layout does not move.

**Entry gate:** D-6 in the tree. Sequenced after F-8. He asked for the skin before that editor.

**Work items:**

- [x] Live shell receives `color-scheme`, `--pav-*`, and uncommented local `@font-face` rules, injected as `#pav-file-theme` and removed on unmount. Remote `url()` values are emptied. A short file inherits Dark.
- [x] `ui/public/styles/pavillion-dark.theme` and `pavillion-light.theme` are commented templates. Parsed values match `dark.css` and `light.css`. The kit id `45801522ef` is in the file.
- [x] `--pav-icon-*` may be HTML, a class string, or an image filename. The eight keys are mail, contacts, calendars, files, activity, settings, update, and help. One `i` or one `img` is accepted. The nav `.icon` element is replaced.
- [x] Fonts and Icons Source is Theme or Override. Theme removes inline `--pav-font` and `--pav-font-mono`. A profile that already changed fonts or icons migrates to Override.
- [x] `<img class="nav-mark">` uses `--pav-mark`. `none` hides it. A filename becomes `pav-theme://` and the image uses `is-shown`.
- [x] `pav-theme` in `pav_protocol` and on the `lib.rs` scheme list. `img-src` and `font-src` allow `pav-theme:`. Theme reads cap at 256 KB. Assets cap at 2 MB. Basename only.
- [x] Andrew's window check.

**Done means:** Editing a starter's colors, an icon HTML value, or `--pav-mark` changes the window after Refresh, and the poster shows the colors. The poster does not show Font Awesome glyphs.

**Exit gate:** `make test` exits 0. `rg '#[0-9a-fA-F]{3,6}' ui/src` finds none. In `make dev` or `make build`, the checks in the try-list pass. Anything that depends on the new CSP scheme is confirmed in `make build`, because `make dev` does not apply `tauri.conf.json` CSP. Andrew accepted the window on 2026-10-17.

**Accomplished:** The live shell takes `color-scheme`, `--pav-*`, and an uncommented `@font-face` whose `url()` is a local file, rewritten to `pav-theme:`. A remote `https:` or `data:` url that is not a local filename is emptied. A short file inherits Dark for properties it does not declare. The sheet is `#pav-file-theme` on `document.head` and is removed in `stopShell`. The poster does not load the kit. An icon value may be HTML, a Font Awesome class string, or an image filename. The eight keys are mail, contacts, calendars, files, activity, settings, update, and help. The shell accepts one `i` (class only) or one `img` and always replaces the button's `.icon` node. Update is `#update .icon`. While a file is selected, that file's declared icons and `--pav-mark` paint even when Icons source is Override. An undeclared icon key restores `data-icon-definition`. Fonts Source Override still replaces the theme fonts. A profile with no `fontSource` whose fonts differ from the defaults, or no `iconSource` whose `icons` object has keys, migrates to Override. Switching to Override copies the computed values. `<img class="nav-mark">` shows with class `is-shown`. `none` hides it. `pav-theme` is served by `pav_protocol` and is on the `lib.rs` scheme list. CSP allows it on `img-src` and `font-src` only. Theme text caps at 256 KB and an asset at 2 MB. The name is one basename. `make dev` does not apply `tauri.conf.json` CSP, so a `pav-theme:` image is confirmed in `make build`. `make test` is 68 Unity tests and 44 Vitest tests. `rg '#[0-9a-fA-F]{3,6}' ui/src` finds none.

**Lessons learned:** WebKit's `SVGElement.className` is read-only. Font Awesome replaces `<i class="icon">` with `<svg>`, and assigning `className` throws `Attempted to assign to readonly property.` Replace the node. That throw used to skip the save, so the theme looked applied and was forgotten after a restart. `paintTheme` swallows the paint error and the save still runs. A theme choice repaints in place and does not call `location.reload`. Changing the kit id still reloads, from `reloadIcons` in `main.js`. The host hears `light` or `dark` from the file's first `color-scheme` ident, and the same value is not sent twice. `getPropertyValue` may return quotes; strip one matching pair. `iframe.sandbox` is a read-only token list; set the attribute to an empty string. The shared icon sanitizer is still F-7.

**Handoff:** J-1 is next, in a fresh session. Do not start it here. D-5 is step 114. Its entry gate is met, and the sequence still runs J-1 first. F-8 still owns the icon editor and will replace this painting. A new `--pav-*` variable ships in both starter files in the same change. `@font-face` in the starters stays a comment. Do not send `file:` through `normalizeTheme`. Do not rewrite a stored `iconSource` on load. `lib.rs` stays a 199-line forwarder. `pav-theme` is already on its scheme list.

## D-5: Theme extras

**Kind:** Medium
**Status:** Not started.

**Goal:** Density, gradient tuning, and a checked contrast floor, on top of D-4's switch.

**Entry gate:** D-7 complete. Density, gradient tuning, and the contrast audit stay late. D-6 and D-7 were pulled forward; this phase was not.

**Work items:**

- [ ] Density `compact` or `comfortable` (`preferences.density`, per the D-4 Lock) in Settings → Theme.
- [ ] Gradient tuning for both themes (roof-mark angle, how far the red reaches).
- [ ] Contrast at 4.5:1 for text pairs: a Vitest that parses both theme files and computes contrast for the listed pairs.

**Done means:** Both densities and both themes look right and pass the contrast test.

**Exit gate:** `make test` exits 0 including the contrast test. In `make build`, all three modes and both densities look right.

## D-8: Settings → Formats

**Kind:** Medium
**Status:** Not started. Added 2026-10-19 at Andrew's request. The same day he moved it ahead of N-3 in the sequence; it keeps step number 132.

**Goal:** One place, Settings → Formats, where the user sets how dates and times are shown everywhere in the app (date, time, datetime, and the other display formats below), with every conversion done by Luxon. Storage, the wire, and exports do not change: UTC ms and ISO 8601 stay the stored and exported forms.

**Entry gate:** Q-1 complete (the row before it in the sequence). D-8 comes before N-3 so the message list (N-3), the calendar views (T-3), tasks (X), notes (Y), and every later screen use `formats.js` from the start. Until it lands, only the H-4 log view and the activity list format times with their own fixed 24 h code in the profile zone; D-8 switches them.

**Work items:**

- [ ] Add `luxon` as an npm dependency of `ui/` (pinned, bundled by Vite; record the gzip cost of `ui/dist` in Accomplished). No other date library.
- [ ] `ui/src/formats.js`: the only module that turns a stored instant into display text. `formatInstant(ms|iso, kind)` for `kind` in `date`, `time`, `timeSeconds` (`HH:mm:ss.SSS`, the log view), `datetime`, `dateShort` (message list), `weekday`, `relative` (Luxon `toRelative`), and `duration`. It reads the profile zone (`prefs.get` `timezone`, default `America/Vancouver`, else the system zone) and the profile locale.
- [ ] Defaults are 24 h: `date` `yyyy-MM-dd`, `time` `HH:mm`, `timeSeconds` `HH:mm:ss.SSS`, `datetime` `yyyy-MM-dd HH:mm`. The existing profile `time_format` (`24h`, `prefs.set` `timeFormat`) stays the 12/24 h switch; a format string chosen in the pane wins over it.
- [ ] Where the formats live: proposed `preferences.ui.formats` (one key per kind, Luxon format strings), validated in the UI with Luxon before save. Lock entry in this file and in [B-CONFIG.md](B-CONFIG.md) before code.
- [ ] Settings → Formats pane: one row per kind with the format field, a live preview of now and of a fixed sample instant, a Reset per row, and a zone picker that writes `timezone` (IANA names from `Intl.supportedValuesOf("timeZone")`).
- [ ] Switch every screen to `formats.js`: the log view (H-4 commit 4 shows local 24 h `HH:mm:ss.SSS` in the profile zone with its own code; it moves to `timeSeconds`), the activity list, and any screen already using `Intl.DateTimeFormat` or a hand-written stamp. Later phases (N-3 message list, T calendar views, X tasks, Y notes) call `formats.js` and never format a date themselves.
- [ ] Help: a Formats page under Settings that lists the Luxon format tokens (`yyyy`, `yy`, `MM`, `M`, `MMM`, `MMMM`, `dd`, `d`, `EEE`, `EEEE`, `HH`, `H`, `hh`, `h`, `mm`, `ss`, `SSS`, `a`, `ZZ`, `ZZZZ`, `z`, quoting with `'...'`) with an example for each, and links to the Luxon formatting docs.

**Tests:**

- Vitest `formats.test.js`: each kind's default against fixed instants in `America/Vancouver` (both sides of a DST change) and `UTC`; 24 h by default; a custom format string; an invalid format string is refused with a message; the zone fallback order.
- Vitest: the Formats pane saves, previews, and resets; the log view re-renders on a format change.
- A bypass-style Vitest that fails on `toLocaleString`, `toLocaleTimeString`, `Intl.DateTimeFormat`, or a new date formatter outside `formats.js` (same pattern as `bypass.test.js`).
- `make test` exits 0; the window check covers the pane and the log view.

**Done means:** Changing a format or the zone in Settings → Formats changes every date and time on screen at once, the defaults are 24 h in the profile zone, and Help lists the tokens.

**Exit gate:** `make test` exits 0. In `make build`, the pane and two screens (the log view and the activity list) follow a changed format and zone.

**Open questions for Andrew:**

- D-Q2: `preferences.ui.formats` (UI blob, no core change) or core-validated profile fields like `time_format`?
- D-Q3: which kinds beyond date, time, and datetime (relative times, durations, first day of week, number and size formats)?
- D-Q4: a locale setting separate from the system locale?
- D-Q5: do exports and copy stay ISO UTC with `Z` (the H-4 choice) or follow the display format?

## D-9: Icon Wizard (DRAFT)

**Kind:** Medium
**Status:** Not started. DRAFT spec, docs only; no code in this phase until Andrew signs off the open questions. Added 2026-10-19 at Andrew's request (renamed from "icon picker" the same day).

This records Andrew's ideas. It does not decide the look: layout, sizes, spacing, control styles, previews, and animation are his to set (D-Q6 onward).

**Goal:** One popup, the Icon Wizard, that composes an icon and returns it to the caller. Used from Settings → Accounts (the account icon) and from a Settings → Icons page (see D-Q11); later folders and labels.

**What it does (Andrew's ideas):**

- Up to 3 layers, drawn on top of each other.
- Each layer has its own color, flip horizontal, flip vertical, rotate, scale, and translate.
- A layer is a Font Awesome icon or an imported image (SVG, PNG, or JPG, for example a headshot), with the same transforms.
- Choosing a Font Awesome layer opens a search popup inside the wizard: a search field; a family toggle or pull-down (Classic, Sharp, Duotone, Sharp Duotone, Brands, and any other family the installed Font Awesome provides); a style (Solid, Regular, Light, Thin); then a grid of results.
- The result replaces the interim account icon value (K-Q9), which already reserves the field for a composed icon.

**Decisions (Andrew, 2026-10-19):**

- Output: a Font Awesome composition is a self-contained HTML snippet: Font Awesome layers markup with each layer's classes, color, and transforms inline. That snippet is what is stored and what is rendered.
- Account storage: the account `icon` field holds the snippet, or a reference to it (K-Q9). The interim chooser already stores a one-element snippet, so its values stay valid.
- Imported images live in a separate asset store, designed in this phase: likely SQLite blobs per profile, with no files outside the database. This is a new storage element to track beside the profile and the store (B Lock, when it is designed).
- Color: one better color picker than the native `<input type="color">`, shared by the wizard and the account color. It is bundled from npm with a permissive licence (MIT, ISC, BSD, or Apache-2.0), no CDN.

**What is installed today (checked 2026-10-19):** the Font Awesome Pro Plus 7.3.1 SVG kit `45801522ef`, loaded from `kit.fontawesome.com` (D Lock); no `@fortawesome` npm package and no Pro files in the repo (not vendored, by Lock). The D Lock names Duotone, Sharp, and Thin as the families in use. Which further Pro Plus families the kit enables (for example Chisel, Etch, Jelly, Notdog, Slab, Thumbprint, Whiteboard) is set in the kit's settings on fontawesome.com and was not checked from the box.

**Entry gate:** Andrew answers the open questions below.

**Work items:** none yet (DRAFT).

**Tests:** to be written with the spec.

**Open questions for Andrew:**

- D-Q6: Look and behaviour of the wizard and of the search popup: layout, layer list, transform controls, preview sizes, grid density. Not decided here.
- D-Q7: Sanitizing the snippet on render: the allowlist of tags (`span`, `i`, image element?), classes (`fa-*` only?), and inline style properties (color, `--fa-*` variables, transform?). The shared icon sanitizer accepts one `<i>` today and will need widening.
- D-Q7a: How image layers sit in the snippet: likely a reference to a stored asset (for example a `pav-asset:` id) that the renderer resolves, not a `data:` URL.
- D-Q7b: Color picker candidates to evaluate (licence, size, keyboard and alpha support, theming on `--pav-*`): Pickr (`@simonwep/pickr`, MIT), vanilla-colorful (MIT), iro.js (`@jaames/iro`, MPL-2.0, so check the licence rule), Coloris (`@melloware/coloris`, MIT). None is chosen.
- D-Q8: Asset store design: table layout, ids, dedupe, how an asset is freed when no icon refers to it, and export or backup.
- D-Q9: Size limits: bytes per imported image, pixel size, SVG sanitizing rules (F-HARDEN icon sanitizer), and total per profile.
- D-Q10: Font Awesome licence and availability: the kit is Pro Plus but hosted; the search needs icon metadata (names, families, styles, keywords). Use the kit's API, bundle a metadata file, or get a licensed download? Pro files stay out of the repo unless Andrew changes that Lock.
- D-Q11: Scope of Settings → Icons: it exists today (Source Theme or Override, kit, icon overrides). Does the wizard's page become part of it, a new section within it, or a separate heading?
- D-Q12: Rendering at small sizes (16 px in the table and tree): how 3 layers and photos stay legible; whether a simplified fallback is drawn below some size.
- D-Q13: Recent picks and favourites: wanted, and where kept?

## D-10: Module top bars: Accounts Clone and search, Help bar (DRAFT)

**Kind:** Medium
**Status:** Not started. DRAFT spec, docs only; no code in this phase until Andrew answers the open questions. Added 2026-10-19 at Andrew's request.

**Goal:** The Accounts module and the Help panel get a top bar like the Logs panel's: a title on the left, a search field on the right, and module buttons.

**What it does (Andrew's ideas, 2026-10-19):**

- Accounts top bar: a **Clone** button. It copies the selected account into a new unsaved form: every field except the username, the password, and the id. A new id and number come from Add as usual. Clone needs a selected row, so the table gets row selection (optionally single-select through Tabulator's SelectRow module); Clone is disabled when nothing is selected.
- Accounts top bar: a **search** field like the Logs panel (H-3) that filters the table as you type.
- Help bar reworked to match: the title `Help` on the left, search on the right, and a **Back** button to the right of search. Back returns to the panel that opened Help, or, when Help was opened from the nav Help button, to the last panel shown before it.

**Entry gate:** Andrew answers the open questions below.

**Work items:** none yet (DRAFT).

**Tests:** to be written with the spec (Vitest: Clone copies all but username, password, and id; Clone disabled without a selection; search filters rows and groups; Back returns to the opener).

**Open questions for Andrew:**

- D-Q14: Clone and the copied values: are `capabilities` and `capabilitiesChecked` (K-Q10) copied, or reset to `[]` and `null` until the clone's first Test? Is the email copied (it usually matches the username), and is the name copied as is or as `<name> (copy)`?
- D-Q15: Clone with an unsaved form: does it ask before discarding edits, and does the clone open in the form ready for Add, or is it added at once?
- D-Q16: Selection: the table today highlights the selected row (`pav-row-selected`) from the form. Is Tabulator's SelectRow module (single row) wanted, or is the existing highlight enough as the selection? Keyboard up and down?
- D-Q17: Accounts search: which columns does it match (name, number, username, email, server), is it plain substring or the Logs query syntax, and does it hide whole server groups that have no match?
- D-Q18: Help Back: what counts as "the panel that opened Help" (section, Settings heading, a tab), how far back the history goes (one step or a stack), and what Back does when there is no earlier panel (hidden or disabled).
- D-Q19: Help search moves from where it is today to the bar's right side: same behaviour, or does it also search topic bodies?
- D-Q20: A shared top-bar widget (title, search, buttons) for Logs, Accounts, and Help, in `ui/src/common/`, or each module draws its own?

## Working log

- 2026-10-14 D-1: Dark frame. `theme.css` has the five variables. Title row shows Pavillion. Profile slot and body are empty.
- 2026-10-14 D-2: `api.js` calls `status()` once. The body shows `ready` and the profile name is blank.
- 2026-10-14 D-3: A file used as the working directory shows `error` and a blank name. A normal directory still shows Default and ready.
- 2026-10-14 Shell preview: nav, tabs, gradients, and the kit are on screen. Not a phase. Width, zoom, and the window are not saved.
- 2026-10-14 Shell preferences: nav width, the splits, zoom, and the window are saved through the core and restored on launch. Not a phase. Hidden chrome is stored and not applied.
- 2026-10-15 Per-pane zoom: `preferences.zoom` gained `navButtons`, `navBody`, `mailList`, and `mailDetail` beside `shell`. The Zoom section has one slider per pane, the shell scale multiplies each pane, `save_layout` carries the zoom object, and a profile missing the keys gains them on the next load. Not a phase.
- 2026-10-16 D-4 theme layer added from old plan §10. Lock references moved to J-TABS and F-HARDEN. The 50–300% zoom note marked superseded.
- 2026-10-15 Fonts setting: a Fonts heading and panel under Settings. Vanadium Sans is the primary font through `--font`, switchable live; not persisted yet. Not a phase.
- 2026-10-17 D-4 in progress: semantic names, `light.css`, and the Theme select are in the tree. `make test` is 67 Unity tests and 38 Vitest tests. The window check is still open.
- 2026-10-17 D-4 complete. Andrew accepted the look. The same sitting pulled D-6 and D-7 forward. Catalog, skin, `pav-theme`, and the commented starter files are in the tree. `make test` is 68 Unity tests and 43 Vitest tests. `PAVILLION_API_VERSION` stays 3. The D-6 and D-7 window check is still open. Do not start J-1 before that check.
- 2026-10-17 D-6/D-7 still in progress. Theme choice saves even when icon paint throws. A file theme paints its icons without a reload. The folder field is blank, with placeholder `Default:` plus the working directory, and Browse. Theme Default, Undo, and Redo do not change zoom. The window check is still open.
- 2026-10-17 D-6 and D-7 complete. Andrew accepted the window. The launch terminal no longer prints profile info. `core ready` stays in the profile log and the Activity log. `make test` is 68 Unity tests and 44 Vitest tests. The next phase is J-1. Do not start it in this sitting.
- 2026-10-18 Recorded at the J-9 close (moved from the index Resume): the host page still starts on `color-scheme: dark` and `background: #000`, and `data-pav-theme` follows the resolved theme. `pav-theme` is on the existing scheme list in `src-tauri/src/lib.rs`.
- 2026-10-19 D-8 added (not started): Settings → Formats, app-wide display formats through Luxon, sequence step 132 after D-5. The H-4 log view's local 24 h time moves to it when it lands.
- 2026-10-19 D-8 moved (Andrew): sequence row 132 now sits just before N-3 (step 61), entry gate Q-1, so N-3, T-3, and later screens use Settings → Formats; D-Q1 (placement) resolved and removed.

## Shell preview

**Status:** In the tree (2026-10-14). Not a phase. Andrew likes the direction. Do not mark it complete.

**Accomplished:** `ui/index.html` is the shell. `ui/styles/dark.css` holds the colors and the gradients. `ui/styles/theme.css` holds the rules and only uses variables. `ui/src/shell.js` switches sections, keeps home tabs, and drags the nav and the splits. `ui/src/main.js` still calls `status()` once, through `api.js`. `ui/public/mark.png` is a copy of `src-tauri/icons/source.png` and is the Mail button. The other seven buttons are Sharp Thin from the kit. `security.csp` in `tauri.conf.json` is the §11 string, plus `ws://localhost:1420` for the dev server.

On a normal launch the profile name is under Settings, on the Zoom heading. It read `Default`. The state word is not drawn. An error uses the banner and shows `result.error` when that field is set. The word `ready` is not in the HTML, CSS, or JS.

The page also calls `prefs()` and `save_layout` through `api.js`. The core thread is the only writer. `pavillion_start`, `pavillion_stop`, and `pavillion_status` are unchanged. A profile without `preferences.ui` gains that object on load. Width and the splits clamp to the stored limits. Zoom clamps to `0.5`–`3`. The window is created hidden, then shown at the saved size and position. Move, resize, and quit write `preferences.window`. A drag writes the nav or the split when the pointer goes up. The zoom slider writes when the value is committed. `chrome` is left as stored. Decorations stay on.

What the window showed: Mail's tree and a side-by-side split, Calendars' month with the mini calendar and two swatches, Contacts' letters and search, Settings with Zoom and Theme. The nav edge moved and was put back. Theme says System, and dark is the file in use. There is no light theme and no theme icon on the shell.

A later check dragged the nav to 430, set zoom to 150%, moved the mail split, and resized the window to 1500×940. Quit and the next launch restored those values, including the split at the same pixel. The nav, the split, and the zoom were then put back. The window left on disk is 1500×940, with zoom at 100% and the nav at 320.

**Variances:** The numbers from §10 are the defaults (`nav` 320 within 200–500, splits at half within a quarter and three quarters). The page reads them from the core and writes them back. Hidden chrome is stored and not turned on. The tab spacer has `data-tauri-drag-region`. Activity is an empty tab. Help is one sentence. Update spins for about a second and a half and does not sync. The mailbox names, the calendar rows, and the letter lists are placeholders. Week start is Monday in the preview and is not read from the profile. Files says there are no folders.

**Lessons learned:** WebKit lets an author `display` override the `hidden` attribute. `[hidden] { display: none !important; }` is what made one section show at a time. Before that rule, every panel was stacked in the window.

`.panel` is a column. A split that is also a panel stays a column, so the message sat under the list. `.panel.split { flex-direction: row; }` puts them side by side. The divider then sits at half the workspace.

The SVG kit rendered with the §11 CSP. Sharp Thin and the roof mark both showed. The kit's own accessibility pass is off, so each button has its own accessible name. `font-src` was left unset and the SVG icons still appeared.

`make test` was not run for the first preview. The preference save was tested after that: 23 tests, 0 failures. Two of those cover the round trip and a profile that was missing `ui`.

The window is shown only after the saved rectangle is applied, so it does not flash the 1200×800 from `tauri.conf.json`. On this X11 session Tauri's position and the frame reported by the window manager differ by the decoration offset. Saving Tauri's position and setting that same position on the next launch comes back to the same place. `gen` is a keyword in Rust edition 2024, so the shell does not use it as a name.

**Handoff:** No phase is open. Nav width, the splits, zoom, and the window are saved. The page does not read `Pavillion.json`. Hidden chrome is stored and the decorations stay on. Activity is still an empty tab. When that view is built, it asks the core for the log and does not open the file. Do not vendor the kit, and leave its domain list open. Do not put the word `ready` in the page. Placeholders stay until the screen they stand in for is real.
- 2026-10-16 Review: theming is split in two. D-4 (variables plus the dark/light/system switch) moves early, right after G-4. D-5 (density, gradients, contrast audit) stays late.

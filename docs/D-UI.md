# D — UI

**Status:** D-4, D-6, and D-7 complete (2026-10-17). D-10 complete (2026-10-21). D-8 complete (2026-10-23). The index names N-3 next (step 61). D-5 and D-9 are not started. The shell preview below is not a phase.
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
| D-8 | Settings → Formats: app-wide display formats through Luxon (before N-3) | Medium | Complete |
| D-9 | Icon Wizard (DRAFT spec, docs only): up to 3 layered icons or images per icon, stored as an HTML snippet | Medium | Not started |
| D-10 | Module bars (toolbar and footer convention): Accounts Clone and search, the Help bar with search and Back | Medium | Complete |

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
- Times are 24 h (`HH:mm`) by default everywhere, dates `2026-10-14` or `Oct 14` (`preferences.time_format`). Default time zone `America/Vancouver`. From D-8, `ui/src/formats.js` is the only module that turns a stored instant into display text. Screens call `formatInstant`. They do not format a date themselves. The strings live in `preferences.ui.formats` (opaque to the core): `locale` (absent means the system locale), and one string per kind `date`, `time`, `timeSeconds`, `datetime`, `dateShort`, `weekday`, `relative`, `duration`. Defaults are `yyyy-MM-dd`, `HH:mm`, `HH:mm:ss.SSS`, `yyyy-MM-dd HH:mm`, `MMM d`, `EEEE`, `long`, and `h:mm:ss`. `relative` is `long`, `short`, or `narrow`. A missing time kind follows `time_format`: `24h` uses those defaults, and `12h` uses `h:mm a`, `h:mm:ss.SSS a`, and `yyyy-MM-dd h:mm a`. A saved string that differs from that default wins. Reset clears the saved string. `formatInstant(ms, "dateShort")` uses `time` on the same local day, `dateShort` in the same local year, and `date` otherwise. The zone is the profile `timezone` when it is a real IANA name, else the system zone, else `America/Vancouver`. Storage stays UTC milliseconds. Settings → Formats (heading `formats`, after Fonts) edits the strings, the locale, and the zone.
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
**Status:** Complete (2026-10-23, step 132). Andrew accepted the Formats window the same day. `make test`: 247 Unity, 2 Rust, 397 Vitest, exit 0. Gzip -9 of `ui/dist` is 360042 bytes. The phase stamp is `0.68.0`. CI on `a8903db` ([40059550732](https://github.com/500Foods/Pavillion/actions/runs/40059550732)): fedora and macos-core passed, windows-core failed. The `formats.test.js` check now allows `01:30 PST`, `02:30 PDT`, and `02:30 MST` for `2026-11-01T09:30:00.000Z`. That fix re-ran on `47ee864` ([40062205611](https://github.com/500Foods/Pavillion/actions/runs/40062205611)): fedora and windows-core passed, so the formats check is fixed. macos-core failed `zoom-wheel.test.js` at 5000 ms. The 20000 ms timeout is in `4d8b53e`. CI for that commit ([40064822844](https://github.com/500Foods/Pavillion/actions/runs/40064822844)) is green: fedora, macos-core, windows-core. The docs merge `69d58e9` ([40065380950](https://github.com/500Foods/Pavillion/actions/runs/40065380950)) is green on the same three jobs. The agent does not commit.

**Goal:** One place, Settings → Formats, where the user sets how dates and times are shown everywhere in the app (date, time, datetime, and the other display formats below), with every conversion done by Luxon. Storage and the wire stay UTC milliseconds. Copy, Save, and Print in the log view use the display strings. The core command `log.export` still writes UTC lines.

**Entry gate:** Q-1 complete (the row before it in the sequence). D-8 comes before N-3 so the message list (N-3), the calendar views (T-3), tasks (X), notes (Y), and every later screen use `formats.js` from the start. Until it lands, only the H-4 log view and the activity list format times with their own fixed 24 h code in the profile zone; D-8 switches them.

**Work items:**

- [x] Add `luxon` as an npm dependency of `ui/` (pinned, bundled by Vite; record the gzip cost of `ui/dist` in Accomplished). No other date library.
- [x] `ui/src/formats.js`: the only module that turns a stored instant into display text. `formatInstant(ms|iso, kind)` for `kind` in `date`, `time`, `timeSeconds` (`HH:mm:ss.SSS`, the log view), `datetime`, `dateShort` (message list), `weekday`, `relative` (Luxon `toRelative`), and `duration`. It reads the profile zone (`prefs.get` `timezone`, default `America/Vancouver`, else the system zone) and the profile locale.
- [x] Defaults are 24 h: `date` `yyyy-MM-dd`, `time` `HH:mm`, `timeSeconds` `HH:mm:ss.SSS`, `datetime` `yyyy-MM-dd HH:mm`. The existing profile `time_format` (`24h`, `prefs.set` `timeFormat`) stays the 12/24 h switch; a format string chosen in the pane wins over it.
- [x] Where the formats live: `preferences.ui.formats` (one key per kind, Luxon format strings), validated in the UI before save. Lock entry in this file and in [B-CONFIG.md](B-CONFIG.md) before code.
- [x] Settings → Formats pane: one row per kind with the format field, a live preview of now and of a fixed sample instant, a Reset per row, and a zone picker that writes `timezone` (IANA names from `Intl.supportedValuesOf("timeZone")`). Each row has a short description, one example of the default pattern, and a list of default patterns. The current default is marked. Custom keeps any other allowed string.
- [x] Switch every screen to `formats.js`: the log view (H-4 commit 4 shows local 24 h `HH:mm:ss.SSS` in the profile zone with its own code; it moves to `timeSeconds`), the activity list, and any screen already using `Intl.DateTimeFormat` or a hand-written stamp. Later phases (N-3 message list, T calendar views, X tasks, Y notes) call `formats.js` and never format a date themselves.
- [x] Help: a Formats page under Settings that lists the Luxon format tokens (`yyyy`, `yy`, `MM`, `M`, `MMM`, `MMMM`, `dd`, `d`, `EEE`, `EEEE`, `HH`, `H`, `hh`, `h`, `mm`, `ss`, `SSS`, `a`, `ZZ`, `ZZZZ`, `z`, quoting with `'...'`) with an example for each, and links to the Luxon formatting docs.

**Tests:**

- Vitest `formats.test.js`: each kind's default against fixed instants in `America/Vancouver` (both sides of a DST change) and `UTC`; 24 h by default; a custom format string; an invalid format string is refused with a message; the zone fallback order.
- Vitest: the Formats pane saves, previews, and resets; the log view re-renders on a format change.
- A bypass-style Vitest that fails on `toLocaleDateString`, `toLocaleTimeString`, or `Intl.DateTimeFormat` outside `formats.js`. `toLocaleString` on counts and sizes stays.
- `make test` exits 0; the window check covers the pane and the log view.

**Done means:** Changing a format or the zone in Settings → Formats changes every date and time on screen at once, the defaults are 24 h in the profile zone, and Help lists the tokens.

**Exit gate:** `make test` exits 0. In `make build`, the pane and two screens (the log view and the activity list) follow a changed format and zone.

**Resolved (Andrew, 2026-10-23):**

- D-Q2: `preferences.ui.formats`. The core does not read the strings. `time_format` stays the 12/24 switch until a saved string differs from that default. Heading `formats`, after Fonts.
- D-Q3: eight kinds (`date`, `time`, `timeSeconds`, `datetime`, `dateShort`, `weekday`, `relative`, `duration`). `dateShort` is the message-list rule. No number or size formats. `week_start` stays as it is. The bypass test flags date APIs only.
- D-Q4: `preferences.ui.formats.locale`, empty means the system locale.
- D-Q5: Copy, Save, and Print follow the display format. `log.export` stays UTC in the core.

**Accomplished:** `luxon` 3.7.2 is pinned in `ui/package.json`. `ui/src/formats.js` is the only module that turns a stored instant into display text. The eight kinds are `date`, `time`, `timeSeconds`, `datetime`, `dateShort`, `weekday`, `relative`, and `duration`. Defaults are 24 h: `yyyy-MM-dd`, `HH:mm`, `HH:mm:ss.SSS`, `yyyy-MM-dd HH:mm`, `MMM d`, `EEEE`, `long`, and `h:mm:ss`. A missing time string follows `time_format`; 12 h uses `h:mm a`, `h:mm:ss.SSS a`, and `yyyy-MM-dd h:mm a`. A saved string that differs from that default wins. The strings live in `preferences.ui.formats`, which the core does not read. An empty locale uses the computer's locale. Settings → Formats, after Fonts, describes each kind, shows the default pattern on the sample, and lists default patterns. The current default is marked. Custom keeps any other string the token allowlist accepts. Now and Sample follow the selected zone. Reset returns that row to its default and does not save until Apply. A string equal to the default is left out of the save. `formatInstant` of `dateShort` uses the time pattern on the same local day, the `dateShort` pattern in the same year, and the date pattern otherwise. The log, the activity list, account check times, Limits, and the log session line call `formats.js`. Copy, Save, and Print use the display line. Save pages `log.query` and writes through `export.write_text`. `log.export` still writes UTC lines. Help lists the tokens. Developer → Formats names the allowlist. Gzip -9 of every file in `ui/dist`, summed, is 360042 bytes. The page chunk is 193126 bytes and the Tabulator chunk is 57569. The size is a record. It is not a reason to drop Luxon. `PAVILLION_API_VERSION` stays 3.

**Lessons learned:** Luxon does not throw on an unknown token, so the page allowlists the tokens Help lists. `EE` and a date `SS` are not tokens. Duration `SS` is. `ZZZZZ` is the long zone name, and `ZZ` is the offset. `dateShort` is not one Luxon string: the saved value is the this-year pattern, and `formatInstant` applies the three-way rule. A string equal to the current default is omitted, so `time_format` still switches 12 h and 24 h. The core cannot link Luxon. Save therefore pages `log.query` and writes display lines through `export.write_text`, whose cap is 8 MB. `log.export` stays the UTC file, and its cap is 32 MB. Match counting stays on the UTC line so it agrees with `log.query`. A format change repaints the dates already on screen. A full settings refresh would wipe unsaved fields. Once the time pattern is wider than 12 columns, level color uses the stored level. Option examples are rendered in the selected zone. `2026-11-01T09:30:00.000Z` in `America/Vancouver` is not one string. tzdata before 2026b falls back to `01:30 PST`. tzdata 2026b keeps British Columbia on -07, so the same instant is `02:30 MST`, or `02:30 PDT` where ICU has the offset and still says PDT. The test allows those three. The 2026-03-08 spring-forward stays `01:30 PST` then `03:30 PDT`.

**Handoff:** N-3 is next, step 61, in [N-MAIL.md](N-MAIL.md). It was not started. Its entry gate is N-2 and N-3a, both complete, and D-8. The time column calls `formats.js` and does not format a date. `message.list` gains `{view:{kind:"unified", name}}` with `inbox`, `unread`, `flagged`, `drafts`, and `sent`. Unread is every unseen message. Flagged is every flagged message. Inbox, Drafts, and Sent are those roles across accounts. Drafts and Sent show To. The nav entries stay in N-6. Kind `search` stays as Q-1 left it. Kind `threaded` is accepted and stays flat until N-6. `debug.seed_messages` is `{mailboxId, count}`, absent when `NDEBUG` is set, local rows only, clamped to `listRows`, and it does not use the network. The mail list theme extends `.pav-table` in `ui/public/styles/theme.css`. New variables ship in `dark.css`, `light.css`, and both starter `*.theme` files. There is no `ui/styles/list.css`. The Q-1 handoff names that file; this decision wins. The Lock row is the account color bar, from (To in Sent and Drafts), subject, preview, the `dateShort` time, the flag marks, and the attachment icon. `message.list` omits a row whose `keywords` contain `$deleted`, and `total` counts the rows that remain. The keyword stays stored. `mailbox.list` recounts unread and total from local rows and skips `$deleted` (N-Q6). Unread is not seen, not draft, and not `$deleted`. The stored server counts stay for sync. N-5 still moves to Trash or destroys. The search field stays in Q-2. Do not start Q-2. The Accounts table polish stays paused in [TABULATOR.md](TABULATOR.md). Do not touch the quit path. Keep the header insert straight-line. `PAVILLION_API_VERSION` stays 3. The phase stamp is `0.68.0`. `make test`: 247 Unity, 2 Rust, 397 Vitest, exit 0. Gzip -9 of `ui/dist` is 360042 bytes. CI on `a8903db` ([40059550732](https://github.com/500Foods/Pavillion/actions/runs/40059550732)) failed `windows-core` because that instant was pinned to `01:30 PST`. The test now allows `01:30 PST`, `02:30 PDT`, and `02:30 MST`. On `47ee864` ([40062205611](https://github.com/500Foods/Pavillion/actions/runs/40062205611)) fedora and windows-core passed. macos-core failed the zoom-wheel test at 5000 ms. The 20000 ms timeout is in `4d8b53e`. CI [40064822844](https://github.com/500Foods/Pavillion/actions/runs/40064822844) is green: fedora, macos-core, windows-core. The agent does not commit.

## D-9: Icon Wizard (DRAFT)

**Kind:** Medium
**Status:** Not started. DRAFT spec, docs only; no code in this phase until Andrew signs off the open questions. Added 2026-10-19 at Andrew's request (renamed from "icon picker" the same day).

This records Andrew's ideas. It does not decide the look: layout, sizes, spacing, control styles, previews, and animation are his to set (D-Q6 onward).

**Open item, Settings → Icons editor (diagnosed 2026-10-20, for this rework):** Andrew could not change the nav icons himself: he tried removing Mail's `fa-flip-horizontal` and switching Update to `fa-up-down`. What was found in `ui/src/shell.js` (the `#icon-form` handlers) and in a mock mount:
1. **Wrong comparison for unsaved changes.** The Preview and Reload & Save buttons compare each field with its `defaultValue`, the default text in the template HTML, not with the saved override. Removing the flip from a saved `fa-paper-plane-top fa-rotate-270 fa-flip-horizontal` makes the field equal the template default again. The editor then counts no change and disables both buttons, so the change cannot be applied or saved (reproduced in a mock mount: both buttons stay disabled). The same happens whenever an edit returns to the shipped value, for example `fa-up-down` now that it is the default.
2. **Edits are only kept in memory until a button is pressed.** Typing writes `preferences.ui.icons[key]` in memory but schedules no save. The value is written only by Preview or Reload & Save, or by chance with an unrelated later save. Leaving the page without pressing either keeps a stale edit in memory, and it may persist later.
3. **Saved overrides win over shipped defaults.** With Icon source "Override" (`preferences.ui.iconSource: "override"`), every key saved in `preferences.ui.icons` is used as is, so a new shipped default never reaches a profile that saved all keys. Andrew's profile has `iconSource: "override"` and all eight keys saved, including `mail: "... fa-flip-horizontal"` and `update: "... fa-arrows-rotate"`.
4. **A file theme ignores overrides.** When the chosen theme is a file theme (not the built-in dark or light), `applyIcons` paints the theme file's `--pav-icon-*` and ignores the overrides at load. A Preview still shows the override until the next reload. This does not affect Andrew today: his `themeChoice` is the built-in `dark`.
5. **Not the cause:** no class allowlist rejects `fa-flip-horizontal` or `fa-up-down`, and the nav does read `preferences.ui.icons`.

For the rework:
- compare against the saved value (or track an explicit dirty flag);
- save on change, or warn on leave;
- offer a per-key "Use theme default" that deletes the key from `preferences.ui.icons`;
- show which source each icon comes from.

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

## D-10: Module bars: toolbar and footer, Accounts Clone and search, Help bar

**Kind:** Medium
**Status:** Complete (2026-10-21, step 134). Andrew accepted the Accounts bars and the Help bar (search and Back) the same day. The Accounts table polish is not this phase. It stays paused in [TABULATOR.md](TABULATOR.md).

**Goal:** One convention for the bars of every module, and its first users: Accounts and Help.

### Module bar convention (Lock, 2026-10-20)

- **Scope.** These rules apply to every module in the app, present and future (Settings modules, Help, Logs, and later Mail, Contacts, Calendar lists). A new module uses the shared builders; it does not draw its own bars. A module without search, a list, or Export simply omits those parts; Refresh is always present in the footer when the module shows data that can be reloaded.
- **Terms.** A *module* is one self-contained section of a view, for example Accounts or Themes inside Settings, or Help. *Panel* stays the layout area and *view* a whole screen (Mail, Settings).
- **Module toolbar** (top, `.pav-module-toolbar`, built by `moduleToolbar()` in `ui/src/common/module-bar.js`): the title on the left (`.pav-module-title`), a spacer, then view tools such as the column chooser (`tools`), then the search field (`.pav-module-search`, `type="search"`, like the Logs bar) when the module has one, then the module's action buttons to the right of search (for example Back on Help), then the Help jump last. Module buttons that act on a selection (Clone) sit left of the search, after the title.
- **Module footer** (bottom, `.pav-module-footer`, built by `moduleFooter()`): the navigator at the bottom left (`.pav-module-nav`). Its **first button is always Refresh** (`fa-arrows-rotate`, title `Refresh`; its own constant `REFRESH` in `module-bar.js`, not the Update nav icon, which became `fa-up-down` on 2026-10-20), which reloads the module's data the way the Themes Refresh button reloads the theme list. (2026-10-20, Andrew) Each click spins the icon once, 360° over `--pav-refresh-spin-duration` (0.5s; all four theme files). `moduleFooter` adds the class `is-spinning`, which runs the `pav-refresh-spin` keyframes, and removes it on `animationend`. A click during the spin restarts it cleanly: the class is removed, a reflow is forced, and the class is added again. Under `prefers-reduced-motion: reduce` there is no spin: JS does not add the class, and CSS sets `animation: none`. For a module with a table, Refresh reloads the data and then calls the table's `refit()` (TABULATOR.md). After it, for a module with a record list, First, Previous, Next, Last. Export and other list actions sit on the right (`.pav-module-footer-end`).
- Both bars are `.button-bar` rows on `--pav-*` variables (border `--pav-border`, 1px divided by `--pav-bar-zoom`), so Button bars zoom applies. Buttons are icon buttons with a `title` and `aria-label`; text, where shown, is `textContent`.
- **Layout (2026-10-20 window review).** A module fills its whole area: toolbar, body, footer, with no gap or padding between them. A split body (Accounts: table, divider, form) runs from the toolbar's bottom rule to the footer's top rule, so the 1px divider meets both bars. Padding belongs inside a pane (the form), never on the module body. The footer navigator is left-aligned (`flex: none`, under the left pane); the end group is pushed right with `margin-left: auto`.
- **Zoom and sizing (2026-10-20).** Module bars are `.button-bar` elements in the page markup, so Button bars zoom sets `zoom` and `--pav-bar-zoom` on them exactly as on the nav bars and the tab strip. Every bar uses the same sizes from `theme.css`: `--pav-bar-icon-size` (icons, scaled by `--pav-bar-zoom` because WebKit leaves rem-sized SVG unzoomed), `--pav-bar-font-size` (labels, tabs, titles, search), `--pav-bar-pad-y` and `--pav-bar-pad-x` (button padding, the same as the `::before` strut), so a nav bar, the tabs, a module toolbar, and a module footer line up at every zoom. Module bar icons carry the `icon` class like the nav icons.
- **Tooltips (2026-10-20, Andrew).** Every icon-only control uses the shared tooltip (`ui/src/common/tooltip.js`, `.pav-tooltip`, the one the nav icons use), never a native `title`. The tooltip is delegated from the app root, so dynamically created elements work without a bind call. An element is a tooltip host when it has `data-pav-tip="text"` (any element: buttons, header icons, cells, badges), or when it is a `button` with an `aria-label` and no visible text (its label is the tip). The tip text is read when the tooltip is shown, so a changed label shows at once. Explicit `tooltips.bind(el, text)` still works and wins. Builders set `data-pav-tip` and `aria-label`, not `title`: the module bars (`module-bar.js`, including the column chooser and Help Back), tab close, the account icon choices, the account table's header icons, group count and feature cells, and tree icons. `title` stays only where a native tooltip is wanted for truncated text (theme list rows, the log time) and on the iframe (`title` names it for assistive tech).
- **Table modules.** A module with a table may put a column chooser button (`fa-table-columns`) on the toolbar's right side, immediately before search (TABULATOR.md, Column chooser). Table colours are the `--pav-table-*` variables (TABULATOR.md, Table look). A split's table pane is not capped at a percentage: it may grow until its partner pane reaches its own minimum (Accounts: the form keeps `20rem`; 2026-10-20). A module body with its own zoom rows (Accounts table and Accounts form, K-Q14) zooms in place. A table scales its font and column widths (TABULATOR.md, Zoom in place) and never uses CSS zoom; a form may use CSS zoom. Zoom never changes a pane's width.
- **Tab close.** The close button is the right-most region of the tab, full tab height, its own hover background; its icon is smaller (`--pav-tab-close-icon-size`). Hovering it highlights only the close region.
- A Tabulator table inside a module keeps its own header and `bottomCalc` row; the navigator is no longer in the Tabulator `footerElement`.

### Decisions (D-Q14 to D-Q20, answered 2026-10-20)

- D-Q14: Clone copies every account field except the username, the password, and the id/number (new ones come from Add). The name becomes `<name> (copy)`. The email is copied. Capabilities are not copied (`[]`/not checked until the clone's first Test), because they belong to the credentials. No secret is read, copied, or sent to JS.
- D-Q15: Clone opens the copy in the form, unsaved, with no row selected, ready for Add; nothing is added at once. Unsaved edits on the previous row are dropped, as when another row is clicked.
- D-Q16: the existing single-row selection (K-Q12) is the selection; Clone is disabled when no row is selected (including while a clone or a new account is in the form).
- D-Q17: Accounts search is a plain, case-insensitive substring over name, number, email, username, and the server label. Tabulator `FilterModule` `setFilter(fn)`; a server group with no match disappears. Escape clears it. The navigator walks only the shown rows.
- D-Q18: Back returns to the last tab shown before Help (one step), which is the module that opened Help through a help jump, or the last panel when Help came from the nav. Back is disabled when there is no such tab or it was closed.
- D-Q19: Help search keeps its behaviour (titles, text, samples); it only moves to the right of the bar.
- D-Q20: one shared widget in `ui/src/common/module-bar.js`.
- D-Q21 (Andrew, 2026-10-20): the Accounts toolbar has **Add**, **Clone**, and **Delete**, in that order after the title. Add clears the selection and opens a blank form (name focused) for a new account; it is created when the form's Add is pressed with the session URL, username, and password filled, because `account.add` requires `kind`, `sessionUrl`, `username`, and `secret` (no blank account on the core side; a core-side draft account is an open item, D-Q22). Delete needs a selected row, asks for confirmation (`Delete <name>? Its saved password and local mail and data are deleted.`), then calls `account.remove {id, purgeLocal:true}`, which deletes the stored secret (`pav_secret_delete`), the profile entry, and the account rows. Delete is disabled without a selection.
- D-Q22 (open, core): should `account.add` accept a draft account without URL, username, or secret so toolbar Add can create and select a real row at once? Today it cannot. Also: `account.remove` ignores the result of `pav_secret_delete`; should a failed secret delete fail the call or log `WARN`?

**Export (2026-10-20 window review):** the blob download did nothing in WebKitGTK. Export now builds the CSV in the page (`accountsCsv`: the rows shown, so a search narrows it, in display order; CRLF; a cell starting with `= + - @` gets a leading apostrophe), asks `host.dialog.save` for a path (`pavillion-accounts.csv`, filter CSV), and calls `export.write_text {path, text}` (API.md). Cancel writes nothing.

**Work items:**

- [x] `module-bar.js` (`moduleToolbar`, `moduleFooter`, `paintFooterNav`) and the `.pav-module-*` rules in `theme.css`.
- [x] Accounts: toolbar with Add, Clone, Delete, and search; footer with Refresh, First, Previous, Next, Last, and Export; the table loses its footer element.
- [x] Help: toolbar with title `Help`, search, Back.
- [~] Themes: Refresh stays on the folder bar. Moving it was "if cheap" and was not done. No later phase owns it.

**Tests (Vitest):** `cloneAccount` drops username, password, id, number, and capabilities; Clone disabled without a selection; `accountMatches` over the searched fields; footer Refresh first and navigator state; Help Back tracker returns to the opener and is empty with no earlier tab.

**Done means:** Accounts and Help use the shared bars. Add, Clone, Delete, search, Refresh, the navigator, and Export work on Accounts. Help search stays, and Back returns to the previous tab.

**Exit gate:** Andrew accepts the bars in the window. Passed on 2026-10-21. The last full `make test` recorded in the index, with this phase already in the tree, is 173 Unity tests, 2 Rust tests, and 342 Vitest tests, exit 0 (2026-10-20). This close-out did not rerun it.

**Accomplished:** `ui/src/common/module-bar.js` builds the toolbar and the footer. Both are `.button-bar` rows, so Button bars zoom applies. The toolbar is the title, selection actions, the column chooser, search, other actions, then the help jump. The footer's first button is Refresh. It spins once per click unless motion is reduced. A list adds First, Previous, Next, and Last, and puts Export on the right. Icon-only controls use the shared tooltip. Accounts has Add, Clone, and Delete, then search. Clone copies every field except the username, the password, the id, and the number. The name gains ` (copy)`. Capabilities are cleared. The copy opens unsaved. Delete confirms, then `account.remove` with `purgeLocal`. Search is a case-insensitive substring over name, number, email, username, and the server label. Escape clears it. Export builds the CSV in the page and writes it through `export.write_text` after the save dialog, because a blob download did nothing in WebKitGTK. Help's bar is the title, search, then Back. Back returns to the last non-Help tab and is disabled when that tab is gone. Help covers those controls, and Developer → Module bars names the builders. D-Q22 stays open: `account.add` still requires a session URL, a username, and a secret, so toolbar Add opens a blank form instead of inserting a draft row.

**Lessons learned:** A module bar has to be a real `.button-bar` in the page. A bar drawn inside Tabulator's footer does not pick up Button bars zoom or the shared strut. WebKitGTK does not complete a blob download from the page. The save dialog plus `export.write_text` is the path that writes the file. Restarting the Refresh spin means removing the class, forcing a reflow, and adding the class again. A native `title` fights the shared tooltip. `data-pav-tip` is what the builders set. The Accounts table's remaining look and behavior issues are a separate list. Closing the bars did not close that list.

**Handoff:** M-3 is the next sequenced phase. D-10 does not block it. A later module uses `moduleToolbar` and `moduleFooter` and does not draw its own bars. Themes Refresh stays where it is. D-Q22 stays open until a phase that owns `account.add` asks it. The Accounts table polish stays in [TABULATOR.md](TABULATOR.md) and is not a sequenced phase.

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
- 2026-10-21 D-10 complete. Andrew accepted the Accounts bars and the Help bar. Themes Refresh was left on the folder bar. The phase stamp moves to `0.58.0` with the H-4 close. This sitting did not rerun `make test`.
- 2026-10-23 D-8 in progress. Andrew answered D-Q2 through D-Q5 and chose this phase before N-3. `formats.js`, the Formats pane, and the screen switches are in. Help lists the tokens. `make test` is 247 Unity tests, 2 Rust tests, and 396 Vitest tests, exit 0. The window is not accepted, so the luxon gzip line stays open and this phase is not closed. N-3 was not started.
- 2026-10-23 D-8 in progress. The Formats pane describes each kind, shows the default pattern on the sample, and lists default patterns. Custom keeps any other allowed string. `make test` is 247 Unity tests, 2 Rust tests, and 397 Vitest tests, exit 0. The window is not accepted. N-3 was not started.
- 2026-10-23 D-8 complete, step 132. Andrew accepted the Formats window. Luxon 3.7.2 is pinned. Gzip -9 of `ui/dist` is 360042 bytes. The phase stamp is `0.68.0`. `make test` is 247 Unity tests, 2 Rust tests, and 397 Vitest tests, exit 0. N-3 is next, step 61, and was not started. CI has not run on this tree. The agent does not commit.
- 2026-10-24 D-8 CI on `a8903db`, run [40059550732](https://github.com/500Foods/Pavillion/actions/runs/40059550732): fedora and macos-core passed, windows-core failed. `ui/src/formats.test.js` "both sides of a Vancouver DST change stay on the local clock", line 48: `2026-11-01T09:30:00.000Z` with `HH:mm ZZZZ` expected `01:30 PST`, got `02:30 PDT`. The `08:30Z` assertion above it passed. Andrew: fix this. N-3 was not started.
- 2026-10-24 The Vancouver check allows `01:30 PST` (tzdata before 2026b), `02:30 PDT` (ICU with the -07 offset and the old name), and `02:30 MST` (tzdata 2026b). The March spring-forward assertions stay exact. CI has not re-run. N-3 was not started.
- 2026-10-24 D-8 CI is green. `4d8b53e`, run [40064822844](https://github.com/500Foods/Pavillion/actions/runs/40064822844): fedora, macos-core, windows-core. The docs merge `69d58e9`, run [40065380950](https://github.com/500Foods/Pavillion/actions/runs/40065380950), is green on the same three jobs.

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

The window is shown only after the saved rectangle is applied, so it does not flash the 1200×800 from `tauri.conf.json`. From 2026-10-23 it also waits until the splash cover has been laid out (`pav-splash-painted`). The webview background is `#000000`. The page does not call `show()`. A missed signal shows the window after 2 s. Andrew accepted that window the same day. On this X11 session Tauri's position and the frame reported by the window manager differ by the decoration offset. Saving Tauri's position and setting that same position on the next launch comes back to the same place. `gen` is a keyword in Rust edition 2024, so the shell does not use it as a name.

**Handoff:** No phase is open. Nav width, the splits, zoom, and the window are saved. The page does not read `Pavillion.json`. Hidden chrome is stored and the decorations stay on. Activity is still an empty tab. When that view is built, it asks the core for the log and does not open the file. Do not vendor the kit, and leave its domain list open. Do not put the word `ready` in the page. Placeholders stay until the screen they stand in for is real.
- 2026-10-16 Review: theming is split in two. D-4 (variables plus the dark/light/system switch) moves early, right after G-4. D-5 (density, gradients, contrast audit) stays late.

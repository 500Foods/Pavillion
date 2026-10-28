# D — UI

**Status:** D-4, D-6, and D-7 complete (2026-10-17). D-10 complete (2026-10-21). D-8 complete (2026-10-23). N-3 complete (2026-10-24). D-9 is in progress (pulled forward 2026-10-28). D-5 is not started. The shell preview below is not a phase.
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
| D-9 | Icon Wizard + Settings → Icons (DRAFT, absorbs F-3/F-7/F-8 UI): libraries, registry tree, popup with Library / Foundry / SVG / PNG tabs | Hard | In progress |
| D-10 | Module bars (toolbar and footer convention): Accounts Clone and search, the Help bar with search and Back | Medium | Complete |

Kind: **Easy** is the static frame or one function in `api.js`. **Medium** crosses one boundary. **Hard** spans several surfaces in one phase (D-9 is Hard).

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

**Lessons learned:** WebKit's `SVGElement.className` is read-only. Font Awesome replaces `<i class="icon">` with `<svg>`, and assigning `className` throws `Attempted to assign to readonly property.` Replace the node. That throw used to skip the save, so the theme looked applied and was forgotten after a restart. `paintTheme` swallows the paint error and the save still runs. A theme choice repaints in place and does not call `location.reload`. Changing the kit id still reloads, from `reloadIcons` in `main.js`. The host hears `light` or `dark` from the file's first `color-scheme` ident, and the same value is not sent twice. `getPropertyValue` may return quotes; strip one matching pair. `iframe.sandbox` is a read-only token list; set the attribute to an empty string. The shared icon sanitizer was planned as F-7; as of 2026-10-28 it lands in D-9.

**Handoff:** J-1 is next, in a fresh session. Do not start it here. D-5 is step 114. Its entry gate is met, and the sequence still runs J-1 first. F-8 owned the icon editor when this landed; that work moved into D-9 on 2026-10-28. A new `--pav-*` variable ships in both starter files in the same change. `@font-face` in the starters stays a comment. Do not send `file:` through `normalizeTheme`. Do not rewrite a stored `iconSource` on load. `lib.rs` stays a 199-line forwarder. `pav-theme` is already on its scheme list.

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

## D-9: Icon Wizard and Settings → Icons (DRAFT)

**Kind:** Hard
**Status:** In progress (2026-10-28). Andrew pulled this phase forward. D-Q6–D-Q17 stay locked. Last finished work item: Settings → Icons section tabs. Each tab is a plain table with a Default check when the icon has no saved override, the icon at one size, and the name. A row opens the Icon Wizard. Close, Cancel, Escape, and a click on the backdrop close it, and it asks only after an edit. Ctrl+wheel zooms the Wizard from 100% to 300% by 5% (default 100%, `preferences.ui.iconWizardZoom`), and the Away/Toward switch applies. The profile-save fix (`@` only in the URL authority) is in the tree and still needs a rebuild. File-scoped checks for that save: `icons-wizard` Vitest and the Unity icon test. The earlier full `make test` was 255 Unity, 2 Rust, 419 Vitest, exit 0; this slice did not re-run it. The running binary still has the old check until the next build. The next successful save also finishes the one-time icons reset, which clears the stored class-string icons. Still open: Andrew's window check of that swatch, and the SVG and PNG canvas editor (import, scale, rotate, flip, and Apply view store the asset; drag, a mask, and a live canvas are not in yet). Material fill and weight are not on the Library tab; a style value cannot contain the quotes `font-variation-settings` needs. The window is not accepted. Do not mark this phase done. The phase stamp stays `0.69.0`. `PAVILLION_API_VERSION` stays 3. The agent does not commit. Added 2026-10-19 (renamed from "icon picker" the same day). Expanded 2026-10-28: the UI and product work of F-3, F-7, and F-8 is absorbed here. The Icons security Lock stays in [F-HARDEN.md](F-HARDEN.md). F-3, F-7, and F-8 as implementation phases are superseded by D-9.

Look (sizes, spacing, control styles, animation) is Andrew's to set. This section fixes what each screen does and what it stores.

**Goal:** One popup, the Icon Wizard, opens from any icon slot fully populated (library loaded, current value, default, presets) and returns one sanitized icon value. Settings → Icons lists every icon in the app by where it appears and supplies the library/kit the Wizard uses. The same Wizard serves account icons, profile photos, and later folders and labels.

### Settings → Icons module

What it shows:

- **Libraries.** The list in `preferences.ui.iconLibraries` (F-HARDEN Lock shape: type + https URL on the allowlist). **No search metadata ships in the app tree** (D-Q10). Presets offered in the UI (hosts must already be allowlisted): **Font Awesome kit** (`45801522ef`, default enabled) and stubs for **Material Symbols** and **Iconify** (or similar) that the user can enable/add. Add (type, URL or kit id), enable, reorder, remove. On enable/add, fetch that library's script/stylesheet **and** its search metadata; **cache metadata in the profile SQLite** for reuse. One library is **active** for Foundry; Library mode search uses the **FA searcher** when the active/selected lib is FA, else the **generic library searcher** (Iconify and others). An off-list host shows an error and the note that a new host needs a rebuild. The page builds `<script>`/`<link>` from validated URLs; no HTML from settings. Replaces today's kit field and Source select; migrate `ui.fontawesome_kit` into the FA preset entry.
- **Registry tabs.** Every entry in the icon registry `{ key, section, label, default, sizeHint? }`, one tab per `section` (Nav, Mail, Mail list, and the rest of the registry). The selected tab is a plain table, not Tabulator: a **Default** check when that key has no saved override, the icon at one size, and the name. A click on the row opens the Wizard. The registry covers every icon the UI paints; any code that paints an icon reads it from the registry. The Wizard's 16 px chip (D-Q12) is unchanged.
- **Recent cap.** Settings → Icons option for how many recent picks to keep (`preferences.ui.iconRecentMax`, default **12**; D-Q13). Trims `iconRecent` when lowered.
- **Search** filtering the open tab by label or key. **Revert all** (confirm) clears every saved icon. A per-row Revert button is not on this table.
- **Click a row → Icon Wizard** for that key, preloaded with: key, label, section; current value; default value (theme value if a file theme sets one, else shipped); active library and the enabled list; the presets for that key.

What it stores: overrides only, `preferences.ui.icons[key] = html`, through `prefs.ui.set`. A key absent from the map means "use the default". Saving is immediate on Wizard Apply; there is no separate Save button on the page.

### Icon Wizard popup

Chrome (J-1 dialog; D-Q6, Andrew 2026-10-28):

- **Title:** the label and section (e.g. "Mail — Nav"); key shown small.
- **Left square preview:** side length equals the combined height of the two toolbars; shows the icon in progress; updates live on every change; every paint goes through the sanitizer. When the key is small-chrome (D-Q12), also show a **16 px chip** beside the large preview, rendered on **both white and black** backgrounds for contrast.
- **Toolbar 1:** mode tabs **Icon Library | Icon Foundry | SVG | PNG**, plus **Accept** and **Cancel**. Opens on the tab that matches the current value (FA/Material snippet → Library if it is a single plain icon, else Foundry; `pav-asset:` SVG → SVG; `pav-asset:` bitmap → PNG; system id → Library, System shelf). Accept sanitizes, stores, updates every use live (toast with Undo). Cancel discards. Switching tabs keeps each tab's state for the session; Accept takes the active tab's value.
- **Dismiss.** Close, Cancel, Escape, and a click on the backdrop all close the Wizard. That asks only when an edit has made the working icon differ from the one that was opened.
- **Zoom.** Ctrl+wheel over the Wizard zooms it from 100% to 300% by 5% (default 100%, `preferences.ui.iconWizardZoom`). The Away/Toward switch applies. A wheel over the color picker inside the Wizard still zooms the picker.
- **Toolbar 2:** **default first**, then system shelf, then recent (D-Q13). Curated per-key presets empty in v1 (D-Q16). Choosing one loads it into the work area and the square preview.
- **Work area** (rest of the panel): the active mode (search grid, Foundry editor, or image canvas).
- **Revert to default** is **Revert all** on the Icons page. It is not a column on the section table, and it is not required on Toolbar 1.
- Sanitizer rejection shows under the work area; Accept is disabled; what was removed is listed as text (e.g. `removed: onerror on img; img`).

#### 1. Icon Library

Pick one icon from a library.

- **Library select** (enabled libraries, active one preselected) and, for FA, **family** (Classic, Sharp, Duotone, Sharp Duotone, Brands, plus any family the kit enables) and **style** (Solid, Regular, Light, Thin) filters; for Material, style (Outlined/Rounded/Sharp) and fill/weight where the font supports it.
- **Presets** live on **Toolbar 2** (not duplicated in the work area): default first, then system shelf, then recent picks (D-Q13 locked: shared prefs list, cap via Settings → Icons option default 12; no favourites). Curated `presets: []` stay empty in v1 (D-Q16); may revisit with presets-per-library later.
- **Search field** over the selected library's **cached** metadata (names + keywords; D-Q10). FA kit → FA searcher (family/style filters); other libs → generic searcher. Results grid fills the work area. Click selects; double-click Accepts. If metadata is not yet cached, fetch-and-store then search.
- **Light modifiers** for a single icon: color, size step, rotate (90/180/270), flip H/V, fixed width. Anything needing nesting (rotate *and* flip, layers, stacks) shows "Open in Foundry", which carries the current choice across.
- Output: one `<i class="fa-…">` or `<span class="material-symbols-…">name</span>`, or a system reference.

#### 2. Icon Foundry

Write icon HTML directly, like Font Awesome's own wizard.

- **CodeMirror 6, HTML mode** (already in `ui/package.json`), multiline. The document is text only; it is never inserted into the DOM. **Preview** button refreshes the preview strip through the sanitizer; no refresh on keystroke.
- **Tool bar** that edits the text: insert icon (opens the Library search inline, inserts at cursor); wrap selection in `fa-layers` / `fa-stack`; wrap in a span to apply a second transform (this is how rotate + flip is done: an outer element rotates, an inner one flips); flip H/V, rotate (fixed and `--fa-rotate-angle`), scale/grow/shrink, shift (`fa-transform` / `--fa-*` vars), color and duotone colors (`--fa-primary-color`, `--fa-secondary-color`, opacity), animation classes (beat, fade, spin, …; any kit `fa-*`, D-Q12a locked), layer text/counter. Tools act on the element at the cursor.
- Uses the active FA kit (or Material for ligature spans).
- Output: the sanitized HTML snippet.

#### 3. SVG

Import an SVG and adjust it.

- **Pick file** (native dialog) or choose a previously imported SVG asset from this profile.
- Sanitized on import (F-HARDEN sanitizer, SVG rules: no script, no `on*`, no `foreignObject`, no external `href`, no `<use>` to outside, no CSS `url()`); refused with the reason if not.
- **Canvas editor** (profile-photo style): drag to translate, scale slider/wheel, rotate (free + 90° steps), flip H/V, crop to square viewbox, optional color override for single-color SVGs (`currentColor`), background none/color. Round/square mask preview.
- Output: the **transformed SVG itself**: transforms baked into a wrapping `<g transform>` and `viewBox`, stored as an asset; snippet is always `<img src="pav-asset:ID">` (D-Q7a). Stays vector. Animation (SMIL/CSS) still runs in the SVG document the `<img>` loads.

#### 4. PNG

Import a bitmap (**PNG only**, D-Q9) and adjust it.

- Same canvas editor as SVG: translate, scale, rotate, flip, square crop, circle/rounded mask, background fill.
- Output: a **rendering** of the final view, not the raw file: drawn to an offscreen canvas at a fixed output size (**128×128**, D-Q9), encoded PNG, stored as an asset; snippet `<img src="pav-asset:ID">`. The **original is kept** for re-editing (D-Q8).
- This is the path for profile photos and headshots.

### Outputs by mode

| Mode | Stored value | Asset |
|---|---|---|
| Library | one FA `<i>` or Material `<span>`, or `pav-system:ID` img | none |
| Foundry | sanitized HTML snippet (layers / nesting allowed) | none |
| SVG | `<img src="pav-asset:ID">` (D-Q7a) | sanitized, transformed SVG |
| PNG | `<img src="pav-asset:ID">` | rendered PNG (+ optional original) |

Every value is stored as HTML under the key (or the caller's field) and passes the shared sanitizer before any DOM write. No free file paths, no large `data:` URLs in preferences.

### Callers

- Settings → Icons (any registry key).
- Settings → Accounts: the account `icon` field (K-Q9); replaces the 12-icon row and the `<i>`-only gate in `accountSwatch`.
- Later: profile photo, folders, labels, Grok Bots. Caller passes `{ title, current, default, presets, modes }` and gets back `{ html }` or cancel; a caller may restrict modes (e.g. photo = PNG only).

### System icons

A built-in shelf of shipped images, referenced by stable id, not by path: first entry `mark` (the roof mark, `src-tauri/icons/source.png` → `ui/public/mark.png`). Resolved by the renderer to the shipped file (`pav-system:mark`, D-Q7a). Shown in the Library presets for every key. Nav Mail's shipped default returns to `mark`.

### Bugs this phase must kill (diagnosed 2026-10-20)

Andrew could not change the nav icons himself (removing Mail's `fa-flip-horizontal`, switching Update to `fa-up-down`). In `ui/src/shell.js` (`#icon-form`):
1. **Wrong comparison for unsaved changes.** Preview and Reload & Save compare with the field's `defaultValue` (template text), not the saved override; an edit back to the shipped value counts as no change and both buttons stay disabled.
2. **Edits kept only in memory until a button is pressed.** Typing writes `preferences.ui.icons[key]` in memory with no save; a stale edit may persist later.
3. **Saved overrides win over shipped defaults.** With `iconSource: "override"` every saved key is used, so a new default never reaches a profile that saved all keys (Andrew's profile: all eight saved).
4. **A file theme ignores overrides** at load (`applyIcons` paints the theme's `--pav-icon-*`).
5. Not the cause: no class allowlist rejects those classes.

The rework: dirty = differs from saved; Apply stores immediately; overrides store only changed keys (Revert deletes the key); precedence override > theme > shipped, shown by the source badge; the Source select is removed. Migration: one-shot clear of `preferences.ui.icons` (D-Q14 locked: reset all).

### Decisions

- Andrew, 2026-10-19: Foundry/Library output is a self-contained HTML snippet; the account `icon` field holds it (K-Q9). Imported images live in a per-profile asset store with no files outside the database. One color picker from npm (MIT/ISC/BSD/Apache-2.0), no CDN, shared with the account color. **Resolved D-Q7b:** Coloris; Accounts uses Coloris for color and the Icon Wizard for icon.
- Andrew, 2026-10-28: one phase (old D-9 + F-3/F-7/F-8 UI). Four mode tabs: Icon Library, Icon Foundry, SVG, PNG. SVG result is the SVG; PNG result is a rendering. Settings → Icons is the grouped registry list plus library/kit choice, and supplies everything the popup needs. Same popup for account icons and profile photos. The roof mark is a pickable system icon (answers old D-Q11: Wizard is opened from the Icons page; the page is the list, not an editor).
- Andrew, 2026-10-28 (D-Q6): Wizard chrome — left of the top bars, a **square preview** whose side equals the combined height of **two** standard toolbars; it always shows the icon in progress and updates live on every change. **Toolbar 1:** the four mode tabs (Icon Library | Icon Foundry | SVG | PNG) plus **Accept** and **Cancel**. **Toolbar 2:** suggested presets for this key, **default first**, then curated alternatives (and system picks where relevant). Below both bars: the mode work area (search, Foundry editor, or image canvas). No separate Current/Default/Preview strip; the square is the working preview. Accept writes; Cancel discards. (Revert-to-default stays available from the Icons list and/or as a control in the work area — confirm in D-Q13/UI polish if needed.)
- Andrew, 2026-10-28 (D-Q7): **Broad first, clamp later.** Icon sanitizer is a **blocklist**, not a tight allowlist: support multicolor icons, gradients, and animated SVG (SMIL/CSS) that the user is fond of. Still strip active content that can run script in the main window (`script`, event handlers `on*`, `javascript:` URLs, `iframe`/`object`/`embed`/`foreignObject`, remote `<use href>`, etc.). Style may include colors, gradients, filters, transforms, and animation — refine the blocklist only after real icons break or scare us. **Remote images:** outside https links are fine as a *source*; the Wizard **fetches and stores** the bytes in the asset store and the snippet references the stored asset (`pav-asset:` / system id), never hotlinks the remote URL at paint time. F-HARDEN Icons Lock text for the sanitizer will be revised to this blocklist policy when D-9 is signed off (libraries stay allowlisted https hosts).
- Andrew, 2026-10-28 (D-Q7a): SVG (and PNG) results always use `<img src="pav-asset:ID">` after store; no inline `<svg>` as the default snippet. System mark uses `pav-system:mark`.
- Andrew, 2026-10-28 (D-Q7b): Color picker is **Coloris** (`@melloware/coloris`, MIT). Custom `swatches` array we own (seed defaults + optional user-added in prefs); Hex/RGB/HSL toggle + alpha; dark theme. Shared with **Accounts color**. Accounts also uses the **Icon Wizard** for the account icon (both tools).
- Andrew, 2026-10-28 (D-Q8): Asset store = SQLite in the profile DB; SHA-256 content ids; dedupe on insert; keep PNG originals for re-edit; **no auto-GC** — user browses/deletes via an asset list. Export rides profile backup.
- Andrew, 2026-10-28 (D-Q9): Bitmap import **PNG only**, **5 MB**/file; stored render **128×128**; soft warn at **128 MB** profile total (no hard block). No JPEG/WebP; no 64×64 twin.
- Andrew, 2026-10-28 (D-Q10): No bundled icon metadata. Settings → Icons = library list; FA kit shipped default; add Material / Iconify / other kits as links; fetch metadata into profile SQLite cache. FA-specific search tool + separate generic searcher for Iconify and the rest. May use FA Duotone Sharp Solid together with Iconify families.
- The old 3-layer model is replaced by Foundry (nesting in text) for FA, and by SVG/PNG for images. Mixing a photo with FA layers is not a goal (D-Q15 locked: no mix).
- Security per F-HARDEN Icons Lock: allowlisted https libraries, kit only (FA Pro not vendored), registry, overrides as key→html, shared sanitizer before every DOM write, CodeMirror is a text editor only.

- Andrew, 2026-10-28 (D-Q12): One value per key (no dual store). Wizard shows a 16 px chip for small-chrome keys on white and black backgrounds. The Icons page paints each icon at one size in the section table (the earlier per-row display size is withdrawn).
- Andrew, 2026-10-28 (D-Q12a): Allow any `fa-*` class the active kit defines (broader than an animation-only allowlist); sanitizer still strips script vectors.
- Andrew, 2026-10-28 (D-Q13): Recent picks only (deduped in `preferences.ui.iconRecent`); cap is a Settings → Icons option (default 12, e.g. `preferences.ui.iconRecentMax`); no favourites/stars.
- Andrew, 2026-10-28 (D-Q14): One-shot reset of all saved icon overrides (`preferences.ui.icons` cleared once); not equals-default prune.
- Andrew, 2026-10-28 (D-Q15): No combining images with FA layers; modes stay separate.
- Andrew, 2026-10-28 (D-Q16): Default-only presets for v1 (`presets: []`); may revisit with presets-per-library later.
- Andrew, 2026-10-28 (D-Q17): Registry = every existing paint path + missing-key test; grow with modules; no speculative keys; account/profile icons are callers not rows.
### Open questions for Andrew

- D-Q7a: **Locked 2026-10-28.** SVG mode results are always stored as an asset and referenced with `<img src="pav-asset:ID">` (and `pav-system:mark` for the roof mark). Same paint path as bitmaps; prefs stay small; animation still runs inside the SVG the `<img>` loads. Inline sanitized `<svg>` is not the default snippet form (Foundry may still emit FA/`<i>` HTML).
- D-Q7b: **Locked 2026-10-28.** **Coloris** (`@melloware/coloris`, MIT) — npm, no CDN; Hex/RGB/HSL toggle, alpha, dark theme. Custom swatches via `swatches: string[]` (empty disables). Seed a default row (logo reds + warm neutrals); may persist user-added swatches in prefs. Shared with **Accounts color**; Accounts also opens the **Icon Wizard** for the account icon. iro.js stays out (MPL-2.0).
- D-Q8: **Locked 2026-10-28.** Per-profile SQLite asset table (bytes in the DB, not loose files). **id** = content-addressed SHA-256 of the bytes (snippet uses a short hex form, e.g. `pav-asset:<hex>`); columns: id, mime, bytes, created, kind (`svg` / `png-render` / `png-original` / `fetch`). **Dedupe** on insert by hash. **Keep PNG originals** beside the rendered PNG so PNG mode can re-open crop/scale. SVG has no separate original. **No automatic GC** — unused assets stay until the user deletes them. Settings (or Wizard) exposes an **asset list** to browse and delete. Export/backup rides the profile DB.
- D-Q9: **Locked 2026-10-28.** Bitmap import is **PNG only** (refuse JPEG/WebP/other with a clear reason). **Import cap 5 MB** (file bytes). Stored render is a single **128×128** PNG (CSS scales for smaller UI; 64×64 rejected as too soft for real photos). Soft warn in the asset list at **128 MB** per-profile total; never hard-block Accept. SVG mode unchanged (its own path; same 5 MB import cap).
- D-Q10: **Locked 2026-10-28.** **No metadata bundled in the app tree.** Settings → Icons holds a **list of supported icon libraries** (typed entries on the host allowlist). Ship the **FA kit** (`45801522ef`) as the default enabled library. Users can add further kits/libraries (e.g. Material Symbols, Iconify collections) as links; on add/enable the Wizard **fetches** that library's search metadata (and stylesheet/script as today) and **caches it in the profile SQLite** (blob/table) for offline reuse until refreshed or removed. **Two search surfaces in Library mode:** (1) **FA search** — kit-aware (families/styles: Duotone Sharp Solid etc.); (2) **generic library search** — for Iconify and other non-FA libraries once loaded. Active library (or multi-select of enabled libs) drives which searcher runs. Pro SVGs/CSS/fonts still never vendored into the repo (F-HARDEN); cache is per-profile data, not source.
- D-Q12: **Locked 2026-10-28.** **One value per key** — no dual large/small store. Wizard square preview also shows a **16 px chip** when the registry marks the key as small-chrome, so mush is visible before Accept. The 16 px chip is drawn on **both a white and a black background** (contrast check without a separate light/dark icon). Settings → Icons is one tab per section and a plain table: a Default check when there is no saved override, the icon at one size, and the name. A row click opens the Wizard. If a photo must work at 16 px, pick a simpler Library glyph for that key or accept the soft look; revisit dual large+small later only if real keys need it.
- Andrew, 2026-10-28: The Wizard closes from Close, Cancel, Escape, or the backdrop. It asks only after an edit changes the working icon. Ctrl+wheel zooms the Wizard, 100–300% by 5%, default 100%, stored as `preferences.ui.iconWizardZoom`. The Away/Toward switch applies.
- D-Q12a: **Locked 2026-10-28.** Allow **any `fa-*` class the active kit defines** (animation utilities included: spin, beat, fade, bounce, flip, shake, pulse, speed/delay modifiers, and other kit class names). Sanitizer does **not** strip `fa-*` by animation policy; still strips script vectors / hostile attributes per F-HARDEN blocklist. Nav defaults stay static; Foundry may add motion.
- D-Q13: **Locked 2026-10-28.** **Recent only** — no favourites/stars. On Accept, push the sanitized snippet into `preferences.ui.iconRecent` (profile prefs): **deduped**, newest first. Cap is a **Settings → Icons option** (default **12**; stored e.g. `preferences.ui.iconRecentMax`). Shared across keys; shown on Toolbar 2 after system shelf. Overrides already pin per-key choices.
- D-Q14: **Locked 2026-10-28.** **Reset all** — one-shot clear of `preferences.ui.icons` on first load of the new Icons code (migration flag). All eight (and any other) saved overrides go; shipped defaults (incl. Mail → mark when that lands) take effect. No equals-default prune; no leave-as-is.
- D-Q15: **Locked 2026-10-28.** **No mix.** Library/Foundry = FA (or Material) HTML only; SVG/PNG = `pav-asset:` img only. FA layers/stacks stay FA-only. No photo-inside-layers paint path in D-9.
- D-Q16: **Locked 2026-10-28.** **Default-only for v1** — registry `presets: []` everywhere; Toolbar 2 shows default + system shelf + recent (no curated alternatives). Rely on Library search. May revisit later with a **presets-per-library** option (not in D-9 scope).
- D-Q17: **Locked 2026-10-28.** **Every icon `ui/src` paints today** has a registry key (Nav, module bars, Accounts, Help, Settings, existing dialogs/toasts/tree, tab kinds, Update spin, …). Missing-key test fails CI. **Grow with modules** — no speculative keys for unbuilt UI. Account icons and profile photos are Wizard callers, not registry rows (per-entity).

**Entry gate:** Andrew answers the open questions; J-1 and E-7 complete (dialogs, tree engine; both met); F-2 complete.

**Work items (high level, for Grok Build):**

- [x] Libraries: `preferences.ui.iconLibraries`, allowlist from one source (`ui/src/common/iconhosts.json` → C + JS), `prefs.ui.set` validation, CSP Google Fonts hosts, `loadIconLibraries(list)` replacing the raw-HTML kit branch in `ui/src/main.js`, migration of `ui.fontawesome_kit`.
- [x] Shared sanitizer (**blocklist** per D-Q7: gradients/animation OK; strip script vectors) with the hostile suite; HTML-sink grep test `ui/test/no-html-sinks.test.js`; revise F-HARDEN Icons Lock text to match.
- [x] Registry `ui/src/common/icons/registry.js` with every painted icon; all painting goes through it; system icon shelf.
- [x] Settings → Icons rewrite: libraries list, registry tree, badges, search, Revert/Revert all; removes `#icon-form` and its bugs.
- [x] Wizard popup `ui/src/common/icons/wizard.js`: left square preview, two toolbars (modes+Accept/Cancel; presets), four tabs.
- [ ] Library tab with metadata search and presets; Foundry tab with CodeMirror and tool bar; SVG and PNG tabs with the shared canvas editor. Search, presets, Foundry, and Material ligatures are in. The canvas editor (drag, mask, live preview) is not.
- [x] Asset store (core + command), `pav-asset:` / `pav-system:` resolution.
- [x] Accounts caller (icon field), Nav Mail default = `mark`.
- [x] Vitest: dirty vs saved; Apply stores only the key; Revert removes the key; Cancel leaves the blob unchanged; no preview on keystroke; library URL validation; sanitizer suite; PNG render size. Unity: library validation, asset store.
- [x] Help page (`ui/src/help/pages.js`) for Icons and the Wizard.

**Done means:** In Settings → Icons Andrew finds Nav → Mail, opens the Wizard, picks the roof mark from System, applies, sees it live; changes Update to `fa-up-down` and back; builds a rotated + flipped icon in Foundry; imports an SVG and a PNG headshot, adjusts, applies one as an account icon; reverts each. Restart keeps them. Pasting the hostile suite's worst input previews as rejected and Apply refuses it. An off-list library URL is refused with the rebuild note; Material Symbols renders.

**Exit gate:** `make test` exits 0; the Done flow works in `make build`; CI green on fedora, macos-core, windows-core.

**Handoff:** Docs only until Andrew signs the open questions. Then Grok Build implements this phase as one Hard session (or split only if he asks). Do not start code here before that sign-off. Security Lock remains [F-HARDEN.md](F-HARDEN.md) Icons. F-3, F-7, and F-8 stay superseded. After Apply lands, Help covers Settings → Icons and the Wizard. The index Resume still names which phase may run; D-9 does not steal the Resume until Andrew pulls it forward.

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
- **Table modules.** A module with a table opens its column chooser from the row indicator header (the three dots, `onIndicatorHeader`; TABULATOR.md Row indicator column). A toolbar column chooser button (`fa-table-columns`, right side, before search) is optional; Accounts dropped its button 2026-10-27. Table colours are the `--pav-table-*` variables (TABULATOR.md, Table look). A split's table pane is not capped at a percentage: it may grow until its partner pane reaches its own minimum (Accounts: the form keeps `20rem`; 2026-10-20). A module body with its own zoom rows (Accounts table and Accounts form, K-Q14) zooms in place. A table scales its font and column widths (TABULATOR.md, Zoom in place) and never uses CSS zoom; a form may use CSS zoom. Zoom never changes a pane's width.
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
- 2026-10-24 N-3 complete, step 61. `make test`: 248 Unity, 2 Rust, 395 Vitest, exit 0. CI on `28ec6a9` is green: fedora, macos-core, windows-core. Gzip -9 of `ui/dist` is 292633 bytes. The index names Q-2 next. The D-8 Formats pane is used by N-3's time column via `formats.js`.

- 2026-10-27 App mark refreshed: red faceted pavilion/M with a silver perimeter stroke (transparent PNG). Source of truth is `src-tauri/icons/source.png`; `ui/public/mark.png` is a copy (Mail nav button and any other roof-mark use). Regenerated the Tauri icon set from that source with `tauri icon`. No frame/squircle around the mark.
- 2026-10-27 Splash mark (Lock): the cover shows `/mark.png` above the "Pavillion <version>" line, 0.75rem gap. The mark is exactly as wide as that rendered line, height auto. CSS does the sizing with no JS measuring: `.splash-lockup` is a `width: max-content` grid column. The image is `width: 0; min-width: 100%`, so only the text sets the column width and the image fills it. It follows the version string, font, zoom and resize on its own. Decorative (`alt=""`). It fades in with the text.
- 2026-10-27 Help mark (Lock): the first Help page ("Pavillion <version>") shows the same mark above its title, sized the same way. The shared class `.pav-mark-lockup` replaces `.splash-lockup`, so the splash and Help use one rule. Jaggies: no CSS forces `image-rendering`, so WebKit smooths images by default and CSS has no other antialias switch. PNG is lossless, so the build does not compress the mark lossily; `make mini` only resizes it to 256 px with Lanczos. The cause is the source: its alpha is binary (0 partly transparent pixels of 1,048,576), so the outer edge is a hard stair-step, and WebKit downscaling about 9× (1024 to roughly 110 px) leaves some of it visible. The fix is in the asset: an antialiased alpha edge, or a pre-downscaled 256 px copy like the mini one.

- 2026-10-27 App mark silver perimeter thickened (~50% heavier rim) so it still reads at small icon sizes; `source.png` / `mark.png` and the Tauri set regenerated.

- 2026-10-27 Soft-edged the app mark PNG (Lanczos 4× up then down to 1024) so the outer edge has fractional alpha; reduces stair-step jaggies when shrunk on splash/Help. Regenerated Tauri icons from the soft `source.png`.
- 2026-10-27 Native splash (replaces the CSS cover in Splash mark and the 2026-10-23 reveal wait; those entries stay as history): the start splash is a native window, not a webview and not CSS. `src-tauri/src/splash.rs` opens it in `setup` right after the saved rect is applied. Since t136 it is drawn inside the main window, not as a second window: the webview is moved into a `gtk::Overlay` in the same GTK window (`default_vbox`), a black drawing area sits over it, and the main window is shown at once. A separate splash window could not match the main window exactly (inner vs outer size, decorations, KWin placement). It is black at once and stays black for `BLACK_HOLD_MS` (400 ms); then the mark (`src-tauri/icons/source.png`, logo only, no version line) fades in over `FADE_MS` (400 ms). The page emits `pav-app-ready` after mount and the first paint (a 32 ms timer stands in when a hidden WebKit view skips animation frames); the shell then shows the main window and fades the cover out (widget opacity, 400 ms) and removes it. The fade-out waits for both ready and the envelope black hold + fade-in + `MIN_SHOW_MS` (2000 ms of fully shown mark), all constants in `splash.rs`. `on_page_load` no longer reveals. A missed signal shows the window after 5 s (Andrew, t135). 2026-10-27 (t138): the kit script load came before any icon was drawn (the kit is a loader that then fetches the bundle, which swaps `<i>` for `<svg>` in a later frame), so the cover faded to blank icon slots. The page waits (t139) for Font Awesome's documented `fontawesome-i2svg-complete` class on `<html>` (MutationObserver on its class, no polling; docs.fontawesome.com SVG Async Loading), then one frame, at most `KIT_WAIT_MS` (4 s), before `pav-app-ready`. `complete` covers the current batch only; icons added later (mail tree, sync reloads) swap after the splash. The page releases the FA kit right after mount, so the icon swap starts behind the splash. The first mail sync is started by the core sync supervisor at process start, not by the page; deferring it until after ready needs a core hook (not done). Size: `MARK_LOGICAL_PX` (128 logical px) times the monitor scale, bilinear from the 1024 px source; change that constant to resize. Linux uses GTK (already linked by Tauri). Since t140 Windows and macOS draw it too, with one timing driver (a worker thread sleeps the same envelope and posts each 16 ms fade step to the main thread). Windows: a layered child HWND (`WS_EX_LAYERED`, Windows 8+) over the WebView2 child, filled each frame with a premultiplied BGRA DIB (black plus the mark, decoded with `png` and scaled bilinearly) through `UpdateLayeredWindow`; the cover fade is the blend constant alpha, and the child follows the client rect on each frame. macOS: an opaque black `NSBox` and a centred `NSImageView` (mark at 128 points) added above the WKWebView in the content view with autoresizing; fades set `alphaValue`. New target deps (`png`, `windows-sys` 0.60, `objc2`, `objc2-foundation`, `objc2-app-kit`) were already in the lock via Tauri except `objc2-core-video` (one macOS-only lock entry the AppKit features pull in). Both were checked with `cargo check` for x86_64-pc-windows-msvc and aarch64-apple-darwin but not yet run on those systems. iOS and Android use their launch screens (storyboard / SplashScreen API, one logo on black); not wired yet. `#splash`, its scroll workaround and its JS are gone; Help keeps `.pav-mark-lockup`.

- 2026-10-27 Vite `chunkSizeWarningLimit` raised from 600 to 2000. The UI ships inside the Tauri binary, so dynamic `import()` splitting does not buy a network win; the main chunk (~654 kB) and Tabulator (~238 kB) stay as they are. Raise again if a real size budget appears.
- 2026-10-28 D-9 in progress. Settings → Accounts color: the swatch is a 1.75rem square text field (the native `type=color` well was the odd shape and the second dialog). Coloris is the only picker. Close is shown. Zoom is its own control, 100–300% by 25, default 200%, saved in `preferences.ui.colorPickerZoom`. Alpha stays off because an account color is `#rgb` or `#rrggbb`. The window was not opened. The canvas editor was not started.
- 2026-10-28 D-9 in progress. "Could not save the profile" on launch and on later UI saves: the default Material URL has `@` in the query, and both the core and the page treated any `@` as credentials. `prefs.ui.set` returned `bad_args`. `@` is now credentials only in the authority. The window was not opened. The on-disk profile was not rewritten. The next build's first successful save also applies the one-time icons reset.
- 2026-10-28 D-9 in progress. Color picker: Ctrl+wheel zooms the open picker (the plus and minus are gone). The swatch row is `--pav-picker-swatches` from the theme, then colors saved in `preferences.ui.colorSwatches`, 24 at most. Add and Remove edit the saved colors. Hover shows a CSS color name, or the theme role when the hex matches one. The window was not opened.
- 2026-10-28 D-9 in progress. Color picker: one Ctrl+wheel notch is 5%. Add, Remove, and Close are icons on one row at the bottom. Add and Remove stay visible and disable when they do not apply. The count is gone. The window was not opened.
- 2026-10-28 D-9 in progress. Settings → Icons: one tab per section, and a plain table (Default check, one icon size, name). A row opens the Wizard. The Wizard closes without asking when nothing was edited, and Ctrl+wheel zooms it (100–300% by 5%, default 100%). The window was not opened.

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
- 2026-10-24 Help got a vertical filter panel: a chevron left of the module title opens a full-height, left-edge strip of icon-only category chips (e27292b, UI only). The convention and the plan to lift it into `ui/src/common/` for Accounts and the other modules are in [UI-FILTER.md](UI-FILTER.md) "Vertical filter panel". Not a phase yet. The lift needs a Lock entry here before code.
- 2026-10-27 Status reconciled against git: D-8 and D-10 complete, Q-2 next (PAVILLION_PLAN.md), D-5 and D-9 not started. The D-9 icon editor dirty-check bug (it compares with the shipped default, not the saved value) has no fix commit since it was written up, so it stays open.
- 2026-10-28 D-9 expanded (docs only, Andrew): absorbs the UI work of F-3, F-7, F-8. Settings → Icons = libraries + registry tree; Wizard popup with four tabs (Icon Library, Icon Foundry, SVG, PNG); SVG stores the SVG, PNG stores a rendering; roof mark is a system icon. Kind Hard. Open questions D-Q6–D-Q17.
- 2026-10-28 D-9 in progress. Andrew pulled it forward. Libraries, the sanitizer, the registry, Settings → Icons, the Wizard, the asset store, the Accounts caller, Nav Mail as the roof mark, and the Help page are in. A Material pick stores a ligature span. `make test`: 255 Unity, 2 Rust, 419 Vitest, exit 0. The phase stamp stays `0.69.0`. The window is not accepted. Still open: the SVG and PNG canvas editor (drag, mask, live preview). Material fill and weight wait on a style value that can hold quotes. Q-2 was not started. The agent does not commit.

# D — UI

**Status:** D-3 complete (2026-10-14). D-4 (theme switch) and D-5 (theme extras) were added 2026-10-16 and aren't started. The shell preview below is not a phase.
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

The first frame, the shell preview, and the theme layer. The shell layout (nav, buttons, home tabs, window) is the [J-TABS.md](J-TABS.md) Lock. Security rules for the main window are the [F-HARDEN.md](F-HARDEN.md) Lock. The tree behind the mail and files navs is [E-TREE.md](E-TREE.md). Mail, contacts, calendars, and files have their own letters (N, O, S, T, U).

## Phases

One phase is one session. Do not start the next one in the same conversation.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| D-1 | Frame and theme variables | Easy | Complete |
| D-2 | `api.js` and status text | Easy | Complete |
| D-3 | The error state | Easy | Complete |
| D-4 | Theme switch: semantic variables, dark, light, follow system | Medium | Not started |
| D-5 | Theme extras: density, gradient tuning, the contrast audit | Medium | Not started |

Kind: **Easy** is the static frame or one function in `api.js`. **Medium** crosses one boundary. **Hard** is unused.

## How a session works

1. Read Resume in [PAVILLION_PLAN.md](../PAVILLION_PLAN.md), the Status column above, and the previous phase's Status, Accomplished, Lessons learned, and Handoff.
2. Re-read only this phase: Goal, Work items, Done means, Exit gate. Open the lock below when the phase cites it.
3. Ask the qualifying questions. Wait for approval before editing source.
4. Do this phase only. Check a work item `[x]` only after its check has passed. Defer with `[~]`, one line, and the phase it moves to.
5. Before marking complete, write Status, Accomplished, Lessons learned, and Handoff, and append the Working log. Then stop.
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

The shell layout is in the [J-TABS.md](J-TABS.md) Lock. Changing the active profile is a restart, as [B-CONFIG.md](B-CONFIG.md) says.

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
**Status:** Not started.

**Goal:** Every surface reads semantic `--pav-*` variables, and `preferences.theme` switches dark, light, and system live. The shell preview already has a Theme setting and a `dark.css`, and screens built after G-4 should be built on variables from the start.

**Entry gate:** G-4 complete (variables namespaced `--pav-*`, styles scoped).

**Lock for this phase (was old plan §10 Theming):**

- Dark first. The theme layer follows Lithium (`docs/Li/LITHIUM-CSS.md`): semantic custom properties (`--pav-surface-*`, `--pav-text-*`, `--pav-accent`, `--pav-border`, `--pav-status-*`), dark defaults, and a theme is an override of those properties. No UI framework, no hard-coded colors in components, no import of Lithium's stylesheets or fonts.
- `preferences.theme` names `dark`, `light`, or `system`. `system` follows `prefers-color-scheme`. The control is the Theme heading under Settings; the shell has no theme icon.
- `ui/styles/theme.css` holds the rules and only references variables. `dark.css` and `light.css` assign the same names. A surface may be a gradient: one angle, black in one corner, a very dark red in the other, taken from the roof mark; panels and buttons differ by how far the red reaches. Active and inactive are two variables. `preferences.ui` stores layout only, never backgrounds.
- Density `compact` or `comfortable` (`preferences.density`) is a handful of custom properties (row height, padding, icon size). E-TREE's third density, spacious, is recorded and not promised.
- Fonts: the system UI font by default, Vanadium Sans when chosen; monospace (Vanadium Mono) for the headers view and the log.

**Work items:**

- [ ] Audit every color literal in `ui/styles` and `ui/src`; move each to a semantic variable. Keep `--pav-log-*` from H-2, and give light mode its own values for them.
- [ ] `light.css` assigning the same names (a first pass. D-5 tunes it).
- [ ] `preferences.theme` (`dark`, `light`, `system`) through `prefs.set`. The Theme setting in Settings now persists. `system` follows `prefers-color-scheme` live.
- [ ] Vitest: `theme-vars.test.js` checks that `dark.css` and `light.css` define the same variable names.

**Done means:** Switching dark, light, and system changes every surface live, with no hard-coded color left.

**Exit gate:** `make test` exits 0. `rg '#[0-9a-fA-F]{3,6}' ui/src` finds none outside theme files. In `make build`, all three modes switch live and survive a restart.

## D-5: Theme extras

**Kind:** Medium
**Status:** Not started.

**Goal:** Density, gradient tuning, and a checked contrast floor, on top of D-4's switch.

**Entry gate:** V-4 complete (after the initial working copy).

**Work items:**

- [ ] Density `compact` or `comfortable` (`preferences.density`, per the D-4 Lock) in Settings → Theme.
- [ ] Gradient tuning for both themes (roof-mark angle, how far the red reaches).
- [ ] Contrast at 4.5:1 for text pairs: a Vitest that parses both theme files and computes contrast for the listed pairs.

**Done means:** Both densities and both themes look right and pass the contrast test.

**Exit gate:** `make test` exits 0 including the contrast test. In `make build`, all three modes and both densities look right.

## Working log

- 2026-10-14 D-1: Dark frame. `theme.css` has the five variables. Title row shows Pavillion. Profile slot and body are empty.
- 2026-10-14 D-2: `api.js` calls `status()` once. The body shows `ready` and the profile name is blank.
- 2026-10-14 D-3: A file used as the working directory shows `error` and a blank name. A normal directory still shows Default and ready.
- 2026-10-14 Shell preview: nav, tabs, gradients, and the kit are on screen. Not a phase. Width, zoom, and the window are not saved.
- 2026-10-14 Shell preferences: nav width, the splits, zoom, and the window are saved through the core and restored on launch. Not a phase. Hidden chrome is stored and not applied.
- 2026-10-15 Per-pane zoom: `preferences.zoom` gained `navButtons`, `navBody`, `mailList`, and `mailDetail` beside `shell`. The Zoom section has one slider per pane, the shell scale multiplies each pane, `save_layout` carries the zoom object, and a profile missing the keys gains them on the next load. Not a phase.
- 2026-10-16 D-4 theme layer added from old plan §10. Lock references moved to J-TABS and F-HARDEN. The 50–300% zoom note marked superseded.
- 2026-10-15 Fonts setting: a Fonts heading and panel under Settings. Vanadium Sans is the primary font through `--font`, switchable live; not persisted yet. Not a phase.

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

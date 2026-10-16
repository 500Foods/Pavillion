# J — TABS

**Status:** Not started (exploration in `TABS-DESIGN.md` 2026-10-16, absorbed and scoped the same day).
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

The tabbed workspace and the shared widgets every later screen uses: context menu, tooltip, toast with undo, and dialog. Andrew confirmed the feature set for the first round on 2026-10-16: pinned tabs, keyboard shortcuts, files dropped on the strip, a right-click menu (pin, duplicate, mute, close variants), muting, session restore, vertical tabs, floating tooltips, tab widths, and sandboxed browsing from a `+` tab.

## Phases

One phase is one session. Do not start the next one in the same conversation.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| J-1 | Common widgets: menu, tooltip, toast with undo, dialog | Medium | Not started |
| J-2 | Tab registry, icon and close button, sub-tabs, nav-body clicks | Medium | Not started |
| J-3 | Keyboard shortcuts, tab context menu, pinned tabs | Medium | Not started |
| J-4 | Drag to reorder, overflow (scroll or wrap), tab widths, file drop | Medium | Not started |
| J-5 | Vertical tabs and section colors | Medium | Not started |
| J-6 | Session restore | Medium | Not started |
| J-7 | The `+` tab: sandboxed browsing in a separate webview, Browser settings, mute | Hard | Not started |

J-1 to J-6 come before accounts. J-7 comes after the initial working copy.

Kind: **Easy** is one module and a test. **Medium** crosses one boundary. **Hard** adds a second webview to the process.

## How a session works

1. Read Resume in [PAVILLION_PLAN.md](../PAVILLION_PLAN.md), the Status column above, and the previous phase's Status, Accomplished, Lessons learned, and Handoff.
2. Re-read only this phase: Goal, Entry gate, Work items, Done means, Exit gate. Open the lock section this phase cites.
3. Ask the qualifying questions. Wait for approval before editing source.
4. Do this phase only. Check a work item `[x]` only after its check has passed. Defer with `[~]`, one line, and the phase it moves to.
5. Before marking complete, write Status, Accomplished, Lessons learned, and Handoff, update the Status column here and in the index, and append the Working log. Then stop.
6. A sitting that ends early sets Status to **in progress**, names the last finished work item, and leaves the rest unchecked.

Leave Accomplished, Lessons learned, and Handoff off a phase that has not started. A UI exit gate is the window. Vitest covers modules that do not need a window.

## What exists (2026-10-16)

- `ui/index.html`: the `.tabs` strip with `#tablist` and a `tabs-spacer` carrying `data-tauri-drag-region`.
- `ui/src/shell.js`: a `sections` map (mail, contacts, calendars, files, settings, activity, help), `tabs = new Map()` keyed by section id, and `openSection(id)` that creates or switches a text-only `<button role="tab">`. Home tabs are permanent. No icon, close, drag, or keyboard.
- Nav-body buttons only set `aria-current`.

## Lock

### Shell layout (was plan §10)

- Full-height nav on the left, tabbed workspace on the right. The nav's right edge sets its width (`ui.nav` 320, 200–500).
- Four icon buttons on top and four on the bottom, equal cells, no borders, icon centered, selected cell takes a background, hover is `filter: drop-shadow` on the icon, a focus ring for the keyboard, each with an accessible name. `prefers-reduced-motion` drops motion on the shadow and the update spin.
- Top: Mail (roof mark), Contacts `fa-sharp fa-thin fa-address-book`, Calendars `fa-sharp fa-thin fa-calendar-days`, Files `fa-sharp fa-thin fa-folder`. Bottom: Activity `fa-sharp fa-thin fa-list-timeline`, Settings `fa-sharp fa-thin fa-gear`, Update `fa-sharp fa-thin fa-arrows-rotate`, Help `fa-sharp fa-thin fa-circle-question`.
- A top button swaps the nav body and focuses that section's home tab. Settings does the same with the settings headings in the nav. Activity and Help open their tabs and leave the nav body alone. Update runs a sync and spins ([I-ACTIVITY.md](I-ACTIVITY.md)); it is not a tab.
- Navs: Mail is the mailbox tree, Files the folder tree ([E-TREE.md](E-TREE.md)); Contacts is a letter rolodex, a search field, then accounts and recents; Calendars is a continuous mini-calendar (from the week before the current month to the week after) and calendars grouped by account with a swatch. An IMAP account still shows Files, with an empty tree.
- Home tabs: Mail is the message list beside the open message (split 0.5, 0.25–0.75); Contacts the rolodex list beside the open contact; Calendars the week view (default); Files the folder contents beside a preview; Settings the chosen heading; Activity the feed or log; Help the help page.
- Window: placement and maximized state restored on launch (`preferences.window`). `chrome` is `system` or `hidden`; hidden uses the empty part of the tab strip as the drag region with the buttons still clickable. Per-account status dots in the nav wait.

### Tabs

- A tab is a registry entry, not a DOM node: `{ id, section, kind, title, icon, closable, pinned, muted, context, order }`. `id` is `section:home` or `section:kind:key` (`mail:msg:<accountId>/<emailId>`). The DOM node is looked up by id in a `Map`. Rows are patched, never rebuilt.
- One tab per id. Opening something that already has a tab switches to it. Duplicate makes `id#2`.
- Home tabs are not closable, have no close button, and take the nav icon (including its override). Sub-tabs are closable and take their kind's icon: message `fa-sharp-duotone fa-solid fa-envelope`, contact `fa-sharp-duotone fa-solid fa-address-card`, event `fa-sharp-duotone fa-solid fa-calendar-day`, file `fa-sharp-duotone fa-solid fa-file`, draft `fa-sharp-duotone fa-solid fa-pen-to-square`, browse `fa-sharp-duotone fa-solid fa-globe`.
- Markup: the tab is a `div role="tab"` containing an icon span, a title span (`textContent`), and a separate close `button` with `aria-label="Close <title>"`. No button inside a button. Tab text 0.8125rem, icon 0.875rem.
- Close order: closing a sub-tab focuses the tab to its left, else its section's home tab. A tab with unsaved work (a composer) asks through the dialog unless its owner reports it saved (drafts autosave, so this is rare).
- A view in a tab gets `onOpen(ctx)` and `onClose()`. `onClose` releases what it holds: unsubscribe events, destroy editors, and call `view.release {handle}` when it opened a core handle. Core-side caches (body cache, blobs) are not freed on tab close; they are shared and evicted by their own policy in [L-STORE.md](L-STORE.md).
- Shortcuts (scoped to the workspace): `Ctrl+Tab` next, `Ctrl+Shift+Tab` previous, `Ctrl+W` and `Ctrl+F4` close, `Ctrl+T` new browse tab (J-7; before it, no-op), `Ctrl+Shift+T` reopen last closed, `Alt+1`…`Alt+9` go to tab N, `Ctrl+R` reload the active view (re-run its load; never reloads the WebView), middle-click closes. `Ctrl+1/2/3` stay section switches (mail, contacts, calendar, plan §6.13); `Ctrl+4` is files.
- Context menu on a tab: Pin/Unpin, Duplicate, Mute/Unmute (browse tabs only), Close, Close others, Close to the right, Reopen closed tab. Home tabs show Pin and Duplicate disabled.
- Pinned tabs sit left of all others, icon only, fixed width 2.25rem, and keep their order.
- Reorder: pointer events (the nav-resize pattern), a drop indicator, and `Alt+Shift+Left/Right` on the focused tab with an `aria-live` announcement. Any tab can move anywhere except that pinned tabs stay in the pinned group.
- Overflow: `ui.tabs.overflow` is `scroll` (default) or `wrap`. Scroll shows chevrons at both ends and the native thin scrollbar under the strip; the drag spacer keeps a minimum of 3rem on the right. Wrap never splits a tab.
- Widths: `ui.tabs.minWidth` 6rem, `ui.tabs.maxWidth` 12.5rem (200 px at 100%), `0` means no max.
- Vertical tabs: `ui.tabs.orientation` `horizontal` (default) or `vertical`; vertical puts the strip as a column between the nav and the workspace, icon + label, full title in the tooltip, pinned icon-only, scrolls vertically. Toggled from the strip's context menu (right-click on empty strip) and from Settings → Tabs.
- Section colors: `ui.colors.{mail,contacts,calendars,files}` as `#rrggbb`, exposed as `--pav-section-<id>`. The active tab gets a `color-mix(in srgb, var(--pav-section-mail) 18%, transparent)` underlay over the surface gradient, and the tab icon takes the color. Account colors (the account `color` field) are separate and paint the message-list color bar, the tree swatch, and account rows.
- Tooltips: one floating tooltip module, anchored to the element (not following the cursor), shown after 500 ms hover or on keyboard focus, flipped to stay in the viewport, `role="tooltip"` with `aria-describedby`. Text only.
- Session restore: on quit and every 30 s, the open tab list (id, section, kind, title, context, pinned, muted, order, active) is saved to `ui.session.tabs`. On launch the active tab loads first and others load when first shown. A tab whose context no longer resolves (a deleted message) is dropped with a toast. `ui.session.restore` (default true) turns it off.
- Dropping files on the strip opens one tab per file through the section that claims the type: `.eml` opens a message tab, `.ics` an event import, `.vcf` a contact import, anything else a file preview tab (U). Until a section exists, the drop shows a toast naming the file and the section that will handle it.

### Common widgets

- `ui/src/common/menu.js` (context and dropdown menus, ARIA `menu`/`menuitem`, arrow keys, `Escape`, Shift+F10 and the Menu key open it for the focused element).
- `ui/src/common/tooltip.js` (above).
- `ui/src/common/toast.js`: `toast(text, { action: "Undo", onAction, timeoutMs: 5000 })`; a stack in the bottom right; `aria-live="polite"`.
- `ui/src/common/dialog.js`: `confirm({ title, text, okLabel, danger })` and `prompt(...)`, built on `<dialog>`, focus trapped, `Escape` cancels. All text with `textContent`.
- Every widget scopes under `.pav-root` and uses `--pav-*` variables (G-4).

## J-1: Common widgets

**Kind:** Medium
**Status:** Not started.

**Goal:** Menu, tooltip, toast, and dialog exist and are used once each in the shell.

**Entry gate:** G-4 and I-1 complete. Widgets do not wait on the activity view; I-2 uses these widgets.

**Work items:**

- [ ] The four modules in the Lock, with CSS in `ui/styles/widgets.css`.
- [ ] Tab icons use the shell's existing icon classes. F-7 moves them onto registry keys `tab.kind.*`. This phase does not take icon HTML.
- [ ] Use them: the Activity nav button's right-click menu (Activity, Log). H-2 has already built the log view and the segmented control; this menu switches those views. I-2 fills the Activity side. Tooltips on the eight nav buttons, a toast on profile save failure, a dialog for the F-4 link confirm. F-4 has landed, so the dialog has a caller.
- [ ] Vitest with jsdom: `menu.test.js` (keyboard navigation, Escape), `dialog.test.js` (resolve values), `toast.test.js` (action fires once, timeout).

**Done means:** The four widgets work by mouse and keyboard and replace any ad hoc versions.

**Exit gate:** `make test` exits 0. In `make dev`: right-click Activity, choose Log; hover a nav button for the tooltip; tab to a nav button and see the tooltip on focus.

## J-2: Tab registry, appearance, and sub-tabs

**Kind:** Medium
**Status:** Not started.

**Goal:** Tabs are a registry with icons and close buttons, and a feature can open a sub-tab.

**Entry gate:** J-1 complete.

**Work items:**

- [ ] `ui/src/shell/tabs.js`: `openHome(section)`, `openTab({section, kind, key, title, icon, context})`, `closeTab(id)`, `activate(id)`, `list()`, and a change listener. `openSection` becomes `openHome`.
- [ ] Render per the Lock: icon, title, close button; home icon from the nav icon (override included).
- [ ] View lifecycle: a section registers `views[kind] = { create(el, ctx), onOpen, onClose }`.
- [ ] Nav-body clicks open or switch to the home tab and pass the selection (`openHome("mail", { mailboxId })`).
- [ ] Debug-only Help item `Open test tab` opens a closable sub-tab so the feature is testable before mail.
- [ ] Vitest: `tabs.test.js` covers one-tab-per-id, close focus order, home not closable, change events.

**Done means:** A sub-tab opens with an icon, closes with its button or middle-click, and focus returns per the Lock.

**Exit gate:** `make test` exits 0. In `make dev`: open three test tabs, close the middle, focus moves left; home tabs show no close button.

## J-3: Shortcuts, tab menu, pinned tabs

**Kind:** Medium
**Status:** Not started.

**Goal:** The tab strip is fully usable from the keyboard and the context menu, and tabs can be pinned.

**Entry gate:** J-2 complete.

**Work items:**

- [ ] `ui/src/common/keys.js`: one shortcut registry with scopes (`global`, `workspace`, `tree`, `list`, `composer`), so `*` in the tree and `*` in the message list do not collide (E Lock). Bindings are data, ready for remapping later.
- [ ] Register the Lock's tab shortcuts. `Ctrl+Shift+T` uses a 20-entry closed-tab stack.
- [ ] Tab context menu with the Lock's items; strip context menu with Vertical tabs (J-5 enables it) and Close all sub-tabs.
- [ ] Pinned tabs per the Lock, persisted in the session (J-6 restores them; until then pin state lasts the run).
- [ ] Vitest: `keys.test.js` (scope resolution, chords like `g i`).

**Done means:** Every action in the tab menu has a keyboard path, and pinned tabs stay left.

**Exit gate:** `make test` exits 0. In `make dev`, with the mouse untouched: open, switch, pin, close, and reopen tabs.

## J-4: Reorder, overflow, widths, file drop

**Kind:** Medium
**Status:** Not started.

**Goal:** Many tabs stay usable.

**Entry gate:** J-3 complete.

**Work items:**

- [ ] Pointer reorder with an indicator; keyboard reorder with the announcement.
- [ ] Overflow modes with a `ResizeObserver`; chevrons scroll by one tab; the active tab is scrolled into view on activate.
- [ ] Min and max width settings under a new Settings → Tabs heading (overflow, widths, orientation placeholder).
- [ ] File drop on the strip (Tauri's drag-drop event through a `hostServices.onFileDrop` hook so the UI stays host-agnostic) with the Lock's routing and the interim toast.
- [ ] All settings in `preferences.ui.tabs`.

**Done means:** Thirty test tabs can be reordered, scrolled, or wrapped, and widths apply live.

**Exit gate:** `make test` exits 0. In `make dev`: open 30 test tabs, switch overflow modes, reorder by mouse and keyboard, drop a file and see the toast.

## J-5: Vertical tabs and section colors

**Kind:** Medium
**Status:** Not started.

**Goal:** The strip can be vertical, and each section can carry a color.

**Entry gate:** J-4 complete.

**Work items:**

- [ ] `.pav-tabs-vertical` layout per the Lock; the toggle in the strip menu and Settings → Tabs; a vertical-strip width that the user drags (`ui.tabs.verticalWidth`, 10–20rem).
- [ ] Settings → Colors heading: one `<input type="color">` per section with reset; `--pav-section-*` applied live.
- [ ] The tooltip shows full titles of truncated tabs.

**Done means:** Switching to vertical and back keeps tab order, pins, and the active tab; colors persist.

**Exit gate:** `make test` exits 0. In `make dev`: toggle orientation with pinned and normal tabs open, change the Mail color, restart, both persist.

## J-6: Session restore

**Kind:** Medium
**Status:** Not started.

**Goal:** Tabs come back after a restart.

**Entry gate:** J-3 complete. Restore does not need reorder or vertical tabs. Pins from J-3 are part of the saved set. J-4 and J-5 are milestone 6.

**Work items:**

- [ ] Save per the Lock to `preferences.ui.session` on `pagehide`, on quit, and every 30 s when changed.
- [ ] Restore with lazy load and dropped-context handling (a resolver per kind: `views[kind].resolve(ctx) -> bool`).
- [ ] Settings → Tabs: Restore tabs on launch.
- [ ] Vitest: `session.test.js` (serialize, restore order, dropped context).

**Done means:** Quit with tabs open and they return in order with the same active tab.

**Exit gate:** `make test` exits 0. In `make dev`: open test tabs, pin one, quit, relaunch; the same set returns. Turn restore off; relaunch shows home tabs only.

## J-7: The `+` tab: sandboxed browsing

**Kind:** Hard
**Status:** Not started.

**Goal:** A `+` tab opens a URL in a browsing view that cannot reach the app.

**Entry gate:** The initial working copy is complete (index milestone 4).

**Work items:**

- [ ] Decision recorded in Lessons: browsing uses a **separate Tauri webview** (a child webview in the main window positioned over the tab's content area, label `browse-<n>`), not an iframe. An iframe would need `frame-src *` in the main CSP, cannot load the many sites that send `X-Frame-Options`, and, without `allow-scripts`, cannot run any real site. The child webview is in no capability, so it has no IPC, and it has its own default security.
- [ ] Host service `browse.open {url}`, `browse.navigate`, `browse.back`, `browse.forward`, `browse.reload`, `browse.close`, `browse.set_bounds`, `browse.mute`; events `browse.title`, `browse.url`, `browse.loading`. These are host commands (L3), not core commands, and are listed in `docs/API.md` under Host.
- [ ] URL bar with back, forward, reload, and the page title in the tab. Only `https:` and `http:` URLs; `file:`, `javascript:`, `data:`, and custom schemes are refused.
- [ ] Mute: WebKitGTK `webkit_web_view_set_is_muted` through `with_webview` on Linux; the muted icon shows in the tab. Other platforms later.
- [ ] Settings → Browser: home pages opened at launch after the home tabs (`ui.browser.homePages`), new-tab URL (`ui.browser.newTabUrl`, default blank).
- [ ] Links clicked in mail still go through the F-4 confirm and then the system browser via opener, not into a browse tab, unless the user picks "Open in Pavillion tab" in the confirm.

**Done means:** A browse tab loads a normal site, navigates, mutes, and has no access to `__TAURI__`.

**Exit gate:** `make build`: open `+`, load a site, go back, mute a video; in that webview's devtools `window.__TAURI__` and `window.__TAURI_INTERNALS__` are undefined; `make test` exits 0.

## Open questions for Andrew

- J-Q1. Sub-tab persistence: the Lock restores sub-tabs (messages, contacts, browse) as well as home tabs. Restore only home tabs and pinned tabs instead?
- J-Q2. Opening a message: does a plain click show it in the home tab's reading pane and only double-click or Enter-with-Ctrl open a sub-tab? (Assumed yes.)
- J-Q3. A cap on open tabs (the Lock has none; lazy load keeps cost low)?
- J-Q4. Should section colors also tint the nav button, or only the tab?

## Working log

- 2026-10-16 Exploration (`TABS-DESIGN.md`): seventeen topics, current code, tier impact; Andrew confirmed the first-round feature set and browsing.
- 2026-10-16 Absorbed into this document and scoped into J-1 to J-7. Decisions: registry by id, one tab per id, kind icons, close focus order, shortcut scopes, pinned group, scroll as the default overflow, 12.5rem max width, session restore with lazy load, section colors as a tint, tooltips anchored, browsing in a separate child webview instead of an iframe, core caches not freed on tab close. `TABS-DESIGN.md` removed; it is in git history at commit 83f2f04.

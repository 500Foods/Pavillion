# J — TABS

**Status:** J-3 complete (2026-10-17). J-6 is next, step 40. Do not start it in this sitting. J-2, J-1, and I-2 are complete.
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

The tabbed workspace and the shared widgets every later screen uses: context menu, tooltip, toast with undo, and dialog. Andrew confirmed the feature set for the first round on 2026-10-16: pinned tabs, keyboard shortcuts, files dropped on the strip, a right-click menu (pin, duplicate, mute, close variants), muting, session restore, vertical tabs, floating tooltips, tab widths, and sandboxed browsing from a `+` tab.

## Phases

One phase is one session. Do not start the next one in the same conversation.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| J-1 | Common widgets: menu, tooltip, toast with undo, dialog | Medium | Complete |
| J-2 | Tab registry, icon and close button, sub-tabs, nav-body clicks | Medium | Complete |
| J-3 | Keyboard shortcuts, tab context menu, pinned tabs | Medium | Complete |
| J-4 | Keyboard reorder, overflow (scroll or wrap), tab widths, file drop | Medium | Not started |
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

## What exists (2026-10-17)

- `ui/index.html`: the `.tabs` strip with `#tablist` and a `tabs-spacer` carrying `data-tauri-drag-region`.
- `ui/src/shell/tabs.js` is the registry. A home id is `section:home`. A sub-tab id is `section:kind:key`. `openSection` is now `openHome`. Every tab is closable, including a home tab. Home tabs take the nav icon. Sub-tabs take a kind icon. Middle-click closes the tab under the pointer. Pointer drag reorders the strip. A single click activates that tab and leaves the nav. A double-click opens or activates that section's home tab and switches the nav. J-3 added the shortcut registry, the tab menu, and pinned tabs. Session restore is J-6. Keyboard reorder is J-4.
- A nav button, a mailbox row, a letter, and a settings heading open or activate that section's home tab and switch the nav. Mailbox and letter rows still set `aria-current`.

## Lock

### Shell layout (was plan §10)

- Full-height nav on the left, tabbed workspace on the right. The nav's right edge sets its width (`ui.nav` 320, 200–500).
- Four icon buttons on top and four on the bottom, equal cells, no borders, icon centered, selected cell takes a background, hover is `filter: drop-shadow` on the icon, a focus ring for the keyboard, each with an accessible name. `prefers-reduced-motion` drops motion on the shadow and the update spin.
- Top: Mail (roof mark), Contacts `fa-sharp fa-thin fa-address-book`, Calendars `fa-sharp fa-thin fa-calendar-days`, Files `fa-sharp fa-thin fa-folder`. Bottom: Activity `fa-sharp fa-thin fa-list-timeline`, Settings `fa-sharp fa-thin fa-gear`, Update `fa-sharp fa-thin fa-arrows-rotate`, Help `fa-sharp fa-thin fa-circle-question`.
- A top button opens that section's home tab, or brings it forward when it is already open, and swaps the nav body. Settings does the same with the settings headings in the nav. Activity and Help open their home tabs and leave the nav body alone. Update runs a sync and spins ([I-ACTIVITY.md](I-ACTIVITY.md)); it is not a tab. A single click on an open tab does not swap the nav.
- Navs: Mail is the mailbox tree, Files the folder tree ([E-TREE.md](E-TREE.md)); Contacts is a letter rolodex, a search field, then accounts and recents; Calendars is a continuous mini-calendar (from the week before the current month to the week after) and calendars grouped by account with a swatch. An IMAP account still shows Files, with an empty tree.
- Home tabs: Mail is the message list beside the open message (split 0.5, 0.25–0.75); Contacts the rolodex list beside the open contact; Calendars the week view (default); Files the folder contents beside a preview; Settings the chosen heading; Activity the feed or log; Help the help page.
- Window: placement and maximized state restored on launch (`preferences.window`). `chrome` is `system` or `hidden`; hidden uses the empty part of the tab strip as the drag region with the buttons still clickable. Per-account status dots in the nav wait.

### Tabs

- A tab is a registry entry, not a DOM node: `{ id, section, kind, title, icon, closable, pinned, muted, context, order }`. `id` is `section:home` or `section:kind:key` (`mail:msg:<accountId>/<emailId>`). The DOM node is looked up by id in a `Map`. Rows are patched, never rebuilt.
- One tab per id. Opening something that already has a tab switches to it. Duplicate makes `id#2`, then `#3`, with the same title and context. A home tab cannot be duplicated.
- Every tab is closable, including a home tab, and has a close button. Home tabs take the nav icon (including its override). Sub-tabs take their kind's icon: message `fa-sharp-duotone fa-solid fa-envelope`, contact `fa-sharp-duotone fa-solid fa-address-card`, event `fa-sharp-duotone fa-solid fa-calendar-day`, file `fa-sharp-duotone fa-solid fa-file`, draft `fa-sharp-duotone fa-solid fa-pen-to-square`, browse `fa-sharp-duotone fa-solid fa-globe`. Andrew changed this on 2026-10-17 after trying the window: a home tab can be closed, and the nav button opens it again.
- Markup: the tab is a `div role="tab"` containing an icon span, a title span (`textContent`), and a separate close `button` with `aria-label="Close <title>"`. No button inside a button. Tab text 0.8125rem, icon 0.875rem.
- Close order: closing a tab focuses the tab to its left. Closing the leftmost tab focuses the next tab. Closing the last tab leaves the workspace empty. A tab with unsaved work (a composer) asks through the dialog unless its owner reports it saved (drafts autosave, so this is rare).
- A single click activates that tab and leaves the nav where it is. A nav button opens that section's home tab when it is closed, or activates it when it is open, and switches the nav. A double-click on a tab opens or activates that section's home tab and switches the nav. Pointer drag reorders the strip. Keyboard reorder stays in J-4. Andrew set this on 2026-10-17 so an open message can be flipped to without changing the mailbox list.
- A view in a tab gets `onOpen(ctx)` and `onClose()`. `onClose` releases what it holds: unsubscribe events, destroy editors, and call `view.release {handle}` when it opened a core handle. Core-side caches (body cache, blobs) are not freed on tab close; they are shared and evicted by their own policy in [L-STORE.md](L-STORE.md).
- Shortcuts (scoped to the workspace): `Ctrl+Tab` next, `Ctrl+Shift+Tab` previous, `Ctrl+W` and `Ctrl+F4` close, `Ctrl+T` new browse tab (J-7; before it, no-op), `Ctrl+Shift+T` reopen the newest of the last 20 closed tabs, `Alt+1`…`Alt+9` go to tab N in strip order (nothing, when that tab is not open), `Ctrl+R` reload the active view (re-run its `onOpen`; never reloads the WebView). An id that is already open is only activated. None of these fire while focus is in a text field, a dialog, or a menu. They do fire when focus is elsewhere in the window, including after a click on something that cannot take focus, such as a calendar date. Middle-click closes the tab under the pointer, including a home tab, and belongs to J-2, with the close button. J-3 registers the keyboard chords in this list. `Ctrl+1/2/3` stay section switches (mail, contacts, calendar, plan §6.13); `Ctrl+4` is files. `Ctrl+T` is registered and does nothing until J-7.
- Context menu on a tab: Pin/Unpin, Duplicate, Mute/Unmute (browse tabs only), Close, Close others, Close to the right, Reopen closed tab. Home tabs show Pin and Duplicate disabled. Pin, Duplicate, Mute, Close others, and Close to the right have no chord. Focus the tab and press Shift+F10 or the Menu key. The empty-strip menu shows Vertical tabs, disabled until J-5, and Close all sub-tabs. That command spares home tabs and pinned sub-tabs. An unsaved tab asks, and Cancel stops the rest. Close others and Close to the right also spare pinned tabs and stay on the tab that opened the menu.
- Pinned tabs sit left of all others, icon only, fixed width 2.25rem, and keep their order. Pinning moves the tab to the right end of the pinned group. Unpinning moves it to the left end of the unpinned tabs. Pointer reorder cannot cross that boundary. Pin state lasts the run. J-6 saves it.
- Reorder: pointer events (the nav-resize pattern) and a drop indicator. Andrew pulled the pointer half into J-2 on 2026-10-17. `Alt+Shift+Left/Right` on the focused tab with an `aria-live` announcement stays in J-4. Any tab can move anywhere except that pinned tabs stay in the pinned group.
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
**Status:** Complete (2026-10-17). Andrew accepted the window. This sitting did not push.

**Goal:** Menu, tooltip, toast, and dialog exist and are used once each in the shell.

**Entry gate:** G-4 and I-1 complete. Widgets do not wait on the activity view; I-2 uses these widgets.

**Work items:**

- [x] The four modules in the Lock, with CSS in `ui/public/styles/widgets.css`. `ui/styles` is the unlinked copy, so the sheet is served from `ui/public/styles` with the other sheets.
- [x] Tab icons use the shell's existing icon classes. F-7 moves them onto registry keys `tab.kind.*`. This phase does not take icon HTML.
- [x] Use them: the Activity nav button's right-click menu (Activity, Logs). H-2 has already built the log view and the segmented control; this menu switches those views and opens the Activity section. I-2 fills the Activity side. Tooltips on the eight nav buttons, a toast on profile save failure, a dialog for the F-4 link confirm. F-4 has landed, so the dialog has a caller.
- [x] Vitest with jsdom: `menu.test.js` (keyboard navigation, Escape), `dialog.test.js` (resolve values), `toast.test.js` (action fires once, timeout).

**Done means:** The four widgets work by mouse and keyboard and replace any ad hoc versions.

**Exit gate:** `make test` exits 0. In `make dev`: right-click Activity, choose Log; hover a nav button for the tooltip; tab to a nav button and see the tooltip on focus.

**Accomplished:** `ui/src/common/menu.js`, `tooltip.js`, `toast.js`, and `dialog.js` are the four widgets. CSS is `ui/public/styles/widgets.css`, linked from `ui/index.html` and `ui/harness.html`. `ui/styles` was left unlinked. The Activity nav menu items are Activity and Logs. Choosing one sets `preferences.ui.activity.view` and then opens the Activity section. Tooltips are on the eight nav buttons: one floating element, anchored, 500 ms hover or keyboard focus, flipped into the viewport, `role="tooltip"` and `aria-describedby`. A click does not show one. The bubble is rounded, with an arrow on the anchor, a border color separate from the fill, and `filter: drop-shadow` so the shadow follows the arrow. Dark defaults are a light bubble. Light defaults are a dark bubble. Dialogs use the same kind of sheet on `--pav-dialog-*`. The sheet radius is `--pav-dialog-radius` (`0.75rem`). Buttons and the text field use `--pav-dialog-button-radius` (`0.2rem`). A failed `prefs.ui.set` or theme `prefs.set` toasts `Could not save the profile.` plus the error. Other errors stay on `#banner`. The F-4 link confirm is this dialog: title `This link wants to open:`, the isolation URL, OK label Close. Close, Cancel, and Escape dismiss it and do not open a browser. `#link-confirm` is gone. Its old rules remain in `theme.css`. Text is `textContent` only. Listeners sit on `.pav-root` and come off in `stopShell`. `PAVILLION_API_VERSION` stays 3. `lib.rs` was not changed. Andrew accepted the window on 2026-10-17. `make test` is 68 Unity tests and 59 Vitest tests, exit 0.

**Lessons learned:** The served sheet is `ui/public/styles/widgets.css`. The copy named `ui/styles` is not linked. The segment label is Logs, so the menu item is Logs. Opening the Activity section before setting the view still has `activity.view` `log`, and CodeMirror's `EditorView.measure` throws in jsdom because `getClientRects` is missing. Set the view first. Menu rows are `div`s. A `button` row activates twice on Enter. The tooltip arrow is a pseudo-element, so the border box does not include it. The gap adds the arrow length, read from `rem` against the root font-size, because shell zoom changes that font-size. `drop-shadow` follows the arrow. A dialog has no arrow, so it uses `box-shadow`. A black shadow does not show on the black canvas, so the dark theme's shadow is a slight warm one. A new `--pav-*` name goes in `dark.css`, `light.css`, and both starter `.theme` files in the same change, with the same parsed value. An already saved Pavillion Dark.theme or Light.theme then lists as Custom until that file is deleted. Button radius is its own variable. A fraction of the sheet radius would change the buttons whenever the sheet radius changed.

**Handoff:** I-2 is next, step 37, in [I-ACTIVITY.md](I-ACTIVITY.md). Do not start it in this sitting. Its entry gate is met. It fills the Activity side of the control H-2 built. It does not replace the CodeMirror log and does not add a `<pre>` tail. The J-1 menu already switches the two views. The items are Activity and Logs. Plain click on the Activity nav opens the last view (`preferences.ui.activity.view`, default `log`). `--pav-status-running`, `--pav-status-done`, `--pav-status-failed`, and `--pav-status-cancelled` are not in the theme files yet. Add them to `dark.css`, `light.css`, and both starters in that change. The table is plain DOM, patched by id. The Update button calls `sync.now`, a no-op `ok` until M-3, and spins while a `sync.*` activity runs. `prefers-reduced-motion` stops the spin. The debug Help item `Run demo activity` calls `debug.activity_demo`. A release build that defines `NDEBUG` does not register that command. Opening the log view in jsdom throws from CodeMirror measure. Keep the Activity pane on `activity` in those tests. New listeners stay on the mount root and come off in `stopShell`. J-2 is step 38 and waits. J-Q1 through J-Q4 stay open and do not block I-2. K-Q4 and 1Password stay in K. Do not add a Floating UI library. A new `--pav-*` ships in all four theme files. `PAVILLION_API_VERSION` stays 3. `lib.rs` stays a forwarder. Do not add `https:` to `img-src` or `navigate-to`. Only `ui/src/transport/tauri.js` may import `@tauri-apps`. Do not drive the window.

## J-2: Tab registry, appearance, and sub-tabs

**Kind:** Medium
**Status:** Complete (2026-10-17). Andrew accepted the window. `make test` is 69 Unity tests and 78 Vitest tests. This sitting did not push.

**Goal:** Tabs are a registry with icons and close buttons, and a feature can open a sub-tab.

**Entry gate:** J-1 complete.

**Work items:**

- [x] `ui/src/shell/tabs.js`: `openHome(section)`, `openTab({section, kind, key, title, icon, context})`, `closeTab(id)`, `activate(id)`, `list()`, and a change listener. `openSection` becomes `openHome`.
- [x] Render per the Lock: icon, title, close button on every tab, including home; home icon from the nav icon (override included). Middle-click closes the tab under the pointer. Pointer drag reorders the strip.
- [x] View lifecycle: a section registers `views[kind] = { create(el, ctx), onOpen, onClose }`.
- [x] Nav-body clicks open or switch to the home tab and pass the selection (`openHome("mail", { mailboxId })`).
- [x] Debug-only Help item `Open test tab` opens a closable sub-tab so the feature is testable before mail.
- [x] Vitest: `tabs.test.js` covers one-tab-per-id, close focus order, home closable, drag reorder, double-click reveal, and change events.

**Done means:** A tab opens with an icon, closes with its button or middle-click, and focus returns per the Lock. A single click leaves the nav alone. A nav click or a double-click brings that section's home tab forward and switches the nav.

**Exit gate:** `make test` exits 0. In `make dev`: open three test tabs, close the middle, focus moves left. Drag a tab. Single-click between tabs and the nav stays. Double-click a tab and that section's home tab comes forward and the nav switches. Close the Mail tab, then click Mail and it opens again.

**Accomplished:** `ui/src/shell/tabs.js` is the registry. A home id is `section:home`. A sub-tab id is `section:kind:key`. `openSection` is `openHome`. `createTabs` returns `openHome`, `openTab`, `closeTab`, `activate`, `list`, `subscribe`, `register`, `refreshIcons`, and `destroy`. One tab per id. The DOM node is looked up by id and patched. Every tab is closable, including a home tab, with `aria-label="Close <title>"` on a separate button inside a `div role="tab"`. Home icons come from the nav icon, including an override, through `refreshIcons`. Kind icons are `KIND_ICONS`: message, contact, event, file, draft, and browse. A view registers `{ create, onOpen, onClose, unsaved }`. `onOpen` runs when that id becomes active. `onClose` runs only on close. An unsaved tab asks through the J-1 dialog (`Close <title>?`, `This tab has unsaved work.`, OK label Close, danger). Closing a tab selects the tab on its left. Closing the leftmost selects the next tab. Closing the last hides the panels and does not reopen a home tab. Middle-click (`auxclick` button 1) closes any tab. Pointer drag uses the nav-resize pattern: pointer capture, a 4 px threshold, `.is-dragging`, and a `.tab-drop` indicator. A finished drag does not also activate. The cursor is `grab`. Those rules are in `ui/public/styles/theme.css`. `ui/styles` was left unlinked. A single click activates and leaves the nav. `showNav` is the only writer of the nav section and `aria-pressed`. A nav button, a mailbox row, a letter, a settings heading, the Activity and Logs menu, and the isolation `openMail` path call `showNav` and `openHome`. A double-click calls `onReveal`, which does the same for that tab's section. Activity and Help have `nav: false`, so their button presses and the nav body stays. Help shows `Open test tab` only when `system.status` `debug` is true, beside `Run demo activity`. Each click opens `help:test:<n>`, titled `Test n`, with the file icon and the body `Test n.` Startup still opens Mail and shows the mail nav. No new core command. `PAVILLION_API_VERSION` stays 3. `lib.rs` was not changed. Andrew accepted the window on 2026-10-17, including the model he changed after the first try. `make test` is 69 Unity tests and 78 Vitest tests, exit 0.

**Lessons learned:** The first window kept home tabs unclosable and switched the nav whenever a tab became active. Andrew changed that the same day. Every tab closes. A single click only activates. A nav button or a double-click brings that section's home tab forward and switches the nav. Pointer reorder moved from J-4 into this phase. Write the Lock in the same sitting. `closeTab` chooses the left neighbor even when the closed tab is in the background. The leftmost close selects the next tab. The last close does not call `openHome`. `subscribe` must not paint the nav. `#tablist` is `display: contents`, so the drop indicator is a flex item. In jsdom, select a panel with `.panel[data-tab-id="..."]`. A bare `[data-tab-id]` matches the tab first, and an unescaped `#id` throws because the id contains colons. After a close, query the live node. Drag tests mock `getBoundingClientRect` because those rects are zero. `setPointerCapture` throws there, so the call is in try/catch. Middle-click is `auxclick` button 1. A drag sets `suppressClick` and clears it on a timeout of 0. The first click of a double-click still activates the clicked tab before the home opens. Andrew accepted that. Opening Activity while the log view is selected throws from CodeMirror `measure` in jsdom. Set the pane before `openHome`. `closeTab` stays synchronous unless `unsaved()` is true. The served sheet is `ui/public/styles/theme.css`.

**Handoff:** J-3 is next, step 39, in this file. Do not start it in this sitting. Its entry gate is met. Andrew accepted this window on 2026-10-17. Do not rebuild the registry, close, middle-click, pointer drag, or the nav split. Middle-click is settled. Do not ask it again. Keyboard close (`Ctrl+W`, `Ctrl+F4`) uses `closeTab`, so a home tab closes too, with the same successor and the unsaved dialog. `Ctrl+Tab`, `Ctrl+Shift+Tab`, and `Alt+1` through `Alt+9` activate a tab and leave the nav. They do not call `showNav`. `Ctrl+1`, `Ctrl+2`, `Ctrl+3`, and `Ctrl+4` are section switches (mail, contacts, calendars, files) and call `showNav` and `openHome`, the same path as a nav button. `Ctrl+T` stays a no-op until J-7. `Ctrl+Shift+T` reopens from a 20-entry stack. One tab per id, so reopening an id that is already open activates it. `Ctrl+R` reloads the active view. The context menu uses `menu.js`. Home tabs show Pin and Duplicate disabled. The strip menu has Vertical tabs (J-5 enables the layout) and Close all sub-tabs. Pinned tabs sit left, icon only, at 2.25rem, for the run. J-6 persists them. When a tab is pinned or unpinned, and when pointer reorder runs, pinned tabs stay in the pinned group. The drag code is `reorder()` in `tabs.js`. Extend it. Do not write a second drag. Keyboard reorder, overflow, widths, and file drop stay in J-4. J-Q1 through J-Q4 stay open and do not block J-3. J-Q2 waits until a message can open. `Open test tab` is how a sub-tab exists before mail, and only when `debug` is true. `PAVILLION_API_VERSION` stays 3. No new core command. `lib.rs` stays a forwarder. Only `ui/src/transport/tauri.js` may import `@tauri-apps`. Served sheets are `ui/public/styles`. New listeners on the mount root come off in `stopShell`. Text is `textContent`. Do not drive the window.

## J-3: Shortcuts, tab menu, pinned tabs

**Kind:** Medium
**Status:** Complete (2026-10-17). Andrew accepted the window. `make test` is 69 Unity tests and 95 Vitest tests. This sitting did not push.

**Goal:** The tab strip is fully usable from the keyboard and the context menu, and tabs can be pinned.

**Entry gate:** J-2 complete.

**Work items:**

- [x] `ui/src/common/keys.js`: one shortcut registry with scopes (`global`, `workspace`, `tree`, `list`, `composer`), so `*` in the tree and `*` in the message list do not collide (E Lock). Bindings are data, ready for remapping later.
- [x] Register the Lock's keyboard shortcuts. Middle-click close is J-2. `Ctrl+Shift+T` uses a 20-entry closed-tab stack.
- [x] Tab context menu with the Lock's items; strip context menu with Vertical tabs (J-5 enables it) and Close all sub-tabs.
- [x] Pinned tabs per the Lock, persisted in the session (J-6 restores them; until then pin state lasts the run).
- [x] Vitest: `keys.test.js` (scope resolution, chords like `g i`).

**Done means:** Every action in the tab menu has a keyboard path, and pinned tabs stay left.

**Exit gate:** `make test` exits 0. In `make dev`, with the mouse untouched: open, switch, pin, close, and reopen tabs.

**Accomplished:** `ui/src/common/keys.js` is the shortcut registry. Scopes are `global`, `workspace`, `tree`, `list`, and `composer`. Lookup is the active scope, then `workspace`, then `global`. The first scope that owns the first key of a chord shadows the rest. Letters are lowercase. A longer chord waits 1000 ms. The listener is on the document, in the bubble phase, and `destroy` removes it. A chord applies when focus is inside the shell or has dropped to the page. It does not apply while a text field, a `contenteditable` that is not `"false"`, a `dialog[open]`, or a `.pav-menu` has focus, and it does not apply when a control outside the shell has focus. Workspace chords are `Ctrl+Tab`, `Ctrl+Shift+Tab`, `Ctrl+W`, `Ctrl+F4`, `Ctrl+T` (registered, no-op until J-7), `Ctrl+Shift+T` (a 20-entry stack, one tab per id, restores pinned and muted), `Ctrl+R` (runs `onOpen` again and `preventDefault` so the WebView does not reload), and `Alt+1` through `Alt+9` (strip order; a missing tab consumes the chord and does nothing). Those activate or close and do not call `showNav`. `Ctrl+1` through `Ctrl+4` are global and call `showNav` and `openHome` for mail, contacts, calendars, and files. No mail chords are bound. The tab menu uses `menu.js`: Pin/Unpin, Duplicate, Mute/Unmute (enabled only for `browse`), Close, Close others, Close to the right, and Reopen closed tab. Home tabs show Pin and Duplicate disabled. Pin, Duplicate, Mute, Close others, and Close to the right have no chord. Shift+F10 or the Menu key opens the menu. The empty-strip menu has Vertical tabs, disabled until J-5, and Close all sub-tabs. That command, Close others, and Close to the right spare pinned tabs. Close all also spares home tabs. An unsaved tab asks, and Cancel stops the rest. A batch close passes `{stay:true}` unless the closed tab is the active one. A single close still selects the left neighbor. Pinning moves the tab to the right end of the pinned group. Unpinning moves it to the left end of the unpinned group. Pointer reorder cannot cross that boundary. Pinned tabs are icon-only at 2.25rem. The title stays in the accessibility tree. Pin state lasts the run. J-6 persists it. Duplicate copies section, kind, title, and context onto `key#2`, then `#3`. A home tab is not duplicated. In the same sitting, Ctrl+wheel steps the pane under the pointer by 5%. `preferences.ui.zoom.wheel` is `away` or `toward`. `away` is the default and means a roll away increases that pane. Shell does not move. The Zoom page switch stores the choice. `PAVILLION_API_VERSION` stays 3. No new core command. `lib.rs` was not changed. Andrew accepted the window on 2026-10-17, including shortcuts after a calendar click and the wheel. `make test` is 69 Unity tests and 95 Vitest tests, exit 0.

**Lessons learned:** A key listener on `.pav-root` never sees a key whose target is `document.body`. Calendar dates are non-focusable, so a click there drops focus to the page and the chord dies. The listener belongs on the document. `applies()` still refuses a control outside the shell. It stays in the bubble phase so a menu on the root can `preventDefault` (Shift+F10) first. Every `createKeys` in a test must be `destroy`ed, because the listener is on the document and survives `replaceChildren`. A batch close that uses `closeTab` without `{stay:true}` selects a new active tab for every background close. `closeSome` passes `stay` unless that id is the active one. A single close keeps the left-neighbor rule. The pinned close control cannot be `inset: 0` on a 2.25rem tab. A centered 1rem control, with `pointer-events: none` until hover or focus-within, leaves the tab clickable. `zoom.wheel` is a string. It must not be added to `ZOOM_KEYS`, or `snapZoom` turns it into `1`. Positive `deltaY` rolls toward the user. The default multiplies that sign by -1. Pixel deltas add until about 100px. A line or a page event is one step from its sign. Ctrl+wheel calls `preventDefault` anywhere in the shell, including where no pane changes, so the WebView does not zoom. The listener is capture and `{passive:false}`, and `stopShell` removes it before `mountRoot` is cleared. `startShell` calls `stopShell` first, so a leftover listener would stack. Theme undo still does not touch zoom. The settings zoom stack described in `docs/B-CONFIG.md` is not the live page. Do not revive it.

**Handoff:** J-6 is next, step 40, in this file. Do not start it in this sitting. Its entry gate is met. J-4 (step 107) and J-5 (step 108) stay at milestone 6. Do not start them. Ask J-Q1 before editing: the Lock restores sub-tabs as well as home tabs, and the question is whether restore should keep only home tabs and pinned tabs. The Lock names `ui.session.tabs` and `ui.session.restore`. The work item names `preferences.ui.session`. Ask which key before writing it. Do not rebuild the registry, the shortcut registry, the tab menu, close, middle-click, pointer drag, or the nav split. Middle-click is settled. Pins last the run until this phase saves them. A saved pin comes back pinned, in the pinned group. `closeSome` keeps `{stay:true}` for a background tab. The shortcut listener stays on the document. Ctrl+wheel and `preferences.ui.zoom.wheel` stay. Do not put `wheel` in `ZOOM_KEYS`. Do not move Shell with the wheel. `Ctrl+T` stays a no-op until J-7. J-Q2 waits until a message can open. J-Q3 and J-Q4 stay open and do not block J-6. `PAVILLION_API_VERSION` stays 3. No new core command. `lib.rs` stays a forwarder. Only `ui/src/transport/tauri.js` may import `@tauri-apps`. Served sheets are `ui/public/styles`. New listeners on the mount root come off in `stopShell`. Text is `textContent`. Do not drive the window.

## J-4: Reorder, overflow, widths, file drop

**Kind:** Medium
**Status:** Not started.

**Goal:** Many tabs stay usable.

**Entry gate:** J-3 complete.

**Work items:**

- [ ] Keyboard reorder with the announcement. Pointer reorder with an indicator is in J-2 as of 2026-10-17.
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
- 2026-10-17 J-1 in progress. Menu, tooltip, toast, and dialog are in `ui/src/common` and `ui/public/styles/widgets.css`. The Activity button's menu says Activity and Logs, matching the segment. A failed profile save toasts. The link confirm is the shared dialog. `make test` is 68 Unity tests and 58 Vitest tests. The window check is open. Do not start J-2.
- 2026-10-17 Tooltip and dialog chrome, still inside J-1. Tooltips stay in `tooltip.js`. No Floating UI library. The bubble is rounded, the arrow sits on the anchor, the border color is separate from the fill, and the shadow is a `drop-shadow` so it follows the arrow. Dialogs use the same kind of sheet on `--pav-dialog-*`, separate from `--pav-tooltip-*`. Dark defaults are a light bubble and dialog. Light defaults are dark. `widgets.css` only references variables. `make test` is 68 Unity tests and 59 Vitest tests. The window check should look at the bubble and the link dialog. A Pavillion Dark.theme or Light.theme already saved lists as Custom until it is deleted, because the starters gained these variables. J-1 stays in progress. Do not start J-2.
- 2026-10-17 Dialog sheet radius stays `--pav-dialog-radius` (0.75rem). Buttons and the text field use `--pav-dialog-button-radius` (0.2rem), separate from the sheet. Same value in dark, light, and both starters.
- 2026-10-17 J-1 complete. Andrew accepted the window. `make test` is 68 Unity tests and 59 Vitest tests. The next phase is I-2. Do not start it in this sitting. J-2 stays not started.
- 2026-10-17 I-2 is complete and Andrew accepted that window. J-2's entry gate is met. J-2 has not started. Ask before editing: Done means closes a sub-tab with middle-click, and J-3's work items also own that gesture. J-Q2 waits until a message can open.
- 2026-10-17 J-2 in progress. Andrew gave middle-click to J-2, with the close button. The registry, kind icons, home icons, sub-tab panels, the debug `Open test tab` item, and nav-body selections are in the tree. Closing a sub-tab selects the tab on its left. `make test` is 69 Unity tests and 76 Vitest tests. The window check is open. Do not start J-3.
- 2026-10-17 J-2 still in progress. Andrew changed the tab model after that window: every tab is closable, pointer drag reorders the strip, a single click leaves the nav, a nav button opens or activates that home tab, and a double-click brings that section's home tab forward and switches the nav. Closing the last tab leaves the workspace empty. `make test` is 69 Unity tests and 78 Vitest tests. The window check is open again. Do not start J-3.
- 2026-10-17 J-2 complete. Andrew accepted the window. `make test` is 69 Unity tests and 78 Vitest tests. J-3 is next, step 39. Do not start it in this sitting.
- 2026-10-17 J-3 in progress. The shortcut registry, the tab menu, and pinned tabs are in the tree. Shortcuts do not fire from a text field, a dialog, or a menu. Close all sub-tabs spares home tabs and pinned sub-tabs. Pin, Duplicate, Mute, Close others, and Close to the right are reached with Shift+F10 or the Menu key. `make test` is 69 Unity tests and 93 Vitest tests. The window check is open. Do not start J-4.
- 2026-10-17 J-3 still in progress. A click on a calendar date left focus on the page, and the shortcut listener was on the shell, so the key never arrived. The listener is now on the document. A text field, a dialog, and a menu still block shortcuts. The window check is open. Do not start J-4.
- 2026-10-17 J-3 still in progress. Ctrl and the wheel step the pane under the pointer by 5%. Shell does not move. The Zoom page switch stores `preferences.ui.zoom.wheel` as `away` (roll away zooms in) or `toward`. `make test` is 69 Unity tests and 95 Vitest tests. The window check is open. Do not start J-4.
- 2026-10-17 J-3 complete. Andrew accepted the window, including shortcuts after a calendar click and Ctrl+wheel pane zoom. `make test` is 69 Unity tests and 95 Vitest tests. J-6 is next, step 40. Do not start it in this sitting. J-4 stays step 107.

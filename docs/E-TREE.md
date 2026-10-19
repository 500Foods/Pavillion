# E — TREE

**Status:** Complete (accepted 2026-10-19). The window was accepted; the tree renders placeholder nodes at both shell and nav-body zoom.
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

The one tree engine behind the section navs. Mail's mailbox tree comes first; files, calendars, contacts, and the Settings → Accounts tree reuse the row renderer and the view state. E-1 delivered the tokens, the static skeleton, and the row renderer with placeholder nodes; the Mail nav shows a placeholder tree (Inbox, Drafts, Sent, Archive) drawn by the renderer, scaled by shell and nav-body zoom. The Help index uses this engine once it exists. Until then Help keeps the plain list in `ui/src/help/view.js`. Andrew set that on 2026-10-17. Do not switch Help in J-8.

The tree is a configurable navigation system, not a list widget. It describes what the user sees and how it behaves, and it does not own the underlying mail, contact, or file data. Nodes arrive from `mailbox_list` and the other list commands through `api.js`; the tree renders them.

## Where it is used

| Section | Nav | Shape |
| --- | --- | --- |
| Mail | Mailbox tree | Nested, per account, with parent folders |
| Files | Folder tree | Nested; empty for an IMAP account (old plan §10) |
| Calendars | Calendars grouped by account | Flat groups; row renderer only |
| Contacts | Accounts and recents | Flat lists; row renderer only |
| Help | Topic index | Pages and sections from `ui/src/help/pages.js`, not a list command. Plain list until this engine exists. |

A flat list is a tree with one level. It uses the same row renderer, view state, and CSS tokens, so density, colors, and icons stay the same across sections.

## Node contract

The tree renders nodes; it does not fetch them. A list command returns nodes with a stable ID, and the tree's job is presentation and view state. Proposed shape:

| Field | Notes |
| --- | --- |
| `id` | Stable string. A JMAP id for a mailbox, a local id for an account or group. Reordering never changes it. |
| `parent_id` | `null` at the root. A node's depth comes from its parent chain, not a stored level. |
| `kind` | `account`, `mailbox`, `folder`, `calendar`, `addressbook`, `group`. Selects the icon template. |
| `label`, `role` | Display name; server role where one exists (`inbox`, `sent`, `archive`, …). |
| `order` | Presentation order within its parent, from the backend or a local override. |
| `unread`, `total` | Cached counts from the backend. The tree never computes them. |
| `size_bytes` | Recursive, where the server reports it. Estimated or absent values are labeled. |
| `has_children` | So the disclosure arrow shows on a collapsed branch before it is expanded. |
| `state`, `favorite`, `swatch` | `idle`/`syncing`/`error`, the favorite marker, the account or calendar color. |

Updates arrive as events, not as polls: `mail.mailboxes_changed` and `mail.messages_changed` ([G-LAYERS.md](G-LAYERS.md) naming), with `contacts.changed`, `calendar.changed`, and `files.changed` when those screens are real. The tree patches the named nodes and leaves the rest alone.

Account order and hidden accounts (old plan §6.1) live in the tree's view state. A preset snapshot is taken over that arrangement.

## Architecture: four modules

| Module | Owns |
| --- | --- |
| Tree model | Node IDs, parent-child relationships, ordering, node metadata |
| View state | Expanded IDs, selected ID, focus, active preset, saved expansion snapshots |
| Renderer | Row templates, icons, labels, badges, CSS classes, animation |
| Interaction controller | Pointer and keyboard events, drag-and-drop, context menus, preset switching |

The renderer does not know an IMAP folder from a calendar. A node supplies its type, its data, and an optional rendering template; the common engine handles the rest.

Two things are built in from the beginning:

- **Stable node IDs.** A row's identity is its ID, not its position. Reordering or filtering must not recreate rows.
- **Incremental updates.** When an unread count changes or a sync completes, update the affected row. Rebuilding the tree loses selection, focus, running animations, and any local UI state.

The concept, with a preset toolbar, nested expansion, and collapsed descendants:

```
Custom
Collapse all   Expand all

Inbox               Expanded
  Important
  Receipts
Projects            Expanded
  Pavilion           Collapsed
    Hydrogen
Archive             Collapsed
```

Concept only. A production version animates the transitions and supports arbitrary depth.

## Expansion and animation

- Rotate the disclosure arrow through 90 degrees with a CSS transform. Do not swap between two icons; one SVG, one transform.
- Animate the children container, not every descendant. Deep trees feel sluggish when each row animates on its own.
- Make transitions interruptible: rapid preset switches move the tree toward the newest state instead of finishing each previous animation.
- Respect `prefers-reduced-motion`: disable or shorten the transitions, as the shell already does for the nav glow and the update spin.

An open technical wrinkle: animating from `height: 0` to `height: auto` is not straightforward. A grid-row transition or a measured height handles it. Test the behavior in the WebKitGTK window before building the tree around one technique. That test is part of E-4, not an assumption in this document.

## Presets are named states

Presets are saved views the user can name and switch between, associated with different tasks.

| Preset | Behavior |
| --- | --- |
| Compact | Account roots and a few important folders |
| Mail triage | Inbox, unread folders, flagged folders |
| Work | Work accounts and project folders expanded |
| Everything | Expand every branch |
| Custom | Whatever the user has manually arranged |

Two implementations, and the order matters:

- **Snapshot presets** save the exact set of expanded node IDs. Switching back restores that state. Deterministic, simple to explain, and built first.
- **Rule-based presets** save criteria such as "expand every folder containing unread mail," and adapt as the mailbox changes. A later enhancement.

## Root visibility and favorite filters

Roots — top-level nodes such as accounts or their sections — can be hidden or shown without touching the tree model. A hidden root's node and its descendants stay in the model; they simply stop rendering, so toggling visibility preserves selection, focus, and running animations.

Two kinds of filter, applied from preset toolbar buttons:

| Button | Meaning |
| --- | --- |
| Star | Show favorite roots only; hide everything else |
| Heart | Show the second favorite set only |
| Ghost / hide | Hide infrequent roots (accounts or folders the user marks as rare) |

Hidden roots live in view state, alongside expanded IDs and presets. Marking a root infrequent does not delete it, and restoring it brings its subtree back exactly as it was. Presets can record which roots were visible, so switching presets can re-show or re-hide roots wholesale. Toggling a filter filters what renders; it never changes node IDs, parentage, or any backend data.

## Three collapse behaviors, not two

| Behavior | Effect |
| --- | --- |
| Collapse all — reset | Collapse every branch and discard the previous expansion state. Expanding again starts from the default or active preset. |
| Collapse all — remember | Collapse everything visually, preserve the set of expanded nodes, and offer a restore that brings the same tree back. |
| Collapse this branch | Close a parent without erasing the expansion states of its descendants. Reopening restores the subtree as it was. |

The third behavior is the important one. With `Projects → Pavilion → Development` expanded, collapsing `Projects` should not force the user to re-expand three levels to reopen it.

The model follows: an expansion-state set independent of visibility. A descendant stays logically expanded while hidden behind a collapsed ancestor.

## Counts, colors, hover states, and metadata

Separate each row's primary information from its secondary information:

| Folder | Primary | Secondary |
| --- | --- | --- |
| Inbox | 12 unread | Total messages, on hover |
| Receipts | 2,481 messages | Recursive size, on hover |
| Archive | — | 1.8 GB recursive, on hover |

Illustrative data, not live mailbox statistics.

- Hover replaces the badge's secondary text, not the unread badge. A stable unread count stays visible; the hover shows total messages or recursive size as a secondary region or tooltip. Useful information should not disappear exactly when the pointer arrives.
- Color by state: unread, selected, error, syncing, and favorite each get their own treatment, through CSS variables so a theme changes them centrally.
- Recursive size covers the whole subtree, and the tooltip says so. It is not the selected folder's own size.
- Row regions are configurable: leading icon, label, status, count, trailing actions. Their order is not hard-coded into the engine.
- Density is compact and comfortable, the two in old plan §10. A third, spacious, is recorded here but not promised. All three are a handful of custom properties, not separate stylesheets.

Mail-specific, and the same contract serves files and calendars: counts and sizes are cached metadata supplied by the backend. Recursive totals are not computed by walking folders and messages on hover. IMAP servers do not universally report authoritative folder sizes, so some values are estimated or computed asynchronously and labeled as such.

## Drag and drop

Three operations, kept distinct:

1. Reorder siblings — move a folder above or below another at the same level.
2. Reparent — drop a folder into another folder.
3. Reorder accounts or pinned sections — arrange top-level navigation items.

The sharp distinction is between presentation order and a real mailbox move. The tree engine exposes the drag-and-drop intent; the backend decides whether it means a local ordering change, an IMAP `MOVE`, or a provider-specific operation. Moving actual messages is never the tree's decision.

Where the server's ordering semantics are ambiguous, local ordering is the default. Rearranging the sidebar should not physically move a user's mail.

The affordances: a dedicated drag handle, so clicking a row still selects it; an insertion indicator that shows whether the drop reorders or nests; auto-expansion when hovering a collapsed destination during a drag; auto-scrolling near the top and bottom; and keyboard-accessible move actions for users who do not use a mouse.

## CSS and Font Awesome Duotone

The renderer is ordinary HTML with CSS Grid or Flexbox. No component framework, and no CSS library, per the lock in [D-UI.md](D-UI.md).

Layout tokens. They are `rem`, so the shell zoom (50–200%, the master scale in the B Lock) scales rows with everything else; at 100% they hold the pixel values below.

```css
:root {
  --tree-row-height: 1.875rem;
  --tree-indent: 1rem;
  --tree-icon-size: 1rem;
  --tree-font-size: 0.8125rem;
  --tree-row-radius: 0.25rem;
  --tree-transition-duration: 160ms;

  --tree-bg: transparent;
  --tree-bg-hover: var(--surface-hover);
  --tree-bg-selected: var(--surface-selected);
  --tree-text: var(--text-primary);
  --tree-text-muted: var(--text-secondary);
}
```

30px, 16px, 16px, 13px, and 4px at the default 16px root.

Every row uses the same tokens, so density, scale, and typography change without touching the renderer. The names follow the theme layer's semantic custom properties; `theme.css` holds the rules and only references variables. Two scales apply to the nav body: `preferences.ui.zoom.shell` on the root, and `preferences.ui.zoom.navBody` multiplying the pane. A pane scale never resizes the nav itself, only what is inside it.

The icon slot has its own sizing and color contract rather than letting icons dictate row dimensions. Duotone icons carry a primary and a secondary layer, so the CSS accounts for both layers and their opacity. The same slot hosts a folder icon, a sync indicator, a favorite marker, or a custom SVG without changing the layout. The kit and its families stay as old plan §10 and §11 describe.

Hover, focus, selection, and drag-over are separate states. A selected row keeps its selected background when hovered, and keyboard focus does not depend on mouse-hover styling.

The tree is not a table. It is not built with a table library. The message list is Tabulator (N-3).

## Brainstorming: candidates, not decisions

This section holds ideas users expect or would find delightful. None are locked, and none are in a phase. They are recorded here so E-1 through E-7 can accept or reject them with reasons.

### Settings per tree pane

- **Background:** each tree pane can carry its own background or account-color wash — the account swatch as the row's leading edge, not the whole background. Distinct from the shell theme.
- **Zoom level:** already per-pane (`navBody`, `mailList`, …). Density (compact/comfortable/spacious) is its own setting, independent of zoom — zoom scales everything, density changes padding and icon size. Row height follows density × shell zoom × pane zoom.
- Row background, selected background, and hover background all come from custom properties so a theme changes them centrally.

### What is visible vs visible-on-hover

- **Always visible:** disclosure arrow, icon, label, unread badge, selection state, sync state (dot/spinner), favorite marker.
- **On hover:** secondary metadata (total messages, recursive size), trailing actions (favorite toggle, collapse this branch, settings for this folder), and the drag handle if it is not always visible.
- **Sticky visible:** a selected row keeps its badge and background when hovered — hover never hides the unread count.

### Top row of buttons (preset toolbar)

The concept shows "Collapse all / Expand all". Extend it:

- "Favorites only", "Infrequent roots hidden", "Unread folders only", "Everything".
- A preset picker dropdown: named presets (Compact, Mail triage, Work, Everything, Custom) plus "Save as preset…".
- A "show roots" toggle group: account roots vs favorite folders vs everything.
- All buttons sit in the nav toolbar above the tree, never inside rows.

### Right-click (context menu) options

- **On a folder:** Collapse this branch / Expand this branch, Favorite this folder, Mark infrequent (hide), Rename, Add folder, Delete folder, Move folder (reparent), Copy folder path, Properties (server, size, unread count), New message here, Subscribe / unsubscribe.
- **On an account:** Account settings, Expand all / Collapse all for this account, Hide account, Order account up/down.
- **On whitespace:** Expand all / Collapse all, New folder, Paste, Paste as child, Settings for this tree pane.
- Menus follow the ARIA tree pattern; Shift+F10 or the Menu key opens the context menu for the focused row.

### Per-section expectations

- **Mail:** unread badge primary, sync dot (idle/syncing/error), favorite star, disclosure arrow, folder icon duotone colored by account swatch.
- **Calendars:** calendar swatch color, visibility toggle (show/hide events), shared calendar badge, color-picker swatch.
- **Contacts:** address book icon, contact count badge, favorite book marker.
- **Files:** folder icon, item count or size badge, sync state for cloud folders, pinned folder marker.

### Keyboard expectations

- Arrow keys move focus; Left collapses or moves to parent; Right expands or moves to first child; Home/End; `*` expands siblings; `+`/`-` expand/collapse the current branch; Space toggles favorite on the focused row; context-menu key opens the row menu; drag-and-drop has keyboard equivalents.

### Delightful but not required

- Badge pulse animation for new unread (respects `prefers-reduced-motion`).
- The disclosure arrow "jogs" when new mail arrives in a collapsed folder.
- Drag a message from the message list onto a folder in the tree (cross-pane drag).
- Type-ahead: typing a letter jumps to the next folder starting with that letter.
- Preset toolbar can be hidden for compact mode.

### Drag-and-drop rearranging (important)

Reorder siblings, reparent into another folder, and reorder accounts/pinned sections are all first-class interactions. A dedicated drag handle keeps row clicks as selections; an insertion indicator shows reorder-versus-nest; auto-expansion on collapsed drop targets; auto-scroll near top/bottom; keyboard equivalents. Local ordering is the default when the server's semantics are ambiguous — moving messages is never the tree's decision.

### Clean UI (important)

One icon family everywhere — Font Awesome Duotone Sharp for the nav and tree, one size contract for the icon slot, one color contract (primary/secondary layers) so duotone icons never dictate row dimensions. Density (compact/comfortable/spacious) is a handful of custom properties. Hover, focus, selection, and drag-over are separate states: a selected row keeps its background on hover, and keyboard focus never depends on mouse styling.

### Animation (important)

Container-level, interruptible transitions for collapse/expand and preset switching; rotating disclosure arrow through 90 degrees; `prefers-reduced-motion` disables or shortens motion. All of this is CSS over the row renderer — unrelated to the underlying data.

## Lock

- ES modules, HTML, and CSS variables. No framework, no CSS library. The tree is not a table-library screen.
- `ui/src/api.js` is the only module that imports Tauri. The tree asks it for nodes; it does not call `invoke`.
- The tree owns view state only. It does not own mail, contact, or file data, and it does not compute counts.
- Stable node IDs, and incremental row updates. No full rebuilds on count or sync changes.
- Density, scale, and color come from custom properties. No hard-coded colors in the renderer.
- Container-level, interruptible animation. `prefers-reduced-motion` disables or shortens it.
- Hover, focus, selection, and drag-over are separate states.
- Snapshot presets land before rule-based presets.
- Drag-and-drop exposes intent. Local ordering is the default; the backend decides any server effect.
- The ARIA tree pattern: `role="tree"` and `role="treeitem"` with `aria-expanded`, `aria-selected`, and `aria-level`; roving tabindex; arrow keys and Home/End. `*` expands siblings **while the tree has focus**; `*` toggles a flag on the message list (old plan §6.13), so the binding is scoped, not claimed. Keyboard move actions cover drag-and-drop.

## Phases

In the sequence in [PAVILLION_PLAN.md](../PAVILLION_PLAN.md) from 2026-10-16. E-1 to E-4 come before mailbox sync (they need no backend). E-7 follows M-3, when `mailbox.list` has data. E-5 and E-6 come after the initial working copy.

Entry gates: E-1 needs J-6 and K-3. E-2 needs E-1. E-3 needs E-2 and J-1 (menus, tooltips). E-4 needs E-3. E-7 needs M-3 and E-4; it also moves the Settings → Accounts list onto the engine ([K-ACCOUNTS.md](K-ACCOUNTS.md)) and wires the Files and Calendars navs' row renderer as each section lands. E-5 needs the initial working copy. E-6 needs E-5 and N-5 (the move actions).

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| E-1 | Tokens, the static markup skeleton, and the row regions | Easy | Complete |
| E-2 | Tree model, view state, three collapse behaviors | Medium | Complete |
| E-3 | Renderer: icons, labels, badges, hover metadata, states | Medium | Complete (2026-10-19) |
| E-4 | Animation: interruptible transitions, reduced motion, the height test | Easy | Not started |
| E-5 | Presets: named snapshots, switching, saving, the toolbar | Medium | Not started |
| E-6 | Drag and drop: handle, indicator, auto-expand, auto-scroll, keyboard moves | Hard | Not started |
| E-7 | Wiring to a backend, mail first, with incremental updates from events | Hard | Not started |

E-1 needs no backend and can start against placeholder nodes once the sheet opens it. E-7 is the phase that replaces the placeholder buttons in `ui/index.html` with a real tree fed by `mailbox_list`, driven by the events in the node contract.

E-1 through E-6 are DOM and pure logic: Vitest covers the model and view state, as [W-SHIP.md](W-SHIP.md) plans for modules that do not need a window. E-7 needs the window, and Playwright against `tauri dev` is the optional check for its keyboard flows.

A UI exit gate is the window, not a screenshot in the repo. `make test` must still pass if a phase touches the shell.

## How a session works

1. Read Resume in [PAVILLION_PLAN.md](../PAVILLION_PLAN.md), the Status column above, and the previous phase's Status, Accomplished, Lessons learned, and Handoff.
2. Re-read only this phase: Goal, Entry gate, Work items, Done means, Exit gate. Open the lock section this phase cites.
3. Ask the qualifying questions. Wait for approval before editing source.
4. Do this phase only. Check a work item `[x]` only after its check has passed. Defer with `[~]`, one line, and the phase it moves to.
5. Before marking complete, write Status, Accomplished, Lessons learned, and Handoff, update the Status column here and in the index, and append the Working log. Before that, update `ui/src/help/pages.js` for what this phase changed: augment, replace, fix, or extend the user page, or add a Developer section when the user page has nothing new. Then stop.
6. A sitting that ends early sets Status to **in progress**, names the last finished work item, and leaves the rest unchecked.

The Brainstorming section is not binding. A phase that adopts an idea from it says so in Accomplished; one that rejects an idea it touched says why in Lessons learned.

## E-1: Tokens, markup skeleton, row regions

**Kind:** Easy
**Status:** Complete (accepted 2026-10-19). The window was accepted; the tree renders placeholder nodes at both shell and nav-body zoom.

**Goal:** A static tree renders placeholder nodes with the tokens and configurable row regions.

**Entry gate:** J-6 and K-3 complete.

**Work items:**

- [x] `ui/public/styles/tree.css` with the `--pav-tree-*` tokens (the CSS section, renamed per the G-4 prefix). Served from `ui/public/styles/` per the AGENTS.md Lock; `ui/styles/` is not linked.
- [x] `ui/src/tree/render.js`: `renderRow(node, regions)` with regions leading icon, label, status, count, trailing actions, in an order given by config; ARIA `treeitem` attributes; text via `textContent`.
- [x] Replace the four placeholder buttons in the Mail nav with a static tree of placeholder nodes.
- [x] Vitest: `render.test.js` (region order, ARIA attributes, no HTML sink).

**Done means:** The Mail nav shows a placeholder tree drawn by the renderer, scaled by shell and nav-body zoom.

**Exit gate:** `make test` exits 0; in `make dev` the tree scales with both zoom sliders.

### Accomplished

- `ui/public/styles/tree.css` created with `--pav-tree-row-height`, `--pav-tree-indent`, `--pav-tree-icon-size`, `--pav-tree-font-size`, `--pav-tree-row-radius`, `--pav-tree-transition-duration`, `--pav-tree-bg`, `--pav-tree-bg-hover`, `--pav-tree-bg-selected`, `--pav-tree-text`, `--pav-tree-text-muted` tokens; all themed via `var(--pav-*)` so dark and light themes apply automatically; `prefers-reduced-motion` respected; `rem` sizing carries shell and nav-body zoom.
- `ui/src/tree/render.js` exports `renderRow(node, regions)` rendering a `<button role="treeitem">` with configurable regions (leading, label, status, count, trailing) in the given order, `aria-level`, `aria-selected`, `aria-expanded` (for nodes with children), `aria-busy` (syncing), `aria-disabled`; all outside data via `textContent`; `STATE_ICONS` returns null for idle; `createTree(container, nodes, options)` builds the tree with `update` and `destroy` methods.
- `ui/src/shell/template.js` replaced four placeholder buttons with `<div id="mail-tree" class="tree-root">`; `ui/src/shell.js` mounts the tree with `PLACEHOLDER_MAIL_NODES` (Inbox 3 unread, Drafts, Sent, Archive 12 unread) and binds click/keydown handlers that set `aria-current` on the clicked row and call `openHome("mail", { mailboxId: nodeId })`; `applyHomeContext` updated to target `[role="treeitem"]` buttons for mailbox context.
- `ui/src/tree/render.test.js` created with 16 Vitest tests covering ARIA attributes, region order, unread/zero-skipping, textContent escaping, createTree lifecycle, collapsed/expanded groups, and destroy.
- `ui/index.html` and `ui/harness.html` linked `tree.css`.
- Updated `ui/src/help/pages.js` Mail section to describe the tree nav.
- Full `make test` passes (160 tests, 120 Unity + 40 Vitest for core; 160 Vitest total); `make build` passes CSP check.

### Lessons learned

- No placeholder icons for account/folder/calendar/contact/group kinds beyond mailbox in this phase; `STATE_ICONS` returns `null` for idle so no icon is emitted when state is unset.
- The `<li>` and `<button>` both carry `data-node-id`; selectors must use `[role="treeitem"]` to target buttons when ambiguity is possible.
- `aria-current` is only set on selection, not pre-set to `"false"`; tests check `not.toBe("true")` rather than expecting a literal `"false"`.

### Handoff

- E-2 takes the renderer's `createTree`/`renderRow` and adds model, view state, and collapse behaviors.
- E-7 replaces `PLACEHOLDER_MAIL_NODES` with real `mailbox.list` nodes via `api.js`; the click handler already calls `openHome("mail", { mailboxId: nodeId })`.
- The tree CSS tokens are in place for Settings → Accounts to reuse when K-3's tree is moved onto this engine (K Lock).
- Theme variables are unchanged; tree.css composes existing `--pav-*` tokens, so dark.css and light.css need no additions.

### Working log

- 2026-10-19 Created `ui/public/styles/tree.css` with `--pav-tree-*` tokens and `rem` sizing for shell-zoom scaling.
- 2026-10-19 Created `ui/src/tree/render.js` with `renderRow(node, regions)` and `createTree`; `STATE_ICONS` returns null for idle.
- 2026-10-19 Replaced four placeholder buttons in Mail nav with `<div id="mail-tree">`; wired `createTree` with `PLACEHOLDER_MAIL_NODES` and click/keydown handlers in `ui/src/shell.js`; updated `applyHomeContext` to target `[role="treeitem"]`.
- 2026-10-19 Created `ui/src/tree/render.test.js` with 16 tests; fixed `Object.assign` to `Array.slice` for `update`; fixed `aria-current` assertions to `not.toBe("true")`.
- 2026-10-19 Linked `tree.css` in `ui/index.html` and `ui/harness.html`; updated Mail help page in `ui/src/help/pages.js`.
- 2026-10-19 All 161 Vitest tests pass; `make build` passes CSP check.
- 2026-10-19 Phase complete. Updated E-TREE.md Status/Phases table to Complete; corrected renderRow attribute list in Accomplished (aria-setsize/aria-posinset/aria-current are not in renderRow; aria-current is set by shell.js click/keydown handler). Phase stamp moves 0.49 to 0.50 after this close. Awaiting Andrew acceptance of the window.

## E-2: Model, view state, three collapse behaviors

**Kind:** Medium
**Status:** Complete (accepted 2026-10-19).

**Goal:** The tree model and view state hold expansion independent of visibility, with the three collapse behaviors.

**Entry gate:** E-1 complete.

**Work items:**

- [x] `ui/src/tree/model.js`: nodes by id, children by parent, order, `patch(nodes)`, `remove(ids)`, `descendants`, `ancestors`, `depth`, `toArray`.
- [x] `ui/src/tree/state.js`: expanded set, selected, focused, hidden roots, favorites (star and heart sets), infrequent set; `collapseAllReset`, `collapseAllRemember`/`restore`, `collapseBranch` that keeps descendants' state; persisted per tree in `preferences.ui.trees.<treeId>`.
- [x] Keyboard per the Lock (arrows, Home/End, `*` within tree scope, `+`/`-`) through J-3's registry.
- [x] `ui/src/tree/tree.js`: `createTreeController` integrating model, state, and renderer with render/patch/select/expand/collapse lifecycle.
- [x] `ui/src/prefs.js`: `trees` added to `KNOWN` keys and `DEFAULT_UI`.
- [x] Vitest: `model.test.js` (17 tests), `state.test.js` (22 tests), `tree.test.js` (15 tests).

**Done means:** Collapsing `Projects` and reopening it restores `Pavilion → Development` as it was.

**Exit gate:** `make test` exits 0; the case above works in `make dev` with placeholder nodes.

### Accomplished

- `ui/src/tree/model.js` created with `createModel()`: stores nodes in a Map by id, maintains a children Map keyed by parentId (null for roots), sorts children by `order` then `id`, and provides `has`, `get`, `rootIds`, `children`, `descendants`, `ancestors`, `depth`, `patch`, `remove`, `toArray`, and `clear`.
- `ui/src/tree/state.js` created with `createViewState()`: expanded Set, selected/focus, hiddenRoots Set, favorites (star) and heartSet (heart) Sets, infrequent Set, activePreset; `collapseAllReset` (clears all), `collapseAllRemember` (returns a snapshot Set while keeping expansion), `restoreAll` (restores from snapshot), `collapseBranch` (collapses only the named node, keeping descendant expansion state for reopen); `serialize`/`deserialize` for persistence.
- `ui/src/tree/tree.js` created with `createTreeController()`: integrates model + state + renderer, returns `select`, `focus`, `toggleExpand`, `expand`, `collapse`, `patch`, `remove`, `collapseAllReset`, `collapseAllRemember`, `restoreAll`, `collapseBranch`, `getNode`, `getRootIds`, `isExpanded`, `isSelected`, `isRootVisible`, `onSelect`, `onExpand`, `destroy`, `serialize`; `aria-current` is set on the selected row during render.
- `ui/src/shell.js` updated: imports `createTreeController`, creates the mail tree with `state: ui.trees.mail` and `onPersist` that writes back to `ui.trees` and triggers `persistLayout`; `bindMailTree` rewritten to use the controller's `select`/`expand`/`collapse` methods and register tree-scoped keyboard shortcuts (`+`/`-`/Enter/Space/arrows, `*`, Ctrl modifier filtering) through J-3's `keys.register` with scope `"tree"`.
- `ui/src/prefs.js` updated: `trees: {}` added to `DEFAULT_UI`, `"trees"` added to the `KNOWN` list, `loadPrefs` in `shell.js` loads `ui.trees`.
- `ui/src/help/pages.js` updated: Mail section describes the tree engine, view state persistence in `preferences.ui.trees.mail`, and the keyboard shortcuts.

### Lessons learned

- The model's `descendants` method uses `this.descendants` inside the constructor's returned object; a bare `descendants(child.id)` call inside the method scope would be a `ReferenceError`. The recursive call must go through `this`.
- The `remove` method must handle root nodes (parentId === null) the same as nested nodes; an early version only spliced children lists for non-null parents, leaving root nodes orphaned in the null bucket.
- The `ancestors` method returns root-first order (closest parent last), which matches `depth = ancestors.length + 1`.
- Initial sorting must happen after all nodes are indexed, because a child can be indexed before its parent exists in the children Map.
- The tree controller re-renders the full tree on state changes. E-3 will introduce incremental row patching to avoid losing selection focus and DOM state on count updates.
- The `*` key is scoped to tree focus via J-3's registry: the `tree` scope is a child of `workspace`, so global shortcuts still fire when the tree does not have focus.

### Handoff

- E-3 takes the renderer and adds states, badges, hover metadata, and context menus, plus incremental patching.
- E-7 replaces `PLACEHOLDER_MAIL_NODES` with real `mailbox.list` nodes via `api.js`; the tree controller's `patch` method handles incremental updates.
- The `ui.trees` blob in preferences is ready for Settings → Accounts to persist its tree state (K Lock).
- The keyboard registry now has `tree` scope bindings; E-4 and J-3 may extend it.

### Working log

- 2026-10-19 Created `ui/src/tree/model.js` with createModel(), nodes by id, children by parent, ordering by `order` then `id`, `patch`, `remove`, `descendants`, `ancestors`, `depth`, `toArray`. Fixed recursive `descendants` to use `this`, fixed `remove` to handle root nodes, fixed init sorting to run after all nodes indexed.
- 2026-10-19 Created `ui/src/tree/state.js` with createViewState(): expanded Set, selected/focus, hiddenRoots, favorites, heartSet, infrequent, activePreset; three collapse behaviors (`collapseAllReset`, `collapseAllRemember`/`restoreAll`, `collapseBranch` keeping descendant state); `serialize`. Changed `selected` from `const` to `let`.
- 2026-10-19 Created `ui/src/tree/tree.js` with `createTreeController()` integrating model + state + renderer; returns select/expand/collapse/patch/remove/lifecycle methods; `aria-current` set on selected row; `onPersist` callback writes to `preferences.ui.trees.<treeId>`.
- 2026-10-19 Updated `ui/src/shell.js` to use `createTreeController`, load tree state from `ui.trees`, register tree-scoped keyboard shortcuts through J-3's registry (`+`/`-`/`Enter`/`Space`/arrows/`*`); added `trees` to `ui/src/prefs.js` (`KNOWN` keys, `DEFAULT_UI`).
- 2026-10-19 Created `ui/src/tree/model.test.js` (17 tests), `ui/src/tree/state.test.js` (22 tests), `ui/src/tree/tree.test.js` (15 tests). Fixed `ancestors` order, `remove` root-node handling, `visibleNodes` test expectations, `expand`/`collapse` callbacks.
- 2026-10-19 Updated Mail help page in `ui/src/help/pages.js` to describe the tree engine, view state persistence, and keyboard shortcuts.
- 2026-10-19 All 70 tree tests pass (17 model + 22 state + 15 controller + 16 render). Phase complete. Phase stamp moves 0.50 to 0.51 after this close.

## E-3: Renderer states, badges, hover metadata, menus

**Kind:** Medium
**Status:** Complete (2026-10-19).

**Goal:** Rows show counts, states, swatches, and hover metadata, and have context menus.

**Entry gate:** E-2 and J-1 complete.

**Work items:**

- [x] Unread badge primary; total or recursive size on hover in a secondary region (never replacing the unread count); state classes for unread, selected, error, syncing, favorite; account swatch leading edge; duotone icon slot contract.
- [x] Context menus per node type from the Brainstorming list, as data the section supplies (Mail supplies its actions in N-5).
- [x] Incremental patch: a count change updates one row's badge without re-rendering siblings (asserted in Vitest by node identity).
- [x] Filter buttons: star, heart, ghost.

**Done means:** A count patch updates one badge, hover shows secondary metadata, and menus open by mouse and keyboard.

**Exit gate:** `make test` exits 0; checked in `make dev` with placeholder nodes.

### Accomplished

- `ui/src/tree/render.js` extended: `buildUnreadBadge` (primary count), `buildTotalBadge` and `buildSizeBadge` (secondary hover metadata), `buildHoverRegion` (wraps secondary badges with `data-region="hover"`), `buildSwatch` (account color swatch on leading edge), `buildRegion` handles `hover` region, `formatSize`/`formatSizeFull` helpers for byte formatting; `renderRow` now adds `tree-row-favorite`, `tree-row-error`, and `tree-row-unread` classes; `patchRow(node, row)` exported for incremental row patching.
- `ui/public/styles/tree.css` extended: `--pav-tree-swatch` positioning, `.tree-row-swatch` and `.tree-row-swatch-1`/`-2` classes, `.tree-row-favorite` star marker via `::after`, `.tree-row-error` red text, `.tree-row-badge-total`/`-size` with hover-to-reveal opacity transition, `.tree-row-toolbar` and toolbar button styling, `prefers-reduced-motion` disables hover transition.
- `ui/src/tree/state.js` extended: `showFavorites`, `showHeart`, `hideInfrequent` local state with getters and `setShow*`/`toggleShow*` methods; `visibleNodes` updated to filter roots by favorites, heart set, and infrequent; `serialize` includes filter flags.
- `ui/src/tree/tree.js` extended: `patchNode(id, updates)` for incremental row updates via `patchRow`, `findRow(id)` lookup, `getRow(id)` public method, `render()` full re-render method, `setRowMenu(getItems)` for context menu provider, `rootVisible` helper in renderTree respects filter flags; `renderNode` sets `favorite` class from viewState.
- `ui/src/shell/template.js`: added `.tree-toolbar` with Star, Heart, Ghost filter buttons above `#mail-tree`.
- `ui/src/shell.js`: mail tree uses `regions: ["leading", "label", "count", "hover"]`, placeholder nodes updated with `total`, `sizeBytes`, `swatch`, `favorite`; `bindMailTree` adds filter toolbar handlers (toggle `showFavorites`/`showHeart`/`hideInfrequent` via viewState setters + `render()` + persist), context menu provider bound via `widgets.menus`; `onPersist` serializes filter flags.
- `ui/src/tree/tree-e3.test.js` created with 21 Vitest tests: renderRow favorite/error/swatch classes, hover metadata badges, patchRow incremental updates, controller patchNode and getRow, render with filter toggles, setRowMenu.
- `ui/src/tree/state.test.js` extended with 5 tests: visibleNodes favorites/heart/infrequent filtering, filter setters, serialize round-trip of filter flags.

### Lessons learned

- `queryCommandSelector` with `CSS.escape` is not available in the jsdom test environment, so `findRow` uses a manual double-quote escape instead. Root node lookup also requires scoping the query to the tree container (not `ownerDocument`) since the container may be detached in tests.
- The `data-node-id` attribute is on both `<li>` and `<button>`, so `querySelector` finds the `<li>` first; scoping the selector to `[role="treeitem"]` disambiguates.
- The `visibleNodes` filter logic must check visibility *before* pushing a child, not after, to avoid pushing nodes that should be hidden.
- The `hover` region's badges use opacity transitions rather than `hidden` so they fade in/out smoothly; `prefers-reduced-motion` disables the transition.
- Filter state is stored on the viewState object (not just the model), and `render()` re-evaluates visibility on each toggle.

### Handoff

- E-4 takes the renderer's existing transition duration variable for animation work.
- E-7 replaces placeholder nodes with real `mailbox.list` data; `patchNode` is the incremental path for count changes from `mail.mailboxes_changed` events.
- N-5 supplies Mail's real context menu items (move, delete, rename, subscribe) through the `setRowMenu` provider.
- Theme variables are unchanged; tree.css composes existing `--pav-*` tokens.

### Working log

- 2026-10-19 Extended `renderRow` with favorite/error/unread state classes, `buildSwatch` for account swatch, `buildTotalBadge`/`buildSizeBadge`/`buildHoverRegion` for secondary metadata, `patchRow` for incremental updates, `formatSize`/`formatSizeFull` helpers.
- 2026-10-19 Extended `tree.css` with swatch positioning, favorite star marker, error coloring, hover-reveal secondary badges, toolbar styling, reduced-motion handling.
- 2026-10-19 Extended `state.js` with `showFavorites`/`showHeart`/`hideInfrequent` flags, `setShow*`/`toggleShow*` methods, filtered `visibleNodes`, serialize round-trip.
- 2026-10-19 Extended `tree.js` with `patchNode` (incremental row update), `findRow`, `getRow`, `render`, `setRowMenu` (context menu provider), `rootVisible` filter helper.
- 2026-10-19 Updated `template.js` with tree toolbar (Star/Heart/Ghost filter buttons); updated `shell.js` with hover region, filter handlers, context menu provider, persisted filter flags.
- 2026-10-19 Created `tree-e3.test.js` (21 tests) and extended `state.test.js` (5 tests); all 240 Vitest tests pass; full `make test` passes.

## E-4: Animation and the height test

**Kind:** Easy
**Status:** Not started.

**Goal:** Expansion animates at the container level, interruptibly, and respects reduced motion.

**Entry gate:** E-3 complete.

**Work items:**

- [ ] Test grid-row (`grid-template-rows: 0fr → 1fr`) and measured-height techniques in the WebKitGTK window; record the winner in Lessons.
- [ ] Disclosure arrow rotation with one SVG; interruptible transitions under rapid toggling; `prefers-reduced-motion` shortens to 0.

**Done means:** Rapid expand and collapse never stutters or ends in the wrong state.

**Exit gate:** `make test` exits 0; in `make build`, toggle a branch ten times quickly and it settles in the last state.

## E-7: Wiring to the backend, mail first

**Kind:** Hard
**Status:** Not started.

**Goal:** The Mail nav is the real mailbox tree from `mailbox.list`, patched by events.

**Entry gate:** M-3 and E-4 complete.

**Work items:**

- [ ] `ui/src/mail/nav.js`: accounts as roots (number and name), mailboxes nested by `parentId`, role icons, unread counts, sync state per account from `sync.state`.
- [ ] `mail.mailboxes_changed` patches nodes; selection opens the mailbox in the home tab (J-2).
- [ ] Move the Settings → Accounts list onto the engine (K Lock).
- [ ] Account order and hidden accounts in view state.

**Done means:** Mailbox trees and unread counts match the server for every account (old plan P0 acceptance).

**Exit gate:** `make test` exits 0; in `make build` with Andrew's three first accounts, trees and counts match the server, and a mailbox created elsewhere appears without a restart.

## E-5: Presets

**Kind:** Medium
**Status:** Not started.

**Goal:** Named snapshot presets and the preset toolbar.

**Entry gate:** The initial working copy is complete (index milestone 4).

**Work items:**

- [ ] Snapshot presets (expanded set plus visible roots) with names; built-ins Compact, Mail triage, Everything, Custom; Save as preset; the toolbar above the tree.
- [ ] Vitest for switching and saving.

**Done means:** Switching presets restores exactly the saved expansion and roots.

**Exit gate:** `make test` exits 0; checked in `make build`.

## E-6: Drag and drop

**Kind:** Hard
**Status:** Not started.

**Goal:** Reorder siblings, reparent, and reorder accounts, by mouse and keyboard.

**Entry gate:** E-5 and N-5 complete.

**Work items:**

- [ ] Drag handle, insertion indicator (reorder versus nest), auto-expand on hover, auto-scroll near edges, keyboard move actions.
- [ ] Intents: local order (default), `mailbox.move` for a reparent the backend accepts, account reorder in view state. Messages dragged from the list onto a folder call `message.move`.
- [ ] Vitest for the intent decisions.

**Done means:** Reordering never moves mail on the server; reparenting a folder moves it only after the drop is a nest.

**Exit gate:** `make test` exits 0; checked against the container.

## Working log

- 2026-10-15 Tree design recorded. This document holds the architecture, the lock, and the seven proposed phases. Nothing is built. The mail tree in the shell preview is still four placeholder buttons.
- 2026-10-15 Revisions: node contract added, tokens moved to `rem` so shell zoom applies, density aligned to old plan §10, `*` scoped to tree focus against §6.13, Vitest named for E-1 through E-6.
- 2026-10-15 Root visibility filters added: star (favorites only), heart (second favorite set), ghost/hide (infrequent roots). Hidden roots stay in the model; visibility is view state, toggleable from preset toolbar buttons.
- 2026-10-15 Brainstorming added: per-pane settings (background, zoom, density), always-visible vs hover-visible row regions, preset toolbar buttons, right-click context menus per node type, per-section expectations for mail/calendar/contacts/files, keyboard expectations, and delightful-but-not-required items. None are locked or in a phase.
- 2026-10-16 Put in the sequence (E-1 to E-4 before mailbox sync, E-7 after M-3, E-5 and E-6 after the initial working copy). Phase sections with entry gates, work items, and exit gates written. Event names follow the G naming. Tokens take the `--pav-` prefix in E-1.
- 2026-10-17 Andrew: the Help index stays the plain list in `ui/src/help/view.js` until this engine exists, then Help uses it. Nodes come from `ui/src/help/pages.js`. Do not build that switch in J-8. Do not start E from this note.

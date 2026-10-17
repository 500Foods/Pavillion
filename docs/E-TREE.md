# E — TREE

**Status:** Design recorded (2026-10-15). In the sequence from 2026-10-16. No phase has started.
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

The one tree engine behind the section navs. Mail's mailbox tree comes first; files, calendars, contacts, and the Settings → Accounts tree reuse the row renderer and the view state. Nothing is built: the mail tree in the shell preview is still four placeholder buttons in `ui/index.html`. The Help index uses this engine once it exists. Until then Help keeps the plain list in `ui/src/help/view.js`. Andrew set that on 2026-10-17. Do not switch Help in J-8, and do not start E from that note.

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
| E-1 | Tokens, the static markup skeleton, and the row regions | Easy | Not started |
| E-2 | Tree model, view state, and the three collapse behaviors | Medium | Not started |
| E-3 | Renderer: icons, labels, badges, hover metadata, states | Medium | Not started |
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
**Status:** Not started.

**Goal:** A static tree renders placeholder nodes with the tokens and configurable row regions.

**Entry gate:** J-6 and K-3 complete.

**Work items:**

- [ ] `ui/styles/tree.css` with the `--pav-tree-*` tokens (the CSS section, renamed per the G-4 prefix).
- [ ] `ui/src/tree/render.js`: `renderRow(node, regions)` with regions leading icon, label, status, count, trailing actions, in an order given by config; ARIA `treeitem` attributes; text via `textContent`.
- [ ] Replace the four placeholder buttons in the Mail nav with a static tree of placeholder nodes.
- [ ] Vitest: `render.test.js` (region order, ARIA attributes, no HTML sink).

**Done means:** The Mail nav shows a placeholder tree drawn by the renderer, scaled by shell and nav-body zoom.

**Exit gate:** `make test` exits 0; in `make dev` the tree scales with both zoom sliders.

## E-2: Model, view state, three collapse behaviors

**Kind:** Medium
**Status:** Not started.

**Goal:** The tree model and view state hold expansion independent of visibility, with the three collapse behaviors.

**Entry gate:** E-1 complete.

**Work items:**

- [ ] `ui/src/tree/model.js`: nodes by id, children by parent, order, `patch(nodes)`, `remove(ids)`.
- [ ] `ui/src/tree/state.js`: expanded set, selected, focused, hidden roots, favorites (star and heart sets), infrequent set; `collapseAllReset`, `collapseAllRemember`/`restore`, `collapseBranch` that keeps descendants' state; persisted per tree in `preferences.ui.trees.<treeId>`.
- [ ] Keyboard per the Lock (arrows, Home/End, `*` within tree scope, `+`/`-`) through J-3's registry.
- [ ] Vitest: `model.test.js`, `state.test.js` (collapse branch keeps descendants, remember and restore, hidden roots keep selection).

**Done means:** Collapsing `Projects` and reopening it restores `Pavilion → Development` as it was.

**Exit gate:** `make test` exits 0; the case above works in `make dev` with placeholder nodes.

## E-3: Renderer states, badges, hover metadata, menus

**Kind:** Medium
**Status:** Not started.

**Goal:** Rows show counts, states, swatches, and hover metadata, and have context menus.

**Entry gate:** E-2 and J-1 complete.

**Work items:**

- [ ] Unread badge primary; total or recursive size on hover in a secondary region (never replacing the unread count); state classes for unread, selected, error, syncing, favorite; account swatch leading edge; duotone icon slot contract.
- [ ] Context menus per node type from the Brainstorming list, as data the section supplies (Mail supplies its actions in N-5).
- [ ] Incremental patch: a count change updates one row's badge without re-rendering siblings (asserted in Vitest by node identity).
- [ ] Filter buttons: star, heart, ghost.

**Done means:** A count patch updates one badge, hover shows secondary metadata, and menus open by mouse and keyboard.

**Exit gate:** `make test` exits 0; checked in `make dev` with placeholder nodes.

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

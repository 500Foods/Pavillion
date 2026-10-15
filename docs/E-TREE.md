# E — TREE

**Status:** Design recorded (2026-10-15). No phase in this document. The phases below are not in the sequence and none has started.
**Resume:** The one tree engine behind the section navs. Mail's mailbox tree comes first; files, calendars, and contacts reuse the row renderer and the view state. This document holds the architecture and the lock. Nothing is built: the mail tree in the shell preview is still four placeholder buttons in `ui/index.html`, and the phases below wait for mail to be real (§14 P0 in [PAVILLION_PLAN.md](../PAVILLION_PLAN.md)).

The tree is a configurable navigation system, not a list widget. It describes what the user sees and how it behaves, and it does not own the underlying mail, contact, or file data. Nodes arrive from `mailbox_list` and the other list commands through `api.js`; the tree renders them.

## Where it is used

| Section | Nav | Shape |
| --- | --- | --- |
| Mail | Mailbox tree | Nested, per account, with parent folders |
| Files | Folder tree | Nested; empty for an IMAP account (plan §10) |
| Calendars | Calendars grouped by account | Flat groups; row renderer only |
| Contacts | Accounts and recents | Flat lists; row renderer only |

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

Updates arrive as events, not as polls: `mail://mailboxes-changed` and `mail://messages-changed` today (plan §2.3), with `contacts://changed`, `calendar://changed`, and the files equivalent when those screens are real. The tree patches the named nodes and leaves the rest alone.

Account order and hidden accounts (plan §6.1) live in the tree's view state. A preset snapshot is taken over that arrangement.

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
- Density is compact and comfortable, the two in plan §10. A third, spacious, is recorded here but not promised. All three are a handful of custom properties, not separate stylesheets.

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

Layout tokens. They are `rem`, so the shell zoom (50–300%, plan §10) scales rows with everything else; at 100% they hold the pixel values below.

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

Every row uses the same tokens, so density, scale, and typography change without touching the renderer. The names follow the theme layer's semantic custom properties; `theme.css` holds the rules and only references variables. Two scales apply to the nav body: `preferences.zoom.shell` on the root, and `preferences.zoom.navBody` multiplying the pane. A pane scale never resizes the nav itself, only what is inside it.

The icon slot has its own sizing and color contract rather than letting icons dictate row dimensions. Duotone icons carry a primary and a secondary layer, so the CSS accounts for both layers and their opacity. The same slot hosts a folder icon, a sync indicator, a favorite marker, or a custom SVG without changing the layout. The kit and its families stay as plan §10 and §11 describe.

Hover, focus, selection, and drag-over are separate states. A selected row keeps its selected background when hovered, and keyboard focus does not depend on mouse-hover styling.

The tree is not a table. It is not built with Tabulator or another table library; the message list makes that call the other way.

## Lock

- ES modules, HTML, and CSS variables. No framework, no CSS library. The tree is not a Tabulator screen.
- `ui/src/api.js` is the only module that imports Tauri. The tree asks it for nodes; it does not call `invoke`.
- The tree owns view state only. It does not own mail, contact, or file data, and it does not compute counts.
- Stable node IDs, and incremental row updates. No full rebuilds on count or sync changes.
- Density, scale, and color come from custom properties. No hard-coded colors in the renderer.
- Container-level, interruptible animation. `prefers-reduced-motion` disables or shortens it.
- Hover, focus, selection, and drag-over are separate states.
- Snapshot presets land before rule-based presets.
- Drag-and-drop exposes intent. Local ordering is the default; the backend decides any server effect.
- The ARIA tree pattern: `role="tree"` and `role="treeitem"` with `aria-expanded`, `aria-selected`, and `aria-level`; roving tabindex; arrow keys and Home/End. `*` expands siblings **while the tree has focus**; `*` toggles a flag on the message list (plan §6.13), so the binding is scoped, not claimed. Keyboard move actions cover drag-and-drop.

## Phases

Not in the sequence in [PAVILLION_PLAN.md](../PAVILLION_PLAN.md), and none started. They open when mail is real (§14 P0) and a mailbox tree has data behind it.

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

E-1 through E-6 are DOM and pure logic: Vitest covers the model and view state, as plan §13 already plans for modules that do not need a window. E-7 needs the window, and Playwright against `tauri dev` is the optional check for its keyboard flows.

A UI exit gate is the window, not a screenshot in the repo. `make test` must still pass if a phase touches the shell.

## Working log

- 2026-10-15 Tree design recorded. This document holds the architecture, the lock, and the seven proposed phases. Nothing is built. The mail tree in the shell preview is still four placeholder buttons.
- 2026-10-15 Revisions: node contract added, tokens moved to `rem` so shell zoom applies, density aligned to plan §10, `*` scoped to tree focus against §6.13, Vitest named for E-1 through E-6.

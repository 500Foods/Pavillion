# E — TREE: Feature Inventory and Planned Extensions

**Status:** Working document. Captures the full feature surface requested and planned beyond E-5 and E-6. Each bullet below is a candidate E-phase; the index in [PAVILLION_PLAN.md](../PAVILLION_PLAN.md) decides the order.

## Current state

E-1 through E-4 and E-7 are complete:

- **E-1**: tokens (`--pav-tree-*`), skeleton, `renderRow(node, regions)` with configurable regions, `createTree` lifecycle, ARIA treeitem, `textContent` for all outside data. 16 Vitest tests.
- **E-2**: model (`model.js` with nodes-by-id, children-by-parent, ordering, `patch`/`remove`/`descendants`/`ancestors`/`depth`, `toArray`), view state (`state.js` with expanded set, selected/focused, hidden roots, star/heart/infrequent sets, three collapse behaviors, persistence to `preferences.ui.trees`), controller (`tree.js` with `createTreeController`), keyboard shortcuts via J-3. 54 Vitest tests.
- **E-3**: badges (primary unread, secondary total/size on hover), state classes (unread, selected, error, syncing, favorite), account swatch, context menus via `setRowMenu`, filter buttons (star/heart/ghost), incremental `patchRow`. 21 Vitest tests.
- **E-4**: `max-height` transitions on `.tree-group`, rotating disclosure chevron, interruptible transitions via incremental class toggling, `prefers-reduced-motion` disable. 13 Vitest tests.
- **E-7**: `ui/src/mail/nav.js` wires real `mailbox.list` data, `mail.mailboxes_changed` patches, `sync.state` painting, `accounts.changed` reload, hide-account writes `hiddenRoots`.

## Where to look for what exists now

| Feature | File(s) | Status |
|---|---|---|
| CSS tokens and layout | `ui/public/styles/tree.css` | Exists, `--pav-tree-*` prefix |
| Row renderer | `ui/src/tree/render.js` | Exists, `renderRow`, `patchRow`, `createTree` |
| Model | `ui/src/tree/model.js` | Exists, full CRUD |
| View state | `ui/src/tree/state.js` | Exists, persistence, filters |
| Controller | `ui/src/tree/tree.js` | Exists, `createTreeController` |
| Tests for render | `ui/src/tree/render.test.js` | 16 tests |
| Tests for model | `ui/src/tree/model.test.js` | 17 tests |
| Tests for state | `ui/src/tree/state.test.js` | 22 tests |
| Tests for controller | `ui/src/tree/tree.test.js` | 15 tests |
| Tests for E-3 features | `ui/src/tree/tree-e3.test.js` | 21 tests |
| Tests for animation | `ui/src/tree/tree-animation.test.js` | 13 tests |
| Tree integration in mail | `ui/src/mail/nav.js` | Complete, E-7 |

## Feature gaps and planned E-phases

The following is a consolidated list of every capability mentioned in the brainstorm, the E-TREE Lock, the AGENTS.md rules, and Andrew's requests. Each item notes whether it is **already present**, **partially present**, or **not started**, and what phase it would slot into.

### Filtering and search

| Feature | Status | Notes |
|---|---|---|
| Tag/label filtering per node | Not started | Nodes would carry a `tags: string[]` or `labels` field; a toolbar button or input filters roots/branches. Builds on E-5's preset toolbar. |
| Node search (type-ahead) | Not started | "Typing a letter jumps to the next folder starting with that letter" from the Brainstorming section. A small inline search or keystroke buffer on the tree container. |
| Rule-based filtering presets | Not started | Beyond snapshot presets (E-5), criteria like "expand every folder containing unread mail." Listed under "rule-based presets" as a later enhancement. |
| Section-level search (separate from mail query) | Not started | A general tree filter that narrows visible rows by label match, distinct from Q-2's mail query. UI-FILTER.md is a working reference, not a Lock. |

### Rendering and icons

| Feature | Status | Notes |
|---|---|---|
| Custom icons per node | Partially present | `node.icon` is checked in `buildLeadingIcon` and used if present; otherwise `ICON_MAP[node.kind]` is the fallback. The D-9 Lock covers custom icon HTML through the registry, and F-8's icon settings tree uses the E tree. Needs integration with the icon registry (`ui/src/icons/registry.js`) and the icon sanitizer (F-Lock). |
| Zoom-safe chevron and icons | Partially present | Icons use `--pav-tree-icon-size` in `rem`, so shell zoom scales them. The chevron rotates via CSS transform (E-4). May need `--pav-tree-icon-size-zoom` overrides if the icon slot needs independent scaling. |
| Account swatch on leading edge | Present | `buildSwatch` with `--pav-tree-swatch` classes or inline `backgroundColor`. Supports `swatch: 1`, `swatch: 2`, or a hex string. CSS has `.tree-row-swatch-1` / `.tree-row-swatch-2` / `.tree-row-swatch-color`. |
| Density variants | Partially present | Lock says "compact and comfortable, a third spacious recorded but not promised." The CSS tokens (`--pav-tree-row-height`, padding) are the lever. Needs a `preferences.ui.density` field wired in. |
| Duotone icon slot contract | Present in CSS | `tree.css` sizing rules account for primary/secondary layers via `--pav-tree-icon-size`. Font Awesome kit is Duotone Sharp. No separate handling needed beyond what the kit provides. |

### Animation and transitions

| Feature | Status | Notes |
|---|---|---|
| Chevron rotation | Present | `tree-row-expanded .tree-row-disclosure { transform: rotate(90deg) }`, transition on `transform`. |
| Container-level expand/collapse | Present | `.tree-group` `max-height` transition, `tree-group-expanded`/`tree-group-collapsed` classes. |
| Interruptible transitions | Present | E-4's controller toggles classes on existing DOM instead of full `renderTree` for single-node operations. |
| Reduced motion | Present | `@media (prefers-reduced-motion: reduce)` disables transitions, uses `display: none/block`. |
| Badge pulse for new unread | Not started | "Badge pulse animation for new unread" from Brainstorming. Needs a `pulse` class and a CSS keyframe, gated on `prefers-reduced-motion`. |
| Disclosure "jog" on new mail | Not started | "The disclosure arrow 'jogs' when new mail arrives in a collapsed folder." A one-shot CSS animation on the chevron when a collapsed ancestor gets an unread patch. |

### Counts, metadata, and badges

| Feature | Status | Notes |
|---|---|---|
| Multiple summary values per row | Partially present | Currently: primary `unread` badge, secondary `total`/`sizeBytes` on hover. Extending to more badges (flagged, answered, attachments) means a badge stack in the count or trailing region. |
| Multiple flags per row | Not started | "favorited, starred, etc." Currently only `favorite` (star marker) and `state` (error/sync). A `flags: string[]` array on nodes with flag icons in a trailing region, or data attributes for CSS. |
| Hide metadata, show on hover | Present | Hover region badges use `opacity: 0` → `opacity: 1` on `.tree-row:hover`. `prefers-reduced-motion` disables the transition. |
| Collapsed row aggregate counts | Not started | "A parent folder does not add its children" per N-3. Could add an option to show recursive counts on collapsed parents (slower, server-reported or client-summed). |
| Recursive size tooltip | Present | `sizeBytes` + `sizeLabel: "recursive"` → `formatSizeFull` sets an `aria-label` like "1.8 GB recursive size." |

### Expansion and visibility

| Feature | Status | Notes |
|---|---|---|
| Remember expansion on restart | Present | E-2 persists `expanded` in `preferences.ui.trees.<treeId>`. |
| Three collapse behaviors | Present | `collapseAllReset`, `collapseAllRemember`/`restoreAll`, `collapseBranch`. |
| Hide individual branches | Partially present | `hiddenRoots` hides root-level nodes. Could extend to `hiddenNodes` (any node ID) for hiding specific folders, not just roots. |
| Expand/collapse animations | Present | E-4. |
| Auto-expand on drag hover | Not started | E-6's "auto-expansion when hovering a collapsed destination during a drag." |
| Animate on preset switch | Not started | E-5's "rapid preset switches move the tree toward the newest state." Builds on E-4's interruptible transitions. |

### Drag and drop

| Feature | Status | Notes |
|---|---|---|
| Drag handle per row | Not started | E-6. A grip element wrapping the icon, `draggable="false"` on the icon itself, `pointer-events: none` on the icon, `preventDefault` on row `pointerdown`. |
| Insertion indicator | Not started | E-6. CSS classes or a thin line element showing reorder-vs-nest target. |
| Reorder siblings | Not started | E-6. |
| Reparent | Not started | E-6. The tree exposes the intent; the backend decides server effect. |
| Reorder accounts/pinned sections | Not started | E-6. |
| Keyboard move actions | Not started | E-6. ARIA `move`, Home/End, etc. |
| Cross-pane drag (messages onto folders) | Not started | "Drag a message from the message list onto a folder in the tree." Needs coordination with the mail list. |

### Universal / aggregate folders

| Feature | Status | Notes |
|---|---|---|
| Universal Inbox | Not started | A synthetic node that aggregates `in` mailboxes across accounts. The node would have a `virtual: true` flag and a `sources: string[]` (mailbox IDs). Counts aggregate from the sources. Selection opens a unified view in the mail tab. |
| Universal Spam, Unified Trash | Not started | Same mechanism: a virtual node whose `kind` is `virtual` (or `mailbox` with `role: "unified-inbox"`), and a `unifiedView` field. |
| Virtual node data model | Not started | Needs a model change: `virtual: true`, `aggregateOf: string[]`, `aggregated: { unread, total, sizeBytes }`. The model and renderer must accept these without breaking existing `parent_id` semantics. |
| Sync for virtual nodes | Not started | Virtual nodes don't exist on the server; they're synthesized from `mailbox.list` results. Could live in `nav.js` or a new `virtual-tree.js` module. |

### Presets and toolbar

| Feature | Status | Notes |
|---|---|---|
| Snapshot presets (named) | Not started | E-5. Expanded set + visible roots saved as a named preset. Built-ins: Compact, Mail triage, Work, Everything, Custom. |
| Save as preset | Not started | E-5. |
| Preset toolbar above tree | Present | `template.js` has `.tree-toolbar` with Star/Heart/Ghost filter buttons. Presets would add a dropdown. |
| Rule-based presets | Not started | "Expand every folder containing unread mail." A later enhancement after snapshots. |

### Context menus

| Feature | Status | Notes |
|---|---|---|
| Per-node-type menus | Partially present | `setRowMenu(getItems)` exists; the section supplies `{ label, onSelect, disabled, current }`. Actual menu data for Mail is deferred to N-5. |
| Right-click + keyboard (Shift+F10, Menu) | Not started | J-1's `ui/src/common/widgets.js` menus module binds per-row. The tree passes the node ID to the provider. |
| Menu items from Brainstorming | Not started | Rename, Properties, New message here, Subscribe/Unsubscribe, etc. Section-specific; deferred per Lock. |

### Keyboard navigation

| Feature | Status | Notes |
|---|---|---|
| Arrow keys | Present | E-2 registered `+`/`-`/`Enter`/`Space`/arrows through J-3's registry with `"tree"` scope. |
| Home/End | Not started | Listed in Brainstorming and Lock, but not explicitly implemented. |
| `*` expands siblings | Present | Scoped to tree focus via J-3's registry. |
| `+`/`-` expand/collapse | Present | Registered in E-2. |
| Type-ahead | Not started | "Typing a letter jumps to the next folder starting with that letter." |
| Keyboard drag equivalents | Not started | E-6. |

### Accessibility

| Feature | Status | Notes |
|---|---|---|
| ARIA tree/treeitem roles | Present | `role="tree"`, `role="treeitem"`, `role="group"`, `aria-level`, `aria-selected`, `aria-expanded`, `aria-busy`. |
| Roving tabindex | Partially present | Rows are `<button>` elements (naturally focusable). A `tabindex="-1"` on non-selected items and a roving handler would complete this per the Lock. |
| Focus rings | Present | CSS `button:focus-visible { outline: 2px solid var(--pav-accent) }`. |
| Contrast | Present | Theme variables (`--pav-tree-text`, `--pav-tree-bg-selected`, etc.) reference `--pav-*` semantic variables that have 4.5:1+ contrast per D-Lock. |

### Cross-section reuse

| Feature | Status | Notes |
|---|---|---|
| Calendar tree (flat groups) | Not started | T-1/T-2. Reuses row renderer; needs calendar-specific `kind` and swatch color. |
| Contacts tree (flat lists) | Not started | S-1/S-2. Accounts and recents flat lists. |
| Files tree (nested) | Not started | U-1/U-2. Reuses row renderer; empty for IMAP-only accounts. |
| Settings → Accounts tree | Deferred | Still a plain nested list per K-3/Accomplished. Andrew decided (2026-10-21) it stays Tabulator. E-7 will not move it. |

### Proposed E-phase breakdown

Based on the above inventory, here are the candidate E-phases. The PAVILLION_PLAN index decides order; these are ordered roughly by dependency.

**E-5: Presets (already sequenced)**

- Goal: Named snapshot presets, toolbar above tree, save as preset, built-in presets (Compact, Mail triage, Work, Everything, Custom).
- Entry gate: Initial working copy (milestone 4).
- Work items:
  - [ ] Preset manager in `state.js`: `presets` map (`name → {expanded, hiddenRoots, showFavorites, showHeart, hideInfrequent}`), `savePreset(name, snapshot)`, `applyPreset(name)`, `deletePreset(name)`, `renamePreset(old, new)`.
  - [ ] Toolbar dropdown in `template.js` next to Star/Heart/Ghost: preset picker with built-ins + saved + "Save as…".
  - [ ] `applyPreset` restores expansion + filters + hidden roots atomically, then `render()`.
  - [ ] Vitest: `preset.test.js` — save, apply, delete, round-trip persistence in `serialize`.

**E-6: Drag and drop (already sequenced)**

- Goal: Reorder siblings, reparent, reorder accounts, by mouse and keyboard.
- Entry gate: E-5 and N-5 complete.
- Work items:
  - [ ] Drag handle element per row: a grip `<button>` in the leading region, wrapping the icon, with `draggable="false"` on the icon, `preventDefault` on `pointerdown`.
  - [ ] Insertion indicator: a CSS class or thin element on the target row showing insert-before, insert-after, or nest.
  - [ ] Auto-expand on hover: a timer (150-200ms) on `dragover` that expands a collapsed branch.
  - [ ] Auto-scroll near top/bottom: 20px threshold, 4px/step scroll, `requestAnimationFrame`.
  - [ ] Intent API: `onDropIntent(intent, targetId)` where intent is `{operation: "reorder"|"reparent"|"reorder-account", sourceId, targetId, position}`. The backend decides server effect.
  - [ ] Keyboard equivalents through J-3 registry: `Ctrl+Shift+Up`/`Ctrl+Shift+Down` reorder, `Ctrl+Shift+Right` reparent into, `Ctrl+Shift+Left` move out.
  - [ ] Vitest: `drag.test.js` — intent decisions, no accidental server calls.

**E-8: Universal / aggregate nodes**

- Goal: Virtual nodes that aggregate mailboxes across accounts (Universal Inbox, Spam, Trash, or user-defined aggregates).
- Entry gate: E-7 complete (real mailbox tree).
- Work items:
  - [ ] Model: accept `virtual: true`, `sources: string[]`, `aggregateOf: string[]` fields. `patch` handles virtual node updates.
  - [ ] State: virtual node IDs persist in `selected` and `expanded` like real nodes.
  - [ ] Renderer: `buildLeadingIcon` supports `kind: "virtual"` with a multi-envelope icon. Aggregate counts render in the count region (computed from sources, cached on the node).
  - [ ] Sync: `nav.js` synthesizes virtual nodes at startup and patches them when `mail.mailboxes_changed` arrives for a source account.
  - [ ] Vitest: `aggregate.test.js` — virtual node insertion, count aggregation, patch propagation.

**E-9: Node filtering and search**

- Goal: Filter the visible tree by tag/label, and by text search.
- Entry gate: E-7 complete.
- Work items:
  - [ ] Node field: `tags: string[]` (or `labels`). The model stores and patches it.
  - [ ] State: `tagFilter: string[]` (show only nodes with any of these tags), `labelFilter: string` (case-insensitive substring on label).
  - [ ] Toolbar: a tag pill selector and a search input, persisted in `preferences.ui.trees.<treeId>`.
  - [ ] `visibleNodes` updated to check tags and label match against the node and its ancestors (a hidden ancestor hides descendants).
  - [ ] Vitest: `filter.test.js` — tag filtering, label search, ancestor-hide propagation.

**E-10: Type-ahead and enhanced keyboard**

- Goal: Type-ahead navigation, Home/End, and complete roving tabindex.
- Entry gate: E-7 complete.
- Work items:
  - [ ] Type-ahead buffer: on `keydown` in the tree, accumulate alphanumerics for 500ms; find the next node (in visible order) whose label starts with the typed prefix.
  - [ ] Home/End: jump to first/last visible node; Ctrl+Home/End to first/last root.
  - [ ] Roving tabindex: only the selected/focused row has `tabindex="0"`, others `tabindex="-1"`. Arrow keys call `focus` then the J-3 registry.
  - [ ] Vitest: `keyboard.test.js` — type-ahead prefix match, Home/End movement, roving tabindex on re-render.

**E-11: Multiple flags and badge stack**

- Goal: Per-row flags (favorited, starred, answered, forwarded, has-attachment) and a configurable badge stack.
- Entry gate: E-7 complete.
- Work items:
  - [ ] Node field: `flags: string[]` and `badges: { key: string, value: number | string }[]`.
  - [ ] State: `flagOrder: string[]` defines icon rendering order; defaults in `state.js`.
  - [ ] Renderer: a `flags` region (configurable) renders small icons; `patchRow` updates flags incrementally.
  - [ ] CSS: `.tree-row-flag` with per-flag icon via `data-flag` attribute, sized at `--pav-tree-icon-size * 0.85`.
  - [ ] Vitest: `flags.test.js` — flag add/remove patch, badge count rendering.

**E-12: Density and icon scaling**

- Goal: Comfortable and spacious density variants; zoom-safe icon scaling.
- Entry gate: E-1 (tokens exist), D-4/D-6 (theme catalog and skin).
- Work items:
  - [ ] `preferences.ui.density` field (`"compact"`, `"comfortable"`, `"spacious"`), persisted.
  - [ ] CSS: `--pav-tree-row-height-comfortable`, `--pav-tree-row-height-spacious` mapped via `[data-density="comfortable"] .tree { ... }`.
  - [ ] Zoom-safe icons: a `--pav-tree-icon-size-scale` multiplier that the Zoom settings panel (`preferences.ui.zoom.navBody`) drives.
  - [ ] Vitest: `density.test.js` — class application, variable switching.

### Notes on integration with other systems

- **Icons**: All icons come through the Font Awesome kit (`45801522ef`). The icon registry (`ui/src/icons/registry.js`) lists every icon key the UI draws. Custom per-node icons (E-9 notes) go through the icon sanitizer (F-Lock) before reaching the DOM. The tree must not emit raw `<i class=...>` from outside data; it uses `icon(key)` from the registry or `node.icon` which is an already-sanitized class string.
- **Persistence**: Tree view state (expansion, selection, filters, presets) lives in `preferences.ui.trees.<treeId>` per E-2's `serialize()`. The 64 KB UI blob clamp (B Lock) applies; the tree store is a plain object and stays small.
- **Keyboard**: The J-3 registry scopes shortcuts. The tree registers under `"tree"` scope. New shortcuts (type-ahead, Home/End, drag equivalents) must go through `keys.register` so they don't collide with workspace-level bindings. The `*` key is already scoped so it fires only when the tree has focus (against the message list's `*`).
- **Events**: `mail.mailboxes_changed` patches the mail tree. Virtual/aggregate nodes (E-8) need their own event or a synthesized patch when source accounts change. A generic `tree.nodes_changed` event could carry patches for any tree, not just mail.
- **Drag and drop intent**: The tree does not move mail. It calls `onDropIntent(...)` and the backend (N-5 for mail, U-2 for files) decides whether it's a local reorder, a `mailbox.move`, or rejected.
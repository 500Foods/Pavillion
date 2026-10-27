# UI — FILTERS

A working reference, not a Lock. The locks that own filtering live in their letters:
- `docs/E-TREE.md` Lock controls tree root filters (star, heart, ghost).
- `docs/D-UI.md` Lock controls the shared module-bar search field.
- `docs/Q-SEARCH.md` Lock controls mail search (local and server).
- `docs/H-LOGGING.md` Lock controls the log level, subsystem, and text filters.
- `docs/N-MAIL.md` Lock controls mail list rows and `$deleted`.

This page collects the conventions every module should follow when it lets the
user narrow a list of rows, so all tree views and all Tabulator views filter the
same way.

## Status

Partly in place (2026-10-27). The Accounts search field follows the pattern. The vertical filter panel (below) exists in Help only (e27292b, 2026-10-24, "just the UI bits"). The Accounts module does not have one yet. Mail and Files will pick up both when they get a search field.

## The pattern

A filter narrows what renders, not what is stored. Clearing the filter restores
every row, and filtering never changes a row id, parentage, or backend data.
The filter text is a plain, case-insensitive substring over the columns a
module declares, set on the shared `FilterModule` (`table.setFilter(fn)`) or on
the tree renderer's `visibleNodes` predicate. An empty string or a cleared
field removes the filter. Escape clears it.

### Placement

- On a Tabulator table, the search field is the `.pav-module-search` input on
  the module toolbar, to the left of the column chooser (TABULATOR.md). It sets
  `table.setFilter(fn)` from the typed value. Empty groups disappear.
- On a plain-DOM tree, the filter buttons (Star, Heart, Ghost) sit in the
  `.tree-toolbar` above the tree, as in E-TREE.md. A future text filter joins
  them on the left of that bar.
- The keyboard focus matters: `/` focuses the search field of the active module
  (Q-2), Escape clears it, and Enter is not swallowed.

### What is filtered

- Accounts: name, number, email, username, and the server label. (D-Q17)
- Mail list: sender/recipient name, subject, and preview text.
- Files: name and path.
- Activity: the Action column and the account label.
- Log: severity level, subsystem, and a text search over the UTC file line.
- Tree roots: favorites, heart set, and infrequent (preset buttons only).

### Persistence

A module search stays in memory while that tab is open and does not survive a
restart. The log filters persist in `preferences.ui.log` (H-4). A tree's filter
flags persist in `preferences.ui.trees.<treeId>` (E-TREE.md). Saved searches in
Mail live in `preferences.ui.savedSearches` (Q-2).

### Row identity under a filter

Filtering hides rows; it does not renumber them. The selected row stays
selected when it survives, and the keyboard navigator walks only the shown
rows. The module footer's Refresh, First, Previous, Next, and Last operate on
the shown rows, not the underlying data.

## Applying it to a table

```
import { createTable } from "../common/table.js";

const table = createTable(el, {
  columns: [...],
  data: rows,
  select: true,
});

setSearch(value) {
  if (value.trim() === "") {
    table.setFilter(null);
  } else {
    table.setFilter((row) => accountMatches(row, value));
  }
}
```

`setFilter(null)` removes the predicate so every row shows. A predicate returns
`true` to keep a row. The match is a case-insensitive `String(row.field)
  .toLowerCase().includes(value.toLowerCase())`.

## Applying it to a tree

The tree controller already re-evaluates root visibility on each toggle. A
text filter joins the preset buttons: call `render()` after updating the view
state, and `visibleNodes` checks visibility before it pushes a child.

## Notes

- Filter the rendered set, not the model.
- Empty groups must disappear (TABULATOR.md).
- The navigator and the footer counts walk only the shown rows.
- A filter change does not emit a core command unless the module decides to
  persist it.

## Vertical filter panel (2026-10-24, Help)

This is the reusable left-edge filter bar. Help is the only module that has it so far: `ui/src/help/view.js` (`createHelpView`), markup in `ui/src/shell/template.js` (`#help-filter-panel`), and CSS in `ui/public/styles/theme.css` (`.help-filter-*`).

- **Toggle:** a chevron button (`fa-chevron-right`, `aria-pressed`, label "Toggle filter panel") is inserted before the module toolbar's `.pav-module-title`, followed by a 1px separator that matches the search field border.
- **Panel:** a full-height column, inserted as the first child of the module's content flex row, before the index. Closed, it is `width: 0; opacity: 0` with `aria-hidden="true"`. Open, it is `width: var(--pav-help-control-width)` with a right border, and width and opacity animate over `--pav-help-transition`. The panel's right border lines up with the chevron separator.
- **Chips:** an icon-only vertical strip (`.help-filter-icon-strip`) of `.help-filter-chip` buttons, one active at a time (`aria-pressed`), each with an `aria-label` and a fancy tooltip from the shell tooltip host (`options.tooltips.bind`). Help's chips are All, Pages, Dev, Settings and Mail. They combine with the toolbar search: search first, then the category set.
- **Zoom:** `shell.js` maps `#help-filter-panel` to the `navButtons` zoom (Button bars), the same as the module bars.
- **State:** the chosen chip lives in memory only and is not persisted.

**To reuse it:** the classes and the theme variables are still named `help-*`. Before a second module (Accounts first) adopts it, lift it into `ui/src/common/` as a helper next to `module-bar.js` with neutral `pav-filter-*` classes and `--pav-filter-*` variables in all four theme files. The module passes a chip list `{id, tip, icon, cat}` and an `onChange(cat)` callback, and keeps its own predicate (Help: page-id sets; a Tabulator module: `table.setFilter`). This lift is not done yet and needs a Lock entry in D-UI.md first.

# Tabulator in Pavillion

A working reference, not a Lock. The Lock for the shared table is N-MAIL ("Shared table component"); K-Q8 and K-Q11 cover the accounts table. Read 2026-10-20.

**Doc links.** `https://tabulator.info/docs/6.6/...` URLs fall back to the home page. The real pages are under `https://tabulator.info/docs/6.x/<page>/` (below: `D/<page>`). That site still says "Latest version 6.5.0". The 6.6.0 and 6.6.1 notes are only on GitHub: https://github.com/tabulator-tables/tabulator/releases.

## Version, install, import

- `tabulator-tables` **6.6.1**, pinned exactly in `ui/package.json`, MIT, bundled from `node_modules` (no CDN).
- Changes since 6.4:
  - **6.6.0:** range fill handle; `initialValue` when an edit starts; a Bootstrap 5 border fix.
  - **6.6.1:** `rangeEdited` also emits the changed cells.
  - **6.5.3:** fixes that matter to us: virtual renderer stack overflow, identical ids across table instances, `selectableRows` limit in `selectRows`, faster row templates and event buses.
  - **6.5.0:** `smart` and `smarter` filters, `selectableRangeInitializeDefault`.
  - Upgrades 6.0 → 6.4 have no breaking changes (D/upgrade, D/deprecated).
- **Import today:** `ui/src/common/table.js` does `import("tabulator-tables")` (ESM build `dist/js/tabulator_esm.mjs`). It takes `Tabulator` and registers `FormatModule`, `InteractionModule` and `GroupRowsModule`.
- **Keep core plus modules; never `TabulatorFull`** (D/modules). The package has `sideEffects` set, so Vite tree-shakes the modules we don't register. Today's chunk `tabulator_esm-*.js` is 166 KB (40 KB gzip -9). The minified full ESM build is 455 KB. Each new feature means registering its module in `table.js`, the only importer.
- Module per feature:

| Feature | Module |
| --- | --- |
| Remote and progressive load | `AjaxModule`, plus `PageModule` for progressive |
| Sort | `SortModule` |
| Filter | `FilterModule` |
| Row select | `SelectRowModule` |
| Cell range | `SelectRangeModule` |
| Keys | `KeybindingsModule` |
| Frozen columns | `FrozenColumnsModule` |
| Move columns | `MoveColumnsModule` |
| Resize columns | `ResizeColumnsModule` |
| Auto redraw on resize | `ResizeTableModule` |
| Layout persistence | `PersistenceModule` |
| Menus | `MenuModule` (needs `PopupModule`) |
| Footer calculations | `ColumnCalcsModule` |
| Copy | `ClipboardModule` |
| Download | `DownloadModule` |
| Edit | `EditModule` |

## Features for the Mail views (N-3 and later)

| Need | Options / API | Docs |
| --- | --- | --- |
| 100k+ rows | Virtual vertical renderer (`renderVertical:"virtual"`, the default). **Requires a fixed `height`** (or `maxHeight`); without one every row is drawn. Tune with `renderVerticalBuffer` (px). Pagination turns the virtual DOM off. | D/layout (Virtual DOM, Render Buffer), D/virtual-dom |
| Load through our core, not HTTP | `ajaxURL:"pav:message.list"` (any truthy string; the Ajax module only loads when a URL is set) plus `ajaxRequestFunc(url, config, params) → Promise` that calls `api.message.list(...)`. With `ajaxRequestFunc`, `ajaxURLGenerator` is not called. | D/data (Overriding the Request Promise) |
| Load while scrolling | `progressiveLoad:"scroll"` and `progressiveLoadScrollMargin` (default 2× table height). The reply must be `{last_page, data}` (`last_page` > 0). `paginationSize` is the page (100 per N Lock). Any `setData` or `updateData` stops progressive loading. | D/data (Progressive Ajax Loading), D/page |
| Sort and filter in the core | `sortMode:"remote"`, `filterMode:"remote"`: sorters and filters arrive in `params.sort` and `params.filter` in `ajaxRequestFunc`. A header click triggers a reload. | D/data, D/sort, D/filter |
| Row selection, shift and ctrl | `selectableRows:true` (or a number); `selectableRowsRangeMode:"click"` for shift-click ranges; `selectableRowsCheck(row)`; `selectableRowsPersistence`. Events `rowSelectionChanged(data, rows, selected, deselected)`, `rowSelected`, `rowDeselected`. **`setData` clears the selection.** | D/select |
| Cell ranges (not for mail) | `selectableRange`, `selectableRangeInitializeDefault:false` (otherwise cell [0,0] is selected on load). | D/range |
| Keyboard | Default bindings: arrows, PageUp/PageDown (`scrollPageUp`/`Down`), Home/End, and ctrl+C (copy) when the table has focus. `navUp`/`navDown` move **cell focus for editing**; there is no built-in "move the selected row". Mail needs a custom binding: `keybindings:{...}` with our own actions calling `selectRow` and `scrollToRow`, routed through the J shortcuts registry. | D/keybindings, D/navigation |
| Frozen columns | Column `frozen:true` (left or right edge only). Frozen rows: `row.freeze()`, which redraws. | D/layout (Frozen Columns) |
| Column layout into our prefs | `persistence:{columns:["width","visible"], sort:true}` (avoid `"title"`, which is the default list for `true`); `persistenceID:"mail-list"`; `persistenceWriterFunc(id, type, data)` and `persistenceReaderFunc(id, type)` that write into `preferences.ui`. **The reader is synchronous**, so read from the prefs blob the page already loaded. Never cookie or localStorage. | D/persist |
| Group by thread or date | `groupBy:"field"` or a function, `groupHeader(value, count, data, group)` returning a DOM node, `groupStartOpen` (bool, function, or array), `groupValues` for a fixed order, `groupToggleElement` (`"arrow"`, `"header"`, `false`), event `groupClick`. Groups are built from the loaded rows, so with remote sort the core must sort by the group key first. | D/group |
| Move and resize columns | `movableColumns:true` with `columnMoved`; column `resizable` with `columnResized`. Persist both. | D/move, D/layout |
| Column calcs and footers | `bottomCalc` (and `groupClosedShowCalcs`) need `ColumnCalcsModule`. A plain text footer is `footerElement` (core). | D/column-calcs |
| Menus | `rowContextMenu`, `headerMenu` (Menu and Popup modules). Menu labels must be text. | D/menu |
| Copy and download | `clipboard:"copy"`; `download("csv", ...)`. XLSX and PDF need SheetJS or jsPDF through `dependencies:{...}` (6.3); not wanted. | D/clipboard, D/download |
| Responsive | `responsiveLayout:"hide"` or `"collapse"`; with `fitColumns`, give droppable columns a `minWidth`. | D/layout |
| Editing | Not for mail. Accounts edit in the form. | D/edit |

## Theming

- We load **no Tabulator stylesheet**. `.pav-table` rules in `ui/public/styles/theme.css` use only `--pav-*` variables.
- Tabulator's own themes are SCSS (`$backgroundColor`, and so on) compiled to fixed colours. `dist/css/tabulator.css` has **no CSS custom properties**, so it can't follow our themes without overrides (D/style, D/theme).
- The structural rules matter, and today we copy only what we use:
  - header and row layout
  - `.tabulator-tableholder` overflow
  - group rows (`.tabulator-row.tabulator-group`)
  - `.pav-table-footer`
- Each new module needs its structural CSS copied from `node_modules/tabulator-tables/dist/css/tabulator.css` and recoloured with `--pav-*`:
  - frozen cells: `.tabulator-frozen{position:sticky;left:0;z-index:11;background-color:inherit}`
  - sort arrows: `.tabulator-col-sorter`, `.tabulator-arrow`
  - the group toggle arrow
  - `.tabulator-menu` and `.tabulator-popup-container`
  - `.tabulator-range-selected`
  - the row selection state `.tabulator-selected`
  - the virtual DOM's `.tabulator-tableholder` height
- The alternative is to import `tabulator.css` once and override it. That brings fixed colours, which the theme-vars test forbids in `theme.css`, so copying is the rule.

## Gotchas

- **Hidden containers:** a table built while `display:none` (closed panel, other tab) measures 0. Call `table.redraw()` (`redraw(true)` for a full re-render) when it becomes visible. Our `ResizeObserver` covers the visible case. `ResizeTableModule` with `autoResize` does the same.
- **No height means no virtualisation.** The accounts table uses `renderVertical:"basic"` on purpose (few rows). The mail list must set a height (for example `height:"100%"` of a sized pane).
- **jsdom:** Tabulator lays out nothing under jsdom (no sizes), so rows don't render. Unit-test row and column logic with plain functions and a fake `createTable` (as `accounts.test.js` does). Test that options reach Tabulator with a mocked `tabulator-tables` (as `table.test.js` does). Real rendering is a window check.
- **Callbacks became events in 5.0:** use `table.on("rowClick", (e, row) => …)` and `on("tableBuilt")`. Options like `rowClick:` are ignored. Signatures carry the event first for UI events, and components (`row.getData()`), not raw data.
- **`tableBuilt` first:** calls such as `replaceData`, `setSort` and `selectRow` before `tableBuilt` throw or are lost. Our wrapper queues data until then.
- **No external listeners on cells:** the virtual DOM recreates row elements. Bind through formatters (fresh nodes each render) or table events, not `querySelector` and `addEventListener`.
- **HTML sinks:** `formatter:"html"`, string `groupHeader`, string `footerElement` and string titles go through `innerHTML`. Ours return DOM built with `textContent`. Keep it that way for anything carrying mail data.
- **`setData` vs `replaceData`:** `setData` resets scroll and selection and stops progressive load. `replaceData` keeps scroll position. `updateData` patches by `index`; give every row a stable `index` (`rowId`).
- **Performance:**
  - Keep formatters cheap (no layout reads).
  - Avoid `variableHeight` rows in the mail list; a fixed row height keeps scrolling smooth (6.5 fixed jumps with variable heights, but the cost remains).
  - Don't call `redraw(true)` on every change.
  - With grouping, prefer `groupStartOpen:true`, and remember group headers count against the virtual buffer.
  - Group plus progressive load can split one group across pages.
- **Rows created during the first render** (`rowFormatter`): our `rowClass` reapplies on every render.

## How `ui/src/common/table.js` should evolve (suggestions)

**Done 2026-10-20 (K-Q12):** modules registered per option (no `TabulatorFull`); data passed once; ResizeTable instead of our own ResizeObserver; tri-state sort with Font Awesome `headerSortElement`; `columnCalcs:"table"`; single selection changed only by the page (`selectableRowsCheck` gate), kept across `replaceData`; `footerElement` node; `download` through Export and Download modules. Still open: remote loading, persistence, J-registry keyboard.

1. **One module list.** Register only what callers ask for, from a map (`{groupBy: GroupRowsModule, selectable: SelectRowModule, …}`), so screens don't pull in mail modules. `registerModule` is global, so registering more than once is harmless.
2. **A `remote` option.** `{load(params) → Promise<{rows, lastPage}>}` maps to `ajaxURL:"pav:"`, `ajaxRequestFunc`, `progressiveLoad:"scroll"`, `sortMode`/`filterMode:"remote"`. Pages never see Ajax.
3. **A `persist` option.** `{id, read(type), write(type, data)}` backed by `preferences.ui.tables[id]`, with columns limited to `["width","visible"]` plus order.
4. **A `select` option.** `{mode:"single"|"multi", onChange}` maps to `selectableRows`, `selectableRowsRangeMode:"click"` and `rowSelectionChanged`. This replaces `rowClass` for selection (K-Q11 Clone needs it; D-Q16).
5. **Keyboard through the J shortcuts registry:** arrow keys move the selection, Enter opens. Disable the bindings we don't use (`keybindings:{copyToClipboard:false}` and so on, per Lock).
6. **Expose a small handle**, not the Tabulator instance: `setData`, `replaceData`, `updateRow`, `select(id)`, `scrollTo(id)`, `redraw()`, `destroy()`.
7. **Drop the double load:** pass `data` only in `tableBuilt` (today it is in the constructor and `replaceData` again).
8. **Optionally swap our `ResizeObserver` for `ResizeTableModule`** (`autoResize`, default true). Either way, also redraw when a hidden panel is shown.

## Exercises for the accounts table (today)

1. **Real-window check of K-Q11.** Verify group headers (host plus URL, count), the footer total, and column order with 2 servers and an account with no URL. Check that the group arrow is absent (`groupToggleElement:false`).
2. **Single-row select.** Register `SelectRowModule`; `selectableRows:1`, `selectableRowsPersistence:false`; drive the form from `rowSelectionChanged` instead of `rowClick` plus `rowClass`. Prepares Clone (D-10).
3. **Keyboard.** Focus the table, then Up/Down moves the selection (custom `keybindings` plus `selectRow`/`scrollToRow`).
4. **Sort by Name or number within groups.** Register `SortModule` with `initialSort` (local; a handful of rows).
5. **Column widths into prefs.** Register `PersistenceModule` with `persistenceReaderFunc`/`WriterFunc` on `preferences.ui.tables.accounts`, columns `["width"]`. Confirm nothing lands in localStorage.
6. **Header filter or search box.** Use `FilterModule` with `setFilter(fn)` from the D-10 search field; check that empty groups disappear.
7. **Hidden-panel redraw.** Open Settings on another heading, add an account through the CLI or mock, switch to Accounts, and check the layout.

## Notes from the K-Q12 round (2026-10-20)

- `columnCalcs` defaults to `true`, which with `groupBy` puts calcs in **each group only**; use `"table"` for a footer row only, `"both"` for both.
- The bottom calc row is prepended into the footer element, so a page-owned `footerElement` sits below it.
- Calc cells use `bottomCalcFormatter` or plaintext, never the column's own formatter.
- `headerSortElement` as a function is called with `(column, dir)` where dir is `"asc"`, `"desc"` or `"none"`; returning a node avoids `innerHTML`.
- `selectableRows:1` toggles on click; `selectableRowsCheck` gates both clicks and `selectRow`, so the wrapper opens it only around its own calls.
- ResizeTable observes size and visibility only with the **virtual** renderer; with `"basic"` it falls back to window resize.
- `download` needs the Export module as well as Download. Hidden columns join the CSV with `download:true`; `download:false` drops a visible one; `titleDownload` and `accessorDownload` rename and map values.
- Tauri: `table.download` makes a blob and clicks an `<a download>`. Whether WebKitGTK saves it (and where) needs a window check; the dialog plugin only returns a path.
- **Tree-shaking trap:** `import("tabulator-tables")` and then `lib[name]` keeps the whole namespace (the chunk went from 166 kB to 458 kB). The modules are re-exported by name from `ui/src/common/tabulator-lib.js`, which `table.js` loads dynamically: 216 kB (53 kB gzip) with ten modules. Adding a module means adding it there too.

**D-10 (2026-10-20):** the navigator and Export left the Tabulator `footerElement` for the module footer (D-UI D-10 convention, `ui/src/common/module-bar.js`); the table keeps only its `bottomCalc` row. Search uses `FilterModule` through `createTable(...).setFilter(fn)`; empty groups disappear. `ids()` returns only shown rows.

**D-10 revision (2026-10-20):** Accounts Export no longer uses Tabulator `download` (a blob download does nothing in WebKitGTK); the page builds the CSV and the core writes it (`export.write_text`). The Export and Download modules are no longer registered by Accounts; the `download` option stays in the wrapper for later tables.

## Table look and features (2026-10-20, Andrew; Lock)

- **Group header.** The label is the server **host** only (`mail.500foods.com`); an empty or unparsable session URL is `(no server)`. The row count is a separate muted badge (`.pav-group-count`) pushed to the right, never run into the label. Built with `textContent`.
- **Expand and collapse.** `groupToggleElement:"header"` (the whole header row toggles; a larger target than the arrow) and `groupStartOpen:true`; the open state is not saved. Tabulator's own `.tabulator-group-toggle` arrow is hidden; the header draws `fa-sharp-duotone fa-solid fa-chevron-down` in `.pav-group-toggle`, rotated −90° (pointing right) while the row lacks `.tabulator-group-visible`. Chevron, not caret: the module footer navigator already uses chevrons, so the shapes match, and one icon rotated by CSS needs no re-render when Tabulator toggles.
- **Header.** The sort icon sits inline after the title (`.tabulator-col-title-holder` is a flex row; the title takes the free width, the sorter does not wrap), so the header is one line high.
- **Theme variables.** Every Tabulator colour in `theme.css` comes from `--pav-table-*`, defined in `dark.css`, `light.css`, and both starter `*.theme` files: `header-bg`, `header-fg`, `row-bg`, `row-alt-bg`, `row-hover-bg`, `selected-bg`, `selected-fg`, `group-bg`, `group-fg`, `group-count-bg`, `group-count-fg`, `border`, `footer-bg`, `footer-fg`, `sort-icon`, `group-toggle`, `indicator`, `check-size`, `divider`, `header-pad-y`, `header-pad-x`. Selected rows and group headers are deliberately stronger than the old button surfaces.
- **Column chooser (reusable).** `createColumnChooser` in `ui/src/common/column-chooser.js` opens a small popover from a toolbar button (`fa-table-columns`, title `Columns`) listing every chooser column with a checkbox; locked columns (Accounts: Name) show a disabled checked box. A change calls `table.setColumnVisible(field, on)` at once and saves. `Reset` shows the defaults. Escape or a click outside closes it. Hidden fields are stored per table in `preferences.ui.tables.<tableId>.hidden` (an array of field names; unknown fields are ignored on load; Accounts uses `accounts`). The button sits on the module toolbar right side, just before search (D-10 convention). Alternative not built: a chooser row under the header.

## Row indicator column (2026-10-20, Andrew; Lock)

A reusable option of the shared table, in the style of a classic database grid. `createTable(el, {indicator: true, ...})` puts a narrow column first, to the left of every page column (for Accounts, left of `#`).

- **Shape.** Field `__pav_indicator`, fixed width `INDICATOR_WIDTH` (22 px, exported by `ui/src/common/table.js`), `frozen: true` (FrozenColumnsModule is registered when any column is frozen), `headerSort: false`, `resizable: false`, `download: false`, no `bottomCalc` (its footer calc cell stays empty). The column is added by the wrapper, so pages never list it: it is not in any column chooser and cannot be hidden.
- **Header.** `fa-sharp-duotone fa-solid fa-ellipsis-vertical` (three vertical dots), `aria-hidden`; the column title for assistive tech is "Row indicator".
- **States.** A cell is empty unless its row has a state. The current row (the one `select(id)` chose, so it follows clicks, the navigator and Up/Down) shows `fa-caret-right`. `setRowState(id, "editing")` shows `fa-pen` on that row instead; `setRowState(id, null)` clears it. Editing wins over current. Only `current` is wired today; `editing` is the hook for later edit-in-place or dirty-form work (Accounts leaves it unset for now).
- **Why caret-right.** It is the classic grid "current record" triangle, solid and readable at 22 px; `fa-play` reads as media playback.
- **Look.** `.pav-table-indicator` cells use `--pav-table-indicator` (icon colour) on the row's own background; the header uses the header colours.
- **Width.** A page that sets its pane `min-width` from its columns adds `INDICATOR_WIDTH` when it uses the indicator.

## Header row and checkbox columns (2026-10-20, Andrew; Lock)

- **Unsortable columns.** A column that has no useful order (Accounts: the colour swatch) sets `headerSort:false`, so it shows no sort icon.
- **Title-to-sort gap.** In `.tabulator-col-title-holder` the title and the sort icon are 2 px apart (`gap` in `theme.css`), so the icon sits next to the title.
- **Widths.** Narrow columns have fixed widths, sized for the title, the 2 px gap, the sort icon and the cell padding, so nothing clips. Text titles of about four letters (JMAP, IMAP, SMTP) are 80 px. A nav-icon title is 68 px. Only the main text column grows (`widthGrow`, with a `minWidth`). The pane minimum width is the sum of the widths.
- **Checkbox cells.** A read-only checkbox column (class `pav-col-check`, which centres the title and sort icon together in the header) centres a disabled checkbox sized by `--pav-table-check-size` (in all four theme files), shows the last-checked time as a tooltip, and puts the checked count in the footer (`bottomCalc`).

## Fitted titles, resizing, dividers, striping (2026-10-20, Andrew; Lock)

- **Fitted titles.** In Andrew's real window, text headers clipped at a fixed 80 px (`J...`). A checkbox column's title no longer shrinks (`flex: none`, no ellipsis). After the table is built, `createTable` measures each fixed column that has no saved width. The measurement is the rendered title's `scrollWidth`, plus the sort icon, the 2 px gap and the column's computed horizontal padding. When the column is narrower than that, the wrapper widens it with `column.setWidth`. Measuring the live element uses the real font and zoom, which a guessed width cannot do. The coded width is also a generous fallback (protocol columns 100 px). The measurement only ever widens a column. In a test DOM (no layout) it measures 0 and changes nothing.
- **Resizable columns.** Data columns can be resized: Number, Name and the check columns. The row indicator and the swatch stay fixed (`resizable:false`). `ResizeColumnsModule` is registered when any column is resizable. On `columnResized`, the wrapper calls `onColumnResized(field, width)`. The page saves the width in `preferences.ui.tables.<tableId>.widths` as `{field: px}`, next to `hidden`. On load, saved widths replace the coded `width`. A field that is no longer a resizable column, or a value that is not a finite number between 30 and 1000, is dropped. The stretch column (Name) is not saved, so it keeps filling the rest. A resize also recomputes the pane minimum width.
- **Reset.** The column chooser has one Reset. It shows every column and restores the coded widths (`widths` cleared, each column set back with `setColumnWidth`). One button, because "make the table as shipped" is one intent. A separate "Reset widths" can be added later if it is wanted.
- **Group dividers.** A column with `cssClass` `pav-col-divider-left` or `pav-col-divider-right` draws a 1 px line in `--pav-table-divider` (in all four theme files). It runs the full height of that column: the header cell, every row cell and the footer calc cell. Accounts: right of the indicator, left of the swatch (the same line), left of JMAP, and left of Mail.
- **Zebra striping.** Tabulator marks rows `tabulator-row-even` and `tabulator-row-odd` by visible position, including inside groups, and recounts after sorting, filtering or collapsing a group. Even rows use `--pav-table-row-alt-bg`. Hover and selected rules come after the stripe and use the same specificity or higher, so they win. The stripe was already wired; at 3% it could not be seen, so it is 6% (dark) and 5% (light) from 2026-10-20. Tabulator counts group header rows in that index, so a group's first row may start on either shade.

## No horizontal scrollbar, compact header, full-height dividers (2026-10-20, Andrew; Lock)

- **No horizontal scrollbar.** Tabulator `fitColumns` takes the vertical scrollbar's width from the space it shares out. Before, the pane minimum was the column sum plus 2 px and ignored that width. Widening a column to fit its title also did not recompute the minimum. So with Name at its `minWidth`, the columns overran and a horizontal bar appeared. Now:
  - the page's minimum is the live column sum, plus the measured vertical scrollbar width (`tableholder.offsetWidth - clientWidth`), plus 2 px;
  - the wrapper calls `onWidths()` after any fit, resize, visibility change or redraw, so the page can recompute it;
  - `.tabulator-tableholder` has `overflow-x: hidden` as a backstop. At the true minimum nothing is cut, and Name absorbs the rest down to its `minWidth`.
- **Footer calc row stays on one line.** `.tabulator-footer` and its calc row are `white-space: nowrap` with `overflow: hidden`. Before, an overrunning calc row wrapped its last cell onto a second line, which was the stray count under the footer (Andrew saw a `2`).
- **Compact header.** Header cells use `--pav-table-header-pad-y` and `--pav-table-header-pad-x` (all four theme files), with `line-height: 1.2`. An icon in a header title is `font-size: 1em; line-height: 1`, so a nav icon does not make the row taller than the one-line title plus arrow.
- **Full-height dividers.** Cell borders stop at the last row. So the wrapper also paints each divider column's line as a 1 px background gradient on `.tabulator-tableholder`. Its x offset is measured from that column's header cell. That covers the empty space below the rows. Row cells keep their own border, so the line is unbroken where rows paint over the background. Offsets are recomputed on render, redraw, column resize, column visibility change and zoom (`redraw()`).
- **Zoom.** A table inside a zoomed pane is redrawn and re-measured when the zoom changes: the page calls `handle.redraw()`, which runs Tabulator `redraw(true)`, then fits titles and repaints the dividers.

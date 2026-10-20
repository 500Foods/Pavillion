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

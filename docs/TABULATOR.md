# Tabulator in Pavillion

A working reference, not a Lock. The Lock for the shared table is N-MAIL ("Shared table component"); K-Q8 and K-Q11 cover the accounts table. Read 2026-10-20.

## Status and open issues (2026-10-20)

Andrew paused the Tabulator work on 2026-10-20. This section is the current state. The dated sections further down record how it got here, and when they disagree, the later section wins.

**Reconciled 2026-10-27 against git (2c7d741..18acada):**
- **(d) partly settled:** the Mail message list (N-3, `ui/src/mail/list.js`) now builds on the shared `createTable` from `ui/src/common/table.js`, with the virtual renderer and the `.pav-table` theme variables. Contacts and the log are still undecided.
- **New guard:** `createTable` asks Tabulator `getRow` only for ids in the rows it was given (`rowPresent`, 497eb59), so a miss no longer logs a Tabulator warning.
- **(a) and (b):** no commit since 2026-10-20 changes table measurement or the header/footer row CSS, so the code does not show a fix. They stay open until Andrew confirms them fixed in the real window.
- **(c):** decided 2026-10-28. Tables and Details are the shared keys. The mail list uses `setScale` on `mailList`. Survival across a restart is still unconfirmed in the real window.

**2026-10-30.** The shared table keeps the row area the width of its holder: fixed columns keep a real width, and `widthGrow` columns take the rest. See "Columns fill the pane". Open issue (a) stays open until the window shows that a split drag no longer leaves a gap or a horizontal bar.

**Works (seen in Andrew's real window unless noted):**
- **Grouping:** rows grouped by host (`(no server)` for an empty or bad URL). The whole group header toggles, with a chevron; groups start open and the open state is not saved.
- **Sorting:** tri-state sort on every column except the swatch, with Font Awesome arrows pinned to the right edge of the header cell.
- **Footer counts:** the account count under `#`, and checked counts under JMAP, IMAP, SMTP, Mail, Contacts, Calendars and Files.
- **Module footer:** Refresh, the navigator (First, Previous, Next, Last, Up and Down) and Export. Export writes CSV through the core's `export.write_text` (API.md).
- **Row indicator:** a caret on the current row. The `setRowState(id, "editing")` hook exists, but nothing calls it yet.
- **Column chooser:** opened from the row indicator header (the three dots; 2026-10-27 the Accounts toolbar Columns button was removed), with Name locked. One Reset restores both the shown columns and the original widths.
- **Column resizing:** user resizing works (Andrew confirmed it). Widths are saved at 100% zoom in `preferences.ui.tables.<id>.widths` and hidden columns in `.hidden`.
- **Zoom:** Tables and Details are shared rows (2026-10-28). Accounts, Icons, Libraries, and Assets scale the table in place. Accounts, Libraries, and Assets forms use CSS zoom. The Icons editor uses the Icon Wizard zoom. The mail list uses `setScale` on Mail list. The list pane does not get CSS zoom.
- **Dividers:** full-height divider lines: cell borders plus a background line on the rows area.
- **Striping:** zebra striping, with hover and selected rows on top.
- **Theme variables:** every table colour and size is a `--pav-table-*` variable, in all four theme files.

**Open issues:**
- **(a) Wrong width:** the table is sometimes drawn at a width that differs from its container: rows or the header end short of, or past, the pane edge. The redraw triggers (a hidden pane being shown, a split drag, zoom, the font scale) or the measurement order need work. Cause not yet found. Workaround: the footer Refresh calls `refit()` (see "Refresh refits the table").
- **(b) Vertical alignment:** the vertical alignment of the header and footer rows is still not right.
- **(c) Remembering zoom and widths:**
  - Widths are saved per table (`preferences.ui.tables.<id>.widths`).
  - The zoom is shared. `preferences.ui.zoom.tables` is the left-hand table on Accounts, Icons, Libraries, and Assets. `preferences.ui.zoom.details` is the form on Accounts, Libraries, and Assets. The mail list keeps `mailList`. A saved `accountsTable` is copied to `tables` once, and `accountsForm` to `details`.
  - It is still unconfirmed in the real window that the zoom and the widths survive a restart.
- **(d) Other tables:** the mail list, Icons, Libraries, and Assets use `createTable`. Contacts is still undecided. The log stays CodeMirror. Empty homes are not tables yet.

**Lessons learned:**
- **Imports:** import the core plus named modules through `ui/src/common/tabulator-lib.js` (FormatModule, SortModule, GroupRowsModule, ColumnCalcsModule, SelectRowModule, FilterModule, FrozenColumnsModule, ResizeColumnsModule, MoveColumnsModule, Export/Download). Never import TabulatorFull. Register a module only when a column or option needs it. From 2026-10-30, MoveColumns and ResizeColumns are the exception: every table registers them, and `movable: false` is what leaves MoveColumns out.
- **Never CSS-zoom a Tabulator.** In WebKit, client rects are scaled but offset sizes are not, so the widths, heights, footer and fit go wrong. Scale the table's font size and its fixed column widths (`setScale`) instead, and give all table padding in `em`.
- **Store widths at 100%,** that is, the pixel width divided by the scale. Scale them when applying, and do not scale the stretch column (Name).
- **Resize handles** (`.tabulator-col-resize-handle`) are siblings placed after each header and row cell. Style them `position: relative; inline-block` with negative margins. Positioning them absolutely stacks them all at the right edge, and resizing silently stops working.
- **Striping:** Tabulator's even/odd classes count group header rows too, so a group may start on either shade. Hover and selected rules must come after the stripe rule.
- **Minimum width:** a pane's minimum width must include the vertical scrollbar width (`offsetWidth - clientWidth` of the rows area), because `fitColumns` subtracts it. Recompute the minimum after fits and resizes, but not on zoom.
- **Footer:** keep the calc row and footer `white-space: nowrap`. A wrapped calc row shows up as stray numbers under the footer.
- **Redraw** (`redraw(true)`, then re-fit the titles and repaint the dividers) after the table becomes visible, after a zoom change, and after a column resize or visibility change. Tabulator measures only what is laid out, and a hidden pane measures 0.
- **Titles:** fit titles by measuring the rendered title, plus the arrow, the gap and the padding. Only ever widen. Content fit, from 2026-10-30, may narrow a column once to the header and the rendered cells; after that it only widens. See "Columns fill the pane".
- **Calc cells:** use the column's `cssClass` and `hozAlign`, so alignment classes reach the footer too.
- **Dividers:** cell borders end at the last row. A full-height line needs a background (or overlay) on `.tabulator-tableholder` at the measured column offsets.
- **Data flow:** pass data once (constructor, then `replaceData`). After an add, the page must reload and select the new row itself (K-Q13: Save with no selection silently did nothing).

**Doc links.** `https://tabulator.info/docs/6.6/...` URLs fall back to the home page. The real pages are under `https://tabulator.info/docs/6.x/<page>/` (below: `D/<page>`). That site still says "Latest version 6.5.0". The 6.6.0 and 6.6.1 notes are only on GitHub: https://github.com/tabulator-tables/tabulator/releases.

## Header tooltips (2026-10-20, Andrew; Lock)

Column headers use the shared tooltip (D-UI.md Tooltips), never a native `title`. `columnDefaults` does not set `headerTooltip` (2026-10-27): that option belongs to Tabulator's TooltipModule, which is not registered, so Tabulator 6.6.1 logged `Invalid column definition option: headerTooltip` as a ui WARN. Without the module Tabulator adds no native header tooltip, and `tipHeaders` still strips any `title`. `createTable` runs `tipHeaders()` at the start of every relayout (tableBuilt, renderComplete, sort, visibility change from the column chooser, resize, `setScale`/`refit`): for each column it removes any `title` on the header cell and its children and sets `data-pav-tip` on the whole `.tabulator-col` from the definition `title`, so hovering the sort icon or padding also shows it. Tabulator keeps the column element across redraws, so this is idempotent and touches no listeners: sort click, resize handles and drag are untouched. Icon-only headers need a real `title` and draw the icon with `titleFormatter`: the account icon column is `title: "Account icon"` with an empty `titleFormatter`; Mail, Contacts, Calendars and Files keep their names as `title`. A column whose `title` is empty gets no tip.

## Refresh refits the table (2026-10-20, Andrew; Lock)

- **`handle.refit()`** is the reusable "make it right again" call for any table. It runs Tabulator `redraw(true)`, which re-measures the container width and re-lays the columns. It then re-applies the fixed column widths (the base widths at 100%, times the current width scale: saved widths, coded widths, or widths widened to fit their titles), fits the titles again, repaints the full-height dividers, and calls `onWidths` so the page recomputes the pane minimum. It also re-selects the current row, so the selection and the indicator stay on the same row if that row still exists. `redraw()` does the same without re-applying the widths.
- **Module footer Refresh** on a table view: reload the data (`setData`, unchanged), then `refit()` once the new rows are in. Accounts: `refresh()` reloads `account.list`, keeps `selectedId` when that account still exists, then calls `table.refit()`. Every table should wire its footer Refresh the same way. This is the manual fix for open issue (a), the table drawn at the wrong width, until its cause is found.

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
| Load while scrolling | Not used for the mail list (N-3a, Andrew, 2026-10-22). One `message.list` reply, at most `listRows`, is passed as `data`. Pagination turns the virtual DOM off. `progressiveLoad:"scroll"` remains available for a later list that needs it. | D/data (Progressive Ajax Loading), D/page |
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
- **Column chooser (reusable).** `createColumnChooser` in `ui/src/common/column-chooser.js` opens a small popover from a toolbar button (`fa-table-columns`, title `Columns`) listing every chooser column with a checkbox; locked columns (Accounts: Name) show a disabled checked box. A change calls `table.setColumnVisible(field, on)` at once and saves. `Reset` shows the defaults. Escape or a click outside closes it. Hidden fields are stored per table in `preferences.ui.tables.<tableId>.hidden` (an array of field names; unknown fields are ignored on load; Accounts uses `accounts`). The button sits on the module toolbar right side, just before search (D-10 convention). Alternative not built: a chooser row under the header. (2026-10-27: a toolbar button is optional; Accounts now uses only the row indicator header. The default placement is still right-aligned under the anchor; `placement: "below-right"` drops it down and to the right.)

## Row indicator column (2026-10-20, Andrew; Lock)

A reusable option of the shared table, in the style of a classic database grid. `createTable(el, {indicator: true, ...})` puts a narrow column first, to the left of every page column (for Accounts, left of `#`).

- **Shape.** Field `__pav_indicator`, fixed width `INDICATOR_WIDTH` (40 px, exported by `ui/src/common/table.js`), `frozen: true` (FrozenColumnsModule is registered when any column is frozen), `headerSort: false`, `resizable: false`, `download: false`, no `bottomCalc` (its footer calc cell stays empty). It is not a drag source: `MoveColumnsModule` skips a frozen column, and the column definition does not carry a `movable` key (Tabulator 6.6 has no such column option, and an unknown key logs an invalid-option warning). The column is added by the wrapper, so pages never list it: it is not in any column chooser and cannot be hidden.
- **Header.** `fa-sharp-duotone fa-solid fa-ellipsis-vertical` (three vertical dots), `aria-hidden`; the column title for assistive tech is "Row indicator". (2026-10-27, Andrew) When the page passes `onIndicatorHeader(anchor)` to `createTable`, the dots are drawn inside a `<button type="button" class="pav-table-indicator-button" aria-label="Choose columns" aria-haspopup="dialog">`, the column title (and so its tooltip) becomes "Choose columns", and a click (or Enter/Space) calls `onIndicatorHeader(button)` without sorting or selecting. The page opens the same `openColumnChooser` popover as its toolbar Columns button, anchored on that button. Accounts does this, and (2026-10-27, Andrew) its toolbar Columns button is gone: the dots are the only way in. Without the option the header stays a plain icon. The popover opens with `placement: "below-right"`: its top-left corner at the dots' bottom-left, so it drops down and to the right (clamped inside the window). It fades and slides in over 120 ms (`@starting-style`, opacity and a 4 px `translateY`); close is immediate. Clicking the dots again while it is open closes it (toggle): `openColumnChooser` on the anchor that already owns the open popover closes it and returns null.
- **States.** A cell is empty unless its row has a state. The current row (the one `select(id)` chose, so it follows clicks, the navigator and Up/Down) shows `fa-caret-right`. `setRowState(id, "editing")` shows `fa-pen` on that row instead; `setRowState(id, null)` clears it. Editing wins over current. Only `current` is wired today; `editing` is the hook for later edit-in-place or dirty-form work (Accounts leaves it unset for now).
- **Why caret-right.** It is the classic grid "current record" triangle, solid and readable at 40 px; `fa-play` reads as media playback.
- **Look.** `.pav-table-indicator` cells use `--pav-table-indicator` (icon colour) on the row's own background; the header uses the header colours. The header cell has no padding. The chooser button fills that cell (the full 40 px, and at least the header row tall).
- **Width.** A page that sets its pane `min-width` from its columns adds `INDICATOR_WIDTH` when it uses the indicator. (2026-10-28, Andrew) The width moved from 22 px to 40 px because the chooser button was too small to click. Every table that uses the indicator, including Accounts, gets the wider gutter.

## Header row and checkbox columns (2026-10-20, Andrew; Lock)

- **Unsortable columns.** A column that has no useful order (Accounts: the colour swatch) sets `headerSort:false`, so it shows no sort icon.
- **Title-to-sort gap.** In `.tabulator-col-title-holder` the title and the sort icon are 2 px apart (`gap` in `theme.css`), so the icon sits next to the title.
- **Widths.** Narrow columns have fixed widths, sized for the title, the 2 px gap, the sort icon and the cell padding, so nothing clips. Text titles of about four letters (JMAP, IMAP, SMTP) are 80 px. A nav-icon title is 68 px. Only the main text column grows (`widthGrow`, with a `minWidth`). The pane minimum width is the sum of the widths.
- **Checkbox cells.** A read-only checkbox column (class `pav-col-check`, which centres the title and sort icon together in the header) centres a disabled checkbox sized by `--pav-table-check-size` (in all four theme files), shows the last-checked time as a tooltip, and puts the checked count in the footer (`bottomCalc`).

## Fitted titles, resizing, dividers, striping (2026-10-20, Andrew; Lock)

- **Fitted titles.** In Andrew's real window, text headers clipped at a fixed 80 px (`J...`). A checkbox column's title no longer shrinks (`flex: none`, no ellipsis). After the table is built, `createTable` measures each fixed column that has no saved width. The measurement is the rendered title's `scrollWidth`, plus the sort icon, the 2 px gap and the column's computed horizontal padding. When the column is narrower than that, the wrapper widens it with `column.setWidth`. Measuring the live element uses the real font and zoom, which a guessed width cannot do. The coded width is also a generous fallback (protocol columns 100 px). The measurement only ever widens a column. In a test DOM (no layout) it measures 0 and changes nothing.
- **Resizable columns.** Data columns can be resized: Number, Name and the check columns. The row indicator and the swatch stay fixed (`resizable:false`). `ResizeColumnsModule` is registered when any column is resizable. On `columnResized`, the wrapper calls `onColumnResized(field, width)`. The page saves the width in `preferences.ui.tables.<tableId>.widths` as `{field: px}`, next to `hidden`. On load, saved widths replace the coded `width`. A field that is no longer a resizable column, or a value that is not a finite number between 30 and 1000, is dropped. The stretch column (Name) is not saved, so it keeps filling the rest. A resize also recomputes the pane minimum width.
- **Reset.** The column chooser has one Reset. It shows every column and restores the coded widths (`widths` cleared, each column set back with `setColumnWidth`). One button, because "make the table as shipped" is one intent. A separate "Reset widths" can be added later if it is wanted. On Icons, Libraries, and Assets, Reset also clears `order` and moves the columns back to the shipped order.

## Movable columns (2026-10-28, Andrew; Lock)

Icons, Libraries, and Assets (`ui/src/settings/split-table.js`) pass `movable: true` to `createTable`. That sets Tabulator `movableColumns: true` and registers `MoveColumnsModule` after `FrozenColumnsModule`, so the frozen indicator is already marked frozen when move starts. Every data column on those tables is `resizable: true`. A drag saves the field order in `preferences.ui.tables.<id>.order` (an array of field names; unknown fields are dropped; a missing field is appended in the shipped order). Fixed columns that are resized are saved in `widths`: Libraries On, Group, and Style; Icons Default and the icon; Assets Kind and Size. A `widthGrow` column (Name, Key, URL, Id) can be resized for the session and is not saved, so it keeps filling the row. Accounts is unchanged aside from the wider indicator: its swatch stays `resizable: false`, and Accounts does not move columns. There is no column `movable` flag.
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

## Zoom in place, footer count, alignment, resize handles (2026-10-20, Andrew; Lock)

This replaces the CSS-zoom parts of the previous section.

- **Zoom by font and scaled widths, not CSS zoom.** CSS `zoom` on the table broke Tabulator's measurements in WebKit. Rows and group headers ended about 90 px short of the header, and the footer calc row was clipped. Tabulator reads client rects (scaled) and offset sizes (not scaled), and the two disagree under zoom. So a table does not get CSS `zoom`. Instead:
  - `handle.setScale(widthScale, fontScale)` sets the table element's `font-size` to `fontScale × 100%`, sets every fixed column to its base width × `widthScale`, then redraws and re-measures;
  - every Tabulator length in `theme.css` (cell and header padding) is in `em`, so it follows the font;
  - the base widths are the coded widths, or the saved ones, at 100%;
  - Name absorbs the difference. Only when the fixed columns no longer fit at a high zoom can the rows area scroll horizontally (`overflow-x: auto` again);
  - zoom never changes the pane: its minimum is computed from base widths at 100% and does not move with zoom.
  - **Two scales:** the shell zoom already reaches the font through the root `rem`, but px widths do not follow it. Width scale = Shell × the table zoom. Font scale = the table zoom. From 2026-10-28 that table zoom is Tables for the settings splits and Mail list for the message list. See Shared zooms.
  - **Saved widths:** saved and reported widths are base widths (px ÷ scale).
  - **The form:** a form with no table keeps CSS `zoom`. From 2026-10-28 that key is Details, and it covers the Accounts, Libraries, and Assets forms. The Icons editor is the Icon Wizard zoom.
- **Full width.** With measurements consistent again, `fitColumns` fills the holder: header, group headers, rows and the calc row all end at the right edge.
- **Footer count.** Accounts shows the account count as a plain number under `#`. Name has no calc. The footer no longer has `overflow: hidden`. The calc row stays on one line with `nowrap`, and its height is the content height, so nothing is cut off.
- **Alignment.** Cells and calc cells of `#` and the checkbox columns are centred, using the classes `pav-col-center` and `pav-col-check` with `justify-content: center`. Header titles are left-aligned. Header cells, calc cells and cells centre their content vertically (`align-items: center`). The header uses one line height.
- **Sort arrow pinned right.** In every header the title takes the remaining width (`flex: 1`, ellipsis only when truly needed), and the arrow sits at the right edge (`margin-left: auto`, `flex: none`). It never overlaps the title. The fitted-titles pass still widens a column whose title would clip at its default width.
- **Resize handles.** Tabulator inserts each `.tabulator-col-resize-handle` as a sibling after its header cell or row cell, not inside it. The old CSS positioned the handles `absolute; right: 0`, which stacked every handle at the far right of the header, so nothing could be dragged. The handle is now what Tabulator expects: `position: relative; display: inline-block; width: 6px; margin: 0 -3px; z-index: 11; vertical-align: middle` (`3px` and no right margin for the last one), with the `col-resize` cursor. `ResizeColumnsModule` is exported by `tabulator-lib.js` and registered when any column sets `resizable: true`.

## Shared zooms (2026-10-28, Andrew; Lock)

This extends Zoom in place. A table still uses `setScale` and never CSS `zoom`.

- **Tables** (`preferences.ui.zoom.tables`, 5%–500%). Width scale = Shell × Tables. Font scale = Tables. The Accounts, Icons, Libraries, and Assets tables. The pane minimum stays at the 100% widths.
- **Details** (`preferences.ui.zoom.details`, 5%–500%, CSS `zoom`, multiplied by Shell). The Accounts form, the Libraries form, and the Assets form. The Icons right pane is not in this zoom.
- **Mail list** keeps `mailList` and uses the same `setScale` pair (width = Shell × Mail list, font = Mail list). The list pane does not get CSS `zoom`. Mail detail stays CSS `zoom`.
- **Settings** (`preferences.ui.zoom.settings`) is CSS `zoom` on the body of the settings pages that are not those splits, multiplied by Shell. Button bars stay on Button bars.
- **Color picker** and **Icon Wizard** are `preferences.ui.colorPickerZoom` and `preferences.ui.iconWizardZoom`, beside `zoom`. 100%–300% by 5%. Shell does not multiply them. One value follows that control everywhere.
- A profile that still has `accountsTable` or `accountsForm` copies that value into `tables` or `details` when the new key is missing, then drops the old key.

## Download options without the Download module (2026-10-27, Lock)

Tabulator 6.6.1 warns `Invalid column definition option: <name>` for a column option whose module is not registered. `download`, `titleDownload` and `accessorDownload` belong to the Export and Download modules, which `createTable` registers only when the page passes `download: true`. Accounts does not: its Export builds the CSV itself (`accountsCsv` with `EXPORT_FIELDS`) and writes it through `export.write_text`. Page column definitions may keep those keys (Accounts' `exportFields` reads `download`). When `options.download` is off, `tabulatorOptions` strips them from the copies it hands Tabulator, including the row indicator's `download: false`. The same rule applies to any later module-owned option: never pass Tabulator an option whose module is not registered. `lockWidth` is one of those: the wrapper reads it, then deletes it from the copy Tabulator sees.

## Columns fill the pane (2026-10-30)

This is the default for every `createTable`. It replaces the parts of the sections above that say a table does not move columns, that only a title fit changes a width, and that a resize is saved for every resizable column including the stretch column.

- **Fill.** `layout` is `fitColumns`, `height` is `100%`, `autoResize` is on, and rows stay virtual. ResizeTable already redraws when the pane or a split changes size. The wrapper then gives the spare width to the `widthGrow` columns, so the header and the rows stay the width of `.tabulator-tableholder` (`clientWidth`, which already excludes a vertical scrollbar).
- **Who grows.** A column with `widthGrow` shares that spare space by its weight and is not given a fixed `width`. `fitColumns` treats a missing `widthGrow` as 1, and `widthGrow: 0` is the same, so every other column is given a width. A table that names no `widthGrow` column grows its last visible column that is not frozen and not `resizable: false`, with `minWidth` 48 when it had none.
- **Who stays fixed.** Those columns size to the header and the cells on screen, down to `minWidth` (the stated one, or 40). The measurement is the same one fitted titles use, plus the widest rendered cell. A measure of 0 changes nothing, so a hidden pane and a test DOM stay put. The first real measure may shrink a coded width. Later measures only widen, so scrolling a virtual list does not jitter. `lockWidth` (a saved width) and a width set in this session are left as they are.
- **Resize.** Dragging a fixed column keeps that width (`definition.width` is updated, because `fitColumns` counts the definition, not the live width) and gives the difference to the `widthGrow` columns. Dragging one of several grow columns keeps the dragged width until the pane width or the fixed columns change; the other grow columns share what remains. Dragging the only grow column moves the nearest resizable fixed column (the next one, otherwise the previous) and then the grow column is set back to whatever still fills the pane. `resizableColumnFit` is not used: it always moves the neighbour, which is not the designated column.
- **Scroll.** When the minimums no longer fit, grow columns stay at `minWidth` and the holder may scroll. `overflow-x` stays `auto`.
- **Handles and moving.** `columnDefaults.resizable` is `"header"`. An explicit `resizable: true` is stored as `"header"`. `resizable: false` stays, including the row indicator and the account and mail swatches. Columns move by default. `movable: false` opts out. MoveColumns is registered after FrozenColumns, so the indicator stays frozen. There is still no per-column `movable` flag in Tabulator 6.6.
- **What is saved.** Accounts and the mail list save order the same way the icon tables do: `preferences.ui.tables.accounts.order` and `preferences.ui.tables.mail.order`. Mail saves `receivedAt`, `flags`, and `hasAttachment`. A saved `from` or `to` slot follows the address column (From, or To in Sent and Drafts). Reset clears the order as well as the widths and the hidden columns.
- **Remembered (2026-10-30).** A drag is kept across a restart. `preferences.ui.iconPane`, `libraryPane`, and `assetPane` are restored on launch, the same way the Accounts split already was. When a table has more than one `widthGrow` column, the drag saves every one of those live widths (whole pixels from 30 to 4000) and the next open uses them as the weights, so the share comes back and the row still fills the pane. Icons saves Name and Key. Libraries saves Name and the address. Mail saves From, Subject, and the preview that way; the address column is stored as `from`. A table with one `widthGrow` column does not save that column. Accounts Name and the asset id stay the slack, and the fixed column beside them is what is saved, because dragging the slack column moves that neighbour. Reset clears the weights as well as the fixed widths. The selected row and the list scroll are kept as well. `preferences.ui.tables.<id>.selected` is the row id and `.scroll` is `{ top, left }` in pixels. Mail stores one `tables.mail.place`, `{ mailbox, selected, top, left }`, and restores it when that folder opens. A column Reset does not clear the selected row or the scroll.
- **Position (2026-10-30).** A full redraw sets the holder to the top, including the redraw when a hidden pane is shown. The wrapper writes the saved `{ top, left }` again after those redraws, and tells the virtual renderer to move its window there, until the user wheels over the rows, presses the rows, or moves the selection. A scroll event from the redraw is not stored over the saved offset. Up and Down move one row. Page Up and Page Down move by the number of rows that fit in the holder. Home and End move to the first and last row. The list follows that row.
- **Which column grows.** Mail: From weight 2 (`minWidth` 96), Subject weight 3 (`minWidth` 120), preview weight 2 (`minWidth` 80). Accounts: Name, `minWidth` 120. Icons: Name and Key. Libraries: Name and the address. Assets: the id.

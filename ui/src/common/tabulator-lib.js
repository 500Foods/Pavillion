/* The Tabulator core plus the modules the wrapper may register, imported by
   name so Vite tree-shakes the rest (TABULATOR.md). table.js loads this file
   with a dynamic import, so it stays in its own chunk. Never TabulatorFull. */
export {
  Tabulator,
  FormatModule,
  InteractionModule,
  ResizeTableModule,
  GroupRowsModule,
  SortModule,
  ColumnCalcsModule,
  SelectRowModule,
  ExportModule,
  DownloadModule,
  FilterModule,
  FrozenColumnsModule,
  ResizeColumnsModule,
  MoveColumnsModule,
} from "tabulator-tables";

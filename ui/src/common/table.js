/* The shared table (N Lock, Shared table component): the only module that
   imports Tabulator, loaded on first use as its own chunk. Screens call
   createTable(el, {columns, data, rowId, onRowClick, rowClass}); the look is
   .pav-table on --pav-* variables in theme.css, and no Tabulator stylesheet
   is loaded. Formatters build DOM with textContent. */
let loading = null;

function loadTabulator() {
  if (!loading) {
    loading = import("tabulator-tables").then(({ Tabulator, FormatModule, InteractionModule }) => {
      Tabulator.registerModule([FormatModule, InteractionModule]);
      return Tabulator;
    });
  }
  return loading;
}

/* rowClass(data) names one pav-row-* class (or none) for the row. */
function markRow(row, rowClass) {
  const el = row.getElement();
  for (const name of [...el.classList]) {
    if (name.startsWith("pav-row-")) {
      el.classList.remove(name);
    }
  }
  const cls = rowClass(row.getData());
  if (cls) {
    el.classList.add(cls);
  }
}

export function createTable(el, options) {
  el.classList.add("pav-table");
  let table = null;
  let built = false;
  let destroyed = false;
  let observer = null;
  let data = options.data || [];

  void loadTabulator().then((Tabulator) => {
    if (destroyed) {
      return;
    }
    table = new Tabulator(el, {
      data,
      columns: options.columns || [],
      index: options.rowId || "id",
      layout: "fitColumns",
      renderVertical: "basic",
      placeholder: options.placeholder || "",
      ...(typeof options.rowClass === "function" ? { rowFormatter: (row) => markRow(row, options.rowClass) } : {}),
    });
    table.on("tableBuilt", () => {
      built = true;
      void table.replaceData(data);
    });
    /* N Lock: shared tables fit their panel; redraw when the panel or a
       divider changes the element's size. */
    if (typeof ResizeObserver === "function") {
      observer = new ResizeObserver(() => {
        if (built && table) {
          table.redraw();
        }
      });
      observer.observe(el);
    }
    if (typeof options.onRowClick === "function") {
      table.on("rowClick", (_event, row) => options.onRowClick(row.getData()));
    }
  });

  return {
    setData(next) {
      data = next;
      if (built) {
        void table.replaceData(next);
      }
    },
    destroy() {
      destroyed = true;
      if (observer) {
        observer.disconnect();
        observer = null;
      }
      if (table) {
        table.destroy();
        table = null;
      }
      el.classList.remove("pav-table");
    },
  };
}

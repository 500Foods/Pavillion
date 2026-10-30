/* Settings → Assets. Icon assets in an Accounts-style table. */
import { iconClass } from "../common/icons/registry.js";
import { ensureSplit, mountModuleTable } from "./split-table.js";

const CHOOSER = [
  { field: "kindLabel", title: "Kind" },
  { field: "bytes", title: "Size", locked: true },
  { field: "id", title: "Id", locked: true },
];

const WIDTH_FIELDS = ["kindLabel", "bytes"];

const KIND_LABELS = {
  svg: "SVG",
  "png-render": "PNG",
  "png-original": "PNG original",
  fetch: "Fetched",
};

function kindLabel(kind) {
  return KIND_LABELS[kind] || kind || "Asset";
}

export function mountAssetsPanel(root, ctx) {
  const doc = root.ownerDocument;
  ensureSplit(root, {
    toolbar: "assets-toolbar",
    footer: "assets-footer",
    tree: "assets-tree",
    pane: "assets-tree-pane",
    form: "assets-form",
    split: "assetPane",
    splitLabel: "Resize asset panes",
  });
  let assets = [];
  let warn = false;
  let failed = "";
  let selectedId = null;
  let view = null;

  function rows() {
    return assets.map((asset) => ({
      id: asset.id,
      kind: asset.kind || "",
      kindLabel: kindLabel(asset.kind),
      bytes: Number.isInteger(asset.bytes) ? asset.bytes : 0,
      set: kindLabel(asset.kind),
    }));
  }

  function selected() {
    return assets.find((asset) => asset.id === selectedId) || null;
  }

  function paintForm() {
    const pane = root.querySelector("#assets-form");
    if (!pane) {
      return;
    }
    pane.replaceChildren();
    if (failed) {
      const note = doc.createElement("p");
      note.className = "pav-icon-note";
      note.textContent = failed;
      pane.append(note);
      return;
    }
    const asset = selected();
    if (!asset) {
      const empty = doc.createElement("p");
      empty.className = "settings-empty";
      empty.textContent = warn ? "Icon assets are over 128 MB." : "Select an asset.";
      pane.append(empty);
      return;
    }
    const box = doc.createElement("div");
    box.className = "settings-form";
    const title = doc.createElement("h2");
    title.className = "settings-form-title";
    title.textContent = kindLabel(asset.kind);
    const preview = doc.createElement("img");
    preview.className = "settings-asset-preview";
    preview.alt = "";
    preview.src = `pav-asset://${asset.id}`;
    const meta = doc.createElement("p");
    meta.textContent = `${asset.bytes} bytes`;
    const ident = doc.createElement("p");
    ident.className = "muted";
    ident.textContent = asset.id;
    const del = doc.createElement("button");
    del.type = "button";
    del.textContent = "Delete";
    del.addEventListener("click", () => {
      void removeSelected();
    });
    box.append(title, preview, meta, ident);
    if (warn) {
      const over = doc.createElement("p");
      over.className = "pav-icon-note";
      over.textContent = "Icon assets are over 128 MB.";
      box.append(over);
    }
    const actions = doc.createElement("div");
    actions.className = "settings-form-actions";
    actions.append(del);
    box.append(actions);
    pane.append(box);
  }

  async function refresh() {
    if (!ctx.api || !ctx.api.asset || typeof ctx.api.asset.list !== "function") {
      assets = [];
      failed = "";
      if (view) {
        view.paint();
      }
      paintForm();
      return;
    }
    try {
      const listed = await ctx.api.asset.list();
      assets = listed && Array.isArray(listed.assets) ? listed.assets : [];
      warn = Boolean(listed && listed.warn);
      failed = "";
      if (selectedId && !assets.some((asset) => asset.id === selectedId)) {
        selectedId = null;
      }
      if (!selectedId && typeof ctx.tableSelected === "function") {
        const savedId = ctx.tableSelected("assets");
        if (savedId && assets.some((asset) => asset.id === savedId)) {
          selectedId = savedId;
        }
      }
    } catch (err) {
      assets = [];
      failed = err instanceof Error ? err.message : "Could not list icon assets.";
    }
    if (view) {
      view.paint();
    }
    paintForm();
  }

  async function removeSelected() {
    const asset = selected();
    if (!asset || !ctx.api || !ctx.api.asset || typeof ctx.api.asset.delete !== "function") {
      return;
    }
    const ask = typeof ctx.confirm === "function" ? ctx.confirm : () => Promise.resolve(true);
    const ok = await ask({
      title: "Delete asset?",
      text: "This stored icon file is deleted.",
      okLabel: "Delete",
      danger: true,
    });
    if (!ok) {
      return;
    }
    await ctx.api.asset.delete({ id: asset.id });
    selectedId = null;
    await refresh();
  }

  function columns(documentRef, hidden, widths) {
    const off = new Set(hidden || []);
    const saved = widths || {};
    const defs = [
      {
        title: "Kind",
        field: "kindLabel",
        width: saved.kindLabel || 110,
        ...(saved.kindLabel ? { lockWidth: true } : {}),
        minWidth: 80,
        resizable: true,
        formatter: (cell) => {
          const span = documentRef.createElement("span");
          span.textContent = cell.getValue();
          return span;
        },
      },
      {
        title: "Size",
        field: "bytes",
        width: saved.bytes || 88,
        ...(saved.bytes ? { lockWidth: true } : {}),
        minWidth: 72,
        resizable: true,
        hozAlign: "right",
        formatter: (cell) => {
          const span = documentRef.createElement("span");
          const n = cell.getValue();
          span.textContent = Number.isFinite(n) ? `${n} bytes` : "";
          return span;
        },
      },
      {
        title: "Id",
        field: "id",
        minWidth: 120,
        widthGrow: 1,
        resizable: true,
        formatter: (cell) => {
          const span = documentRef.createElement("span");
          span.textContent = cell.getValue();
          return span;
        },
      },
    ];
    for (const column of defs) {
      if (off.has(column.field)) {
        column.visible = false;
      }
    }
    return defs;
  }

  view = mountModuleTable(root, {
    ids: {
      toolbar: "assets-toolbar",
      footer: "assets-footer",
      tree: "assets-tree",
      pane: "assets-tree-pane",
    },
    tableId: "assets",
    title: "Assets",
    noun: "asset",
    searchId: "assets-search",
    searchLabel: "Search assets",
    empty: "No icon assets.",
    chooserLabel: "Asset columns",
    chooser: CHOOSER,
    widthFields: WIDTH_FIELDS,
    columns,
    groupBy: "set",
    groupOrder: (data) => {
      const order = [];
      for (const row of data) {
        if (!order.includes(row.set)) {
          order.push(row.set);
        }
      }
      return order;
    },
    getRows: rows,
    match: (row, needle) => {
      const q = needle.toLowerCase();
      return row.id.toLowerCase().includes(q) || row.kindLabel.toLowerCase().includes(q);
    },
    getSelected: () => selectedId,
    onSelect: (id) => {
      selectedId = id;
      if (typeof ctx.saveTableSelected === "function") {
        ctx.saveTableSelected("assets", id);
      }
      if (view) {
        view.reselect();
      }
      paintForm();
    },
    actions: [
      {
        id: "asset-delete",
        action: "delete",
        icon: iconClass("accounts.delete"),
        text: "Delete",
        label: "Delete the selected asset",
        onClick: () => void removeSelected(),
      },
    ],
    onRefresh: () => {
      void refresh();
    },
    onPaintNav({ toolbar }) {
      const button = toolbar && toolbar.querySelector("#asset-delete");
      if (button) {
        button.disabled = !selected();
      }
    },
    tableHidden: ctx.tableHidden,
    saveTableHidden: ctx.saveTableHidden,
    tableWidths: ctx.tableWidths,
    saveTableWidths: ctx.saveTableWidths,
    tableOrder: ctx.tableOrder,
    saveTableOrder: ctx.saveTableOrder,
    tableSelected: ctx.tableSelected,
    saveTableSelected: ctx.saveTableSelected,
    tableScroll: ctx.tableScroll,
    saveTableScroll: ctx.saveTableScroll,
  });

  return {
    refresh,
    paintForm,
    setZoom(width, font) {
      if (view && typeof view.setZoom === "function") {
        view.setZoom(width, font);
      }
    },
  };
}

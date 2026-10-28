/* Settings → Libraries. Shipped families, grouped, with Add and Remove.
   A change to the address or enabled reloads the window after the save. */
import { iconClass } from "../common/icons/registry.js";
import {
  LIBRARY_CATALOG,
  catalogById,
  fontAwesomeKitId,
  librariesError,
  libraryGroupId,
  libraryGroupLabel,
  libraryStyleId,
  libraryStyleLabel,
  libraryUrl,
} from "../common/iconhosts.js";
import { ensureSplit, mountModuleTable } from "./split-table.js";

const CHOOSER = [
  { field: "on", title: "On" },
  { field: "name", title: "Name", locked: true },
  { field: "set", title: "Group" },
  { field: "styleLabel", title: "Style" },
  { field: "url", title: "URL" },
];

const WIDTH_FIELDS = ["on", "set", "styleLabel"];

function libraryId(name, list) {
  let base = String(name || "").trim().toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "");
  if (!/^[a-z]/.test(base)) {
    base = `lib${base ? `-${base}` : ""}`;
  }
  base = base.slice(0, 32).replace(/-$/g, "") || "library";
  let id = base;
  let n = 2;
  const taken = new Set((list || []).map((entry) => entry.id));
  while (taken.has(id)) {
    const suffix = `-${n}`;
    id = `${base.slice(0, 32 - suffix.length)}${suffix}`;
    n += 1;
  }
  return id;
}

function loadSignature(list) {
  return JSON.stringify((list || []).map((entry) => [entry.id, entry.type, entry.url, entry.enabled === true]));
}

export function mountLibrariesPanel(root, ctx) {
  const doc = root.ownerDocument;
  ensureSplit(root, {
    toolbar: "libraries-toolbar",
    footer: "libraries-footer",
    tree: "libraries-tree",
    pane: "libraries-tree-pane",
    form: "libraries-form",
    split: "libraryPane",
    splitLabel: "Resize library panes",
  });
  const ui = () => ctx.ui;
  let selectedId = null;
  let drafting = false;
  let view = null;

  const form = doc.createElement("div");
  form.className = "settings-form";
  const title = doc.createElement("h2");
  title.className = "settings-form-title";
  const nameInput = doc.createElement("input");
  nameInput.type = "text";
  nameInput.autocomplete = "off";
  nameInput.spellcheck = false;
  nameInput.setAttribute("aria-label", "Library name");
  const groupInput = doc.createElement("select");
  groupInput.setAttribute("aria-label", "Library group");
  for (const spec of LIBRARY_CATALOG) {
    const option = doc.createElement("option");
    option.value = spec.id;
    option.textContent = spec.label;
    groupInput.append(option);
  }
  const styleInput = doc.createElement("select");
  styleInput.setAttribute("aria-label", "Library style");
  const kitInput = doc.createElement("input");
  kitInput.type = "text";
  kitInput.autocomplete = "off";
  kitInput.spellcheck = false;
  kitInput.setAttribute("aria-label", "Font Awesome kit id");
  const address = doc.createElement("p");
  address.className = "pav-icon-key";
  const enabledInput = doc.createElement("input");
  enabledInput.type = "checkbox";
  enabledInput.checked = true;
  const enabledLabel = doc.createElement("label");
  enabledLabel.className = "settings-check";
  enabledLabel.append(enabledInput, doc.createTextNode(" Enabled"));
  const saveBtn = doc.createElement("button");
  saveBtn.type = "button";
  saveBtn.textContent = "Save";
  const error = doc.createElement("p");
  error.className = "pav-icon-note";
  const note = doc.createElement("p");
  note.className = "muted";
  note.textContent = "Save reloads the window when the address or the on/off switch changes. A Font Awesome style keeps the same address. Pavillion loads only the families it ships. Another family is added to the app.";

  function field(text, control) {
    const label = doc.createElement("label");
    label.className = "settings-field";
    const span = doc.createElement("span");
    span.textContent = text;
    label.append(span, control);
    return label;
  }

  const kitField = field("Kit id", kitInput);
  const addressField = field("Address", address);
  const actions = doc.createElement("div");
  actions.className = "settings-form-actions";
  actions.append(saveBtn);
  form.append(
    title,
    field("Name", nameInput),
    field("Group", groupInput),
    field("Style", styleInput),
    kitField,
    addressField,
    enabledLabel,
    actions,
    error,
    note,
  );

  function fillStyles(spec, preferred) {
    const current = preferred || styleInput.value;
    styleInput.replaceChildren();
    for (const style of spec.styles) {
      const option = doc.createElement("option");
      option.value = style.id;
      option.textContent = style.label;
      styleInput.append(option);
    }
    styleInput.value = spec.styles.some((style) => style.id === current) ? current : spec.styles[0].id;
  }

  function syncGroupFields() {
    const fa = groupInput.value === "fontawesome";
    kitField.hidden = !fa;
    addressField.hidden = fa;
    address.textContent = libraryUrl(groupInput.value, styleInput.value, kitInput.value);
  }

  function list() {
    return Array.isArray(ui().iconLibraries) ? ui().iconLibraries : [];
  }

  function rows() {
    return list().map((entry) => ({
      id: entry.id,
      name: entry.name || entry.id,
      url: entry.url || "",
      on: entry.enabled === true,
      set: libraryGroupLabel(entry),
      styleLabel: libraryStyleLabel(entry),
      type: entry.type === "stylesheet" ? "stylesheet" : "script",
    }));
  }

  function selected() {
    return list().find((entry) => entry.id === selectedId) || null;
  }

  function showForm() {
    const pane = root.querySelector("#libraries-form");
    if (pane && !pane.contains(form)) {
      pane.replaceChildren(form);
    }
  }

  function paintEntry(entry) {
    const group = libraryGroupId(entry) || "fontawesome";
    const spec = catalogById(group) || LIBRARY_CATALOG[0];
    groupInput.value = spec.id;
    fillStyles(spec, libraryStyleId(entry));
    kitInput.value = spec.id === "fontawesome" ? fontAwesomeKitId(entry.url) : "";
    enabledInput.checked = entry.enabled === true;
    syncGroupFields();
  }

  function paintForm() {
    const pane = root.querySelector("#libraries-form");
    if (!pane) {
      return;
    }
    if (drafting) {
      showForm();
      title.textContent = "Add library";
      return;
    }
    const entry = selected();
    if (!entry) {
      pane.replaceChildren();
      const empty = doc.createElement("p");
      empty.className = "settings-empty";
      empty.textContent = "Select a library, or add one.";
      pane.append(empty);
      return;
    }
    showForm();
    title.textContent = entry.name || "Library";
    nameInput.value = entry.name || "";
    paintEntry(entry);
    error.textContent = "";
  }

  function startDraft() {
    drafting = true;
    selectedId = null;
    nameInput.value = "Font Awesome";
    groupInput.value = "fontawesome";
    fillStyles(LIBRARY_CATALOG[0], "sharp-duotone-solid");
    kitInput.value = "";
    enabledInput.checked = true;
    error.textContent = "";
    syncGroupFields();
    if (view) {
      view.reselect();
    }
    paintForm();
    nameInput.focus();
  }

  async function commit(next) {
    const problem = librariesError(next);
    if (problem) {
      error.textContent = problem;
      return;
    }
    const previous = list();
    ui().iconLibraries = next;
    error.textContent = "";
    const saved = await ctx.saveNow();
    if (saved === false) {
      ui().iconLibraries = previous;
      return;
    }
    drafting = false;
    if (view) {
      view.paint();
    }
    paintForm();
    if (loadSignature(previous) !== loadSignature(next) && typeof ctx.reload === "function") {
      ctx.reload();
    }
  }

  async function saveForm() {
    const spec = catalogById(groupInput.value);
    if (!spec) {
      error.textContent = "an icon library group is not one Pavillion ships";
      return;
    }
    const style = styleInput.value;
    const kit = kitInput.value.trim();
    if (spec.id === "fontawesome" && !/^[A-Za-z0-9]+$/.test(kit)) {
      error.textContent = "a Font Awesome library needs a kit id";
      return;
    }
    const url = libraryUrl(spec.id, style, kit);
    const enabled = enabledInput.checked === true;
    const name = nameInput.value.trim();
    const next = list().map((entry) => ({ ...entry }));
    if (drafting || !selected()) {
      const id = libraryId(name || "library", next);
      next.push({ id, name: name || id, group: spec.id, style, type: spec.type, url, enabled });
      selectedId = id;
    } else {
      const index = next.findIndex((entry) => entry.id === selectedId);
      if (index < 0) {
        return;
      }
      next[index] = {
        ...next[index],
        name: name || next[index].id,
        group: spec.id,
        style,
        type: spec.type,
        url,
        enabled,
      };
    }
    await commit(next);
  }

  async function removeSelected() {
    const entry = selected();
    if (!entry) {
      return;
    }
    const ask = typeof ctx.confirm === "function"
      ? ctx.confirm
      : () => Promise.resolve(true);
    const ok = await ask({
      title: "Remove library?",
      text: `${entry.name || entry.id} is removed. The window reloads.`,
      okLabel: "Remove",
      danger: true,
    });
    if (!ok) {
      return;
    }
    selectedId = null;
    drafting = false;
    await commit(list().filter((item) => item.id !== entry.id));
  }

  function columns(documentRef, hidden, widths) {
    const off = new Set(hidden || []);
    const saved = widths || {};
    const defs = [
      {
        title: "On",
        field: "on",
        width: saved.on || 56,
        minWidth: 40,
        resizable: true,
        hozAlign: "center",
        headerSort: false,
        formatter: (cell) => {
          if (cell.getValue() !== true) {
            return "";
          }
          const mark = documentRef.createElement("i");
          mark.className = "icon fa-sharp fa-thin fa-check";
          mark.setAttribute("aria-hidden", "true");
          mark.draggable = false;
          return mark;
        },
      },
      {
        title: "Name",
        field: "name",
        minWidth: 120,
        widthGrow: 1,
        resizable: true,
        formatter: (cell) => {
          const span = documentRef.createElement("span");
          span.textContent = cell.getValue();
          return span;
        },
      },
      {
        title: "Group",
        field: "set",
        width: saved.set || 148,
        minWidth: 96,
        resizable: true,
        formatter: (cell) => {
          const span = documentRef.createElement("span");
          span.textContent = cell.getValue();
          return span;
        },
      },
      {
        title: "Style",
        field: "styleLabel",
        width: saved.styleLabel || 168,
        minWidth: 96,
        resizable: true,
        formatter: (cell) => {
          const span = documentRef.createElement("span");
          span.textContent = cell.getValue();
          return span;
        },
      },
      {
        title: "URL",
        field: "url",
        minWidth: 160,
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

  groupInput.addEventListener("change", () => {
    const spec = catalogById(groupInput.value);
    if (spec) {
      fillStyles(spec);
    }
    syncGroupFields();
  });
  styleInput.addEventListener("change", syncGroupFields);
  kitInput.addEventListener("input", syncGroupFields);
  saveBtn.addEventListener("click", () => {
    void saveForm();
  });

  if (!selectedId && list()[0]) {
    selectedId = list()[0].id;
  }

  view = mountModuleTable(root, {
    ids: {
      toolbar: "libraries-toolbar",
      footer: "libraries-footer",
      tree: "libraries-tree",
      pane: "libraries-tree-pane",
    },
    tableId: "libraries",
    title: "Libraries",
    noun: "library",
    searchId: "libraries-search",
    searchLabel: "Search libraries",
    empty: "No libraries yet.",
    chooserLabel: "Library columns",
    chooser: CHOOSER,
    widthFields: WIDTH_FIELDS,
    columns,
    groupBy: "set",
    groupOrder: (data) => {
      const labels = LIBRARY_CATALOG.map((spec) => spec.label).filter((name) => data.some((row) => row.set === name));
      if (data.some((row) => row.set === "Other")) {
        labels.push("Other");
      }
      return labels;
    },
    getRows: rows,
    match: (row, needle) => {
      const q = needle.toLowerCase();
      return row.name.toLowerCase().includes(q)
        || row.url.toLowerCase().includes(q)
        || row.set.toLowerCase().includes(q)
        || row.styleLabel.toLowerCase().includes(q);
    },
    getSelected: () => selectedId,
    onSelect: (id) => {
      drafting = false;
      selectedId = id;
      paintForm();
      if (view) {
        view.reselect();
      }
    },
    actions: [
      {
        id: "library-add",
        action: "add",
        icon: iconClass("accounts.add"),
        text: "Add",
        label: "Add a library",
        onClick: () => startDraft(),
      },
      {
        id: "library-delete",
        action: "delete",
        icon: iconClass("accounts.delete"),
        text: "Remove",
        label: "Remove the selected library",
        onClick: () => void removeSelected(),
      },
    ],
    onPaintNav({ toolbar }) {
      const button = toolbar && toolbar.querySelector("#library-delete");
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
  });
  paintForm();

  return {
    paint() {
      view.paint();
      paintForm();
    },
  };
}

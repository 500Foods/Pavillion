/* Column chooser (TABULATOR.md): a popover of checkboxes, one per column,
   opened from a toolbar button. Locked columns show a disabled checked box.
   onChange(hiddenFields) gets the new hidden list after each change and
   after Reset. DOM built with textContent; look on --pav-table-*. */
export function hiddenAfter(hidden, field, visible) {
  const set = new Set(hidden);
  if (visible) {
    set.delete(field);
  } else {
    set.add(field);
  }
  return [...set];
}

export function openColumnChooser(anchor, options, documentRef = anchor.ownerDocument) {
  const host = anchor.closest(".pav-root") || documentRef.body;
  for (const old of host.querySelectorAll(".pav-column-chooser")) {
    old.remove();
  }
  let hidden = [...(options.hidden || [])];
  const box = documentRef.createElement("div");
  box.className = "pav-column-chooser";
  box.setAttribute("role", "dialog");
  box.setAttribute("aria-label", options.label || "Columns");

  function paint() {
    box.replaceChildren();
    for (const column of options.columns) {
      const label = documentRef.createElement("label");
      const check = documentRef.createElement("input");
      check.type = "checkbox";
      check.dataset.field = column.field;
      check.checked = column.locked === true || !hidden.includes(column.field);
      check.disabled = column.locked === true;
      check.addEventListener("change", () => {
        hidden = hiddenAfter(hidden, column.field, check.checked);
        options.onChange(hidden, column.field, check.checked);
      });
      label.append(check, documentRef.createTextNode(column.title));
      box.append(label);
    }
    const reset = documentRef.createElement("button");
    reset.type = "button";
    reset.dataset.reset = "1";
    reset.textContent = "Reset";
    reset.addEventListener("click", () => {
      hidden = [];
      options.onChange(hidden, null, true);
      paint();
    });
    box.append(reset);
  }

  function close() {
    box.remove();
    documentRef.removeEventListener("pointerdown", outside, true);
    documentRef.removeEventListener("keydown", onKey, true);
    anchor.setAttribute("aria-expanded", "false");
  }
  function outside(event) {
    if (!box.contains(event.target) && !anchor.contains(event.target)) {
      close();
    }
  }
  function onKey(event) {
    if (event.key === "Escape") {
      event.stopPropagation();
      close();
      anchor.focus();
    }
  }

  paint();
  host.append(box);
  const rect = anchor.getBoundingClientRect();
  box.style.top = `${Math.round(rect.bottom)}px`;
  box.style.left = `${Math.round(Math.max(4, rect.right - box.getBoundingClientRect().width))}px`;
  anchor.setAttribute("aria-expanded", "true");
  documentRef.addEventListener("pointerdown", outside, true);
  documentRef.addEventListener("keydown", onKey, true);
  const first = box.querySelector("input:not([disabled])");
  if (first) {
    first.focus();
  }
  return { element: box, close };
}

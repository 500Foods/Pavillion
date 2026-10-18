import { CHORD_DIGITS, RAIL_ITEMS } from "../prefs.js";

const DRAG_PX = 4;

/* The list is built once. A drag reorders those nodes. Toggles and shortcut
   selects are not the drag handle, so a click still changes them. */
export function createFeatureEditor(listEl, handlers) {
  const doc = listEl.ownerDocument;
  const rows = new Map();
  for (const item of RAIL_ITEMS) {
    const row = doc.createElement("div");
    row.className = "feature-row";
    row.dataset.railKey = item.key;

    const grip = doc.createElement("span");
    grip.className = "feature-grip";
    grip.setAttribute("aria-hidden", "true");
    const gripIcon = doc.createElement("i");
    gripIcon.className = "icon fa-sharp-duotone fa-solid fa-bars";
    gripIcon.draggable = false;
    grip.append(gripIcon);

    const icon = doc.createElement("span");
    icon.className = "feature-icon";
    icon.setAttribute("aria-hidden", "true");

    const name = doc.createElement("span");
    name.className = "feature-label";
    name.textContent = item.label;

    const chordLabel = doc.createElement("label");
    chordLabel.className = "feature-chord";
    const select = doc.createElement("select");
    select.dataset.chord = item.key;
    select.setAttribute("aria-label", `Shortcut for ${item.label}`);
    const none = doc.createElement("option");
    none.value = "";
    none.textContent = "None";
    select.append(none);
    for (const digit of CHORD_DIGITS) {
      const option = doc.createElement("option");
      option.value = digit;
      option.textContent = `Ctrl+${digit}`;
      select.append(option);
    }
    chordLabel.append(select);
    row.append(grip, icon);
    if (item.toggle) {
      const check = doc.createElement("label");
      check.className = "feature-check";
      const box = doc.createElement("input");
      box.type = "checkbox";
      box.dataset.feature = item.key;
      box.setAttribute("aria-label", `Show ${item.label}`);
      const off = doc.createElement("i");
      off.className = "icon feature-check-off fa-sharp-duotone fa-solid fa-square";
      off.setAttribute("aria-hidden", "true");
      const on = doc.createElement("i");
      on.className = "icon feature-check-on fa-sharp-duotone fa-solid fa-square-check";
      on.setAttribute("aria-hidden", "true");
      check.append(box, off, on);
      row.append(check);
    } else {
      const gap = doc.createElement("span");
      gap.className = "feature-gap";
      gap.setAttribute("aria-hidden", "true");
      row.append(gap);
    }
    row.append(name, chordLabel);
    rows.set(item.key, row);
  }

  const topLabel = doc.createElement("div");
  topLabel.className = "feature-side";
  topLabel.dataset.railFixed = "true";
  topLabel.textContent = "Top";

  const bottomLabel = doc.createElement("div");
  bottomLabel.className = "feature-side";
  bottomLabel.dataset.railFixed = "true";
  bottomLabel.textContent = "Bottom";

  const divider = doc.createElement("div");
  divider.className = "feature-divider";
  divider.dataset.railDivider = "true";
  divider.setAttribute("role", "separator");
  divider.setAttribute("aria-label", "Divider between the top rail and the bottom rail");

  let dragging = false;
  let suppressClick = false;
  let releaseDrag = null;

  function arrange(rail) {
    if (dragging || !rail) {
      return;
    }
    const top = Array.isArray(rail.top) ? rail.top : [];
    const bottom = Array.isArray(rail.bottom) ? rail.bottom : [];
    const nodes = [topLabel];
    for (const key of top) {
      const row = rows.get(key);
      if (row) {
        nodes.push(row);
      }
    }
    nodes.push(divider, bottomLabel);
    for (const key of bottom) {
      const row = rows.get(key);
      if (row) {
        nodes.push(row);
      }
    }
    listEl.replaceChildren(...nodes);
    refreshIcons();
  }

  function refreshIcons() {
    for (const item of RAIL_ITEMS) {
      const row = rows.get(item.key);
      const slot = row && row.querySelector(".feature-icon");
      if (!slot) {
        continue;
      }
      const source = doc.querySelector(`.nav [data-rail="${item.key}"] .icon`);
      slot.replaceChildren();
      if (source) {
        slot.append(source.cloneNode(true));
      }
    }
  }

  function paint(features, chords) {
    const flags = features || {};
    const digits = chords || {};
    for (const item of RAIL_ITEMS) {
      const row = rows.get(item.key);
      if (!row) {
        continue;
      }
      const box = row.querySelector("[data-feature]");
      if (box) {
        box.checked = flags[item.key] === true;
      }
      const select = row.querySelector("[data-chord]");
      if (select) {
        const digit = typeof digits[item.key] === "string" ? digits[item.key] : "";
        select.value = CHORD_DIGITS.includes(digit) ? digit : "";
      }
      row.classList.toggle("is-off", item.toggle && flags[item.key] !== true);
    }
  }

  function onChange(event) {
    const target = event.target;
    if (!(target instanceof Element)) {
      return;
    }
    if (target.matches("[data-feature]")) {
      handlers.onToggle(target.dataset.feature, target.checked === true);
      return;
    }
    if (target.matches("[data-chord]")) {
      handlers.onChord(target.dataset.chord, target.value);
    }
  }

  function dropBefore(clientY, moving) {
    for (const child of listEl.children) {
      if (child === moving || child.dataset.railFixed === "true") {
        continue;
      }
      const rect = child.getBoundingClientRect();
      if (clientY < rect.top + rect.height / 2) {
        return child;
      }
    }
    return null;
  }

  function splitAtDivider() {
    const top = [];
    const bottom = [];
    let side = top;
    for (const child of listEl.children) {
      if (child === divider) {
        side = bottom;
        continue;
      }
      const key = child.dataset.railKey;
      if (key) {
        side.push(key);
      }
    }
    return { top, bottom };
  }

  function onPointerDown(event) {
    if (event.button !== 0) {
      return;
    }
    const target = event.target;
    if (!(target instanceof Element)) {
      return;
    }
    if (target.closest("input, select, option, label")) {
      return;
    }
    const row = target.closest("[data-rail-key]");
    if (!row || !listEl.contains(row)) {
      return;
    }
    event.preventDefault();
    const pointerId = event.pointerId;
    const startY = event.clientY;
    let moving = false;
    const capture = typeof target.setPointerCapture === "function" ? target : row;
    if (typeof capture.setPointerCapture === "function") {
      try {
        capture.setPointerCapture(pointerId);
      } catch {
        /* jsdom has no pointer capture. */
      }
    }
    const move = (pointer) => {
      if (pointer.pointerId !== pointerId) {
        return;
      }
      if (!moving && Math.abs(pointer.clientY - startY) < DRAG_PX) {
        return;
      }
      moving = true;
      dragging = true;
      row.classList.add("is-dragging");
      pointer.preventDefault();
      const before = dropBefore(pointer.clientY, row);
      if (before) {
        listEl.insertBefore(row, before);
      } else {
        listEl.append(row);
      }
    };
    const stop = (pointer) => {
      if (pointer.pointerId !== pointerId) {
        return;
      }
      releaseDrag = null;
      doc.removeEventListener("pointermove", move);
      doc.removeEventListener("pointerup", stop);
      doc.removeEventListener("pointercancel", stop);
      row.classList.remove("is-dragging");
      const didMove = moving;
      if (!didMove) {
        dragging = false;
        return;
      }
      move(pointer);
      dragging = false;
      suppressClick = true;
      setTimeout(() => {
        suppressClick = false;
      }, 0);
      handlers.onArrange(splitAtDivider());
    };
    releaseDrag = () => {
      doc.removeEventListener("pointermove", move);
      doc.removeEventListener("pointerup", stop);
      doc.removeEventListener("pointercancel", stop);
    };
    doc.addEventListener("pointermove", move);
    doc.addEventListener("pointerup", stop);
    doc.addEventListener("pointercancel", stop);
  }

  function onClick(event) {
    if (!suppressClick) {
      return;
    }
    event.preventDefault();
    event.stopPropagation();
  }

  listEl.addEventListener("change", onChange);
  listEl.addEventListener("pointerdown", onPointerDown);
  listEl.addEventListener("click", onClick, true);

  return {
    arrange,
    paint,
    refreshIcons,
    dragging() {
      return dragging;
    },
    destroy() {
      if (releaseDrag) {
        releaseDrag();
        releaseDrag = null;
      }
      listEl.removeEventListener("change", onChange);
      listEl.removeEventListener("pointerdown", onPointerDown);
      listEl.removeEventListener("click", onClick, true);
    },
  };
}

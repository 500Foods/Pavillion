function labelOf(item) {
  return item && typeof item.label === "string" ? item.label : "";
}

function enabledItems(menu) {
  return [...menu.querySelectorAll("[role='menuitem']")].filter(
    (item) => item.getAttribute("aria-disabled") !== "true",
  );
}

function focusItem(menu, index) {
  const items = enabledItems(menu);
  if (items.length === 0) {
    return;
  }
  const next = items[(index + items.length) % items.length];
  for (const item of menu.querySelectorAll("[role='menuitem']")) {
    item.tabIndex = -1;
  }
  next.tabIndex = 0;
  next.focus();
}

function place(menu, x, y) {
  menu.style.left = "0px";
  menu.style.top = "0px";
  const rect = menu.getBoundingClientRect();
  const view = menu.ownerDocument.defaultView;
  const width = view && view.innerWidth ? view.innerWidth : rect.width;
  const height = view && view.innerHeight ? view.innerHeight : rect.height;
  const margin = 4;
  let left = x;
  let top = y;
  if (left + rect.width > width - margin) {
    left = Math.max(margin, width - rect.width - margin);
  }
  if (top + rect.height > height - margin) {
    top = Math.max(margin, height - rect.height - margin);
  }
  menu.style.left = `${Math.round(left)}px`;
  menu.style.top = `${Math.round(top)}px`;
}

export function createMenu(root) {
  if (!(root instanceof Element)) {
    throw new Error("menu requires an element");
  }
  const hosts = new Map();
  let current = null;

  function findHost(node) {
    let el = node instanceof Element ? node : null;
    while (el) {
      if (hosts.has(el)) {
        return el;
      }
      if (el === root) {
        return null;
      }
      el = el.parentElement;
    }
    return null;
  }

  function close() {
    if (!current) {
      return;
    }
    const anchor = current.anchor;
    current.menu.remove();
    current = null;
    if (anchor && anchor.isConnected) {
      anchor.focus();
    }
  }

  function open(anchor, items, point) {
    close();
    const menu = document.createElement("div");
    menu.className = "pav-menu";
    menu.setAttribute("role", "menu");
    const list = Array.isArray(items) ? items : [];
    for (const item of list) {
      const row = document.createElement("div");
      row.className = "pav-menuitem";
      row.setAttribute("role", "menuitem");
      row.tabIndex = -1;
      row.textContent = labelOf(item);
      if (item && item.disabled) {
        row.setAttribute("aria-disabled", "true");
      }
      if (item && item.current) {
        row.setAttribute("aria-current", "true");
      }
      const choose = (event) => {
        event.preventDefault();
        event.stopPropagation();
        if (row.getAttribute("aria-disabled") === "true") {
          return;
        }
        const action = item && item.onSelect;
        close();
        if (typeof action === "function") {
          action();
        }
      };
      row.addEventListener("click", choose);
      row.addEventListener("keydown", (event) => {
        if (event.key === "Enter" || event.key === " ") {
          choose(event);
        }
      });
      menu.append(row);
    }
    root.append(menu);
    const rect = anchor && typeof anchor.getBoundingClientRect === "function"
      ? anchor.getBoundingClientRect()
      : { left: 0, bottom: 0 };
    const x = point ? point.x : rect.left;
    const y = point ? point.y : rect.bottom;
    place(menu, x, y);
    current = { menu, anchor };
    focusItem(menu, 0);
    return menu;
  }

  function itemsFor(anchor) {
    const getItems = hosts.get(anchor);
    return typeof getItems === "function" ? getItems() : [];
  }

  function onContextMenu(event) {
    const host = findHost(event.target);
    if (!host) {
      return;
    }
    event.preventDefault();
    open(host, itemsFor(host), { x: event.clientX, y: event.clientY });
  }

  function onPointerDown(event) {
    if (!current) {
      return;
    }
    const target = event.target instanceof Element ? event.target : null;
    if (target && current.menu.contains(target)) {
      return;
    }
    close();
  }

  function onKeyDown(event) {
    if (current) {
      const items = enabledItems(current.menu);
      const index = items.indexOf(root.ownerDocument.activeElement);
      if (event.key === "Escape") {
        event.preventDefault();
        close();
        return;
      }
      if (event.key === "ArrowDown") {
        event.preventDefault();
        focusItem(current.menu, index < 0 ? 0 : index + 1);
        return;
      }
      if (event.key === "ArrowUp") {
        event.preventDefault();
        focusItem(current.menu, index < 0 ? items.length - 1 : index - 1);
        return;
      }
      if (event.key === "Home") {
        event.preventDefault();
        focusItem(current.menu, 0);
        return;
      }
      if (event.key === "End") {
        event.preventDefault();
        focusItem(current.menu, items.length - 1);
        return;
      }
      return;
    }
    const opens = event.key === "ContextMenu" || (event.key === "F10" && event.shiftKey);
    if (!opens) {
      return;
    }
    const host = findHost(root.ownerDocument.activeElement);
    if (!host) {
      return;
    }
    event.preventDefault();
    open(host, itemsFor(host), null);
  }

  function bind(anchor, getItems) {
    hosts.set(anchor, getItems);
    return () => {
      hosts.delete(anchor);
      if (current && current.anchor === anchor) {
        close();
      }
    };
  }

  root.addEventListener("contextmenu", onContextMenu);
  root.addEventListener("pointerdown", onPointerDown);
  root.addEventListener("keydown", onKeyDown);

  return {
    bind,
    open,
    close,
    destroy() {
      close();
      hosts.clear();
      root.removeEventListener("contextmenu", onContextMenu);
      root.removeEventListener("pointerdown", onPointerDown);
      root.removeEventListener("keydown", onKeyDown);
    },
  };
}

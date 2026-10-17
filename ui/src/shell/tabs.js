export const KIND_ICONS = {
  message: "fa-sharp-duotone fa-solid fa-envelope",
  contact: "fa-sharp-duotone fa-solid fa-address-card",
  event: "fa-sharp-duotone fa-solid fa-calendar-day",
  file: "fa-sharp-duotone fa-solid fa-file",
  draft: "fa-sharp-duotone fa-solid fa-pen-to-square",
  browse: "fa-sharp-duotone fa-solid fa-globe",
};

function isPlain(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

export function createTabs(tablist, options = {}) {
  if (!(tablist instanceof Element)) {
    throw new Error("tabs require an element");
  }
  const workspace = options.workspace instanceof Element ? options.workspace : null;
  const sections = isPlain(options.sections) ? options.sections : {};
  const entries = new Map();
  const nodes = new Map();
  const panels = new Map();
  const views = new Map();
  const listeners = new Set();
  let activeId = null;
  let orderClock = 0;
  let closing = false;
  let suppressClick = false;
  const indicator = tablist.ownerDocument.createElement("div");
  indicator.className = "tab-drop";
  indicator.hidden = true;
  tablist.append(indicator);
  const closedStack = [];
  const menuUnbind = new Map();
  let stripUnbind = () => {};
  if (options.menu && typeof options.menu.bind === "function" && options.strip instanceof Element) {
    stripUnbind = options.menu.bind(options.strip, () => stripItems());
  }

  function list() {
    return [...entries.values()]
      .sort((a, b) => a.order - b.order)
      .map((entry) => snapshot(entry));
  }

  function snapshot(entry) {
    return {
      id: entry.id,
      section: entry.section,
      kind: entry.kind,
      key: entry.key,
      title: entry.title,
      icon: entry.icon,
      closable: entry.closable,
      pinned: entry.pinned,
      muted: entry.muted,
      context: entry.context,
      order: entry.order,
      active: entry.id === activeId,
    };
  }

  function viewCtx(entry) {
    const base = isPlain(entry.context) ? entry.context : {};
    return {
      ...base,
      id: entry.id,
      section: entry.section,
      kind: entry.kind,
      key: entry.key,
      title: entry.title,
    };
  }

  function emit(type, entry) {
    const change = { type, id: entry.id, tabs: list() };
    for (const listener of listeners) {
      listener(change);
    }
  }

  function subscribe(listener) {
    if (typeof listener !== "function") {
      return () => {};
    }
    listeners.add(listener);
    return () => listeners.delete(listener);
  }

  function register(kind, view) {
    if (typeof kind !== "string" || kind === "" || !isPlain(view)) {
      return;
    }
    views.set(kind, view);
  }

  function labelFor(section) {
    if (typeof options.iconLabel !== "function") {
      return "";
    }
    const label = options.iconLabel(section);
    return typeof label === "string" ? label : "";
  }

  function homePanel(section) {
    if (!workspace) {
      return null;
    }
    return workspace.querySelector(`#panel-${section}`);
  }

  function applyContext(entry) {
    if (entry.kind !== "home") {
      return;
    }
    const panel = homePanel(entry.section);
    if (!panel) {
      return;
    }
    if (entry.context == null) {
      delete panel.dataset.tabContext;
      return;
    }
    panel.dataset.tabContext = JSON.stringify(entry.context);
  }

  function ensurePanel(entry) {
    if (entry.kind === "home") {
      return homePanel(entry.section);
    }
    const existing = panels.get(entry.id);
    if (existing) {
      return existing;
    }
    if (!workspace) {
      return null;
    }
    const panel = workspace.ownerDocument.createElement("div");
    panel.className = "panel";
    panel.setAttribute("role", "tabpanel");
    panel.id = `panel-${entry.id}`;
    panel.dataset.tabId = entry.id;
    panel.hidden = true;
    workspace.append(panel);
    panels.set(entry.id, panel);
    const view = views.get(entry.kind);
    if (view && typeof view.create === "function") {
      view.create(panel, viewCtx(entry));
    }
    return panel;
  }

  function paintIcon(span, entry) {
    span.replaceChildren();
    if (entry.kind === "home" && typeof options.homeIcon === "function") {
      const node = options.homeIcon(entry.section);
      if (node instanceof Node) {
        span.append(node);
      }
      return;
    }
    const spec = typeof entry.icon === "string" ? entry.icon.trim() : "";
    if (spec === "" || spec.startsWith("<")) {
      return;
    }
    const icon = span.ownerDocument.createElement("i");
    icon.className = spec;
    icon.setAttribute("aria-hidden", "true");
    span.append(icon);
  }

  function renderTab(entry) {
    let tab = nodes.get(entry.id);
    if (!tab) {
      tab = tablist.ownerDocument.createElement("div");
      tab.setAttribute("role", "tab");
      tab.id = `tab-${entry.id}`;
      tab.dataset.tabId = entry.id;
      tab.tabIndex = -1;
      tab.addEventListener("click", () => {
        if (suppressClick) {
          return;
        }
        activate(entry.id);
      });
      tab.addEventListener("dblclick", (event) => {
        if (suppressClick || (event.target instanceof Element && event.target.closest(".tab-close"))) {
          return;
        }
        const current = entries.get(entry.id);
        if (current && typeof options.onReveal === "function") {
          options.onReveal(current.section);
        }
      });
      tab.addEventListener("mousedown", (event) => {
        if (event.button === 1) {
          event.preventDefault();
        }
      });
      tab.addEventListener("auxclick", (event) => {
        if (event.button !== 1) {
          return;
        }
        event.preventDefault();
        const current = entries.get(entry.id);
        if (!current || !current.closable) {
          return;
        }
        void closeTab(entry.id);
      });
      tab.addEventListener("pointerdown", (event) => {
        if (event.button !== 0 || (event.target instanceof Element && event.target.closest(".tab-close"))) {
          return;
        }
        const startX = event.clientX;
        let moving = false;
        if (typeof tab.setPointerCapture === "function") {
          try {
            tab.setPointerCapture(event.pointerId);
          } catch {
            /* jsdom has no pointer capture. */
          }
        }
        const move = (pointer) => {
          if (!moving && Math.abs(pointer.clientX - startX) < 4) {
            return;
          }
          moving = true;
          tab.classList.add("is-dragging");
          placeIndicator(groupIndex(entry.id, indexAt(pointer.clientX)));
        };
        const stop = (pointer) => {
          tab.removeEventListener("pointermove", move);
          tab.removeEventListener("pointerup", stop);
          tab.classList.remove("is-dragging");
          indicator.hidden = true;
          tablist.append(indicator);
          if (!moving) {
            return;
          }
          suppressClick = true;
          reorder(entry.id, groupIndex(entry.id, indexAt(pointer.clientX)));
          setTimeout(() => {
            suppressClick = false;
          }, 0);
        };
        tab.addEventListener("pointermove", move);
        tab.addEventListener("pointerup", stop);
      });
      const icon = tablist.ownerDocument.createElement("span");
      icon.className = "tab-icon";
      icon.setAttribute("aria-hidden", "true");
      const title = tablist.ownerDocument.createElement("span");
      title.className = "tab-title";
      tab.append(icon, title);
      nodes.set(entry.id, tab);
      tablist.append(tab);
    }
    tab.querySelector(".tab-title").textContent = entry.title;
    tab.classList.toggle("is-pinned", entry.pinned === true);
    tab.classList.toggle("is-muted", entry.muted === true);
    paintIcon(tab.querySelector(".tab-icon"), entry);
    bindTabMenu(entry, tab);
    const panel = ensurePanel(entry);
    if (panel) {
      tab.setAttribute("aria-controls", panel.id);
    }
    let close = tab.querySelector(".tab-close");
    if (entry.closable) {
      if (!close) {
        close = tablist.ownerDocument.createElement("button");
        close.type = "button";
        close.className = "tab-close";
        close.textContent = "×";
        close.addEventListener("click", (event) => {
          event.stopPropagation();
          void closeTab(entry.id);
        });
        tab.append(close);
      }
      close.setAttribute("aria-label", `Close ${entry.title}`);
    } else if (close) {
      close.remove();
    }
    return tab;
  }

  function hidePanels() {
    if (!workspace) {
      return;
    }
    for (const panel of workspace.querySelectorAll(":scope > .panel")) {
      panel.hidden = true;
    }
  }

  function activate(id) {
    const entry = entries.get(id);
    if (!entry) {
      return false;
    }
    const previous = activeId;
    activeId = id;
    for (const [key, node] of nodes) {
      const on = key === id;
      node.setAttribute("aria-selected", on ? "true" : "false");
      node.tabIndex = on ? 0 : -1;
    }
    hidePanels();
    const panel = ensurePanel(entry);
    if (panel) {
      panel.hidden = false;
      const node = nodes.get(id);
      if (node) {
        panel.setAttribute("aria-labelledby", node.id);
      }
    }
    if (previous !== id) {
      const view = views.get(entry.kind);
      if (view && typeof view.onOpen === "function") {
        view.onOpen(viewCtx(entry));
      }
    }
    emit("activate", entry);
    return true;
  }

  function openHome(section, context) {
    const meta = sections[section];
    if (!meta) {
      return null;
    }
    const id = `${section}:home`;
    let entry = entries.get(id);
    if (!entry) {
      entry = {
        id,
        section,
        kind: "home",
        key: "",
        title: typeof meta.title === "string" && meta.title !== "" ? meta.title : section,
        icon: labelFor(section),
        closable: true,
        pinned: false,
        muted: false,
        context: context !== undefined ? context : null,
        order: ++orderClock,
      };
      entries.set(id, entry);
      renderTab(entry);
      applyContext(entry);
      emit("open", entry);
    } else if (context !== undefined) {
      entry.context = context;
      entry.icon = labelFor(section);
      applyContext(entry);
      renderTab(entry);
      emit("context", entry);
      if (activeId === id) {
        const view = views.get(entry.kind);
        if (view && typeof view.onOpen === "function") {
          view.onOpen(viewCtx(entry));
        }
      }
    }
    activate(id);
    return snapshot(entry);
  }

  function openTab(spec) {
    const section = spec && spec.section;
    const kind = spec && typeof spec.kind === "string" ? spec.kind : "";
    const key = spec && spec.key != null ? String(spec.key) : "";
    if (!sections[section] || kind === "" || kind === "home" || key === "") {
      return null;
    }
    const id = `${section}:${kind}:${key}`;
    let entry = entries.get(id);
    if (entry) {
      if (typeof spec.title === "string" && spec.title !== "") {
        entry.title = spec.title;
      }
      if (typeof spec.icon === "string" && spec.icon !== "") {
        entry.icon = spec.icon;
      }
      if (spec.context !== undefined) {
        entry.context = spec.context;
      }
      renderTab(entry);
      activate(id);
      return snapshot(entry);
    }
    const icon = typeof spec.icon === "string" && spec.icon !== "" ? spec.icon : (KIND_ICONS[kind] || "");
    entry = {
      id,
      section,
      kind,
      key,
      title: typeof spec.title === "string" && spec.title !== "" ? spec.title : kind,
      icon,
      closable: true,
      pinned: false,
      muted: false,
      context: spec.context !== undefined ? spec.context : null,
      order: ++orderClock,
    };
    entries.set(id, entry);
    renderTab(entry);
    emit("open", entry);
    activate(id);
    return snapshot(entry);
  }

  async function closeTab(id, opts) {
    const entry = entries.get(id);
    if (!entry || !entry.closable || closing) {
      return false;
    }
    const stay = opts && opts.stay === true;
    const view = views.get(entry.kind);
    const ctx = viewCtx(entry);
    if (view && typeof view.unsaved === "function" && view.unsaved(ctx) === true) {
      const ok = typeof options.confirm === "function" ? await options.confirm(snapshot(entry)) : false;
      if (ok !== true || !entries.has(id)) {
        return false;
      }
    }
    closing = true;
    try {
      /* The closed tab's left neighbor becomes active. A batch close passes stay so a background tab does not steal the active one. The last close leaves the workspace empty. */
      const ordered = [...entries.values()].sort((a, b) => a.order - b.order);
      const index = ordered.findIndex((item) => item.id === id);
      let nextId = null;
      if (index > 0) {
        nextId = ordered[index - 1].id;
      } else if (ordered.length > 1) {
        nextId = ordered[1].id;
      }
      if (view && typeof view.onClose === "function") {
        view.onClose();
      }
      const panel = panels.get(id);
      if (panel) {
        panel.remove();
      }
      panels.delete(id);
      const node = nodes.get(id);
      if (node) {
        node.remove();
      }
      nodes.delete(id);
      entries.delete(id);
      unbindMenu(id);
      rememberClosed(entry);
      const wasActive = activeId === id;
      if (wasActive) {
        activeId = null;
      }
      emit("close", entry);
      /* A background close still selects the left tab, unless this close is one of a batch that must stay put. */
      if (wasActive || !stay) {
        if (!nextId) {
          hidePanels();
        } else if (activeId !== nextId) {
          activate(nextId);
        }
        const next = nodes.get(nextId);
        if (next) {
          next.focus();
        }
      }
      return true;
    } finally {
      closing = false;
    }
  }

  function sortedEntries() {
    return [...entries.values()].sort((a, b) => a.order - b.order);
  }

  function rememberClosed(entry) {
    closedStack.push({
      id: entry.id,
      section: entry.section,
      kind: entry.kind,
      key: entry.key,
      title: entry.title,
      icon: entry.icon,
      pinned: entry.pinned === true,
      muted: entry.muted === true,
      context: isPlain(entry.context) ? { ...entry.context } : entry.context,
    });
    if (closedStack.length > 20) {
      closedStack.shift();
    }
  }

  function applyOrder(ordered) {
    ordered.forEach((entry, index) => {
      entry.order = index + 1;
    });
    orderClock = ordered.length;
    for (const entry of ordered) {
      const node = nodes.get(entry.id);
      if (node) {
        tablist.append(node);
      }
    }
    tablist.append(indicator);
  }

  function focusTab(id) {
    const node = nodes.get(id);
    if (node) {
      node.focus();
    }
  }

  function bindTabMenu(entry, tab) {
    if (!options.menu || typeof options.menu.bind !== "function" || menuUnbind.has(entry.id)) {
      return;
    }
    menuUnbind.set(entry.id, options.menu.bind(tab, () => itemsForTab(entry.id)));
  }

  function unbindMenu(id) {
    const stop = menuUnbind.get(id);
    if (stop) {
      stop();
    }
    menuUnbind.delete(id);
  }

  function itemsForTab(id) {
    const entry = entries.get(id);
    if (!entry) {
      return [];
    }
    const others = sortedEntries().filter((item) => item.id !== id && !item.pinned);
    const right = sortedEntries().filter((item) => item.order > entry.order && !item.pinned);
    return [
      {
        label: entry.pinned ? "Unpin" : "Pin",
        disabled: entry.kind === "home",
        onSelect() {
          setPinned(id, !entry.pinned);
        },
      },
      {
        label: "Duplicate",
        disabled: entry.kind === "home",
        onSelect() {
          duplicate(id);
        },
      },
      {
        label: entry.muted ? "Unmute" : "Mute",
        disabled: entry.kind !== "browse",
        onSelect() {
          setMuted(id, !entry.muted);
        },
      },
      {
        label: "Close",
        onSelect() {
          void closeTab(id);
        },
      },
      {
        label: "Close others",
        disabled: others.length === 0,
        onSelect() {
          void closeOthers(id);
        },
      },
      {
        label: "Close to the right",
        disabled: right.length === 0,
        onSelect() {
          void closeToTheRight(id);
        },
      },
      {
        label: "Reopen closed tab",
        disabled: closedStack.length === 0,
        onSelect() {
          reopenClosed();
        },
      },
    ];
  }

  function stripItems() {
    const sub = sortedEntries().filter((item) => item.kind !== "home" && !item.pinned);
    return [
      { label: "Vertical tabs", disabled: true },
      {
        label: "Close all sub-tabs",
        disabled: sub.length === 0,
        onSelect() {
          void closeAllSubTabs();
        },
      },
    ];
  }

  function setPinned(id, pinned) {
    const entry = entries.get(id);
    if (!entry || entry.kind === "home") {
      return false;
    }
    const next = pinned === true;
    if (entry.pinned === next) {
      return true;
    }
    entry.pinned = next;
    const ordered = sortedEntries().filter((item) => item.id !== id);
    const pinCount = ordered.filter((item) => item.pinned).length;
    ordered.splice(pinCount, 0, entry);
    applyOrder(ordered);
    renderTab(entry);
    emit(next ? "pin" : "unpin", entry);
    return true;
  }

  function duplicate(id) {
    const entry = entries.get(id);
    if (!entry || entry.kind === "home") {
      return null;
    }
    const base = entry.key.replace(/#\d+$/, "");
    let n = 2;
    let key = `${base}#${n}`;
    while (entries.has(`${entry.section}:${entry.kind}:${key}`)) {
      n += 1;
      key = `${base}#${n}`;
    }
    return openTab({
      section: entry.section,
      kind: entry.kind,
      key,
      title: entry.title,
      icon: entry.icon,
      context: isPlain(entry.context) ? { ...entry.context } : entry.context,
    });
  }

  function setMuted(id, muted) {
    const entry = entries.get(id);
    if (!entry || entry.kind !== "browse") {
      return false;
    }
    entry.muted = muted === true;
    renderTab(entry);
    emit("mute", entry);
    return true;
  }

  async function closeSome(ids) {
    for (const id of ids) {
      if (!entries.has(id)) {
        continue;
      }
      const ok = await closeTab(id, { stay: activeId !== id });
      if (!ok && entries.has(id)) {
        return false;
      }
    }
    return true;
  }

  function closeOthers(id) {
    const ids = sortedEntries().filter((item) => item.id !== id && !item.pinned).map((item) => item.id);
    return closeSome(ids);
  }

  function closeToTheRight(id) {
    const entry = entries.get(id);
    if (!entry) {
      return Promise.resolve(false);
    }
    const ids = sortedEntries().filter((item) => item.order > entry.order && !item.pinned).map((item) => item.id);
    return closeSome(ids);
  }

  function closeAllSubTabs() {
    const ids = sortedEntries().filter((item) => item.kind !== "home" && !item.pinned).map((item) => item.id);
    return closeSome(ids);
  }

  function reopenClosed() {
    const snap = closedStack.pop();
    if (!snap) {
      return null;
    }
    if (entries.has(snap.id)) {
      activate(snap.id);
      focusTab(snap.id);
      return snapshot(entries.get(snap.id));
    }
    if (snap.kind === "home") {
      const opened = openHome(snap.section, snap.context == null ? undefined : snap.context);
      if (opened) {
        focusTab(opened.id);
      } else {
        closedStack.push(snap);
      }
      return opened;
    }
    const opened = openTab({
      section: snap.section,
      kind: snap.kind,
      key: snap.key,
      title: snap.title,
      icon: snap.icon,
      context: snap.context,
    });
    if (!opened) {
      closedStack.push(snap);
      return null;
    }
    if (snap.pinned) {
      setPinned(opened.id, true);
    }
    if (snap.muted) {
      setMuted(opened.id, true);
    }
    focusTab(opened.id);
    return snapshot(entries.get(opened.id));
  }

  function reloadActive() {
    if (!activeId) {
      return false;
    }
    const entry = entries.get(activeId);
    const view = views.get(entry.kind);
    if (!view || typeof view.onOpen !== "function") {
      return false;
    }
    view.onOpen(viewCtx(entry));
    return true;
  }

  function cycle(delta) {
    const ordered = sortedEntries();
    if (ordered.length === 0) {
      return false;
    }
    let index = ordered.findIndex((item) => item.id === activeId);
    if (index < 0) {
      index = delta < 0 ? ordered.length - 1 : 0;
    } else {
      index = (index + delta + ordered.length) % ordered.length;
    }
    const id = ordered[index].id;
    activate(id);
    focusTab(id);
    return true;
  }

  function activateAt(index) {
    const ordered = sortedEntries();
    const entry = ordered[index];
    if (!entry) {
      return false;
    }
    activate(entry.id);
    focusTab(entry.id);
    return true;
  }

  function focusActive() {
    if (activeId) {
      focusTab(activeId);
    }
  }

  function groupIndex(id, raw) {
    const ordered = sortedEntries();
    const item = entries.get(id);
    if (!item) {
      return raw;
    }
    const pinCount = ordered.filter((entry) => entry.pinned).length;
    if (item.pinned) {
      return Math.max(0, Math.min(raw, pinCount));
    }
    return Math.max(pinCount, Math.min(raw, ordered.length));
  }

  function indexAt(clientX) {
    const ordered = sortedEntries();
    for (let i = 0; i < ordered.length; i += 1) {
      const node = nodes.get(ordered[i].id);
      if (!node) {
        continue;
      }
      const rect = node.getBoundingClientRect();
      if (clientX < rect.left + rect.width / 2) {
        return i;
      }
    }
    return ordered.length;
  }

  function placeIndicator(index) {
    const ordered = sortedEntries();
    indicator.hidden = false;
    const target = ordered[index];
    const node = target ? nodes.get(target.id) : null;
    if (node) {
      tablist.insertBefore(indicator, node);
    } else {
      tablist.append(indicator);
    }
  }

  function reorder(id, toIndex) {
    const ordered = sortedEntries();
    const from = ordered.findIndex((item) => item.id === id);
    if (from < 0) {
      return;
    }
    let target = toIndex;
    if (target > from) {
      target -= 1;
    }
    const item = ordered[from];
    const pinCount = ordered.filter((entry) => entry.pinned).length;
    /* A pinned tab stays in the pinned group. An unpinned tab stays to the right of it. */
    if (item.pinned) {
      target = Math.max(0, Math.min(target, pinCount - 1));
    } else {
      target = Math.max(pinCount, Math.min(target, ordered.length - 1));
    }
    if (target === from) {
      return;
    }
    ordered.splice(from, 1);
    ordered.splice(target, 0, item);
    applyOrder(ordered);
    indicator.hidden = true;
    emit("reorder", item);
  }

  function refreshIcons() {
    for (const entry of entries.values()) {
      if (entry.kind !== "home") {
        continue;
      }
      entry.icon = labelFor(entry.section);
      const tab = nodes.get(entry.id);
      if (!tab) {
        continue;
      }
      paintIcon(tab.querySelector(".tab-icon"), entry);
    }
  }

  function destroy() {
    closing = true;
    for (const entry of entries.values()) {
      const view = views.get(entry.kind);
      if (view && typeof view.onClose === "function") {
        view.onClose();
      }
    }
    for (const panel of panels.values()) {
      panel.remove();
    }
    for (const node of nodes.values()) {
      node.remove();
    }
    indicator.remove();
    stripUnbind();
    for (const id of menuUnbind.keys()) {
      unbindMenu(id);
    }
    closedStack.splice(0, closedStack.length);
    panels.clear();
    nodes.clear();
    entries.clear();
    listeners.clear();
    views.clear();
    activeId = null;
    closing = false;
  }

  return {
    openHome,
    openTab,
    closeTab,
    activate,
    list,
    subscribe,
    register,
    refreshIcons,
    setPinned,
    duplicate,
    setMuted,
    closeOthers,
    closeToTheRight,
    closeAllSubTabs,
    reopenClosed,
    reloadActive,
    cycle,
    activateAt,
    focusActive,
    closeActive() {
      return activeId ? closeTab(activeId) : Promise.resolve(false);
    },
    destroy,
  };
}

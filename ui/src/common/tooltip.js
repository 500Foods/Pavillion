const HOVER_MS = 500;

/* D-UI Tooltips: delegated hosts. An explicit bind, a data-pav-tip
   attribute, or an icon-only button with an aria-label. */
export function tipText(hosts, el) {
  if (hosts.has(el)) {
    return hosts.get(el);
  }
  if (el.hasAttribute("data-pav-tip")) {
    return el.getAttribute("data-pav-tip") || "";
  }
  if (el.localName === "button" && el.hasAttribute("aria-label") && el.textContent.trim() === "") {
    return el.getAttribute("aria-label") || "";
  }
  return null;
}

function findHost(hosts, root, node) {
  let el = node instanceof Element ? node : null;
  while (el) {
    if (tipText(hosts, el) !== null) {
      return el;
    }
    if (el === root) {
      return null;
    }
    el = el.parentElement;
  }
  return null;
}

function cssPx(raw, rootPx, emPx) {
  const text = String(raw ?? "").trim();
  const value = Number.parseFloat(text);
  if (!Number.isFinite(value) || value < 0) {
    return null;
  }
  if (text.endsWith("rem")) {
    return value * rootPx;
  }
  if (text.endsWith("em")) {
    return value * emPx;
  }
  return value;
}

function themePx(style, name, rootPx, emPx, fallback) {
  const parsed = cssPx(style.getPropertyValue(name), rootPx, emPx);
  return parsed === null ? fallback : parsed;
}

function place(tip, anchor) {
  const view = tip.ownerDocument.defaultView;
  if (!view) {
    return;
  }
  const doc = tip.ownerDocument;
  const rootPx = Number.parseFloat(view.getComputedStyle(doc.documentElement).fontSize) || 16;
  const tipStyle = view.getComputedStyle(tip);
  const emPx = Number.parseFloat(tipStyle.fontSize) || rootPx;
  // The arrow is a pseudo-element, so the border box does not include it.
  // Shell zoom changes the root font-size, and the arrow length is in rem.
  const arrow = themePx(tipStyle, "--pav-tooltip-arrow", rootPx, emPx, 8);
  const radius = themePx(tipStyle, "--pav-tooltip-radius", rootPx, emPx, 0);
  const border = themePx(tipStyle, "--pav-tooltip-border-width", rootPx, emPx, 0);
  const margin = 8;
  const gap = arrow + rootPx * 0.125;
  const rect = anchor.getBoundingClientRect();
  tip.style.left = "0px";
  tip.style.top = "0px";
  const box = tip.getBoundingClientRect();
  const width = view.innerWidth || box.width;
  const height = view.innerHeight || box.height;
  let left = rect.left + (rect.width - box.width) / 2;
  let top = rect.top - box.height - gap;
  let side = "top";
  if (top < margin) {
    side = "bottom";
    top = rect.bottom + gap;
  }
  if (top + box.height > height - margin) {
    top = Math.max(margin, height - box.height - margin);
  }
  if (box.width > 0) {
    left = Math.max(margin, Math.min(left, width - box.width - margin));
  }
  const padWidth = Math.max(0, box.width - border * 2);
  let shift = rect.left + rect.width / 2 - left - border;
  if (padWidth > 0) {
    const inset = Math.min(Math.max(radius - border, 0) + arrow, padWidth / 2);
    shift = Math.max(inset, Math.min(shift, padWidth - inset));
  }
  tip.dataset.side = side;
  tip.style.setProperty("--pav-tooltip-shift", `${Math.round(shift)}px`);
  tip.style.left = `${Math.round(left)}px`;
  tip.style.top = `${Math.round(top)}px`;
}

export function createTooltip(root) {
  if (!(root instanceof Element)) {
    throw new Error("tooltip requires an element");
  }
  const hosts = new Map();
  const tip = document.createElement("div");
  tip.className = "pav-tooltip";
  tip.id = "pav-tooltip";
  tip.setAttribute("role", "tooltip");
  tip.hidden = true;
  root.append(tip);

  let timer = 0;
  let pending = null;
  let anchor = null;
  let keyboard = false;

  function clearDescribed(element) {
    if (!element) {
      return;
    }
    const parts = (element.getAttribute("aria-describedby") || "").split(/\s+/).filter((id) => id && id !== tip.id);
    if (parts.length === 0) {
      element.removeAttribute("aria-describedby");
    } else {
      element.setAttribute("aria-describedby", parts.join(" "));
    }
  }

  function hide() {
    if (timer) {
      root.ownerDocument.defaultView.clearTimeout(timer);
      timer = 0;
    }
    pending = null;
    clearDescribed(anchor);
    anchor = null;
    tip.hidden = true;
    tip.textContent = "";
    delete tip.dataset.side;
    tip.style.removeProperty("--pav-tooltip-shift");
  }

  function show(next) {
    if (timer) {
      root.ownerDocument.defaultView.clearTimeout(timer);
      timer = 0;
    }
    pending = null;
    const text = tipText(hosts, next);
    if (typeof text !== "string" || text === "") {
      hide();
      return;
    }
    if (anchor && anchor !== next) {
      clearDescribed(anchor);
    }
    anchor = next;
    tip.textContent = text;
    tip.hidden = false;
    const parts = (next.getAttribute("aria-describedby") || "").split(/\s+/).filter(Boolean);
    if (!parts.includes(tip.id)) {
      parts.push(tip.id);
      next.setAttribute("aria-describedby", parts.join(" "));
    }
    place(tip, next);
  }

  function onOver(event) {
    const host = findHost(hosts, root, event.target);
    if (!host) {
      return;
    }
    const from = event.relatedTarget;
    if (from instanceof Node && host.contains(from)) {
      return;
    }
    if (timer) {
      root.ownerDocument.defaultView.clearTimeout(timer);
    }
    pending = host;
    timer = root.ownerDocument.defaultView.setTimeout(() => {
      timer = 0;
      if (pending === host && tipText(hosts, host) !== null) {
        show(host);
      }
    }, HOVER_MS);
  }

  function onOut(event) {
    const host = findHost(hosts, root, event.target);
    if (!host) {
      return;
    }
    const to = event.relatedTarget;
    if (to instanceof Node && host.contains(to)) {
      return;
    }
    if (pending === host || anchor === host) {
      hide();
    }
  }

  function onFocusIn(event) {
    if (!keyboard) {
      return;
    }
    const host = findHost(hosts, root, event.target);
    if (!host) {
      return;
    }
    show(host);
  }

  function onFocusOut(event) {
    const host = findHost(hosts, root, event.target);
    if (host && anchor === host) {
      hide();
    }
  }

  function onKeyDown(event) {
    if (event.key === "Tab") {
      keyboard = true;
    }
    if (event.key === "Escape" && anchor) {
      hide();
    }
  }

  function onPointerDown() {
    keyboard = false;
    hide();
  }

  function bind(element, text) {
    hosts.set(element, typeof text === "string" ? text : "");
    return () => {
      hosts.delete(element);
      if (anchor === element || pending === element) {
        hide();
      }
    };
  }

  root.addEventListener("pointerover", onOver);
  root.addEventListener("pointerout", onOut);
  root.addEventListener("focusin", onFocusIn);
  root.addEventListener("focusout", onFocusOut);
  root.addEventListener("keydown", onKeyDown);
  root.addEventListener("pointerdown", onPointerDown);

  return {
    bind,
    hide,
    destroy() {
      hide();
      hosts.clear();
      tip.remove();
      root.removeEventListener("pointerover", onOver);
      root.removeEventListener("pointerout", onOut);
      root.removeEventListener("focusin", onFocusIn);
      root.removeEventListener("focusout", onFocusOut);
      root.removeEventListener("keydown", onKeyDown);
      root.removeEventListener("pointerdown", onPointerDown);
    },
  };
}

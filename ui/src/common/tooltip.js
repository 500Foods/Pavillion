const HOVER_MS = 500;

function findHost(hosts, root, node) {
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

function place(tip, anchor) {
  const margin = 8;
  const rect = anchor.getBoundingClientRect();
  tip.style.left = "0px";
  tip.style.top = "0px";
  const box = tip.getBoundingClientRect();
  const view = tip.ownerDocument.defaultView;
  const width = view && view.innerWidth ? view.innerWidth : box.width;
  const height = view && view.innerHeight ? view.innerHeight : box.height;
  let left = rect.left + (rect.width - box.width) / 2;
  let top = rect.top - box.height - margin;
  if (top < margin) {
    top = rect.bottom + margin;
  }
  if (top + box.height > height - margin) {
    top = Math.max(margin, height - box.height - margin);
  }
  left = Math.max(margin, Math.min(left, width - box.width - margin));
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
  }

  function show(next) {
    if (timer) {
      root.ownerDocument.defaultView.clearTimeout(timer);
      timer = 0;
    }
    pending = null;
    const text = hosts.get(next);
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
      if (pending === host && hosts.has(host)) {
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

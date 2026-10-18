const DRAG_PX = 4;

export function createRailDrag(root, handlers) {
  const doc = root.ownerDocument;
  const nav = root.querySelector(".nav");
  if (!nav) {
    return {
      destroy() {},
    };
  }
  const indicator = doc.createElement("div");
  indicator.className = "rail-drop";
  indicator.setAttribute("aria-hidden", "true");
  let suppressClick = false;

  function bars() {
    const nodes = nav.querySelectorAll(":scope > .nav-buttons");
    return { top: nodes[0] || null, bottom: nodes[1] || null };
  }

  function targetBar(clientY) {
    const found = bars();
    if (!found.top || !found.bottom) {
      return null;
    }
    const topRect = found.top.getBoundingClientRect();
    const bottomRect = found.bottom.getBoundingClientRect();
    const mid = (topRect.bottom + bottomRect.top) / 2;
    if (clientY <= mid) {
      return { name: "top", el: found.top };
    }
    return { name: "bottom", el: found.bottom };
  }

  function beforeButton(bar, clientX, moving) {
    const buttons = [...bar.querySelectorAll("button[data-rail]")].filter((node) => !node.hidden && node !== moving);
    for (const button of buttons) {
      const rect = button.getBoundingClientRect();
      if (clientX < rect.left + rect.width / 2) {
        return button;
      }
    }
    return null;
  }

  function orderFromDrop(rail, key, side, beforeKey) {
    const source = rail || {};
    const top = Array.isArray(source.top) ? source.top.filter((item) => item !== key) : [];
    const bottom = Array.isArray(source.bottom) ? source.bottom.filter((item) => item !== key) : [];
    const dest = side === "bottom" ? bottom : top;
    const index = beforeKey ? dest.indexOf(beforeKey) : -1;
    if (index < 0) {
      dest.push(key);
    } else {
      dest.splice(index, 0, key);
    }
    return { top, bottom };
  }

  function placeIndicator(bar, before) {
    if (before) {
      bar.insertBefore(indicator, before);
    } else {
      bar.append(indicator);
    }
  }

  function onPointerDown(event) {
    if (event.button !== 0) {
      return;
    }
    const target = event.target;
    if (!(target instanceof Element)) {
      return;
    }
    const button = target.closest("button[data-rail]");
    if (!button || button.hidden || !nav.contains(button) || !button.closest(".nav-buttons")) {
      return;
    }
    const startX = event.clientX;
    const startY = event.clientY;
    let moving = false;
    if (typeof button.setPointerCapture === "function") {
      try {
        button.setPointerCapture(event.pointerId);
      } catch {
        /* jsdom has no pointer capture. */
      }
    }
    const move = (pointer) => {
      if (!moving && Math.abs(pointer.clientX - startX) < DRAG_PX && Math.abs(pointer.clientY - startY) < DRAG_PX) {
        return;
      }
      moving = true;
      button.classList.add("is-dragging");
      const dest = targetBar(pointer.clientY);
      if (!dest) {
        return;
      }
      placeIndicator(dest.el, beforeButton(dest.el, pointer.clientX, button));
    };
    const stop = (pointer) => {
      button.removeEventListener("pointermove", move);
      button.removeEventListener("pointerup", stop);
      button.removeEventListener("pointercancel", stop);
      button.classList.remove("is-dragging");
      indicator.remove();
      if (!moving) {
        return;
      }
      suppressClick = true;
      setTimeout(() => {
        suppressClick = false;
      }, 0);
      const dest = targetBar(pointer.clientY);
      if (!dest) {
        return;
      }
      const before = beforeButton(dest.el, pointer.clientX, button);
      handlers.onArrange(orderFromDrop(handlers.rail(), button.dataset.rail, dest.name, before && before.dataset.rail));
    };
    button.addEventListener("pointermove", move);
    button.addEventListener("pointerup", stop);
    button.addEventListener("pointercancel", stop);
  }

  function onClick(event) {
    if (!suppressClick) {
      return;
    }
    event.preventDefault();
    event.stopPropagation();
  }

  nav.addEventListener("pointerdown", onPointerDown);
  nav.addEventListener("click", onClick, true);

  return {
    destroy() {
      indicator.remove();
      nav.removeEventListener("pointerdown", onPointerDown);
      nav.removeEventListener("click", onClick, true);
    },
  };
}

/* H-4: UI forwarding through log.client. User actions are INFO, navigation
   (heading, nav, tab, tree, view changes) is DEBUG. At most 50 lines per
   second leave the UI; the core enforces the same limit and counts drops.
   Only stable names are logged (data attributes, id, aria-label), never
   field values or element text, so no address or secret reaches the log. */

export const FORWARD_PER_SECOND = 50;

const NAV_ATTRS = ["heading", "nav", "section", "settings", "activityView", "view", "tab"];

function stableName(el) {
  const data = el.dataset || {};
  for (const key of ["action", "cmd", "id", "segment"]) {
    if (typeof data[key] === "string" && data[key] !== "") {
      return data[key];
    }
  }
  if (el.id) {
    return el.id;
  }
  const aria = el.getAttribute("aria-label");
  if (aria) {
    return aria.replace(/[^\w .-]/g, "").slice(0, 48);
  }
  for (const key of Object.keys(data)) {
    return key;
  }
  return "";
}

function navName(el) {
  const data = el.dataset || {};
  for (const key of NAV_ATTRS) {
    if (typeof data[key] === "string" && data[key] !== "") {
      return `${key} ${data[key]}`;
    }
  }
  const role = el.getAttribute("role");
  if (role === "tab" || role === "treeitem") {
    const name = stableName(el);
    return name ? `${role} ${name}` : role;
  }
  return "";
}

export function createForwarder(send, now = () => Date.now()) {
  let second = -1;
  let count = 0;

  function line(level, message) {
    const at = Math.floor(now() / 1000);
    if (at !== second) {
      second = at;
      count = 0;
    }
    if (count >= FORWARD_PER_SECOND) {
      return false;
    }
    count += 1;
    Promise.resolve()
      .then(() => send("ui", level, message))
      .catch(() => {});
    return true;
  }

  return {
    action(name) {
      return line("info", `action ${name}`);
    },
    navigate(name) {
      return line("debug", `navigate ${name}`);
    },
  };
}

export function forwardEvents(root, forwarder) {
  const onClick = (event) => {
    const el = event.target instanceof Element
      ? event.target.closest('button, a, [role="tab"], [role="treeitem"], [role="menuitem"], [data-action]')
      : null;
    if (!el || !root.contains(el)) {
      return;
    }
    const nav = navName(el);
    if (nav) {
      forwarder.navigate(nav);
      return;
    }
    const name = stableName(el);
    if (name) {
      forwarder.action(`click ${name}`);
    }
  };
  const onChange = (event) => {
    const el = event.target;
    if (!(el instanceof Element) || el.type === "password") {
      return;
    }
    const name = stableName(el) || el.getAttribute("name") || "";
    if (name) {
      forwarder.action(`change ${name}`);
    }
  };
  root.addEventListener("click", onClick, true);
  root.addEventListener("change", onChange, true);
  return () => {
    root.removeEventListener("click", onClick, true);
    root.removeEventListener("change", onChange, true);
  };
}

/* D-10 module bar convention (D-UI.md): every module has a module toolbar
   (title left, actions, search right, buttons right of search) and a
   module footer (navigator bottom left, Refresh always first; list actions
   on the right). DOM built with textContent; look on --pav-* in theme.css. */

function iconButton(spec, documentRef) {
  const button = documentRef.createElement("button");
  button.type = "button";
  if (spec.id) {
    button.id = spec.id;
  }
  if (spec.action) {
    button.dataset.moduleAction = spec.action;
  }
  button.title = spec.label;
  button.setAttribute("aria-label", spec.label);
  if (spec.icon) {
    const icon = documentRef.createElement("i");
    icon.className = `fa-sharp-duotone fa-solid ${spec.icon}`;
    icon.setAttribute("aria-hidden", "true");
    button.append(icon);
  }
  if (spec.text) {
    button.append(spec.icon ? ` ${spec.text}` : spec.text);
  }
  if (typeof spec.onClick === "function") {
    button.addEventListener("click", () => spec.onClick());
  }
  return button;
}

export const REFRESH = { action: "refresh", icon: "fa-arrows-rotate", label: "Refresh" };

export const NAV_BUTTONS = [
  { action: "first", icon: "fa-backward-step", label: "First" },
  { action: "prev", icon: "fa-chevron-left", label: "Previous" },
  { action: "next", icon: "fa-chevron-right", label: "Next" },
  { action: "last", icon: "fa-forward-step", label: "Last" },
];

/* options: { title, label, actions: [spec], search: { label, placeholder,
   onInput(value) }, after: [spec] }. Returns the bar element. */
export function moduleToolbar(options, documentRef = globalThis.document) {
  const bar = documentRef.createElement("div");
  bar.className = "button-bar pav-module-toolbar";
  bar.setAttribute("role", "toolbar");
  bar.setAttribute("aria-label", options.label || options.title || "");
  const title = documentRef.createElement("span");
  title.className = "pav-module-title";
  title.textContent = options.title || "";
  bar.append(title);
  for (const spec of options.actions || []) {
    bar.append(iconButton(spec, documentRef));
  }
  const spacer = documentRef.createElement("span");
  spacer.className = "pav-module-spacer";
  bar.append(spacer);
  if (options.search) {
    const input = documentRef.createElement("input");
    input.type = "search";
    input.className = "pav-module-search";
    if (options.search.id) {
      input.id = options.search.id;
    }
    input.setAttribute("aria-label", options.search.label || "Search");
    input.placeholder = options.search.placeholder || options.search.label || "Search";
    input.spellcheck = false;
    input.autocomplete = "off";
    const fire = () => options.search.onInput && options.search.onInput(input.value);
    input.addEventListener("input", fire);
    input.addEventListener("keydown", (event) => {
      if (event.key === "Escape" && input.value !== "") {
        input.value = "";
        fire();
      }
    });
    bar.append(input);
  }
  for (const spec of options.after || []) {
    bar.append(iconButton(spec, documentRef));
  }
  return bar;
}

/* options: { label, noun ("account": "First account"), onRefresh, onNav(action) (omit: no navigator),
   end: [spec] }. Refresh is always the first button. */
export function moduleFooter(options, documentRef = globalThis.document) {
  const bar = documentRef.createElement("div");
  bar.className = "button-bar pav-module-footer";
  bar.setAttribute("role", "toolbar");
  bar.setAttribute("aria-label", options.label || "Module actions");
  const nav = documentRef.createElement("span");
  nav.className = "pav-module-nav";
  nav.append(iconButton({ ...REFRESH, onClick: options.onRefresh }, documentRef));
  if (typeof options.onNav === "function") {
    for (const spec of NAV_BUTTONS) {
      const label = options.noun ? `${spec.label} ${options.noun}` : spec.label;
      const button = iconButton({ ...spec, label, onClick: () => options.onNav(spec.action) }, documentRef);
      button.dataset.step = spec.action;
      nav.append(button);
    }
  }
  const end = documentRef.createElement("span");
  end.className = "pav-module-footer-end";
  for (const spec of options.end || []) {
    end.append(iconButton(spec, documentRef));
  }
  bar.append(nav, end);
  return bar;
}

/* Enable the navigator for ids (display order) and the current id. */
export function navState(ids, currentId) {
  const index = ids.indexOf(currentId);
  const count = ids.length;
  return {
    first: count > 0 && index !== 0,
    prev: count > 0 && index > 0,
    next: count > 0 && index < count - 1,
    last: count > 0 && index !== count - 1,
  };
}

export function paintFooterNav(bar, ids, currentId) {
  const state = navState(ids, currentId);
  for (const button of bar.querySelectorAll("button[data-step]")) {
    button.disabled = !state[button.dataset.step];
  }
}

/* Mount a built bar into a host already in the page markup. The host keeps
   its own children (a Help jump, debug buttons) after the built items, and
   stays the .button-bar element, so Button bars zoom (applied to the markup
   at mount) reaches it. Returns the host, or the bar when there is none. */
export function mountBar(host, bar) {
  if (!host) {
    return bar;
  }
  host.classList.add(...bar.classList);
  for (const name of ["role", "aria-label"]) {
    if (!host.hasAttribute(name) && bar.hasAttribute(name)) {
      host.setAttribute(name, bar.getAttribute(name));
    }
  }
  host.prepend(...bar.childNodes);
  return host;
}

/* Where First, Previous, Next, Last go from currentId in ids. */
export function navTarget(ids, currentId, action) {
  if (ids.length === 0) {
    return null;
  }
  const index = ids.indexOf(currentId);
  if (action === "first") {
    return ids[0];
  }
  if (action === "last") {
    return ids[ids.length - 1];
  }
  if (index < 0) {
    return action === "prev" ? ids[ids.length - 1] : ids[0];
  }
  if (action === "prev") {
    return ids[Math.max(0, index - 1)];
  }
  if (action === "next") {
    return ids[Math.min(ids.length - 1, index + 1)];
  }
  return currentId;
}

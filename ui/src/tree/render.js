/* Tree renderer module. Renders nodes as a static tree with configurable
   row regions. Outside data (labels, counts) is set via textContent, never
   innerHTML. The renderer owns DOM only; it does not fetch nodes. */

const DEFAULT_REGIONS = ["leading", "label", "status", "count", "trailing"];

const REGION_CLASSES = {
  leading: "tree-row-leading",
  label: "tree-row-label",
  status: "tree-row-status",
  count: "tree-row-count",
  trailing: "tree-row-trailing",
};

const ICON_MAP = {
  account: "fa-sharp-duotone fa-solid fa-user",
  mailbox: "fa-sharp-duotone fa-solid fa-envelope",
  folder: "fa-sharp-duotone fa-solid fa-folder",
  calendar: "fa-sharp-duotone fa-solid fa-calendar-days",
  addressbook: "fa-sharp-duotone fa-solid fa-address-book",
  group: "fa-sharp-duotone fa-solid fa-users",
};

const STATE_ICONS = {
  syncing: "fa-solid fa-spinner",
  error: "fa-sharp-duotone fa-solid fa-circle-exclamation",
};

function text(node, key) {
  const value = node[key];
  return value === null || value === undefined ? "" : String(value);
}

function createIcon(className, label) {
  const icon = document.createElement("i");
  icon.className = className || "icon";
  icon.setAttribute("aria-hidden", "true");
  if (label) {
    icon.setAttribute("title", label);
  }
  return icon;
}

function regionEl(kind, node, content) {
  const span = document.createElement("span");
  span.className = REGION_CLASSES[kind];
  span.setAttribute("data-region", kind);
  if (content) {
    span.append(content);
  }
  return span;
}

function buildLeadingIcon(node) {
  const iconClass = node.icon || ICON_MAP[node.kind] || "fa-solid fa-circle";
  const icon = createIcon(iconClass);
  return icon;
}

function buildLabel(node) {
  const span = document.createElement("span");
  span.className = "tree-row-text";
  span.textContent = text(node, "label");
  return span;
}

function buildStatusIcon(node) {
  const state = node.state || "idle";
  const iconClass = node.stateIcon || STATE_ICONS[state];
  if (!iconClass) {
    return null;
  }
  return createIcon(iconClass, state);
}

function buildCount(node) {
  const unread = node.unread;
  if (unread === null || unread === undefined || unread === 0) {
    return null;
  }
  const span = document.createElement("span");
  span.className = "tree-row-badge tree-row-badge-unread";
  span.textContent = String(unread);
  span.setAttribute("aria-label", `${unread} unread`);
  return span;
}

function buildRegion(kind, node) {
  if (kind === "leading") {
    return regionEl("leading", node, buildLeadingIcon(node));
  }
  if (kind === "label") {
    return regionEl("label", node, buildLabel(node));
  }
  if (kind === "status") {
    const icon = buildStatusIcon(node);
    return icon ? regionEl("status", node, icon) : null;
  }
  if (kind === "count") {
    const badge = buildCount(node);
    return badge ? regionEl("count", node, badge) : null;
  }
  if (kind === "trailing") {
    return regionEl("trailing", node, null);
  }
  return null;
}

/* Render one row for a tree node. regions is the ordered list of region
   names. The row is a <button> with ARIA treeitem attributes. All outside
   text uses textContent; no HTML is parsed. */
export function renderRow(node, regions = DEFAULT_REGIONS) {
  const row = document.createElement("button");
  row.type = "button";
  row.setAttribute("role", "treeitem");
  row.id = node.id ? `tree-node-${node.id}` : "";
  if (node.id) {
    row.setAttribute("data-node-id", node.id);
  }
  if (node.kind) {
    row.setAttribute("data-node-kind", node.kind);
  }
  if (node.parentId) {
    row.setAttribute("data-parent-id", node.parentId);
  }
  row.setAttribute("aria-level", String(node.level || 1));
  const selected = node.selected === true;
  row.setAttribute("aria-selected", selected ? "true" : "false");
  if (node.hasChildren) {
    row.setAttribute("aria-expanded", node.expanded ? "true" : "false");
  }
  if (node.unread && node.unread > 0) {
    row.classList.add("tree-row-unread");
  }
  if (node.state === "syncing") {
    row.setAttribute("aria-busy", "true");
  }
  if (node.disabled === true) {
    row.setAttribute("aria-disabled", "true");
  }

  for (const kind of regions) {
    const region = buildRegion(kind, node);
    if (region) {
      row.append(region);
    }
  }

  return row;
}

/* Render a level of the tree. parent is the <ul> or container for this level.
   nodes are filtered by parentId. Returns a DocumentFragment. */
export function renderLevel(nodes, parentId, regions, depth = 0) {
  const fragment = document.createDocumentFragment();
  const children = nodes.filter((n) => (n.parentId || null) === (parentId || null));
  for (const node of children) {
    const li = document.createElement("li");
    li.setAttribute("data-node-id", node.id);
    if (node.kind) {
      li.setAttribute("data-node-kind", node.kind);
    }
    const computed = {
      ...node,
      level: depth + 1,
    };
    li.append(renderRow(computed, regions));
    if (computed.hasChildren) {
      const childContainer = document.createElement("ul");
      childContainer.setAttribute("role", "group");
      childContainer.setAttribute("aria-label", `${computed.label || node.id} children`);
      if (!computed.expanded) {
        childContainer.setAttribute("hidden", "");
      }
      childContainer.append(renderLevel(nodes, node.id, regions, depth + 1));
      li.append(childContainer);
    }
    fragment.append(li);
  }
  return fragment;
}

/* Create a tree element bound to a container, with placeholder nodes.
   options.regions controls the row region order. Returns { render, update }. */
export function createTree(container, nodes, options = {}) {
  const regions = options.regions || DEFAULT_REGIONS;
  const root = document.createElement("ul");
  root.className = "tree";
  root.setAttribute("role", "tree");
  root.setAttribute("aria-label", options.label || "Navigation");
  let current = Array.isArray(nodes) ? nodes.slice() : [];
  container.append(root);

  function render() {
    root.replaceChildren();
    root.append(renderLevel(current, null, regions, 0));
  }

  render();

  return {
    container: root,
    render,
    update(newNodes) {
      current = Array.isArray(newNodes) ? newNodes.slice() : [];
      render();
    },
    destroy() {
      root.remove();
    },
  };
}

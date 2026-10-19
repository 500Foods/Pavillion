/* Tree renderer module. Renders nodes as a static tree with configurable
   row regions. Outside data (labels, counts) is set via textContent, never
   innerHTML. The renderer owns DOM only; it does not fetch nodes. */

export const DEFAULT_REGIONS = ["leading", "label", "status", "count", "trailing"];

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

const DISCLOSURE_ICON = "fa-sharp-duotone fa-solid fa-chevron-right";

const SWATCH_MAP = {
  1: "tree-row-swatch tree-row-swatch-1",
  2: "tree-row-swatch tree-row-swatch-2",
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
  return createIcon(iconClass);
}

function buildDisclosure(node) {
  if (!node.hasChildren) {
    return null;
  }
  const icon = createIcon(DISCLOSURE_ICON, "Expand");
  icon.classList.add("tree-row-disclosure");
  return icon;
}

function buildSwatch(node) {
  if (!node.swatch) {
    return null;
  }
  const className = SWATCH_MAP[node.swatch];
  if (!className) {
    return null;
  }
  const span = document.createElement("span");
  span.className = className;
  span.setAttribute("aria-hidden", "true");
  return span;
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

/* Unread count badge (primary). Always visible when nonzero. */
function buildUnreadBadge(node) {
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

/* Total or recursive size badge (secondary). Shown on hover via data attribute
   so CSS toggles visibility, never replacing the unread badge. */
function buildTotalBadge(node) {
  const total = node.total;
  if (total === null || total === undefined || total === 0) {
    return null;
  }
  const span = document.createElement("span");
  span.className = "tree-row-badge tree-row-badge-total";
  span.textContent = String(total);
  span.setAttribute("aria-label", `${total} total`);
  return span;
}

function buildSizeBadge(node) {
  const size = node.sizeBytes;
  if (size === null || size === undefined || size === "" || size === 0) {
    return null;
  }
  const span = document.createElement("span");
  span.className = "tree-row-badge tree-row-badge-size";
  span.textContent = formatSize(size);
  span.setAttribute("aria-label", formatSizeFull(size, node.sizeLabel === "recursive"));
  return span;
}

function formatSize(bytes) {
  const n = Number(bytes);
  if (!Number.isFinite(n) || n <= 0) {
    return "";
  }
  if (n >= 1073741824) {
    return `${(n / 1073741824).toFixed(1)} GB`;
  }
  if (n >= 1048576) {
    return `${(n / 1048576).toFixed(1)} MB`;
  }
  if (n >= 1024) {
    return `${(n / 1024).toFixed(1)} KB`;
  }
  return `${n} B`;
}

function formatSizeFull(bytes, recursive) {
  const label = recursive ? "recursive size" : "size";
  return `${formatSize(bytes)} ${label}`;
}

/* Hover metadata region: holds total / size badges that swap in on hover.
   These are secondary and must not replace the unread badge. */
function buildHoverRegion(node) {
  const total = buildTotalBadge(node);
  const size = buildSizeBadge(node);
  if (!total && !size) {
    return null;
  }
  const span = document.createElement("span");
  span.className = "tree-row-hover";
  span.setAttribute("data-region", "hover");
  if (total) {
    span.append(total);
  }
  if (size) {
    span.append(size);
  }
  return span;
}

function buildRegion(kind, node) {
  if (kind === "leading") {
    const icon = buildLeadingIcon(node);
    const swatch = buildSwatch(node);
    const content = document.createDocumentFragment();
    if (swatch) {
      content.append(swatch);
    }
    if (node.hasChildren) {
      content.append(buildDisclosure(node));
    }
    content.append(icon);
    return regionEl("leading", node, content);
  }
  if (kind === "label") {
    return regionEl("label", node, buildLabel(node));
  }
  if (kind === "status") {
    const icon = buildStatusIcon(node);
    return icon ? regionEl("status", node, icon) : null;
  }
  if (kind === "count") {
    const badge = buildUnreadBadge(node);
    return badge ? regionEl("count", node, badge) : null;
  }
  if (kind === "hover") {
    const meta = buildHoverRegion(node);
    return meta ? regionEl("hover", node, meta) : null;
  }
  if (kind === "trailing") {
    return regionEl("trailing", node, null);
  }
  return null;
}

/* Patch a single rendered row to update badges/metadata without a full
   re-render. row is the DOM <button role="treeitem">. Returns true if the
   row was patched. */
export function patchRow(node, row) {
  if (!(row instanceof Element)) {
    return false;
  }
  const countRegion = row.querySelector("[data-region='count']");
  const hoverRegion = row.querySelector("[data-region='hover']");

  /* Count badge: unread only. */
  const unread = node.unread;
  if (countRegion) {
    if (unread && unread > 0) {
      let badge = countRegion.querySelector(".tree-row-badge-unread");
      if (!badge) {
        badge = buildUnreadBadge(node);
        countRegion.replaceChildren(badge);
      } else {
        badge.textContent = String(unread);
        badge.setAttribute("aria-label", `${unread} unread`);
      }
    } else if (!countRegion.querySelector(".tree-row-badge-unread")) {
      /* No unread badge and none to show — keep region empty. */
    }
  }

  /* Hover region: total / size. */
  const total = node.total;
  const size = node.sizeBytes;
  const hasMeta = (total && total > 0) || (size && size > 0);
  if (hoverRegion) {
    if (hasMeta) {
      let totalBadge = hoverRegion.querySelector(".tree-row-badge-total");
      if (total && total > 0) {
        if (!totalBadge) {
          totalBadge = buildTotalBadge(node);
          hoverRegion.append(totalBadge);
        } else {
          totalBadge.textContent = String(total);
          totalBadge.setAttribute("aria-label", `${total} total`);
        }
      } else if (totalBadge) {
        totalBadge.remove();
      }
      let sizeBadge = hoverRegion.querySelector(".tree-row-badge-size");
      if (size && size > 0) {
        if (!sizeBadge) {
          sizeBadge = buildSizeBadge(node);
          hoverRegion.append(sizeBadge);
        } else {
          sizeBadge.textContent = formatSize(size);
          sizeBadge.setAttribute("aria-label", formatSizeFull(size, node.sizeLabel === "recursive"));
        }
      } else if (sizeBadge) {
        sizeBadge.remove();
      }
    }
  } else if (hasMeta) {
    const hover = buildHoverRegion(node);
    if (hover) {
      row.append(hover);
    }
  }

  /* State classes. */
  const wasUnread = row.classList.contains("tree-row-unread");
  const isUnread = Boolean(unread && unread > 0);
  if (isUnread && !wasUnread) {
    row.classList.add("tree-row-unread");
  } else if (!isUnread && wasUnread) {
    row.classList.remove("tree-row-unread");
  }

  const wasFavorite = row.classList.contains("tree-row-favorite");
  if (node.favorite) {
    if (!wasFavorite) {
      row.classList.add("tree-row-favorite");
    }
  } else if (wasFavorite) {
    row.classList.remove("tree-row-favorite");
  }

  const wasError = row.classList.contains("tree-row-error");
  if (node.state === "error") {
    if (!wasError) {
      row.classList.add("tree-row-error");
    }
  } else if (wasError && node.state !== "error") {
    row.classList.remove("tree-row-error");
  }

  if (node.hasChildren) {
    const wasExpanded = row.classList.contains("tree-row-expanded");
    if (node.expanded === true && !wasExpanded) {
      row.classList.add("tree-row-expanded");
    } else if (node.expanded === false && wasExpanded) {
      row.classList.remove("tree-row-expanded");
    }
  }

  return true;
}

/* Render one row for a tree node. regions is the ordered list of region
   names. The row is a <button> with ARIA treeitem attributes. All outside
   data uses textContent; no HTML is parsed. */
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
    if (node.expanded) {
      row.classList.add("tree-row-expanded");
    }
  }
  if (node.unread && node.unread > 0) {
    row.classList.add("tree-row-unread");
  }
  if (node.favorite) {
    row.classList.add("tree-row-favorite");
  }
  if (node.state === "error") {
    row.classList.add("tree-row-error");
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
      childContainer.classList.add("tree-group");
      if (!computed.expanded) {
        childContainer.classList.add("tree-group-collapsed");
      } else {
        childContainer.classList.add("tree-group-expanded");
      }
      childContainer.append(renderLevel(nodes, node.id, regions, depth + 1));
      li.append(childContainer);
    }
    fragment.append(li);
  }
  return fragment;
}

/* Create a tree element bound to a container, with placeholder nodes.
   options.regions controls the row region order. Returns { render, update,
   patchRow }. */
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



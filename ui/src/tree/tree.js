import { createModel } from "./model.js";
import { createViewState } from "./state.js";
import { renderRow, DEFAULT_REGIONS, patchRow } from "./render.js";

const DEFAULT_REGIONS_LOCAL = DEFAULT_REGIONS;

function renderNode(node, regions, viewState, model) {
  const computed = { ...node };
  computed.level = model.depth(node.id);
  computed.expanded = viewState.isExpanded(node.id);
  const isSelected = viewState.selected === node.id;
  computed.selected = isSelected;
  if (viewState.isFavorite(node.id)) {
    computed.favorite = true;
  }
  const row = renderRow(computed, regions);
  if (isSelected) {
    row.setAttribute("aria-current", "true");
  }
  return row;
}

function renderGroup(node, regions, viewState, model) {
  const fragment = document.createDocumentFragment();
  const li = document.createElement("li");
  li.setAttribute("data-node-id", node.id);
  if (node.kind) {
    li.setAttribute("data-node-kind", node.kind);
  }
  const row = renderNode(node, regions, viewState, model);
  li.append(row);
  if (node.hasChildren) {
    const childContainer = document.createElement("ul");
    childContainer.setAttribute("role", "group");
    childContainer.setAttribute("aria-label", `${node.label || node.id} children`);
    childContainer.classList.add("tree-group");
    const isExpanded = viewState.isExpanded(node.id);
    childContainer.classList.add(isExpanded ? "tree-group-expanded" : "tree-group-collapsed");
    const children = model.children(node.id);
    for (const child of children) {
      childContainer.append(renderGroup(child, regions, viewState, model));
    }
    li.append(childContainer);
  }
  fragment.append(li);
  return fragment;
}

function rootVisible(node, viewState) {
  if (!viewState.isRootVisible(node.id)) {
    return false;
  }
  if (viewState.showFavorites && !viewState.isFavorite(node.id)) {
    return false;
  }
  if (viewState.showHeart && !viewState.isHeart(node.id)) {
    return false;
  }
  if (viewState.hideInfrequent && viewState.isInfrequent(node.id)) {
    return false;
  }
  return true;
}

function renderTree(root, viewState, model, regions) {
  root.replaceChildren();
  const roots = model.rootIds();
  for (const rootId of roots) {
    const node = model.get(rootId);
    if (!node) {
      continue;
    }
    if (!rootVisible(node, viewState)) {
      continue;
    }
    root.append(renderGroup(node, regions, viewState, model));
  }
}

export function createTreeController(container, options = {}) {
  const regions = options.regions || DEFAULT_REGIONS_LOCAL;
  const treeId = options.id || "default";
  const label = options.label || "Navigation";
  const model = createModel(options.nodes || []);
  const viewState = createViewState(options.state || {});
  viewState.setDescendantsProvider((id) => model.descendants(id));

  const root = document.createElement("ul");
  root.className = "tree";
  root.setAttribute("role", "tree");
  root.setAttribute("aria-label", label);
  container.append(root);

  let selectionCallbacks = [];
  let expandCallbacks = [];
  let persisted = false;
  let persistFn = options.onPersist || null;
  let rowMenuProvider = null;

  function emit(callbacks, ...args) {
    for (const cb of callbacks) {
      cb(...args);
    }
  }

  function select(id) {
    const prev = viewState.select(id);
    renderTree(root, viewState, model, regions);
    emit(selectionCallbacks, id, prev);
    if (persistFn) {
      persistFn(treeId, { expanded: [...viewState.expanded], selected: viewState.selected, focused: viewState.focused });
    }
    return prev;
  }

  function focus(id) {
    if (viewState.focused !== id) {
      viewState.focus(id);
      renderTree(root, viewState, model, regions);
    }
  }

  function toggleExpand(id) {
    const now = viewState.toggle(id);
    syncGroupContainer(id);
    syncRowExpanded(id);
    emit(expandCallbacks, id, now);
    if (persistFn) {
      persistFn(treeId, { expanded: [...viewState.expanded] });
    }
    return now;
  }

  function expand(id) {
    const changed = viewState.expand(id);
    if (changed) {
      syncGroupContainer(id);
      syncRowExpanded(id);
      emit(expandCallbacks, id, true);
      if (persistFn) {
        persistFn(treeId, { expanded: [...viewState.expanded] });
      }
    }
    return changed;
  }

  function collapse(id) {
    const changed = viewState.collapse(id);
    if (changed) {
      syncGroupContainer(id);
      syncRowExpanded(id);
      emit(expandCallbacks, id, false);
      if (persistFn) {
        persistFn(treeId, { expanded: [...viewState.expanded] });
      }
    }
    return changed;
  }

  function patch(newNodes) {
    model.patch(newNodes);
    renderTree(root, viewState, model, regions);
  }

  function remove(ids) {
    const removed = model.remove(ids);
    renderTree(root, viewState, model, regions);
    return removed;
  }

  function collapseAllReset() {
    viewState.collapseAllReset();
    for (const id of model.toArray().map((n) => n.id)) {
      if (findGroup(id)) {
        syncGroupContainer(id);
        syncRowExpanded(id);
      }
    }
    emit(expandCallbacks, null, false);
    if (persistFn) {
      persistFn(treeId, { expanded: [], selected: viewState.selected, focused: viewState.focused });
    }
  }

  function collapseAllRemember() {
    const snapshot = viewState.collapseAllRemember();
    for (const id of model.toArray().map((n) => n.id)) {
      if (findGroup(id)) {
        syncGroupContainer(id);
        syncRowExpanded(id);
      }
    }
    if (persistFn) {
      persistFn(treeId, { expanded: [] });
    }
    return snapshot;
  }

  function restoreAll(snapshot) {
    viewState.restoreAll(snapshot);
    for (const id of model.toArray().map((n) => n.id)) {
      if (findGroup(id)) {
        syncGroupContainer(id);
        syncRowExpanded(id);
      }
    }
    if (persistFn) {
      persistFn(treeId, { expanded: [...snapshot] });
    }
  }

  function collapseBranch(id) {
    viewState.collapseBranch(id);
    syncGroupContainer(id);
    syncRowExpanded(id);
    if (persistFn) {
      persistFn(treeId, { expanded: [...viewState.expanded] });
    }
  }

  function findRow(id) {
    const safeId = String(id).replace(/"/g, '\\"');
    const row = root.querySelector(`[data-node-id="${safeId}"][role="treeitem"]`);
    if (row) {
      return row;
    }
    return null;
  }

  function findGroup(id) {
    const row = findRow(id);
    if (!row) {
      return null;
    }
    const group = row.parentElement.querySelector('ul[role="group"]');
    if (group) {
      return group;
    }
    return null;
  }

  function syncGroupContainer(id) {
    const group = findGroup(id);
    if (!group) {
      return;
    }
    if (viewState.isExpanded(id)) {
      group.classList.remove("tree-group-collapsed");
      group.classList.add("tree-group-expanded");
    } else {
      group.classList.remove("tree-group-expanded");
      group.classList.add("tree-group-collapsed");
    }
  }

  function syncRowExpanded(id) {
    const row = findRow(id);
    if (!row) {
      return;
    }
    if (viewState.isExpanded(id)) {
      row.classList.add("tree-row-expanded");
    } else {
      row.classList.remove("tree-row-expanded");
    }
  }

  /* Re-render the full tree. Used after view-state-only changes that affect
     visibility (e.g. filter toggles). */
  function renderTreeFn() {
    renderTree(root, viewState, model, regions);
  }

  function patchNode(id, updates) {
    const node = model.patch([updates.id ? updates : { id, ...updates }]);
    const row = findRow(id);
    if (!row) {
      return false;
    }
    const computed = { ...model.get(id) };
    computed.level = model.depth(id);
    computed.expanded = viewState.isExpanded(id);
    computed.selected = viewState.selected === id;
    return patchRow(computed, row);
  }

  function getRow(id) {
    return findRow(id);
  }

  function setRowMenu(getItems) {
    rowMenuProvider = typeof getItems === "function" ? getItems : null;
    return () => {
      rowMenuProvider = null;
    };
  }

  function getNode(id) {
    return model.get(id);
  }

  function getRootIds() {
    return model.rootIds();
  }

  function isExpanded(id) {
    return viewState.isExpanded(id);
  }

  function isSelected(id) {
    return viewState.selected === id;
  }

  function isRootVisible(id) {
    return viewState.isRootVisible(id);
  }

  renderTree(root, viewState, model, regions);

  return {
    container: root,
    model,
    viewState,
    select,
    focus,
    toggleExpand,
    expand,
    collapse,
    patch,
    patchNode,
    remove,
    collapseAllReset,
    collapseAllRemember,
    restoreAll,
    collapseBranch,
    getNode,
    getRootIds,
    getRow,
    render: renderTreeFn,
    isExpanded,
    isSelected,
    isRootVisible,
    onSelect(cb) {
      selectionCallbacks.push(cb);
      return () => {
        const idx = selectionCallbacks.indexOf(cb);
        if (idx >= 0) {
          selectionCallbacks.splice(idx, 1);
        }
      };
    },
    onExpand(cb) {
      expandCallbacks.push(cb);
      return () => {
        const idx = expandCallbacks.indexOf(cb);
        if (idx >= 0) {
          expandCallbacks.splice(idx, 1);
        }
      };
    },
    setRowMenu,
    destroy() {
      root.remove();
      selectionCallbacks = [];
      expandCallbacks = [];
      rowMenuProvider = null;
    },
    serialize() {
      return viewState.serialize();
    },
  };
}

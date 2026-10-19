/**
 * @vitest-environment jsdom
 */
import { expect, test } from "vitest";
import { createTreeController } from "./tree.js";
import { renderRow, patchRow } from "./render.js";

function withChildren(node) {
  return { ...node, hasChildren: true, expanded: false };
}

function sampleNodes() {
  return [
    { id: "inbox", parentId: null, kind: "mailbox", label: "Inbox", role: "inbox", unread: 3, total: 47, sizeBytes: 20971520, sizeLabel: "recursive", selected: true, hasChildren: false, swatch: 2 },
    { id: "drafts", parentId: null, kind: "mailbox", label: "Drafts", role: "drafts", unread: 0, total: 2, hasChildren: false, swatch: 1 },
    { id: "archive", parentId: null, kind: "mailbox", label: "Archive", role: "archive", unread: 12, total: 2481, sizeBytes: 1932735283, sizeLabel: "recursive", hasChildren: false, swatch: 2 },
    { id: "projects", parentId: null, kind: "folder", label: "Projects", role: null, unread: 0, hasChildren: true, expanded: true },
    { id: "pavilion", parentId: "projects", kind: "folder", label: "Pavilion", role: null, unread: 0, hasChildren: true, selected: false },
    { id: "hydrogen", parentId: "pavilion", kind: "folder", label: "Hydrogen", role: null, unread: 2, hasChildren: false },
  ];
}

test("renderRow adds tree-row-favorite class when favorite is true", () => {
  const node = { id: "f1", kind: "folder", label: "Favorites", selected: false, hasChildren: false, favorite: true, level: 1 };
  const row = renderRow(node, ["label"]);
  expect(row.classList.contains("tree-row-favorite")).toBe(true);
});

test("renderRow does not add tree-row-favorite when favorite is false", () => {
  const node = { id: "f1", kind: "folder", label: "Folder", selected: false, hasChildren: false, favorite: false, level: 1 };
  const row = renderRow(node, ["label"]);
  expect(row.classList.contains("tree-row-favorite")).toBe(false);
});

test("renderRow adds tree-row-error class when state is error", () => {
  const node = { id: "f1", kind: "mailbox", label: "Broken", selected: false, hasChildren: false, state: "error", level: 1 };
  const row = renderRow(node, ["label"]);
  expect(row.classList.contains("tree-row-error")).toBe(true);
});

test("renderRow adds tree-row-unread class for unread > 0", () => {
  const node = { id: "inbox", kind: "mailbox", label: "Inbox", selected: false, hasChildren: false, unread: 3, level: 1 };
  const row = renderRow(node, ["label"]);
  expect(row.classList.contains("tree-row-unread")).toBe(true);
});

test("renderRow renders a swatch element in the leading region when swatch is set", () => {
  const node = { id: "inbox", kind: "mailbox", label: "Inbox", selected: false, hasChildren: false, swatch: 2, level: 1 };
  const row = renderRow(node, ["leading", "label"]);
  const swatch = row.querySelector(".tree-row-swatch");
  expect(swatch).not.toBeNull();
  expect(swatch.classList.contains("tree-row-swatch-2")).toBe(true);
});

test("renderRow omits swatch when not set", () => {
  const node = { id: "inbox", kind: "mailbox", label: "Inbox", selected: false, hasChildren: false, level: 1 };
  const row = renderRow(node, ["leading", "label"]);
  expect(row.querySelector(".tree-row-swatch")).toBeNull();
});

test("renderRow renders hover region with total badge", () => {
  const node = { id: "inbox", kind: "mailbox", label: "Inbox", selected: false, hasChildren: false, total: 47, level: 1 };
  const row = renderRow(node, ["leading", "label", "count", "hover"]);
  const hoverRegion = row.querySelector("[data-region='hover']");
  expect(hoverRegion).not.toBeNull();
  const totalBadge = hoverRegion.querySelector(".tree-row-badge-total");
  expect(totalBadge).not.toBeNull();
  expect(totalBadge.textContent).toBe("47");
});

test("renderRow renders hover region with size badge", () => {
  const node = { id: "archive", kind: "mailbox", label: "Archive", selected: false, hasChildren: false, sizeBytes: 1932735283, level: 1 };
  const row = renderRow(node, ["hover"]);
  const sizeBadge = row.querySelector(".tree-row-badge-size");
  expect(sizeBadge).not.toBeNull();
  expect(sizeBadge.textContent).toBe("1.8 GB");
});

test("renderRow omits hover region when no total or size", () => {
  const node = { id: "drafts", kind: "mailbox", label: "Drafts", selected: false, hasChildren: false, level: 1 };
  const row = renderRow(node, ["hover"]);
  expect(row.querySelector("[data-region='hover']")).toBeNull();
});

test("renderRow keeps unread badge separate from hover badge", () => {
  const node = { id: "inbox", kind: "mailbox", label: "Inbox", selected: false, hasChildren: false, unread: 3, total: 47, level: 1 };
  const row = renderRow(node, ["count", "hover"]);
  const unreadBadge = row.querySelector(".tree-row-badge-unread");
  const totalBadge = row.querySelector(".tree-row-badge-total");
  expect(unreadBadge).not.toBeNull();
  expect(unreadBadge.textContent).toBe("3");
  expect(totalBadge).not.toBeNull();
  expect(totalBadge.textContent).toBe("47");
});

test("patchRow updates unread badge text content", () => {
  const node = { id: "inbox", kind: "mailbox", label: "Inbox", selected: false, hasChildren: false, unread: 3, level: 1 };
  const row = renderRow(node, ["count", "hover"]);

  patchRow({ id: "inbox", unread: 10 }, row);

  const badge = row.querySelector(".tree-row-badge-unread");
  expect(badge.textContent).toBe("10");
  expect(row.classList.contains("tree-row-unread")).toBe(true);
});

test("patchRow removes unread class when count goes to zero", () => {
  const node = { id: "inbox", kind: "mailbox", label: "Inbox", selected: false, hasChildren: false, unread: 5, level: 1 };
  const row = renderRow(node, ["count"]);
  expect(row.classList.contains("tree-row-unread")).toBe(true);

  patchRow({ id: "inbox", unread: 0 }, row);

  expect(row.classList.contains("tree-row-unread")).toBe(false);
});

test("patchRow updates favorite class", () => {
  const node = { id: "f1", kind: "folder", label: "Folder", selected: false, hasChildren: false, level: 1 };
  const row = renderRow(node, ["label"]);
  expect(row.classList.contains("tree-row-favorite")).toBe(false);

  patchRow({ id: "f1", favorite: true }, row);
  expect(row.classList.contains("tree-row-favorite")).toBe(true);

  patchRow({ id: "f1", favorite: false }, row);
  expect(row.classList.contains("tree-row-favorite")).toBe(false);
});

test("patchRow updates total badge in hover region", () => {
  const node = { id: "inbox", kind: "mailbox", label: "Inbox", selected: false, hasChildren: false, total: 47, level: 1 };
  const row = renderRow(node, ["count", "hover"]);

  patchRow({ id: "inbox", total: 100 }, row);

  const totalBadge = row.querySelector(".tree-row-badge-total");
  expect(totalBadge.textContent).toBe("100");
});

test("patchRow adds hover region when total appears on a row without one", () => {
  const node = { id: "inbox", kind: "mailbox", label: "Inbox", selected: false, hasChildren: false, level: 1 };
  const row = renderRow(node, ["count"]);

  const patched = patchRow({ id: "inbox", total: 5 }, row);
  expect(patched).toBe(true);

  const totalBadge = row.querySelector(".tree-row-badge-total");
  expect(totalBadge).not.toBeNull();
  expect(totalBadge.textContent).toBe("5");
});

test("patchRow does not throw on null row", () => {
  expect(patchRow({ id: "x", unread: 5 }, null)).toBe(false);
});

test("controller patchNode updates a single row without re-rendering siblings", () => {
  const container = document.createElement("div");
  const controller = createTreeController(container, {
    id: "test",
    nodes: sampleNodes(),
    regions: ["leading", "label", "count", "hover"],
  });

  const inboxRow = controller.getRow("inbox");
  const draftRow = controller.getRow("drafts");

  expect(inboxRow).not.toBeNull();
  expect(draftRow).not.toBeNull();

  inboxRow.setAttribute("data-patch-marker", "before");

  controller.patchNode("inbox", { unread: 99 });

  const badge = inboxRow.querySelector(".tree-row-badge-unread");
  expect(badge.textContent).toBe("99");
  expect(inboxRow.getAttribute("data-patch-marker")).toBe("before");

  expect(draftRow.getAttribute("data-patch-marker")).toBeNull();

  controller.destroy();
});

test("controller patchNode updates favorite class on the row", () => {
  const container = document.createElement("div");
  const controller = createTreeController(container, {
    id: "test",
    nodes: sampleNodes(),
    regions: ["leading", "label"],
  });

  expect(controller.getRow("drafts").classList.contains("tree-row-favorite")).toBe(false);

  controller.patchNode("drafts", { favorite: true });

  expect(controller.getRow("drafts").classList.contains("tree-row-favorite")).toBe(true);

  controller.destroy();
});

test("controller render method re-renders the full tree", () => {
  const container = document.createElement("div");
  const nodes = sampleNodes();
  nodes.find((n) => n.id === "archive").favorite = true;
  const controller = createTreeController(container, {
    id: "test",
    nodes,
    state: { expanded: ["projects"], favorites: ["archive"] },
    regions: ["leading", "label", "count"],
  });

  const beforeCount = container.querySelectorAll('[role="treeitem"]').length;

  controller.viewState.setShowFavorites(true);
  controller.render();

  const afterCount = container.querySelectorAll('[role="treeitem"]').length;
  expect(beforeCount).toBe(5);
  expect(afterCount).toBe(1);

  controller.viewState.setShowFavorites(false);
  controller.render();

  const finalCount = container.querySelectorAll('[role="treeitem"]').length;
  expect(finalCount).toBe(5);

  controller.destroy();
});

test("controller setRowMenu stores a menu provider", () => {
  const container = document.createElement("div");
  const controller = createTreeController(container, {
    id: "test",
    nodes: sampleNodes(),
    regions: ["leading", "label"],
  });

  const items = [{ label: "Test", onSelect() {} }];
  const stop = controller.setRowMenu(() => items);

  controller.destroy();
  expect(typeof stop).toBe("function");
});

test("controller getRow returns the row element for a node id", () => {
  const container = document.createElement("div");
  const controller = createTreeController(container, {
    id: "test",
    nodes: sampleNodes(),
    regions: ["leading", "label"],
  });

  const row = controller.getRow("inbox");
  expect(row).not.toBeNull();
  expect(row.getAttribute("data-node-id")).toBe("inbox");
  expect(row.getAttribute("role")).toBe("treeitem");

  controller.destroy();
});
/**
 * @vitest-environment jsdom
 */
import { expect, test } from "vitest";
import { createTree, renderRow } from "./render.js";

function placeholderNodes() {
  return [
    { id: "inbox", parentId: null, kind: "mailbox", label: "Inbox", role: "inbox", unread: 3, selected: true, hasChildren: false },
    { id: "drafts", parentId: null, kind: "mailbox", label: "Drafts", role: "drafts", unread: 0, selected: false, hasChildren: false },
    { id: "projects", parentId: null, kind: "folder", label: "Projects", role: null, unread: 0, selected: false, hasChildren: true, expanded: true },
    { id: "pavilion", parentId: "projects", kind: "folder", label: "Pavilion", role: null, unread: 0, selected: false, hasChildren: true, expanded: false },
    { id: "hydrogen", parentId: "pavilion", kind: "folder", label: "Hydrogen", role: null, unread: 2, selected: false, hasChildren: false },
    { id: "archive", parentId: null, kind: "mailbox", label: "Archive", role: "archive", unread: 12, selected: false, hasChildren: false },
  ];
}

test("renderRow sets ARIA treeitem attributes from the node", () => {
  const node = { id: "inbox", kind: "mailbox", label: "Inbox", parentId: null, selected: true, hasChildren: false, unread: 3, level: 1 };
  const row = renderRow(node, ["leading", "label", "count"]);

  expect(row.getAttribute("role")).toBe("treeitem");
  expect(row.getAttribute("aria-level")).toBe("1");
  expect(row.getAttribute("aria-selected")).toBe("true");
  expect(row.getAttribute("aria-expanded")).toBeNull();
  expect(row.getAttribute("data-node-id")).toBe("inbox");
  expect(row.getAttribute("data-node-kind")).toBe("mailbox");
  expect(row.getAttribute("data-parent-id")).toBeNull();
});

test("renderRow shows aria-expanded only for nodes with children", () => {
  const parent = { id: "projects", kind: "folder", label: "Projects", selected: false, hasChildren: true, expanded: true, level: 1 };
  const row = renderRow(parent, ["label"]);
  expect(row.getAttribute("aria-expanded")).toBe("true");

  const collapsed = { id: "projects", kind: "folder", label: "Projects", selected: false, hasChildren: true, expanded: false, level: 1 };
  const row2 = renderRow(collapsed, ["label"]);
  expect(row2.getAttribute("aria-expanded")).toBe("false");

  const leaf = { id: "inbox", kind: "mailbox", label: "Inbox", selected: false, hasChildren: false, level: 1 };
  const row3 = renderRow(leaf, ["label"]);
  expect(row3.getAttribute("aria-expanded")).toBeNull();
});

test("renderRow marks syncing nodes as aria-busy", () => {
  const node = { id: "inbox", kind: "mailbox", label: "Inbox", selected: false, hasChildren: false, state: "syncing", level: 1 };
  const row = renderRow(node, ["label"]);
  expect(row.getAttribute("aria-busy")).toBe("true");
});

test("renderRow marks unread rows with the unread class", () => {
  const node = { id: "inbox", kind: "mailbox", label: "Inbox", selected: false, hasChildren: false, unread: 3, level: 1 };
  const row = renderRow(node, ["label"]);
  expect(row.classList.contains("tree-row-unread")).toBe(true);
});

test("renderRow uses textContent for all outside data", () => {
  const node = { id: "inbox", kind: "mailbox", label: "Inbox <script>alert(1)</script>", selected: false, hasChildren: false, unread: 3, level: 1 };
  const row = renderRow(node, ["leading", "label", "count"]);
  const labelSpan = row.querySelector(".tree-row-text");
  expect(labelSpan.textContent).toBe("Inbox <script>alert(1)</script>");
  expect(labelSpan.innerHTML).not.toContain("<script");
  const badge = row.querySelector(".tree-row-badge");
  expect(badge.textContent).toBe("3");
  expect(badge.innerHTML).toBe("3");
});

test("renderRow renders regions in the configured order", () => {
  const node = { id: "inbox", kind: "mailbox", label: "Inbox", selected: false, hasChildren: false, unread: 3, level: 1 };
  const regions = ["count", "label", "leading"];
  const row = renderRow(node, regions);
  const regionOrder = [...row.querySelectorAll("[data-region]")].map((el) => el.getAttribute("data-region"));
  expect(regionOrder).toEqual(["count", "label", "leading"]);
});

test("renderRow skips count region when unread is zero", () => {
  const node = { id: "drafts", kind: "mailbox", label: "Drafts", selected: false, hasChildren: false, unread: 0, level: 1 };
  const row = renderRow(node, ["leading", "label", "count"]);
  expect(row.querySelector("[data-region='count']")).toBeNull();
});

test("renderRow skips status region when state is idle", () => {
  const node = { id: "inbox", kind: "mailbox", label: "Inbox", selected: false, hasChildren: false, state: "idle", level: 1 };
  const row = renderRow(node, ["status"]);
  expect(row.querySelector("[data-region='status']")).toBeNull();
});

test("createTree builds a tree list with role tree", () => {
  const container = document.createElement("div");
  const tree = createTree(container, placeholderNodes(), { label: "Mail", regions: ["leading", "label", "count"] });

  expect(tree.container.getAttribute("role")).toBe("tree");
  expect(tree.container.getAttribute("aria-label")).toBe("Mail");
  expect(tree.container.className).toContain("tree");
});

test("createTree renders root nodes with correct hierarchy", () => {
  const container = document.createElement("div");
  const nodes = [
    { id: "projects", parentId: null, kind: "folder", label: "Projects", hasChildren: true, expanded: true, level: 1 },
    { id: "child1", parentId: "projects", kind: "folder", label: "Child 1", hasChildren: false, level: 2 },
    { id: "inbox", parentId: null, kind: "mailbox", label: "Inbox", hasChildren: false, level: 1 },
  ];
  createTree(container, nodes, { label: "Test", regions: ["label"] });

  const items = container.querySelectorAll('[role="treeitem"]');
  expect(items).toHaveLength(3);
  const inboxRow = container.querySelector('[data-node-id="inbox"]');
  expect(inboxRow).not.toBeNull();
  const projectsRow = container.querySelector('[data-node-id="projects"] [role="treeitem"]');
  expect(projectsRow).not.toBeNull();
  expect(projectsRow.getAttribute("aria-expanded")).toBe("true");
});

test("createTree hides collapsed children groups", () => {
  const container = document.createElement("div");
  const nodes = [
    { id: "parent", parentId: null, kind: "folder", label: "Parent", hasChildren: true, expanded: false, level: 1 },
    { id: "child", parentId: "parent", kind: "folder", label: "Child", hasChildren: false, level: 2 },
  ];
  createTree(container, nodes, { label: "Test", regions: ["label"] });

  const group = container.querySelector('[role="group"]');
  expect(group).not.toBeNull();
  expect(group.classList.contains("tree-group-collapsed")).toBe(true);
  expect(group.classList.contains("tree-group-expanded")).toBe(false);
});

test("createTree shows expanded children groups", () => {
  const container = document.createElement("div");
  const nodes = [
    { id: "parent", parentId: null, kind: "folder", label: "Parent", hasChildren: true, expanded: true, level: 1 },
    { id: "child", parentId: "parent", kind: "folder", label: "Child", hasChildren: false, level: 2 },
  ];
  createTree(container, nodes, { label: "Test", regions: ["label"] });

  const group = container.querySelector('[role="group"]');
  expect(group).not.toBeNull();
  expect(group.classList.contains("tree-group-expanded")).toBe(true);
  expect(group.classList.contains("tree-group-collapsed")).toBe(false);
});

test("createTree renders placeholder nodes without HTML sink", () => {
  const container = document.createElement("div");
  const dangerous = "Inbox <script>alert(1)</script>";
  const nodes = [
    { id: "inbox", parentId: null, kind: "mailbox", label: dangerous, hasChildren: false, level: 1 },
  ];
  createTree(container, nodes, { label: "Test", regions: ["leading", "label"] });

  const labelSpan = container.querySelector(".tree-row-text");
  expect(labelSpan.textContent).toBe(dangerous);
  expect(labelSpan.innerHTML).not.toContain("<script");
  const rows = container.querySelectorAll('[role="treeitem"]');
  for (const row of rows) {
    expect(row.innerHTML).not.toContain("<script>");
    expect(row.innerHTML).not.toContain("javascript:");
  }
});

test("createTree container has role tree and accessible name", () => {
  const container = document.createElement("div");
  createTree(container, placeholderNodes(), { label: "Mail folders", regions: ["label"] });
  expect(container.querySelector('[role="tree"]').getAttribute("aria-label")).toBe("Mail folders");
});

test("createTree update replaces all nodes", () => {
  const container = document.createElement("div");
  const tree = createTree(container, placeholderNodes(), { label: "Test", regions: ["label"] });

  const initialCount = container.querySelectorAll('[role="treeitem"]').length;
  tree.update([
    { id: "only", parentId: null, kind: "mailbox", label: "Only", hasChildren: false, level: 1 },
  ]);
  const afterCount = container.querySelectorAll('[role="treeitem"]').length;
  expect(initialCount).toBe(6);
  expect(afterCount).toBe(1);
  expect(container.querySelector('[data-node-id="only"]')).not.toBeNull();
});

test("createTree destroy removes the tree from the DOM", () => {
  const container = document.createElement("div");
  const tree = createTree(container, placeholderNodes(), { label: "Test", regions: ["label"] });
  const root = tree.container;
  expect(container.contains(root)).toBe(true);
  tree.destroy();
  expect(container.contains(root)).toBe(false);
});

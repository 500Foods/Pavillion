/**
 * @vitest-environment jsdom
 */
import { expect, test, vi } from "vitest";
import { createTreeController } from "./tree.js";
import { createModel } from "./model.js";
import { createViewState } from "./state.js";

function sampleNodes() {
  return [
    { id: "inbox", parentId: null, kind: "mailbox", label: "Inbox", role: "inbox", unread: 3, hasChildren: false, order: 1 },
    { id: "drafts", parentId: null, kind: "mailbox", label: "Drafts", role: "drafts", unread: 0, hasChildren: false, order: 2 },
    { id: "projects", parentId: null, kind: "folder", label: "Projects", hasChildren: true, order: 3 },
    { id: "pavilion", parentId: "projects", kind: "folder", label: "Pavilion", hasChildren: true, order: 1 },
    { id: "hydrogen", parentId: "pavilion", kind: "folder", label: "Hydrogen", hasChildren: false, order: 1, unread: 2 },
    { id: "archive", parentId: null, kind: "mailbox", label: "Archive", role: "archive", unread: 12, hasChildren: false, order: 4 },
  ];
}

test("tree controller builds from model and renders rows", () => {
  const container = document.createElement("div");
  const tree = createTreeController(container, { label: "Mail", nodes: sampleNodes() });

  expect(tree.container.getAttribute("role")).toBe("tree");
  expect(tree.container.getAttribute("aria-label")).toBe("Mail");

  const items = container.querySelectorAll('[role="treeitem"]');
  expect(items.length).toBeGreaterThan(0);

  const inboxRow = container.querySelector('[data-node-id="inbox"]');
  expect(inboxRow).not.toBeNull();
  expect(inboxRow.getAttribute("aria-current")).toBeNull();

  tree.destroy();
});

test("tree controller select sets aria-current and fires callback", () => {
  const container = document.createElement("div");
  const onSelect = vi.fn();
  const tree = createTreeController(container, { label: "Mail", nodes: sampleNodes() });
  tree.onSelect(onSelect);

  tree.select("inbox");

  expect(tree.isSelected("inbox")).toBe(true);
  const row = container.querySelector('[data-node-id="inbox"] [role="treeitem"]');
  expect(row.getAttribute("aria-current")).toBe("true");
  expect(onSelect).toHaveBeenCalledWith("inbox", null);

  tree.select("drafts");
  expect(tree.isSelected("drafts")).toBe(true);
  expect(container.querySelector('[data-node-id="inbox"] [role="treeitem"]').getAttribute("aria-current")).toBeNull();
  expect(container.querySelector('[data-node-id="drafts"] [role="treeitem"]').getAttribute("aria-current")).toBe("true");

  tree.destroy();
});

test("tree controller toggle expands and collapses with rendering", () => {
  const container = document.createElement("div");
  const tree = createTreeController(container, {
    label: "Mail",
    nodes: sampleNodes(),
  });

  const projectsRow = container.querySelector('[data-node-id="projects"]');
  expect(projectsRow).not.toBeNull();

  expect(tree.isExpanded("projects")).toBe(false);
  const expanded = tree.toggleExpand("projects");
  expect(expanded).toBe(true);
  expect(tree.isExpanded("projects")).toBe(true);

  expect(container.querySelector('[data-node-id="pavilion"]')).not.toBeNull();

  const collapsed = tree.toggleExpand("projects");
  expect(collapsed).toBe(false);
  expect(tree.isExpanded("projects")).toBe(false);

  tree.destroy();
});

test("tree controller collapseBranch keeps descendant expansion", () => {
  const container = document.createElement("div");
  const tree = createTreeController(container, {
    label: "Mail",
    nodes: sampleNodes(),
    state: { expanded: ["projects", "pavilion"] },
  });

  expect(tree.isExpanded("projects")).toBe(true);
  expect(tree.isExpanded("pavilion")).toBe(true);

  tree.collapseBranch("projects");
  expect(tree.isExpanded("projects")).toBe(false);
  expect(tree.isExpanded("pavilion")).toBe(true);

  tree.expand("projects");
  expect(tree.isExpanded("projects")).toBe(true);
  expect(tree.isExpanded("pavilion")).toBe(true);

  tree.destroy();
});

test("tree controller collapseAllReset clears expansion", () => {
  const container = document.createElement("div");
  const tree = createTreeController(container, {
    label: "Mail",
    nodes: sampleNodes(),
    state: { expanded: ["projects", "pavilion"] },
  });

  tree.collapseAllReset();
  expect(tree.isExpanded("projects")).toBe(false);
  expect(tree.isExpanded("pavilion")).toBe(false);

  tree.destroy();
});

test("tree controller collapseAllRemember returns snapshot", () => {
  const container = document.createElement("div");
  const tree = createTreeController(container, {
    label: "Mail",
    nodes: sampleNodes(),
    state: { expanded: ["projects", "pavilion"] },
  });

  const snapshot = tree.collapseAllRemember();
  expect(snapshot).toBeInstanceOf(Set);
  expect(snapshot.has("projects")).toBe(true);
  expect(snapshot.has("pavilion")).toBe(true);

  tree.destroy();
});

test("tree controller restoreAll restores expansion", () => {
  const container = document.createElement("div");
  const tree = createTreeController(container, {
    label: "Mail",
    nodes: sampleNodes(),
  });

  tree.restoreAll(new Set(["projects"]));
  expect(tree.isExpanded("projects")).toBe(true);

  tree.destroy();
});

test("tree controller patch inserts and updates nodes", () => {
  const container = document.createElement("div");
  const tree = createTreeController(container, { label: "Mail", nodes: sampleNodes() });

  tree.patch([{ id: "sent", parentId: null, kind: "mailbox", label: "Sent", unread: 0, hasChildren: false, order: 5 }]);
  expect(tree.getNode("sent")).not.toBeNull();
  expect(tree.getNode("sent").label).toBe("Sent");

  tree.patch([{ id: "inbox", parentId: null, label: "Inbox Updated", unread: 5 }]);
  expect(tree.getNode("inbox").label).toBe("Inbox Updated");
  expect(tree.getNode("inbox").unread).toBe(5);

  tree.destroy();
});

test("tree controller remove deletes nodes", () => {
  const container = document.createElement("div");
  const tree = createTreeController(container, { label: "Mail", nodes: sampleNodes() });

  const removed = tree.remove(["projects"]);
  expect(removed).toContain("projects");
  expect(removed).toContain("pavilion");
  expect(removed).toContain("hydrogen");
  expect(tree.getNode("projects")).toBeNull();
  expect(tree.getNode("inbox")).not.toBeNull();

  tree.destroy();
});

test("tree controller fires expand callbacks on toggle", () => {
  const container = document.createElement("div");
  const onExpand = vi.fn();
  const tree = createTreeController(container, { label: "Mail", nodes: sampleNodes() });
  tree.onExpand(onExpand);

  tree.toggleExpand("projects");
  expect(onExpand).toHaveBeenCalledWith("projects", true);

  tree.toggleExpand("projects");
  expect(onExpand).toHaveBeenCalledWith("projects", false);

  tree.destroy();
});

test("tree controller fires expand callbacks on expand/collapse", () => {
  const container = document.createElement("div");
  const onExpand = vi.fn();
  const tree = createTreeController(container, { label: "Mail", nodes: sampleNodes() });
  tree.onExpand(onExpand);

  tree.expand("projects");
  expect(onExpand).toHaveBeenCalledWith("projects", true);

  tree.collapse("projects");
  expect(onExpand).toHaveBeenCalledWith("projects", false);

  tree.destroy();
});

test("tree controller serialize round-trips", () => {
  const container = document.createElement("div");
  const tree = createTreeController(container, {
    label: "Mail",
    nodes: sampleNodes(),
    state: { expanded: ["projects"], selected: "inbox" },
  });

  tree.select("inbox");
  tree.expand("projects");

  const data = tree.serialize();
  expect(data.expanded).toEqual(expect.arrayContaining(["projects"]));
  expect(data.selected).toBe("inbox");

  tree.destroy();
});

test("tree controller onPersist is called on selection", () => {
  const container = document.createElement("div");
  const onPersist = vi.fn();
  const tree = createTreeController(container, {
    label: "Mail",
    nodes: sampleNodes(),
    onPersist,
  });

  tree.select("inbox");
  expect(onPersist).toHaveBeenCalledWith("default", expect.objectContaining({ selected: "inbox" }));

  tree.destroy();
});

test("tree controller destroys and removes from DOM", () => {
  const container = document.createElement("div");
  const tree = createTreeController(container, { label: "Mail", nodes: sampleNodes() });

  const root = tree.container;
  expect(container.contains(root)).toBe(true);
  tree.destroy();
  expect(container.contains(root)).toBe(false);
});

test("tree controller rootIds and getNode delegate to model", () => {
  const container = document.createElement("div");
  const tree = createTreeController(container, { label: "Mail", nodes: sampleNodes() });

  expect(tree.getRootIds()).toContain("inbox");
  expect(tree.getRootIds()).toContain("projects");
  expect(tree.getNode("inbox").label).toBe("Inbox");
  expect(tree.getNode("nonexistent")).toBeNull();

  tree.destroy();
});

/**
 * @vitest-environment jsdom
 */
import { expect, test } from "vitest";
import { createTreeController } from "./tree.js";
import { renderRow, patchRow, renderLevel } from "./render.js";

function sampleNodes() {
  return [
    { id: "inbox", parentId: null, kind: "mailbox", label: "Inbox", unread: 3, hasChildren: false, order: 1 },
    { id: "projects", parentId: null, kind: "folder", label: "Projects", hasChildren: true, expanded: false, order: 2 },
    { id: "pavilion", parentId: "projects", kind: "folder", label: "Pavilion", hasChildren: true, order: 1 },
    { id: "hydrogen", parentId: "pavilion", kind: "folder", label: "Hydrogen", hasChildren: false, order: 1 },
    { id: "archive", parentId: null, kind: "mailbox", label: "Archive", unread: 12, hasChildren: false, order: 3 },
  ];
}

test("renderRow adds disclosure icon for nodes with children", () => {
  const node = { id: "projects", kind: "folder", label: "Projects", selected: false, hasChildren: true, expanded: true, level: 1 };
  const row = renderRow(node, ["leading", "label"]);

  const disclosure = row.querySelector(".tree-row-disclosure");
  expect(disclosure).not.toBeNull();
  expect(disclosure.getAttribute("aria-hidden")).toBe("true");
});

test("renderRow omits disclosure icon for leaf nodes", () => {
  const node = { id: "inbox", kind: "mailbox", label: "Inbox", selected: false, hasChildren: false, level: 1 };
  const row = renderRow(node, ["leading", "label"]);

  const disclosure = row.querySelector(".tree-row-disclosure");
  expect(disclosure).toBeNull();
});

test("renderRow rotates disclosure via tree-row-expanded class", () => {
  const expanded = { id: "p", kind: "folder", label: "P", selected: false, hasChildren: true, expanded: true, level: 1 };
  const collapsed = { id: "p", kind: "folder", label: "P", selected: false, hasChildren: true, expanded: false, level: 1 };

  const expandedRow = renderRow(expanded, ["leading", "label"]);
  expect(expandedRow.classList.contains("tree-row-expanded")).toBe(true);

  const collapsedRow = renderRow(collapsed, ["leading", "label"]);
  expect(collapsedRow.classList.contains("tree-row-expanded")).toBe(false);
});

test("renderLevel applies tree-group classes instead of hidden attribute", () => {
  const nodes = [
    { id: "parent", parentId: null, kind: "folder", label: "Parent", hasChildren: true, expanded: false, level: 1 },
    { id: "child", parentId: "parent", kind: "folder", label: "Child", hasChildren: false, level: 2 },
  ];
  const fragment = renderLevel(nodes, null, ["label"], 0);
  const group = fragment.querySelector('[role="group"]');
  expect(group).not.toBeNull();
  expect(group.classList.contains("tree-group")).toBe(true);
  expect(group.classList.contains("tree-group-collapsed")).toBe(true);
  expect(group.hasAttribute("hidden")).toBe(false);

  const nodes2 = [
    { id: "parent", parentId: null, kind: "folder", label: "Parent", hasChildren: true, expanded: true, level: 1 },
    { id: "child", parentId: "parent", kind: "folder", label: "Child", hasChildren: false, level: 2 },
  ];
  const fragment2 = renderLevel(nodes2, null, ["label"], 0);
  const group2 = fragment2.querySelector('[role="group"]');
  expect(group2.classList.contains("tree-group-expanded")).toBe(true);
  expect(group2.classList.contains("tree-group-collapsed")).toBe(false);
});

test("controller toggleExpand toggles group container classes", () => {
  const container = document.createElement("div");
  const tree = createTreeController(container, { label: "Mail", nodes: sampleNodes() });

  const projectsGroup = container.querySelector('[data-node-id="projects"] ul[role="group"]');
  expect(projectsGroup).not.toBeNull();
  expect(projectsGroup.classList.contains("tree-group-collapsed")).toBe(true);
  expect(projectsGroup.classList.contains("tree-group-expanded")).toBe(false);

  tree.expand("projects");
  expect(projectsGroup.classList.contains("tree-group-expanded")).toBe(true);
  expect(projectsGroup.classList.contains("tree-group-collapsed")).toBe(false);

  tree.collapse("projects");
  expect(projectsGroup.classList.contains("tree-group-collapsed")).toBe(true);
  expect(projectsGroup.classList.contains("tree-group-expanded")).toBe(false);

  tree.destroy();
});

test("controller toggleExpand toggles tree-row-expanded class on the row", () => {
  const container = document.createElement("div");
  const tree = createTreeController(container, { label: "Mail", nodes: sampleNodes() });

  const projectsRow = container.querySelector('[data-node-id="projects"] [role="treeitem"]');
  expect(projectsRow.classList.contains("tree-row-expanded")).toBe(false);

  tree.expand("projects");
  expect(projectsRow.classList.contains("tree-row-expanded")).toBe(true);

  tree.collapse("projects");
  expect(projectsRow.classList.contains("tree-row-expanded")).toBe(false);

  tree.destroy();
});

test("controller toggleExpand is interruptible: rapid toggling settles in final state", () => {
  const container = document.createElement("div");
  const tree = createTreeController(container, { label: "Mail", nodes: sampleNodes() });

  const projectsGroup = container.querySelector('[data-node-id="projects"] ul[role="group"]');
  const projectsRow = container.querySelector('[data-node-id="projects"] [role="treeitem"]');

  tree.expand("projects");
  tree.collapse("projects");
  tree.expand("projects");
  tree.collapse("projects");
  tree.expand("projects");

  expect(tree.isExpanded("projects")).toBe(true);
  expect(projectsGroup.classList.contains("tree-group-expanded")).toBe(true);
  expect(projectsGroup.classList.contains("tree-group-collapsed")).toBe(false);
  expect(projectsRow.classList.contains("tree-row-expanded")).toBe(true);

  tree.destroy();
});

test("controller patchRow updates tree-row-expanded class", () => {
  const node = { id: "p", kind: "folder", label: "P", selected: false, hasChildren: true, expanded: false, level: 1 };
  const row = renderRow(node, ["leading", "label"]);
  expect(row.classList.contains("tree-row-expanded")).toBe(false);

  patchRow({ id: "p", hasChildren: true, expanded: true }, row);
  expect(row.classList.contains("tree-row-expanded")).toBe(true);

  patchRow({ id: "p", hasChildren: true, expanded: false }, row);
  expect(row.classList.contains("tree-row-expanded")).toBe(false);
});

test("controller patchRow does not add tree-row-expanded for leaf nodes", () => {
  const node = { id: "inbox", kind: "mailbox", label: "Inbox", selected: false, hasChildren: false, level: 1 };
  const row = renderRow(node, ["label"]);
  expect(row.classList.contains("tree-row-expanded")).toBe(false);

  patchRow({ id: "inbox", expanded: true }, row);
  expect(row.classList.contains("tree-row-expanded")).toBe(false);
});

test("controller collapseBranch updates group container class", () => {
  const container = document.createElement("div");
  const tree = createTreeController(container, {
    label: "Mail",
    nodes: sampleNodes(),
    state: { expanded: ["projects", "pavilion"] },
  });

  const projectsGroup = container.querySelector('[data-node-id="projects"] ul[role="group"]');
  expect(projectsGroup.classList.contains("tree-group-expanded")).toBe(true);

  tree.collapseBranch("projects");
  expect(tree.isExpanded("projects")).toBe(false);
  expect(tree.isExpanded("pavilion")).toBe(true);
  expect(projectsGroup.classList.contains("tree-group-collapsed")).toBe(true);

  tree.expand("projects");
  expect(tree.isExpanded("projects")).toBe(true);
  expect(projectsGroup.classList.contains("tree-group-expanded")).toBe(true);

  tree.destroy();
});

test("controller collapseAllReset clears all group containers", () => {
  const container = document.createElement("div");
  const tree = createTreeController(container, {
    label: "Mail",
    nodes: sampleNodes(),
    state: { expanded: ["projects", "pavilion"] },
  });

  tree.collapseAllReset();

  const projectsGroup = container.querySelector('[data-node-id="projects"] ul[role="group"]');
  const pavilionGroup = container.querySelector('[data-node-id="pavilion"] ul[role="group"]');

  expect(projectsGroup.classList.contains("tree-group-collapsed")).toBe(true);
  expect(projectsGroup.classList.contains("tree-group-expanded")).toBe(false);
  expect(pavilionGroup.classList.contains("tree-group-collapsed")).toBe(true);
  expect(pavilionGroup.classList.contains("tree-group-expanded")).toBe(false);

  tree.destroy();
});

test("controller expandAll/collapse via full re-render preserves animation classes on initial render", () => {
  const container = document.createElement("div");
  const tree = createTreeController(container, {
    label: "Mail",
    nodes: sampleNodes(),
    state: { expanded: ["projects"] },
  });

  const projectsGroup = container.querySelector('[data-node-id="projects"] ul[role="group"]');
  const pavilionGroup = container.querySelector('[data-node-id="pavilion"] ul[role="group"]');

  expect(projectsGroup.classList.contains("tree-group-expanded")).toBe(true);
  expect(pavilionGroup).not.toBeNull();
  expect(pavilionGroup.classList.contains("tree-group-collapsed")).toBe(true);

  tree.destroy();
});

test("controller render re-applies group classes after filter toggle", () => {
  const container = document.createElement("div");
  const tree = createTreeController(container, {
    label: "Mail",
    nodes: sampleNodes(),
    state: { expanded: ["projects"], favorites: ["projects"] },
  });

  const pavilionRow = container.querySelector('[data-node-id="pavilion"] [role="treeitem"]');
  expect(pavilionRow).not.toBeNull();

  tree.viewState.setShowFavorites(true);
  tree.render();

  const afterFilter = container.querySelector('[data-node-id="pavilion"] [role="treeitem"]');
  expect(afterFilter).not.toBeNull();
  expect(afterFilter.classList.contains("tree-row-expanded")).toBe(false);

  tree.destroy();
});

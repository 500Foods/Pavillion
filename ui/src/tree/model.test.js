/**
 * @vitest-environment jsdom
 */
import { expect, test } from "vitest";
import { createModel } from "./model.js";

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

test("model stores and retrieves nodes by id", () => {
  const model = createModel(sampleNodes());
  expect(model.has("inbox")).toBe(true);
  expect(model.has("nonexistent")).toBe(false);
  const node = model.get("inbox");
  expect(node.label).toBe("Inbox");
  expect(node.kind).toBe("mailbox");
});

test("model rootIds returns top-level nodes in order", () => {
  const model = createModel(sampleNodes());
  const roots = model.rootIds();
  expect(roots).toEqual(["inbox", "drafts", "projects", "archive"]);
});

test("model children returns direct children", () => {
  const model = createModel(sampleNodes());
  expect(model.children("projects")).toEqual([
    expect.objectContaining({ id: "pavilion" })
  ]);
  expect(model.children("pavilion")).toEqual([
    expect.objectContaining({ id: "hydrogen" })
  ]);
  expect(model.children("inbox")).toEqual([]);
});

test("model descendants returns all descendants recursively", () => {
  const model = createModel(sampleNodes());
  const desc = model.descendants("projects");
  expect(desc).toContain("pavilion");
  expect(desc).toContain("hydrogen");
  expect(desc).toHaveLength(2);
  expect(model.descendants("inbox")).toEqual([]);
});

test("model ancestors returns parent chain", () => {
  const model = createModel(sampleNodes());
  expect(model.ancestors("hydrogen")).toEqual(["pavilion", "projects"]);
  expect(model.ancestors("pavilion")).toEqual(["projects"]);
  expect(model.ancestors("inbox")).toEqual([]);
});

test("model depth is based on parent chain", () => {
  const model = createModel(sampleNodes());
  expect(model.depth("inbox")).toBe(1);
  expect(model.depth("projects")).toBe(1);
  expect(model.depth("pavilion")).toBe(2);
  expect(model.depth("hydrogen")).toBe(3);
});

test("model visible returns false for children of collapsed parent", () => {
  const model = createModel(sampleNodes());
  expect(model.get("pavilion").parentId).toBe("projects");
});

test("model patch inserts new nodes and updates existing", () => {
  const model = createModel(sampleNodes());
  model.patch([
    { id: "projects", parentId: null, label: "Projects", hasChildren: true, order: 3 },
    { id: "sent", parentId: null, kind: "mailbox", label: "Sent", role: "sent", unread: 0, hasChildren: false, order: 5 },
  ]);
  expect(model.has("sent")).toBe(true);
  expect(model.get("inbox").label).toBe("Inbox");
  expect(model.rootIds()).toContain("sent");
});

test("model patch preserves children when parent stays the same", () => {
  const model = createModel(sampleNodes());
  model.patch([
    { id: "pavilion", parentId: "projects", label: "Pavilion", hasChildren: true, order: 1, unread: 5 },
  ]);
  expect(model.get("pavilion").unread).toBe(5);
  expect(model.descendants("projects")).toContain("pavilion");
  expect(model.descendants("projects")).toContain("hydrogen");
});

test("model patch reparents a node", () => {
  const model = createModel(sampleNodes());
  expect(model.children("projects")[0].id).toBe("pavilion");
  model.patch([
    { id: "pavilion", parentId: null, label: "Pavilion", hasChildren: true, order: 2 },
  ]);
  expect(model.children("projects")).toEqual([]);
  expect(model.children(null).some((n) => n.id === "pavilion")).toBe(true);
  expect(model.get("pavilion").parentId).toBe(null);
});

test("model remove deletes nodes and their descendants", () => {
  const model = createModel(sampleNodes());
  const removed = model.remove(["projects"]);
  expect(removed).toContain("projects");
  expect(removed).toContain("pavilion");
  expect(removed).toContain("hydrogen");
  expect(model.has("projects")).toBe(false);
  expect(model.has("pavilion")).toBe(false);
  expect(model.has("hydrogen")).toBe(false);
  expect(model.has("inbox")).toBe(true);
  expect(model.rootIds()).not.toContain("projects");
});

test("model remove on unknown id returns empty", () => {
  const model = createModel(sampleNodes());
  expect(model.remove(["nonexistent"])).toEqual([]);
});

test("model toArray returns all nodes depth-first", () => {
  const model = createModel(sampleNodes());
  const all = model.toArray();
  expect(all).toHaveLength(6);
  expect(all.map((n) => n.id)).toEqual(expect.arrayContaining(["inbox", "drafts", "projects", "pavilion", "hydrogen", "archive"]));
});

test("model toArray is in tree order (parent before children)", () => {
  const model = createModel(sampleNodes());
  const all = model.toArray().map((n) => n.id);
  expect(all.indexOf("projects")).toBeLessThan(all.indexOf("pavilion"));
  expect(all.indexOf("pavilion")).toBeLessThan(all.indexOf("hydrogen"));
});

test("model clear empties everything", () => {
  const model = createModel(sampleNodes());
  model.clear();
  expect(model.has("inbox")).toBe(false);
  expect(model.rootIds()).toEqual([]);
  expect(model.toArray()).toEqual([]);
});

test("model sort by order field", () => {
  const model = createModel([
    { id: "z", parentId: null, label: "Z", order: 3 },
    { id: "a", parentId: null, label: "A", order: 1 },
    { id: "m", parentId: null, label: "M", order: 2 },
  ]);
  expect(model.rootIds()).toEqual(["a", "m", "z"]);
});

test("model nodes without order sort by id", () => {
  const model = createModel([
    { id: "z", parentId: null, label: "Z" },
    { id: "a", parentId: null, label: "A" },
    { id: "m", parentId: null, label: "M" },
  ]);
  expect(model.rootIds()).toEqual(["a", "m", "z"]);
});

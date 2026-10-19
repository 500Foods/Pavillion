/**
 * @vitest-environment node
 */
import { expect, test } from "vitest";
import { createViewState } from "./state.js";

test("viewState tracks expansion independently", () => {
  const vs = createViewState({ expanded: ["a", "b"] });
  expect(vs.isExpanded("a")).toBe(true);
  expect(vs.isExpanded("c")).toBe(false);
});

test("viewState toggle expands and collapses", () => {
  const vs = createViewState();
  expect(vs.toggle("a")).toBe(true);
  expect(vs.isExpanded("a")).toBe(true);
  expect(vs.toggle("a")).toBe(false);
  expect(vs.isExpanded("a")).toBe(false);
});

test("viewState expand and collapse return change only", () => {
  const vs = createViewState();
  expect(vs.expand("a")).toBe(true);
  expect(vs.expand("a")).toBe(false);
  expect(vs.isExpanded("a")).toBe(true);
  expect(vs.collapse("a")).toBe(true);
  expect(vs.collapse("a")).toBe(false);
  expect(vs.isExpanded("a")).toBe(false);
});

test("collapseAllReset clears expansion", () => {
  const vs = createViewState({ expanded: ["a", "b", "c"] });
  vs.collapseAllReset();
  expect(vs.isExpanded("a")).toBe(false);
  expect(vs.isExpanded("b")).toBe(false);
  expect(vs.isExpanded("c")).toBe(false);
});

test("collapseAllRemember returns a snapshot of expansion", () => {
  const vs = createViewState({ expanded: ["a", "b"] });
  const snapshot = vs.collapseAllRemember();
  expect(snapshot).toBeInstanceOf(Set);
  expect(snapshot.has("a")).toBe(true);
  expect(snapshot.has("b")).toBe(true);
  expect(vs.isExpanded("a")).toBe(true);
  expect(vs.isExpanded("b")).toBe(true);
});

test("restoreAll restores expansion from a snapshot", () => {
  const vs = createViewState({ expanded: ["a"] });
  vs.restoreAll(new Set(["x", "y", "z"]));
  expect(vs.isExpanded("a")).toBe(false);
  expect(vs.isExpanded("x")).toBe(true);
  expect(vs.isExpanded("y")).toBe(true);
  expect(vs.isExpanded("z")).toBe(true);
});

test("collapseBranch collapses only that node, not descendants", () => {
  const vs = createViewState({ expanded: ["parent", "child", "grandchild"] });
  vs.collapseBranch("parent");
  expect(vs.isExpanded("parent")).toBe(false);
  expect(vs.isExpanded("child")).toBe(true);
  expect(vs.isExpanded("grandchild")).toBe(true);
});

test("collapseBranch keeps descendant state for reopen", () => {
  const vs = createViewState({ expanded: ["parent", "child", "grandchild"] });
  vs.collapseBranch("parent");
  expect(vs.isExpanded("parent")).toBe(false);
  expect(vs.isExpanded("child")).toBe(true);
  vs.expand("parent");
  expect(vs.isExpanded("parent")).toBe(true);
  expect(vs.isExpanded("child")).toBe(true);
  expect(vs.isExpanded("grandchild")).toBe(true);
});

test("collapseAllReset discards expansion, expand starts fresh", () => {
  const vs = createViewState({ expanded: ["parent", "child"] });
  vs.collapseAllReset();
  vs.expand("parent");
  expect(vs.isExpanded("parent")).toBe(true);
  expect(vs.isExpanded("child")).toBe(false);
});

test("select updates selection and focus", () => {
  const vs = createViewState();
  vs.select("a");
  expect(vs.selected).toBe("a");
  expect(vs.focused).toBe("a");
  vs.select("b");
  expect(vs.selected).toBe("b");
  expect(vs.focused).toBe("b");
});

test("select returns previous selection", () => {
  const vs = createViewState({ selected: "a" });
  const prev = vs.select("b");
  expect(prev).toBe("a");
  expect(vs.selected).toBe("b");
});

test("focus sets focus without changing selection", () => {
  const vs = createViewState({ selected: "a" });
  vs.focus("c");
  expect(vs.focused).toBe("c");
  expect(vs.selected).toBe("a");
});

test("blur clears focus", () => {
  const vs = createViewState({ selected: "a" });
  vs.focus("c");
  vs.blur();
  expect(vs.focused).toBe(null);
  expect(vs.selected).toBe("a");
});

test("hidden roots track visibility independently of model", () => {
  const vs = createViewState({ hiddenRoots: ["a"] });
  expect(vs.isRootVisible("a")).toBe(false);
  expect(vs.hideRoot("b")).toBe(true);
  expect(vs.isRootVisible("b")).toBe(false);
  expect(vs.showRoot("a")).toBe(true);
  expect(vs.isRootVisible("a")).toBe(true);
});

test("toggleRoot hides and shows", () => {
  const vs = createViewState();
  expect(vs.toggleRoot("a")).toBe(true);
  expect(vs.isRootVisible("a")).toBe(false);
  expect(vs.toggleRoot("a")).toBe(false);
  expect(vs.isRootVisible("a")).toBe(true);
});

test("favorites (star) tracking", () => {
  const vs = createViewState({ favorites: ["a"] });
  expect(vs.isFavorite("a")).toBe(true);
  vs.toggleFavorite("a");
  expect(vs.isFavorite("a")).toBe(false);
  vs.toggleFavorite("a");
  expect(vs.isFavorite("a")).toBe(true);
});

test("heart set tracking", () => {
  const vs = createViewState({ heartSet: ["b"] });
  expect(vs.isHeart("b")).toBe(true);
  vs.toggleHeart("b");
  expect(vs.isHeart("b")).toBe(false);
  vs.toggleHeart("b");
  expect(vs.isHeart("b")).toBe(true);
});

test("infrequent tracking", () => {
  const vs = createViewState({ infrequent: ["c"] });
  expect(vs.isInfrequent("c")).toBe(true);
  expect(vs.isInfrequent("a")).toBe(false);
  vs.toggleInfrequent("a");
  expect(vs.isInfrequent("a")).toBe(true);
});

test("activePreset tracks current preset", () => {
  const vs = createViewState({ activePreset: "compact" });
  expect(vs.activePreset).toBe("compact");
  vs.setActivePreset("work");
  expect(vs.activePreset).toBe("work");
  expect(vs.activePreset).not.toBe("compact");
});

test("serialize round-trips through createViewState", () => {
  const vs = createViewState({
    expanded: ["a", "b"],
    selected: "a",
    focused: "b",
    hiddenRoots: ["c"],
    favorites: ["d"],
    heartSet: ["e"],
    infrequent: ["f"],
    activePreset: "compact",
  });
  const data = vs.serialize();
  expect(data.expanded).toEqual(["a", "b"]);
  expect(data.selected).toBe("a");
  expect(data.focused).toBe("b");
  expect(data.hiddenRoots).toEqual(["c"]);
  expect(data.favorites).toEqual(["d"]);
  expect(data.heartSet).toEqual(["e"]);
  expect(data.infrequent).toEqual(["f"]);
  expect(data.activePreset).toBe("compact");

  const vs2 = createViewState(data);
  expect(vs2.isExpanded("a")).toBe(true);
  expect(vs2.isFavorite("d")).toBe(true);
  expect(vs2.isHeart("e")).toBe(true);
  expect(vs2.isInfrequent("f")).toBe(true);
  expect(vs2.isRootVisible("c")).toBe(false);
});

test("descendants provider is set before use", () => {
  const vs = createViewState();
  vs.setDescendantsProvider(() => ["child1", "child2"]);
  expect(vs.descendants("parent")).toEqual(["child1", "child2"]);
});

test("visibleNodes walks only expanded ancestors", () => {
  const vs = createViewState({ expanded: [] });
  const fakeModel = {
    children(id) {
      if (id === null) return [{ id: "root1" }, { id: "root2" }];
      if (id === "root1") return [{ id: "child1" }];
      return [];
    },
  };
  let visible = vs.visibleNodes(fakeModel);
  expect(visible).toContain("root1");
  expect(visible).toContain("root2");
  expect(visible).not.toContain("child1");

  vs.expand("root1");
  visible = vs.visibleNodes(fakeModel);
  expect(visible).toContain("root1");
  expect(visible).toContain("root2");
  expect(visible).toContain("child1");
});

test("visibleNodes filters by favorites when showFavorites is true", () => {
  const vs = createViewState({ expanded: [], favorites: ["root1"] });
  const fakeModel = {
    children(id) {
      if (id === null) return [{ id: "root1" }, { id: "root2" }];
      return [];
    },
  };
  let visible = vs.visibleNodes(fakeModel);
  expect(visible).toHaveLength(2);

  vs.setShowFavorites(true);
  visible = vs.visibleNodes(fakeModel);
  expect(visible).toEqual(["root1"]);
});

test("visibleNodes filters by heart set when showHeart is true", () => {
  const vs = createViewState({ expanded: [], heartSet: ["root2"] });
  const fakeModel = {
    children(id) {
      if (id === null) return [{ id: "root1" }, { id: "root2" }];
      return [];
    },
  };
  vs.setShowHeart(true);
  const visible = vs.visibleNodes(fakeModel);
  expect(visible).toEqual(["root2"]);
});

test("visibleNodes hides infrequent roots when hideInfrequent is true", () => {
  const vs = createViewState({ expanded: [], infrequent: ["root1"] });
  const fakeModel = {
    children(id) {
      if (id === null) return [{ id: "root1" }, { id: "root2" }];
      return [];
    },
  };
  vs.setHideInfrequent(true);
  const visible = vs.visibleNodes(fakeModel);
  expect(visible).toEqual(["root2"]);
});

test("filter setters toggle correctly", () => {
  const vs = createViewState();
  expect(vs.showFavorites).toBe(false);
  vs.setShowFavorites(true);
  expect(vs.showFavorites).toBe(true);
  vs.setShowFavorites(false);
  expect(vs.showFavorites).toBe(false);

  expect(vs.showHeart).toBe(false);
  vs.setShowHeart(true);
  expect(vs.showHeart).toBe(true);

  expect(vs.hideInfrequent).toBe(false);
  vs.setHideInfrequent(true);
  expect(vs.hideInfrequent).toBe(true);
});

test("serialize includes filter flags", () => {
  const vs = createViewState({
    expanded: ["a"],
    favorites: ["b"],
    showFavorites: true,
    showHeart: true,
    hideInfrequent: true,
  });
  const data = vs.serialize();
  expect(data.showFavorites).toBe(true);
  expect(data.showHeart).toBe(true);
  expect(data.hideInfrequent).toBe(true);

  const vs2 = createViewState(data);
  expect(vs2.showFavorites).toBe(true);
  expect(vs2.showHeart).toBe(true);
  expect(vs2.hideInfrequent).toBe(true);
});

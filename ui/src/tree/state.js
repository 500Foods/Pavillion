/* Tree view state: expansion, selection, focus, hidden roots, favorites,
   and the three collapse behaviors. Expansion is independent of visibility:
   a descendant stays logically expanded while hidden behind a collapsed ancestor.
   Persisted per tree in preferences.ui.trees.<treeId>. */

export function createViewState(initial = {}) {
  const expanded = new Set(initial.expanded || []);
  let selected = initial.selected || null;
  let focused = initial.focused || null;
  const hiddenRoots = new Set(initial.hiddenRoots || []);
  const favorites = new Set(initial.favorites || []);
  const heartSet = new Set(initial.heart || initial.heartSet || []);
  const infrequent = new Set(initial.infrequent || []);
  let showFavorites = initial.showFavorites === true;
  let showHeart = initial.showHeart === true;
  let hideInfrequent = initial.hideInfrequent === true;
  let activePreset = initial.activePreset || "custom";

  return {
    get expanded() {
      return new Set(expanded);
    },
    get selected() {
      return selected;
    },
    get focused() {
      return focused;
    },
    get hiddenRoots() {
      return new Set(hiddenRoots);
    },
    get favorites() {
      return new Set(favorites);
    },
    get heartSet() {
      return new Set(heartSet);
    },
    get infrequent() {
      return new Set(infrequent);
    },
    get showFavorites() {
      return showFavorites;
    },
    get showHeart() {
      return showHeart;
    },
    get hideInfrequent() {
      return hideInfrequent;
    },
    get activePreset() {
      return activePreset;
    },
    isExpanded(id) {
      return expanded.has(id);
    },
    select(id) {
      if (id !== selected) {
        const prev = selected;
        selected = id;
        if (focused === null || focused !== id) {
          focused = id;
        }
        return prev;
      }
      return undefined;
    },
    focus(id) {
      if (id !== null && id !== focused) {
        focused = id;
      }
    },
    blur() {
      focused = null;
    },
    toggle(id) {
      if (expanded.has(id)) {
        expanded.delete(id);
        return false;
      }
      expanded.add(id);
      return true;
    },
    expand(id) {
      const changed = !expanded.has(id);
      expanded.add(id);
      return changed;
    },
    collapse(id) {
      const changed = expanded.has(id);
      expanded.delete(id);
      return changed;
    },
    collapseAllReset() {
      const prev = new Set(expanded);
      expanded.clear();
      return prev;
    },
    collapseAllRemember() {
      return new Set(expanded);
    },
    restoreAll(snapshot) {
      expanded.clear();
      for (const id of snapshot) {
        expanded.add(id);
      }
    },
    collapseBranch(id) {
      expanded.delete(id);
      return true;
    },
    collapseDescendants(id) {
      expanded.delete(id);
      for (const childId of this.descendants(id)) {
        expanded.delete(childId);
      }
    },
    descendants(id) {
      return [];
    },
    setDescendantsProvider(fn) {
      this.descendants = fn;
    },
    isRootVisible(id) {
      return !hiddenRoots.has(id);
    },
    showRoot(id) {
      const changed = hiddenRoots.has(id);
      hiddenRoots.delete(id);
      return changed;
    },
    hideRoot(id) {
      const changed = !hiddenRoots.has(id);
      hiddenRoots.add(id);
      return changed;
    },
    toggleRoot(id) {
      if (hiddenRoots.has(id)) {
        hiddenRoots.delete(id);
        return false;
      }
      hiddenRoots.add(id);
      return true;
    },
    isFavorite(id) {
      return favorites.has(id);
    },
    isHeart(id) {
      return heartSet.has(id);
    },
    isInfrequent(id) {
      return infrequent.has(id);
    },
    toggleFavorite(id) {
      if (favorites.has(id)) {
        favorites.delete(id);
        return false;
      }
      favorites.add(id);
      return true;
    },
    toggleHeart(id) {
      if (heartSet.has(id)) {
        heartSet.delete(id);
        return false;
      }
      heartSet.add(id);
      return true;
    },
    toggleInfrequent(id) {
      if (infrequent.has(id)) {
        infrequent.delete(id);
        return false;
      }
      infrequent.add(id);
      return true;
    },
    setActivePreset(name) {
      activePreset = name;
    },
    setShowFavorites(on) {
      showFavorites = on === true;
    },
    setShowHeart(on) {
      showHeart = on === true;
    },
    setHideInfrequent(on) {
      hideInfrequent = on === true;
    },
    toggleShowFavorites() {
      showFavorites = !showFavorites;
      return showFavorites;
    },
    toggleShowHeart() {
      showHeart = !showHeart;
      return showHeart;
    },
    toggleHideInfrequent() {
      hideInfrequent = !hideInfrequent;
      return hideInfrequent;
    },
    visibleNodes(model) {
      const showFav = showFavorites === true;
      const showHr = showHeart === true;
      const hideInf = hideInfrequent === true;
      const result = [];
      function walk(id, parentVisible) {
        const children = model.children(id);
        for (const child of children) {
          if (id !== null && !expanded.has(id)) {
            continue;
          }
          let visible = parentVisible;
          if (id === null) {
            if (showFav && !favorites.has(child.id)) {
              visible = false;
            } else if (showHr && !heartSet.has(child.id)) {
              visible = false;
            } else if (hideInf && infrequent.has(child.id)) {
              visible = false;
            }
          }
          if (visible) {
            result.push(child.id);
          }
          walk(child.id, visible);
        }
      }
      walk(null, true);
      return result;
    },
    serialize() {
      return {
        expanded: [...expanded],
        selected,
        focused,
        hiddenRoots: [...hiddenRoots],
        favorites: [...favorites],
        heartSet: [...heartSet],
        infrequent: [...infrequent],
        showFavorites,
        showHeart,
        hideInfrequent,
        activePreset,
      };
    },
  };
}

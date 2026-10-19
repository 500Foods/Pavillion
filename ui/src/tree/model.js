/* Tree model: nodes by id, children by parent, ordering, and metadata.
   The model owns data only. It does not own DOM, view state, or persistence.
   patch(nodes) replaces or inserts nodes; remove(ids) deletes them. */

export function createModel(initialNodes = []) {
  const nodes = new Map();
  const children = new Map();

  function sortKey(node) {
    const o = typeof node.order === "number" ? node.order : 0;
    return { o, id: node.id };
  }

  function compare(a, b) {
    const ao = sortKey(a);
    const bo = sortKey(b);
    if (ao.o !== bo.o) {
      return ao.o - bo.o;
    }
    return ao.id < bo.id ? -1 : ao.id > bo.id ? 1 : 0;
  }

  function index(node) {
    const id = node.id;
    if (typeof id !== "string" || id === "") {
      return;
    }
    nodes.set(id, { ...node });
    const parentId = node.parentId;
    const key = parentId === null || parentId === undefined ? null : parentId;
    if (!children.has(key)) {
      children.set(key, []);
    }
    const list = children.get(key);
    const existing = list.findIndex((n) => n.id === id);
    if (existing >= 0) {
      list[existing] = nodes.get(id);
    } else {
      list.push(nodes.get(id));
    }
    list.sort(compare);
  }

  for (const node of initialNodes) {
    if (node && typeof node.id === "string" && node.id !== "") {
      index(node);
    }
  }

  return {
    has(id) {
      return nodes.has(id);
    },
    get(id) {
      return nodes.has(id) ? nodes.get(id) : null;
    },
    rootIds() {
      const list = children.get(null);
      return list ? list.map((n) => n.id) : [];
    },
    children(id) {
      const list = children.get(id);
      if (!list) {
        return [];
      }
      return list.slice();
    },
    descendants(id) {
      const result = [];
      const childrenList = children.get(id);
      if (!childrenList) {
        return result;
      }
      for (const child of childrenList) {
        result.push(child.id);
        result.push(...this.descendants(child.id));
      }
      return result;
    },
    ancestors(id) {
      const result = [];
      let current = nodes.get(id);
      while (current) {
        const parentId = current.parentId;
        if (parentId === null || parentId === undefined) {
          break;
        }
        const parent = nodes.get(parentId);
        if (!parent) {
          break;
        }
        result.push(parentId);
        current = parent;
      }
      return result;
    },
    depth(id) {
      return this.ancestors(id).length + 1;
    },
    patch(newNodes) {
      const list = Array.isArray(newNodes) ? newNodes : [];
      const patched = [];
      for (const node of list) {
        if (!node || typeof node.id !== "string" || node.id === "") {
          continue;
        }
        const old = nodes.get(node.id);
        if (old) {
          const merged = { ...old, ...node };
          nodes.set(node.id, merged);
          if (merged.parentId !== old.parentId) {
            const oldKey = old.parentId === null || old.parentId === undefined ? null : old.parentId;
            const oldList = children.get(oldKey);
            if (oldList) {
              const idx = oldList.findIndex((n) => n.id === node.id);
              if (idx >= 0) {
                oldList.splice(idx, 1);
              }
            }
            const newKey = merged.parentId === null || merged.parentId === undefined ? null : merged.parentId;
            if (!children.has(newKey)) {
              children.set(newKey, []);
            }
            const newList = children.get(newKey);
            if (!newList.some((n) => n.id === merged.id)) {
              newList.push(merged);
            }
            newList.sort(compare);
          } else {
            const key = merged.parentId === null || merged.parentId === undefined ? null : merged.parentId;
            const list2 = children.get(key);
            if (list2) {
              const idx = list2.findIndex((n) => n.id === node.id);
              if (idx >= 0) {
                list2[idx] = merged;
              }
              list2.sort(compare);
            }
          }
        } else {
          index(node);
        }
        patched.push(node.id);
      }
      return patched;
    },
    remove(ids) {
      const toRemove = Array.isArray(ids) ? ids : [];
      const removed = new Set();
      for (const id of toRemove) {
        if (!nodes.has(id)) {
          continue;
        }
        const descList = this.descendants(id);
        for (const d of descList) {
          removed.add(d);
          const node = nodes.get(d);
          if (node) {
            if (children.has(d)) {
              children.delete(d);
            }
            const siblings = children.get(node.parentId === null || node.parentId === undefined ? null : node.parentId);
            if (siblings) {
              const idx = siblings.findIndex((n) => n.id === d);
              if (idx >= 0) {
                siblings.splice(idx, 1);
              }
            }
          }
          nodes.delete(d);
        }
        const node = nodes.get(id);
        if (node) {
          if (children.has(id)) {
            children.delete(id);
          }
          const siblings = children.get(node.parentId === null || node.parentId === undefined ? null : node.parentId);
          if (siblings) {
            const idx = siblings.findIndex((n) => n.id === id);
            if (idx >= 0) {
              siblings.splice(idx, 1);
            }
          }
        }
        nodes.delete(id);
        removed.add(id);
      }
      return [...removed];
    },
    toArray() {
      const result = [];
      function walk(id) {
        const childrenList = children.get(id);
        if (!childrenList) {
          return;
        }
        for (const child of childrenList) {
          result.push(nodes.get(child.id));
          walk(child.id);
        }
      }
      walk(null);
      return result;
    },
    clear() {
      nodes.clear();
      children.clear();
    },
  };
}

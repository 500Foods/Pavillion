const LIMIT = 40;

function copyZoom(zoom, keys) {
  const out = {};
  for (const key of keys) {
    out[key] = zoom[key];
  }
  return out;
}

function sameZoom(a, b, keys) {
  return keys.every((key) => a[key] === b[key]);
}

/* Session stack for the zoom sliders. One entry is every zoom key at once. */
export function createZoomHistory(keys) {
  const past = [];
  const future = [];
  return {
    get canUndo() {
      return past.length > 0;
    },
    get canRedo() {
      return future.length > 0;
    },
    push(before, after) {
      if (sameZoom(before, after, keys)) {
        return false;
      }
      past.push(copyZoom(before, keys));
      if (past.length > LIMIT) {
        past.shift();
      }
      future.length = 0;
      return true;
    },
    undo(current) {
      if (past.length === 0) {
        return null;
      }
      future.push(copyZoom(current, keys));
      return past.pop();
    },
    redo(current) {
      if (future.length === 0) {
        return null;
      }
      past.push(copyZoom(current, keys));
      return future.pop();
    },
  };
}

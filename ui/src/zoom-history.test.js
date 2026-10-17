import { expect, test } from "vitest";
import { createZoomHistory } from "./zoom-history.js";

const keys = ["shell", "log"];

test("undo and redo walk zoom snapshots, and a new change drops the redo stack", () => {
  const history = createZoomHistory(keys);
  expect(history.canUndo).toBe(false);
  expect(history.canRedo).toBe(false);
  expect(history.push({ shell: 1, log: 1 }, { shell: 1, log: 1 })).toBe(false);
  expect(history.push({ shell: 1, log: 1 }, { shell: 1.5, log: 1 })).toBe(true);
  expect(history.undo({ shell: 1.5, log: 1 })).toEqual({ shell: 1, log: 1 });
  expect(history.canRedo).toBe(true);
  expect(history.redo({ shell: 1, log: 1 })).toEqual({ shell: 1.5, log: 1 });
  expect(history.push({ shell: 1.5, log: 1 }, { shell: 1.5, log: 2 })).toBe(true);
  expect(history.canRedo).toBe(false);
  history.undo({ shell: 1.5, log: 2 });
  history.undo({ shell: 1.5, log: 1 });
  expect(history.undo({ shell: 1, log: 1 })).toBeNull();
});

test("the stack keeps the newest 40 steps", () => {
  const history = createZoomHistory(keys);
  for (let i = 0; i < 41; i += 1) {
    history.push({ shell: i, log: 1 }, { shell: i + 1, log: 1 });
  }
  expect(history.undo({ shell: 41, log: 1 })).toEqual({ shell: 40, log: 1 });
  let steps = 1;
  while (history.canUndo) {
    history.undo({ shell: 1, log: 1 });
    steps += 1;
  }
  expect(steps).toBe(40);
});

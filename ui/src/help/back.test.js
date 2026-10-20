import { expect, test } from "vitest";
import { createBackTracker } from "./back.js";

test("Back returns to the last non-Help tab while it is open", () => {
  const back = createBackTracker();
  expect(back.target([])).toBeNull();
  back.note({ id: "t1", section: "mail" });
  back.note({ id: "t2", section: "settings" });
  back.note({ id: "h", section: "help" });
  const open = [{ id: "t1" }, { id: "t2" }, { id: "h" }];
  expect(back.target(open)).toEqual({ id: "t2", section: "settings" });
  expect(back.target([{ id: "t1" }, { id: "h" }])).toBeNull();
  back.clear();
  expect(back.target(open)).toBeNull();
});

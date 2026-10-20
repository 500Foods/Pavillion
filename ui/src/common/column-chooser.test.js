/**
 * @vitest-environment jsdom
 */
import { expect, test, vi } from "vitest";
import { hiddenAfter, openColumnChooser } from "./column-chooser.js";

const COLUMNS = [
  { field: "name", title: "Name", locked: true },
  { field: "number", title: "Number" },
  { field: "mail", title: "Mail" },
];

function setup(hidden = []) {
  document.body.replaceChildren();
  const anchor = document.createElement("button");
  document.body.append(anchor);
  const onChange = vi.fn();
  const chooser = openColumnChooser(anchor, { label: "Columns", columns: COLUMNS, hidden, onChange });
  return { anchor, onChange, chooser };
}

test("hiddenAfter adds and removes without duplicates", () => {
  expect(hiddenAfter(["a"], "b", false)).toEqual(["a", "b"]);
  expect(hiddenAfter(["a", "b"], "a", true)).toEqual(["b"]);
  expect(hiddenAfter(["a"], "a", false)).toEqual(["a"]);
});

test("checkboxes reflect hidden, locked is disabled, change reports the new list", () => {
  const { anchor, onChange, chooser } = setup(["mail"]);
  const box = chooser.element;
  expect(anchor.getAttribute("aria-expanded")).toBe("true");
  const name = box.querySelector('[data-field="name"]');
  expect(name.disabled).toBe(true);
  expect(name.checked).toBe(true);
  expect(box.querySelector('[data-field="mail"]').checked).toBe(false);
  const number = box.querySelector('[data-field="number"]');
  number.checked = false;
  number.dispatchEvent(new Event("change"));
  expect(onChange).toHaveBeenLastCalledWith(["mail", "number"], "number", false);
});

test("Reset shows every column, and Escape or an outside click closes", () => {
  const { anchor, onChange, chooser } = setup(["mail", "number"]);
  chooser.element.querySelector("[data-reset]").click();
  expect(onChange).toHaveBeenLastCalledWith([], null, true);
  expect([...document.querySelectorAll(".pav-column-chooser input")].every((i) => i.checked)).toBe(true);
  document.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape" }));
  expect(document.querySelector(".pav-column-chooser")).toBeNull();
  expect(anchor.getAttribute("aria-expanded")).toBe("false");
  setup();
  document.body.dispatchEvent(new Event("pointerdown", { bubbles: true }));
  expect(document.querySelector(".pav-column-chooser")).toBeNull();
});

test("opening again replaces the old popover", () => {
  const { anchor } = setup();
  openColumnChooser(anchor, { columns: COLUMNS, hidden: [], onChange() {} });
  expect(document.querySelectorAll(".pav-column-chooser").length).toBe(1);
});

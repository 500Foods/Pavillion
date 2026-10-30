/**
 * @vitest-environment jsdom
 */
import { expect, test } from "vitest";
import { cleanPlace, cleanScroll, cleanSelected } from "./table-place.js";

test("cleanSelected keeps a row id and drops garbage", () => {
  expect(cleanSelected(" nav.update ")).toBe("nav.update");
  expect(cleanSelected("")).toBe("");
  expect(cleanSelected(12)).toBe("");
  expect(cleanSelected("a\nb")).toBe("");
  expect(cleanSelected("x".repeat(513))).toBe("");
});

test("cleanScroll keeps holder pixels", () => {
  expect(cleanScroll({ top: 40.2, left: 8 })).toEqual({ top: 40, left: 8 });
  expect(cleanScroll({ top: 12 })).toEqual({ top: 12, left: 0 });
  expect(cleanScroll({ top: -1, left: "4" })).toBeNull();
  expect(cleanScroll(null)).toBeNull();
});

test("cleanPlace requires a mailbox id", () => {
  expect(cleanPlace({ mailbox: "mbx:in", selected: "b", top: 20, left: 0 })).toEqual({
    mailbox: "mbx:in",
    selected: "b",
    top: 20,
    left: 0,
  });
  expect(cleanPlace({ mailbox: "", selected: "b", top: 20, left: 0 })).toBeNull();
  expect(cleanPlace({ mailbox: "mbx:in", selected: "bad\nid", top: -4, left: 3 })).toEqual({
    mailbox: "mbx:in",
    selected: "",
    top: 0,
    left: 3,
  });
});

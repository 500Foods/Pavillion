/**
 * @vitest-environment jsdom
 */
import { expect, test, vi } from "vitest";
import { moduleFooter, moduleToolbar, mountBar, navState, navTarget, paintFooterNav } from "./module-bar.js";

test("the toolbar puts title, actions, spacer, search, then after-buttons", () => {
  const onInput = vi.fn();
  const back = vi.fn();
  const bar = moduleToolbar({
    title: "Help",
    actions: [{ action: "x", icon: "fa-plus", label: "X" }],
    search: { id: "s", label: "Search help", onInput },
    after: [{ id: "b", action: "back", icon: "fa-arrow-left", text: "Back", label: "Back", onClick: back }],
  });
  const kinds = [...bar.children].map((el) => el.className || el.tagName);
  expect(kinds).toEqual(["pav-module-title", "BUTTON", "pav-module-spacer", "pav-module-search", "BUTTON"]);
  expect(bar.querySelector(".pav-module-title").textContent).toBe("Help");
  const input = bar.querySelector("#s");
  input.value = "abc";
  input.dispatchEvent(new Event("input"));
  expect(onInput).toHaveBeenLastCalledWith("abc");
  input.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape" }));
  expect(input.value).toBe("");
  expect(onInput).toHaveBeenLastCalledWith("");
  bar.querySelector("#b").click();
  expect(back).toHaveBeenCalledOnce();
});

test("the footer always starts with Refresh, then the navigator", () => {
  const onRefresh = vi.fn();
  const onNav = vi.fn();
  const bar = moduleFooter({ noun: "row", onRefresh, onNav, end: [{ action: "export", label: "Export" }] });
  const nav = [...bar.querySelectorAll(".pav-module-nav button")];
  expect(nav.map((b) => b.dataset.moduleAction || b.dataset.step)).toEqual(["refresh", "first", "prev", "next", "last"]);
  expect(nav[1].getAttribute("aria-label")).toBe("First row");
  nav[0].click();
  expect(onRefresh).toHaveBeenCalledOnce();
  nav[3].click();
  expect(onNav).toHaveBeenCalledWith("next");
  expect(bar.querySelector(".pav-module-footer-end [data-module-action='export']")).not.toBeNull();
  paintFooterNav(bar, ["a", "b"], "a");
  expect(nav[1].disabled).toBe(true);
  expect(nav[3].disabled).toBe(false);
  const only = moduleFooter({});
  expect(only.querySelectorAll("button").length).toBe(1);
});

test("mountBar keeps the host and its own children last", () => {
  const host = document.createElement("div");
  host.className = "button-bar";
  const help = document.createElement("button");
  help.className = "help-jump";
  host.append(help);
  const out = mountBar(host, moduleToolbar({ title: "T" }));
  expect(out).toBe(host);
  expect(host.classList.contains("pav-module-toolbar")).toBe(true);
  expect(host.firstElementChild.textContent).toBe("T");
  expect(host.lastElementChild).toBe(help);
});

test("navState and navTarget", () => {
  expect(navState(["a", "b"], "b")).toEqual({ first: true, prev: true, next: false, last: false });
  expect(navTarget(["a", "b"], "a", "next")).toBe("b");
  expect(navTarget([], null, "first")).toBeNull();
});

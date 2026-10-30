/**
 * @vitest-environment jsdom
 */
import { expect, test, vi } from "vitest";
import { moduleFooter, moduleToolbar, mountBar, navState, navTarget, paintFooterNav, tableNavAction } from "./module-bar.js";

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
  expect(navTarget(["a", "b", "c", "d"], "a", "pageNext", 2)).toBe("c");
  expect(navTarget(["a", "b", "c", "d"], "d", "pagePrev", 3)).toBe("a");
  expect(navTarget(["a", "b", "c"], "b", "pageNext", 10)).toBe("c");
  expect(tableNavAction({ key: "ArrowUp" })).toBe("prev");
  expect(tableNavAction({ key: "ArrowDown" })).toBe("next");
  expect(tableNavAction({ key: "PageUp" })).toBe("pagePrev");
  expect(tableNavAction({ key: "PageDown" })).toBe("pageNext");
  expect(tableNavAction({ key: "Home" })).toBe("first");
  expect(tableNavAction({ key: "End" })).toBe("last");
  expect(tableNavAction({ key: "Home", shiftKey: true })).toBe("");
  expect(tableNavAction({ key: "Enter" })).toBe("");
});

test("Refresh spins once per click, restarts mid-spin, and stops on animationend", () => {
  window.matchMedia = vi.fn(() => ({ matches: false }));
  const onRefresh = vi.fn();
  const bar = moduleFooter({ onRefresh }, document);
  document.body.append(bar);
  const refresh = bar.querySelector('button[data-module-action="refresh"]');
  refresh.click();
  expect(onRefresh).toHaveBeenCalledTimes(1);
  expect(refresh.classList.contains("is-spinning")).toBe(true);
  refresh.click(); /* mid-spin: removed and re-added */
  expect(refresh.classList.contains("is-spinning")).toBe(true);
  expect(onRefresh).toHaveBeenCalledTimes(2);
  refresh.querySelector("i").dispatchEvent(new Event("animationend", { bubbles: true }));
  expect(refresh.classList.contains("is-spinning")).toBe(false);
  bar.remove();
  delete window.matchMedia;
});

test("Refresh does not spin under prefers-reduced-motion, but still refreshes", () => {
  window.matchMedia = vi.fn((q) => ({ matches: q === "(prefers-reduced-motion: reduce)" }));
  const onRefresh = vi.fn();
  const bar = moduleFooter({ onRefresh }, document);
  const refresh = bar.querySelector('button[data-module-action="refresh"]');
  refresh.click();
  expect(onRefresh).toHaveBeenCalledTimes(1);
  expect(refresh.classList.contains("is-spinning")).toBe(false);
  delete window.matchMedia;
});

test("D-UI Tooltips: module bar buttons carry the shared tooltip, no native title", () => {
  const bar = moduleToolbar({ title: "T", actions: [{ action: "add", icon: "fa-plus", label: "Add an account" }] });
  const footer = moduleFooter({ actions: [{ action: "export", icon: "fa-file-export", label: "Export" }] });
  const buttons = [...bar.querySelectorAll("button"), ...footer.querySelectorAll("button")];
  expect(buttons.length).toBeGreaterThan(1);
  for (const button of buttons) {
    expect(button.hasAttribute("title")).toBe(false);
    expect(button.dataset.pavTip).toBeTruthy();
    expect(button.dataset.pavTip).toBe(button.getAttribute("aria-label"));
  }
});

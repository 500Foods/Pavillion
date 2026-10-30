/**
 * @vitest-environment jsdom
 */
import { expect, test } from "vitest";
import { dropIconPaint, paintIcon, removedText, sanitizeIconHtml } from "../src/common/iconsan.js";

const ASSET_ID = `${"ab".repeat(32)}`;
const ASSET_SRC = `pav-asset:${ASSET_ID}`;

function names(result) {
  const found = [];
  const walk = (node) => {
    if (!node) {
      return;
    }
    if (node.nodeType === 1) {
      found.push(String(node.localName || "").toLowerCase());
    }
    for (const child of node.childNodes || []) {
      walk(child);
    }
  };
  walk(result.fragment);
  return found;
}

test("hostile snippets are refused whole", () => {
  const cases = [
    ["<script>alert(1)</script>", "script"],
    ["<img src='pav-system:mark' onerror='alert(1)'>", "onerror"],
    ["<svg onload='alert(1)'></svg>", "onload"],
    ["<svg><script>alert(1)</script></svg>", "script"],
    ["<a href='javascript:alert(1)'>x</a>", "a"],
    ["<svg><use href='https://evil.example/a.svg#x'></use></svg>", "href"],
    ["<svg><foreignObject><div></div></foreignObject></svg>", "foreignobject"],
    ["<svg><style>rect { fill: url(https://evil.example/a) }</style></svg>", "url("],
    ["<i style='background:url(https://evil.example/a)'></i>", "url("],
    ["<i style='background:&#117;rl(https://evil.example/a)'></i>", "url("],
    ["<math><mi>x</mi></math>", "math"],
    ["<template><script>1</script></template>", "template"],
    ["<iframe src='https://evil.example/'></iframe>", "iframe"],
    ["<i class='fa-solid fa-star'><!--</i><script>1</script>", "comment"],
    ["<!--<!--><script>1</script>-->-->", "comment"],
    [`<i class="fa-solid fa-star">${"a".repeat(100 * 1024)}</i>`, "over 8 KB"],
  ];
  for (const [html, needle] of cases) {
    const result = sanitizeIconHtml(html);
    expect(result.ok, html).toBe(false);
    expect(result.fragment, html).toBeNull();
    expect(result.removed.join(" "), html).toContain(needle);
    expect(removedText(result.removed).startsWith("removed:")).toBe(true);
  }
  let deep = "<i class='fa-solid fa-star'></i>";
  for (let n = 0; n < 9; n += 1) {
    deep = `<div>${deep}</div>`;
  }
  const nested = sanitizeIconHtml(deep);
  expect(nested.ok).toBe(false);
  expect(nested.removed.join(" ")).toContain("depth");
});

test("gradients, animation, and stored images pass", () => {
  const gradient = sanitizeIconHtml(`<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 10 10"><defs><linearGradient id="g"><stop offset="0" stop-color="#800000"/><stop offset="1" stop-color="#cc9999"/></linearGradient></defs><rect width="10" height="10" fill="url(#g)"/></svg>`);
  expect(gradient.ok).toBe(true);
  expect(names(gradient)).toContain("lineargradient");
  expect(gradient.fragment.querySelector("rect").getAttribute("fill")).toBe("url(#g)");

  const motion = sanitizeIconHtml(`<svg viewBox="0 0 10 10"><rect width="10" height="10"><animate attributeName="x" from="0" to="8" dur="1s" repeatCount="indefinite"/></rect></svg>`);
  expect(motion.ok).toBe(true);
  expect(names(motion)).toContain("animate");

  const asset = sanitizeIconHtml(`<img src="${ASSET_SRC}" alt="">`);
  expect(asset.ok).toBe(true);
  expect(asset.fragment.querySelector("img").getAttribute("src")).toBe(ASSET_SRC);

  const mark = sanitizeIconHtml(`<img src="pav-system:mark" alt="">`);
  expect(mark.ok).toBe(true);
  expect(mark.fragment.querySelector("img").getAttribute("src")).toBe("pav-system:mark");
});

test("the painter caches a snippet and loads icon schemes", () => {
  dropIconPaint();
  const first = paintIcon(`<img src="${ASSET_SRC}" alt="">`);
  const again = paintIcon(`<img src="${ASSET_SRC}" alt="">`);
  expect(first.ok).toBe(true);
  expect(first.fragment.querySelector("img").getAttribute("src")).toBe(`pav-asset://${ASSET_ID}`);
  expect(again.fragment.querySelector("img").getAttribute("src")).toBe(`pav-asset://${ASSET_ID}`);
  expect(first.fragment).not.toBe(again.fragment);
  const mark = paintIcon(`<img src="pav-system:mark" alt="">`);
  const markImg = mark.fragment.querySelector("img");
  expect(markImg.getAttribute("src")).toBe("pav-system://mark");
  expect(markImg.getAttribute("srcset")).toBe("/mark-64.png 64w, /mark-128.png 128w, /mark-256.png 256w");
  expect(markImg.getAttribute("sizes")).toBe("3rem");
  dropIconPaint(`<img src="${ASSET_SRC}" alt="">`);
});

test("a sibling script refuses the whole snippet", () => {
  const result = sanitizeIconHtml("<i class='fa-solid fa-star'></i><script>1</script>");
  expect(result.ok).toBe(false);
  expect(result.fragment).toBeNull();
});

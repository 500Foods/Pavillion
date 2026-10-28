/* Library, Foundry, and image outputs. The document stays text until Accept. */
import { sanitizeIconHtml } from "../iconsan.js";

export const PNG_RENDER = 128;

export const FA_FAMILIES = [
  { id: "classic", label: "Classic", prefix: "" },
  { id: "sharp", label: "Sharp", prefix: "fa-sharp" },
  { id: "duotone", label: "Duotone", prefix: "fa-duotone" },
  { id: "sharp-duotone", label: "Sharp Duotone", prefix: "fa-sharp-duotone" },
  { id: "brands", label: "Brands", prefix: "fa-brands" },
];

export const FA_STYLES = [
  { id: "solid", label: "Solid", cls: "fa-solid" },
  { id: "regular", label: "Regular", cls: "fa-regular" },
  { id: "light", label: "Light", cls: "fa-light" },
  { id: "thin", label: "Thin", cls: "fa-thin" },
  { id: "semibold", label: "Semibold", cls: "fa-semibold" },
];

export const FA_SIZES = ["", "fa-2xs", "fa-xs", "fa-sm", "fa-lg", "fa-xl", "fa-2xl"];

const NAME_OK = /^[a-z0-9-]{1,80}$/;
const COLOR_OK = /^#[0-9a-fA-F]{6}$/;

export function assetSnippet(id) {
  return `<img src="pav-asset:${id}" alt="">`;
}

export function needsFoundry(choice) {
  return Boolean(choice && choice.rotate && choice.flip);
}

export function librarySnippet(choice) {
  const pick = choice || {};
  if (pick.system === "mark") {
    return `<img src="pav-system:mark" alt="">`;
  }
  const name = NAME_OK.test(pick.name) ? pick.name : "star";
  const libraryId = pick.libraryId || pick.library || "";
  if (libraryId === "material") {
    const face = pick.materialStyle === "outlined" || pick.materialStyle === "rounded" ? pick.materialStyle : "sharp";
    const color = COLOR_OK.test(pick.color || "") ? ` style="color: ${pick.color}"` : "";
    return `<span class="material-symbols-${face}"${color}>${name}</span>`;
  }
  const family = FA_FAMILIES.find((item) => item.id === pick.family) || FA_FAMILIES[3];
  const style = family.id === "brands" ? "" : (FA_STYLES.find((item) => item.id === pick.style) || FA_STYLES[0]).cls;
  const extras = [];
  if (pick.fixed) {
    extras.push("fa-fw");
  }
  if (pick.size && FA_SIZES.includes(pick.size)) {
    extras.push(pick.size);
  }
  if (pick.rotate && !pick.flip) {
    extras.push(`fa-rotate-${pick.rotate}`);
  }
  if (pick.flip && !pick.rotate) {
    extras.push(pick.flip === "v" ? "fa-flip-vertical" : "fa-flip-horizontal");
  }
  const classes = [family.prefix, style, `fa-${name}`, ...extras].filter(Boolean).join(" ");
  const color = COLOR_OK.test(pick.color || "") ? ` style="color: ${pick.color}"` : "";
  return `<i class="${classes}"${color}></i>`;
}

/* Rotate on the outside, flip on the inside. That is the Foundry nesting. */
export function foundryFromLibrary(choice) {
  const inner = librarySnippet({ ...choice, rotate: 0, flip: "" });
  const rot = choice && choice.rotate ? ` fa-rotate-${choice.rotate}` : "";
  return `<span class="fa-layers${rot}">${inner}</span>`;
}

function elementOpen(text, cursor) {
  const before = String(text).slice(0, cursor);
  const starts = ["<i", "<span", "<div"].map((token) => before.lastIndexOf(token));
  const start = Math.max(...starts);
  if (start < 0) {
    return null;
  }
  const end = String(text).indexOf(">", start);
  if (end < 0 || end < cursor && String(text).slice(start, end).includes("<")) {
    return null;
  }
  return { start, end };
}

export function addClassAt(text, cursor, cls) {
  const el = elementOpen(text, cursor);
  if (!el || !cls) {
    return String(text);
  }
  const open = text.slice(el.start, el.end);
  const next = open.includes('class="')
    ? open.replace('class="', `class="${cls} `)
    : `${open} class="${cls}"`;
  return text.slice(0, el.start) + next + text.slice(el.end);
}

export function wrapRange(text, start, end, open, close) {
  const source = String(text);
  const from = Math.max(0, Math.min(start, source.length));
  const to = Math.max(from, Math.min(end, source.length));
  return source.slice(0, from) + open + source.slice(from, to) + close + source.slice(to);
}

export function guessMode(html, kind) {
  const text = String(html || "").trim();
  if (!text) {
    return "library";
  }
  if (text.includes("pav-system:")) {
    return "library";
  }
  if (text.includes("pav-asset:")) {
    if (kind === "png-render" || kind === "png-original" || kind === "image/png") {
      return "png";
    }
    return "svg";
  }
  if (/^<(?:i|span|img)\b/i.test(text)) {
    return "library";
  }
  return "foundry";
}

const SVG_NS = "http://www.w3.org/2000/svg";

function transformOf(transform) {
  const item = transform || {};
  const parts = [];
  if (item.tx || item.ty) {
    parts.push(`translate(${item.tx || 0} ${item.ty || 0})`);
  }
  if (item.rotate) {
    parts.push(`rotate(${item.rotate})`);
  }
  const sx = (item.flipH ? -1 : 1) * (item.scale || 1);
  const sy = (item.flipV ? -1 : 1) * (item.scale || 1);
  if (sx !== 1 || sy !== 1) {
    parts.push(`scale(${sx} ${sy})`);
  }
  return parts.join(" ");
}

/* Bake the view into a wrapping <g>. The stored snippet is always an img. */
export function bakeSvg(svgText, transform, documentRef = globalThis.document) {
  const parsed = sanitizeIconHtml(String(svgText || ""), documentRef);
  if (!parsed.ok) {
    return parsed;
  }
  const svg = parsed.fragment.querySelector("svg");
  if (!svg) {
    return { ok: false, fragment: null, removed: ["svg"] };
  }
  const g = documentRef.createElementNS(SVG_NS, "g");
  const value = transformOf(transform);
  if (value) {
    g.setAttribute("transform", value);
  }
  while (svg.firstChild) {
    g.append(svg.firstChild);
  }
  svg.append(g);
  if (transform && transform.crop) {
    svg.setAttribute("viewBox", `0 0 ${PNG_RENDER} ${PNG_RENDER}`);
  }
  const xml = new XMLSerializer().serializeToString(svg);
  return { ok: true, svg: xml, removed: [] };
}

export function paintRender(ctx, image, transform) {
  const canvas = ctx.canvas;
  canvas.width = PNG_RENDER;
  canvas.height = PNG_RENDER;
  const item = transform || {};
  ctx.clearRect(0, 0, PNG_RENDER, PNG_RENDER);
  if (item.background && item.background !== "none") {
    ctx.fillStyle = item.background;
    ctx.fillRect(0, 0, PNG_RENDER, PNG_RENDER);
  }
  ctx.save();
  ctx.translate(PNG_RENDER / 2 + (item.tx || 0), PNG_RENDER / 2 + (item.ty || 0));
  ctx.rotate(((item.rotate || 0) * Math.PI) / 180);
  const scale = item.scale || 1;
  ctx.scale((item.flipH ? -1 : 1) * scale, (item.flipV ? -1 : 1) * scale);
  const w = image && image.width ? image.width : PNG_RENDER;
  const h = image && image.height ? image.height : PNG_RENDER;
  ctx.drawImage(image, -w / 2, -h / 2, w, h);
  ctx.restore();
}

export function pushRecent(list, html, max) {
  const cap = Math.min(48, Math.max(1, max || 12));
  const next = [html, ...(Array.isArray(list) ? list : [])].filter((item, index, all) => {
    return typeof item === "string" && item.trim() !== "" && all.indexOf(item) === index;
  });
  return next.slice(0, cap);
}

export function putOverride(icons, key, html) {
  return { ...(icons || {}), [key]: html };
}

export function dropOverride(icons, key) {
  const next = { ...(icons || {}) };
  delete next[key];
  return next;
}

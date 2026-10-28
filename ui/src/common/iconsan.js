/* D-9 / F-HARDEN Icons. Blocklist sanitizer for icon snippets.
   Parses, walks, and rebuilds. Never reinserts a parsed node.
   ok: false refuses the whole snippet. */

const SVG_NS = "http://www.w3.org/2000/svg";
const XLINK_NS = "http://www.w3.org/1999/xlink";
const XMLNS_NS = "http://www.w3.org/2000/xmlns/";

export const ICON_MAX_BYTES = 8192;
export const ICON_MAX_DEPTH = 8;
export const ICON_MAX_NODES = 64;

const REFUSED = new Set([
  "script", "iframe", "object", "embed", "foreignobject", "applet", "frame", "frameset",
  "link", "meta", "base", "form", "input", "button", "textarea", "select", "math",
  "template", "noscript", "audio", "video", "canvas",
]);

const HTML_TAGS = new Set(["i", "span", "div", "img"]);

const SVG_TAGS = new Set([
  "svg", "g", "path", "circle", "rect", "ellipse", "line", "polyline", "polygon", "defs",
  "lineargradient", "radialgradient", "stop", "clippath", "mask", "pattern", "symbol",
  "marker", "use", "text", "tspan", "textpath", "title", "desc", "style", "filter",
  "feblend", "fecolormatrix", "fecomponenttransfer", "fecomposite", "feconvolvematrix",
  "fediffuselighting", "fedisplacementmap", "fedistantlight", "fedropshadow", "feflood",
  "fefunca", "fefuncb", "fefuncg", "fefuncr", "fegaussianblur", "feimage", "femerge",
  "femergenode", "femorphology", "feoffset", "fepointlight", "fespecularlighting",
  "fespotlight", "fetile", "feturbulence", "animate", "animatetransform", "animatemotion", "set",
]);

const TAG_CANON = {
  lineargradient: "linearGradient",
  radialgradient: "radialGradient",
  clippath: "clipPath",
  textpath: "textPath",
  feblend: "feBlend",
  fecolormatrix: "feColorMatrix",
  fecomponenttransfer: "feComponentTransfer",
  fecomposite: "feComposite",
  feconvolvematrix: "feConvolveMatrix",
  fediffuselighting: "feDiffuseLighting",
  fedisplacementmap: "feDisplacementMap",
  fedistantlight: "feDistantLight",
  fedropshadow: "feDropShadow",
  feflood: "feFlood",
  fefunca: "feFuncA",
  fefuncb: "feFuncB",
  fefuncg: "feFuncG",
  fefuncr: "feFuncR",
  fegaussianblur: "feGaussianBlur",
  feimage: "feImage",
  femerge: "feMerge",
  femergenode: "feMergeNode",
  femorphology: "feMorphology",
  feoffset: "feOffset",
  fepointlight: "fePointLight",
  fespecularlighting: "feSpecularLighting",
  fespotlight: "feSpotLight",
  fetile: "feTile",
  feturbulence: "feTurbulence",
  animatetransform: "animateTransform",
  animatemotion: "animateMotion",
};

const TEXT_OK = new Set(["i", "span", "div", "title", "desc", "text", "tspan", "textpath", "style"]);

const STYLE_PROPS = new Set([
  "color", "background", "background-color", "fill", "stroke", "opacity", "font-size",
  "font-variation-settings", "transform", "rotate", "scale", "translate", "vertical-align",
  "animation", "animation-duration", "animation-timing-function",
]);
const STYLE_VALUE = /^[#a-zA-Z0-9 .,%()+_-]{1,200}$/;
const CUSTOM_PROP = /^--[a-zA-Z0-9-]{1,40}$/;

const ATTR = new Map();
function allowAttr(canon) {
  ATTR.set(canon.toLowerCase(), canon);
}
[
  "viewBox", "d", "fill", "stroke", "transform", "cx", "cy", "r", "x", "y", "x1", "y1", "x2", "y2",
  "width", "height", "rx", "ry", "points", "offset", "stop-color", "stop-opacity", "opacity",
  "fill-opacity", "stroke-opacity", "stroke-width", "stroke-linecap", "stroke-linejoin",
  "stroke-miterlimit", "stroke-dasharray", "stroke-dashoffset", "fill-rule", "clip-rule",
  "clip-path", "mask", "filter", "color", "display", "visibility", "overflow",
  "color-interpolation", "color-interpolation-filters",
  "gradientUnits", "gradientTransform", "spreadMethod", "fx", "fy", "fr",
  "filterUnits", "primitiveUnits", "result", "in", "in2", "stdDeviation", "type", "mode",
  "operator", "k1", "k2", "k3", "k4", "values", "surfaceScale", "diffuseConstant",
  "specularConstant", "specularExponent", "kernelMatrix", "order", "targetX", "targetY",
  "edgeMode", "divisor", "bias", "preserveAlpha", "scale", "xChannelSelector", "yChannelSelector",
  "flood-color", "flood-opacity", "lighting-color", "numOctaves", "seed", "stitchTiles",
  "baseFrequency", "limitingConeAngle", "pointsAtX", "pointsAtY", "pointsAtZ", "z", "azimuth",
  "elevation", "dx", "dy", "radius", "amplitude", "exponent", "intercept", "slope", "tableValues",
  "attributeName", "attributeType", "from", "to", "by", "dur", "begin", "end", "repeatCount",
  "repeatDur", "keyTimes", "keySplines", "keyPoints", "calcMode", "restart", "additive", "accumulate",
  "markerWidth", "markerHeight", "markerUnits", "orient", "refX", "refY",
  "patternUnits", "patternContentUnits", "patternTransform",
  "maskUnits", "maskContentUnits", "mask-type", "clipPathUnits", "preserveAspectRatio",
  "vector-effect", "dominant-baseline", "text-anchor", "font-family", "font-size", "font-weight",
  "letter-spacing", "lengthAdjust", "startOffset", "method", "spacing", "side", "path", "rotate", "origin",
].forEach(allowAttr);

const ASSET_SRC = /^pav-asset:[0-9a-f]{64}$/;
const SYSTEM_SRC = "pav-system:mark";
const FRAGMENT_HREF = /^#[A-Za-z_][A-Za-z0-9_.:-]{0,63}$/;
const ID_TOKEN = /^[A-Za-z_][A-Za-z0-9_-]{0,63}$/;
const CLASS_OK = /^[A-Za-z0-9_:-][A-Za-z0-9_ :.-]{0,400}$/;
const URL_FRAGMENT = /^url\(\s*#[A-Za-z_][A-Za-z0-9_.:-]{0,63}\s*\)$/;

const paintCache = new Map();
const warnedKeys = new Set();

function parserFor(documentRef) {
  const view = documentRef && documentRef.defaultView;
  const Parser = (view && view.DOMParser) || globalThis.DOMParser;
  return Parser ? new Parser() : null;
}

function fail(removed) {
  return { ok: false, fragment: null, removed };
}

function pushUnique(list, item) {
  if (item && !list.includes(item)) {
    list.push(item);
  }
}

function dangerousText(value) {
  const lower = String(value).toLowerCase();
  if (lower.includes("url(") || lower.includes("expression(") || lower.includes("@import")) {
    return "url(";
  }
  if (String(value).includes("\\")) {
    return "backslash";
  }
  if (lower.includes("javascript:")) {
    return "javascript:";
  }
  return "";
}

/* Filtered style attribute. A bad declaration refuses the snippet. */
export function styleProblem(raw) {
  const source = String(raw ?? "");
  const danger = dangerousText(source);
  if (danger) {
    return danger;
  }
  const parts = source.split(";");
  for (const part of parts) {
    const piece = part.trim();
    if (!piece) {
      continue;
    }
    const colon = piece.indexOf(":");
    if (colon <= 0) {
      return "style";
    }
    const prop = piece.slice(0, colon).trim().toLowerCase();
    const value = piece.slice(colon + 1).trim();
    const custom = prop.startsWith("--");
    if (custom ? !CUSTOM_PROP.test(prop) : !STYLE_PROPS.has(prop)) {
      return prop || "style";
    }
    if (!STYLE_VALUE.test(value)) {
      return prop;
    }
  }
  return "";
}

function styleText(raw) {
  const source = String(raw ?? "");
  if (source.includes("<")) {
    return "style";
  }
  return dangerousText(source);
}

function schemeValue(value) {
  return /^[a-zA-Z][a-zA-Z0-9+.-]*:/.test(String(value).trim());
}

function valueProblem(value) {
  const text = String(value);
  if (text.length > 2000) {
    return "too long";
  }
  if (/[<>\\`]/.test(text)) {
    return "bad character";
  }
  const danger = dangerousText(text);
  if (danger === "url(") {
    if (!URL_FRAGMENT.test(text.trim())) {
      return "url(";
    }
    return "";
  }
  if (danger) {
    return danger;
  }
  if (schemeValue(text)) {
    return "scheme";
  }
  return "";
}

function attrProblem(tag, attr) {
  const rawName = attr.localName || attr.name || "";
  const name = rawName.toLowerCase();
  const value = attr.value;
  if (name.startsWith("on")) {
    return `${name} on ${tag}`;
  }
  if (attr.namespaceURI === XMLNS_NS || name === "xmlns" || name.startsWith("xmlns:")) {
    if (value === SVG_NS || value === XLINK_NS) {
      return "";
    }
    return `xmlns on ${tag}`;
  }
  if (name === "style") {
    const problem = styleProblem(value);
    return problem ? `${problem} on ${tag}` : "";
  }
  if (name === "class") {
    return CLASS_OK.test(value) ? "" : `class on ${tag}`;
  }
  if (name === "id") {
    return ID_TOKEN.test(value) ? "" : `id on ${tag}`;
  }
  if (name === "role") {
    return /^[a-z][a-z0-9-]{0,32}$/.test(value) ? "" : `role on ${tag}`;
  }
  if (name === "alt") {
    return tag === "img" && value.length <= 80 && !/[<>]/.test(value) ? "" : `alt on ${tag}`;
  }
  if (name.startsWith("data-fa-")) {
    if (!/^data-fa-[a-z0-9-]{1,40}$/.test(name)) {
      return `${name} on ${tag}`;
    }
    return valueProblem(value) ? `${name} on ${tag}` : "";
  }
  if (name.startsWith("aria-")) {
    if (!/^aria-[a-z0-9-]{1,40}$/.test(name)) {
      return `${name} on ${tag}`;
    }
    return value.length <= 200 && !/[<>\\]/.test(value) ? "" : `${name} on ${tag}`;
  }
  if (name === "src") {
    if (tag !== "img" || (value !== SYSTEM_SRC && !ASSET_SRC.test(value))) {
      return `src on ${tag}`;
    }
    return "";
  }
  if (name === "href" || name === "xlink:href" || (attr.namespaceURI === XLINK_NS && name === "href")) {
    if (tag !== "use" || !FRAGMENT_HREF.test(value.trim())) {
      return `href on ${tag}`;
    }
    return "";
  }
  if (name === "srcset") {
    return `srcset on ${tag}`;
  }
  if (!ATTR.has(name)) {
    return `${name} on ${tag}`;
  }
  const problem = valueProblem(value);
  return problem ? `${name} on ${tag}` : "";
}

function scan(node, depth, parentTag, ctx) {
  if (!node) {
    return;
  }
  if (node.nodeType === 3) {
    if (!node.textContent || !node.textContent.trim()) {
      return;
    }
    if (!TEXT_OK.has(parentTag)) {
      pushUnique(ctx.removed, `text in ${parentTag || "root"}`);
      ctx.bad = true;
      return;
    }
    if (parentTag === "style") {
      const problem = styleText(node.textContent);
      if (problem) {
        pushUnique(ctx.removed, `${problem} in style`);
        ctx.bad = true;
      }
    }
    return;
  }
  if (node.nodeType === 8 || node.nodeType === 7) {
    pushUnique(ctx.removed, "comment");
    ctx.bad = true;
    return;
  }
  if (node.nodeType !== 1) {
    return;
  }
  ctx.count += 1;
  if (ctx.count > ICON_MAX_NODES) {
    pushUnique(ctx.removed, "over 64 elements");
    ctx.bad = true;
    return;
  }
  if (depth > ICON_MAX_DEPTH) {
    pushUnique(ctx.removed, "over depth 8");
    ctx.bad = true;
    return;
  }
  const tag = (node.localName || node.tagName || "").toLowerCase();
  if (tag === "parsererror") {
    pushUnique(ctx.removed, "parse error");
    ctx.bad = true;
    return;
  }
  if (REFUSED.has(tag) || !HTML_TAGS.has(tag) && !SVG_TAGS.has(tag)) {
    pushUnique(ctx.removed, tag || "element");
    ctx.bad = true;
  } else if (node.attributes) {
    for (const attr of node.attributes) {
      const problem = attrProblem(tag, attr);
      if (problem) {
        pushUnique(ctx.removed, problem);
        ctx.bad = true;
      }
    }
  }
  for (const child of node.childNodes) {
    scan(child, depth + 1, tag, ctx);
  }
}

function rebuild(node, parentTag, documentRef) {
  if (node.nodeType === 3) {
    if (!TEXT_OK.has(parentTag) || !node.textContent) {
      return null;
    }
    return documentRef.createTextNode(node.textContent);
  }
  if (node.nodeType !== 1) {
    return null;
  }
  const tag = (node.localName || "").toLowerCase();
  let el;
  if (HTML_TAGS.has(tag)) {
    el = documentRef.createElement(tag);
  } else {
    el = documentRef.createElementNS(SVG_NS, TAG_CANON[tag] || tag);
  }
  for (const attr of node.attributes) {
    const rawName = (attr.localName || attr.name || "").toLowerCase();
    if (rawName === "xmlns" || rawName.startsWith("xmlns:") || attr.namespaceURI === XMLNS_NS) {
      continue;
    }
    if (rawName === "style") {
      const text = filteredStyle(attr.value);
      if (text) {
        el.setAttribute("style", text);
      }
      continue;
    }
    if (rawName === "href" || (attr.namespaceURI === XLINK_NS && rawName === "href")) {
      el.setAttribute("href", attr.value.trim());
      continue;
    }
    if (rawName === "class") {
      el.setAttribute("class", attr.value);
      continue;
    }
    const canon = ATTR.get(rawName) || rawName;
    el.setAttribute(canon, attr.value);
  }
  for (const child of node.childNodes) {
    const next = rebuild(child, tag, documentRef);
    if (next) {
      el.append(next);
    }
  }
  return el;
}

function filteredStyle(raw) {
  const kept = [];
  for (const part of String(raw).split(";")) {
    const piece = part.trim();
    if (!piece) {
      continue;
    }
    const colon = piece.indexOf(":");
    const prop = piece.slice(0, colon).trim().toLowerCase();
    const value = piece.slice(colon + 1).trim();
    kept.push(`${prop}: ${value}`);
  }
  return kept.join("; ");
}

function rootsOf(doc, asSvg) {
  if (asSvg) {
    return doc.documentElement ? [doc.documentElement] : [];
  }
  const nodes = [];
  if (doc.head) {
    nodes.push(...doc.head.childNodes);
  }
  if (doc.body) {
    nodes.push(...doc.body.childNodes);
  }
  return nodes;
}

export function sanitizeIconHtml(html, documentRef = globalThis.document) {
  if (typeof html !== "string") {
    return fail(["not a string"]);
  }
  if (html.length > ICON_MAX_BYTES) {
    return fail(["over 8 KB"]);
  }
  if (html.includes("<!--") || html.includes("-->") || html.includes("<![") || html.includes("<?")) {
    return fail(["comment"]);
  }
  const parser = parserFor(documentRef);
  if (!parser || !documentRef) {
    return fail(["no parser"]);
  }
  const asSvg = /^\s*(?:<\?xml|<svg\b)/i.test(html);
  const doc = parser.parseFromString(html, asSvg ? "image/svg+xml" : "text/html");
  const ctx = { bad: false, removed: [], count: 0 };
  const roots = rootsOf(doc, asSvg);
  if (asSvg && roots.some((node) => (node.localName || "").toLowerCase() === "parsererror")) {
    return fail(["parse error"]);
  }
  for (const node of roots) {
    scan(node, 1, "", ctx);
  }
  if (ctx.bad) {
    return fail(ctx.removed);
  }
  const fragment = documentRef.createDocumentFragment();
  for (const node of roots) {
    if (node.nodeType === 3 && !node.textContent.trim()) {
      continue;
    }
    const next = rebuild(node, "", documentRef);
    if (next) {
      fragment.append(next);
    }
  }
  return { ok: true, fragment, removed: [] };
}

function rewriteSrc(node) {
  if (!node || node.nodeType !== 1 && node.nodeType !== 11) {
    return;
  }
  if (node.nodeType === 1 && (node.localName || "").toLowerCase() === "img") {
    const src = node.getAttribute("src") || "";
    if (ASSET_SRC.test(src)) {
      node.setAttribute("src", `pav-asset://${src.slice("pav-asset:".length)}`);
    } else if (src === SYSTEM_SRC) {
      node.setAttribute("src", "pav-system://mark");
    }
  }
  for (const child of node.children || []) {
    rewriteSrc(child);
  }
}

/* Cached by snippet text. The caller gets a clone with painted scheme URLs. */
export function paintIcon(html, documentRef = globalThis.document) {
  const key = typeof html === "string" ? html : "";
  let cached = paintCache.get(key);
  if (!cached) {
    cached = sanitizeIconHtml(key, documentRef);
    if (cached.ok) {
      paintCache.set(key, cached);
    }
  }
  if (!cached.ok) {
    return cached;
  }
  const fragment = cached.fragment.cloneNode(true);
  rewriteSrc(fragment);
  return { ok: true, fragment, removed: [] };
}

export function dropIconPaint(html) {
  if (html === undefined) {
    paintCache.clear();
    warnedKeys.clear();
    return;
  }
  paintCache.delete(String(html));
}

export function removedText(removed) {
  return removed && removed.length ? `removed: ${removed.join("; ")}` : "";
}

export function warnIconOnce(key, log) {
  if (!key || warnedKeys.has(key)) {
    return;
  }
  warnedKeys.add(key);
  if (typeof log === "function") {
    log(key);
  }
}

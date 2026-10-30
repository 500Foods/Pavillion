/* Library, Foundry, and image outputs. The document stays text until Accept. */
import { sanitizeIconHtml } from "../iconsan.js";
import { REGISTRY } from "./registry.js";

export const PNG_RENDER = 128;

/* Font Awesome 7 families. prefix is the class. prefixes are findIconDefinition keys.
   Semibold is Utility and Whiteboard, not a style of Classic, Sharp, or Duotone. */
const FA_FAMILY_ROWS = [
  { id: "classic", label: "Classic", prefix: "", styles: [
    { id: "solid", label: "Solid", cls: "fa-solid", prefixes: ["fas"] },
    { id: "regular", label: "Regular", cls: "fa-regular", prefixes: ["far"] },
    { id: "light", label: "Light", cls: "fa-light", prefixes: ["fal"] },
    { id: "thin", label: "Thin", cls: "fa-thin", prefixes: ["fat"] },
  ] },
  { id: "sharp", label: "Sharp", prefix: "fa-sharp", styles: [
    { id: "solid", label: "Solid", cls: "fa-solid", prefixes: ["fass"] },
    { id: "regular", label: "Regular", cls: "fa-regular", prefixes: ["fasr"] },
    { id: "light", label: "Light", cls: "fa-light", prefixes: ["fasl"] },
    { id: "thin", label: "Thin", cls: "fa-thin", prefixes: ["fast"] },
  ] },
  { id: "duotone", label: "Duotone", prefix: "fa-duotone", styles: [
    { id: "solid", label: "Solid", cls: "fa-solid", prefixes: ["fad", "fads"] },
    { id: "regular", label: "Regular", cls: "fa-regular", prefixes: ["fadr"] },
    { id: "light", label: "Light", cls: "fa-light", prefixes: ["fadl"] },
    { id: "thin", label: "Thin", cls: "fa-thin", prefixes: ["fadt"] },
  ] },
  { id: "sharp-duotone", label: "Sharp Duotone", prefix: "fa-sharp-duotone", styles: [
    { id: "solid", label: "Solid", cls: "fa-solid", prefixes: ["fasds"] },
    { id: "regular", label: "Regular", cls: "fa-regular", prefixes: ["fasdr"] },
    { id: "light", label: "Light", cls: "fa-light", prefixes: ["fasdl"] },
    { id: "thin", label: "Thin", cls: "fa-thin", prefixes: ["fasdt"] },
  ] },
  { id: "brands", label: "Brands", prefix: "fa-brands", prefixes: ["fab"], styles: [] },
  { id: "chisel", label: "Chisel", prefix: "fa-chisel", styles: [
    { id: "regular", label: "Regular", cls: "fa-regular", prefixes: ["facr"] },
  ] },
  { id: "etch", label: "Etch", prefix: "fa-etch", styles: [
    { id: "solid", label: "Solid", cls: "fa-solid", prefixes: ["faes"] },
  ] },
  { id: "jelly", label: "Jelly", prefix: "fa-jelly", styles: [
    { id: "regular", label: "Regular", cls: "fa-regular", prefixes: ["fajr"] },
  ] },
  { id: "jelly-duo", label: "Jelly Duo", prefix: "fa-jelly-duo", styles: [
    { id: "regular", label: "Regular", cls: "fa-regular", prefixes: ["fajdr"] },
  ] },
  { id: "jelly-fill", label: "Jelly Fill", prefix: "fa-jelly-fill", styles: [
    { id: "regular", label: "Regular", cls: "fa-regular", prefixes: ["fajfr"] },
  ] },
  { id: "notdog", label: "Notdog", prefix: "fa-notdog", styles: [
    { id: "solid", label: "Solid", cls: "fa-solid", prefixes: ["fans"] },
  ] },
  { id: "notdog-duo", label: "Notdog Duo", prefix: "fa-notdog-duo", styles: [
    { id: "solid", label: "Solid", cls: "fa-solid", prefixes: ["fands"] },
  ] },
  { id: "slab", label: "Slab", prefix: "fa-slab", styles: [
    { id: "regular", label: "Regular", cls: "fa-regular", prefixes: ["faslr"] },
  ] },
  { id: "slab-press", label: "Slab Press", prefix: "fa-slab-press", styles: [
    { id: "regular", label: "Regular", cls: "fa-regular", prefixes: ["faslpr"] },
  ] },
  { id: "thumbprint", label: "Thumbprint", prefix: "fa-thumbprint", styles: [
    { id: "light", label: "Light", cls: "fa-light", prefixes: ["fatl"] },
  ] },
  { id: "graphite", label: "Graphite", prefix: "fa-graphite", styles: [
    { id: "thin", label: "Thin", cls: "fa-thin", prefixes: ["fagt"] },
  ] },
  { id: "utility", label: "Utility", prefix: "fa-utility", styles: [
    { id: "semibold", label: "Semibold", cls: "fa-semibold", prefixes: ["fausb"] },
  ] },
  { id: "utility-duo", label: "Utility Duo", prefix: "fa-utility-duo", styles: [
    { id: "semibold", label: "Semibold", cls: "fa-semibold", prefixes: ["faudsb"] },
  ] },
  { id: "utility-fill", label: "Utility Fill", prefix: "fa-utility-fill", styles: [
    { id: "semibold", label: "Semibold", cls: "fa-semibold", prefixes: ["faufsb"] },
  ] },
  { id: "whiteboard", label: "Whiteboard", prefix: "fa-whiteboard", styles: [
    { id: "semibold", label: "Semibold", cls: "fa-semibold", prefixes: ["fawsb"] },
  ] },
  { id: "kit", label: "Kit", prefix: "fa-kit", prefixes: ["fak"], styles: [] },
  { id: "kit-duotone", label: "Kit Duotone", prefix: "fa-kit-duotone", prefixes: ["fakd"], styles: [] },
];

const FALLBACK_IDS = ["classic", "sharp", "duotone", "sharp-duotone", "brands"];

function copyFamily(row) {
  return {
    id: row.id,
    label: row.label,
    prefix: row.prefix,
    prefixes: [...(row.prefixes || [])],
    styles: (row.styles || []).map((style) => ({ ...style, prefixes: [...style.prefixes] })),
  };
}

function prefixKeys(defs) {
  if (!defs || typeof defs !== "object") {
    return [];
  }
  const keys = defs instanceof Map ? [...defs.keys()] : Object.keys(defs);
  return keys.filter((key) => {
    const bucket = defs instanceof Map ? defs.get(key) : defs[key];
    if (!bucket || typeof bucket !== "object") {
      return false;
    }
    if (bucket instanceof Map) {
      return bucket.size > 0;
    }
    return Object.keys(bucket).length > 0;
  });
}

function catalogFromDefinitions(fa) {
  const present = new Set(prefixKeys(fa && fa.library && fa.library.definitions));
  if (!present.size) {
    return null;
  }
  const families = [];
  for (const row of FA_FAMILY_ROWS) {
    const styles = (row.styles || [])
      .map((style) => ({
        ...style,
        prefixes: style.prefixes.filter((prefix) => present.has(prefix)),
      }))
      .filter((style) => style.prefixes.length);
    const bare = (row.prefixes || []).filter((prefix) => present.has(prefix));
    if (!styles.length && !bare.length) {
      continue;
    }
    families.push({
      id: row.id,
      label: row.label,
      prefix: row.prefix,
      prefixes: bare,
      styles,
    });
  }
  return families.length ? { source: "kit", families } : null;
}

function fallbackCatalog() {
  return {
    source: "fontawesome",
    families: FA_FAMILY_ROWS.filter((row) => FALLBACK_IDS.includes(row.id)).map(copyFamily),
  };
}

/* A successful read of the live kit is reused. The fallback is not cached as a kit. */
let kitCatalog = null;
let kitSource = null;

export function faCatalog(lookup) {
  const explicit = arguments.length > 0;
  const fa = explicit ? lookup : globalThis.FontAwesome;
  if (!explicit && kitCatalog && fa && kitSource === fa) {
    return kitCatalog;
  }
  const built = catalogFromDefinitions(fa);
  if (built) {
    if (!explicit) {
      kitCatalog = built;
      kitSource = fa;
    }
    return built;
  }
  return fallbackCatalog();
}

/* The menu lists every family the Pro+ kit can draw. Placement still uses
   faCatalog, which keeps only the prefixes the kit has filled. */
export function faFamilyList(lookup) {
  const explicit = arguments.length > 0;
  const fa = explicit ? lookup : globalThis.FontAwesome;
  if (fa && typeof fa === "object") {
    return FA_FAMILY_ROWS.map(copyFamily);
  }
  return fallbackCatalog().families;
}

function familyOf(catalog, familyId) {
  return catalog.families.find((item) => item.id === familyId) || null;
}

function prefixesFor(family, styleId) {
  if (!family) {
    return [];
  }
  if (!family.styles.length) {
    return family.prefixes || [];
  }
  const style = family.styles.find((item) => item.id === styleId);
  return style ? style.prefixes : [];
}

export const FA_FAMILIES = FALLBACK_IDS.map((id) => {
  const row = FA_FAMILY_ROWS.find((item) => item.id === id);
  return { id: row.id, label: row.label, prefix: row.prefix };
});

export const FA_STYLES = [
  { id: "solid", label: "Solid", cls: "fa-solid" },
  { id: "regular", label: "Regular", cls: "fa-regular" },
  { id: "light", label: "Light", cls: "fa-light" },
  { id: "thin", label: "Thin", cls: "fa-thin" },
];

export const FA_SIZES = ["", "fa-2xs", "fa-xs", "fa-sm", "fa-lg", "fa-xl", "fa-2xl"];

export const FA_FLAG_GROUPS = [
  { id: "size", label: "Size", exclusive: true, items: [
    { id: "fa-2xs", label: "2XS" },
    { id: "fa-xs", label: "XS" },
    { id: "fa-sm", label: "SM" },
    { id: "fa-lg", label: "LG" },
    { id: "fa-xl", label: "XL" },
    { id: "fa-2xl", label: "2XL" },
    { id: "fa-1x", label: "1x" },
    { id: "fa-2x", label: "2x" },
    { id: "fa-3x", label: "3x" },
    { id: "fa-4x", label: "4x" },
    { id: "fa-5x", label: "5x" },
    { id: "fa-6x", label: "6x" },
    { id: "fa-7x", label: "7x" },
    { id: "fa-8x", label: "8x" },
    { id: "fa-9x", label: "9x" },
    { id: "fa-10x", label: "10x" },
  ] },
  { id: "rotate", label: "Rotate", exclusive: true, items: [
    { id: "fa-rotate-90", label: "90°" },
    { id: "fa-rotate-180", label: "180°" },
    { id: "fa-rotate-270", label: "270°" },
  ] },
  { id: "flip", label: "Flip", exclusive: true, items: [
    { id: "fa-flip-horizontal", label: "Horizontal" },
    { id: "fa-flip-vertical", label: "Vertical" },
    { id: "fa-flip-both", label: "Both" },
  ] },
  /* Classes that restyle one icon. Float and stack classes need a parent, so they stay out. */
  { id: "styling", label: "Styling", exclusive: false, items: [
    { id: "fa-swap-opacity", label: "Swap opacity" },
    { id: "fa-border", label: "Border" },
    { id: "fa-inverse", label: "Inverse" },
  ] },
  { id: "width", label: "Width", exclusive: false, items: [
    { id: "fa-fw", label: "Fixed width" },
  ] },
  { id: "animate", label: "Animate", exclusive: false, items: [
    { id: "fa-beat", label: "Beat" },
    { id: "fa-fade", label: "Fade" },
    { id: "fa-beat-fade", label: "Beat fade" },
    { id: "fa-bounce", label: "Bounce" },
    { id: "fa-flip", label: "Flip" },
    { id: "fa-shake", label: "Shake" },
    { id: "fa-spin", label: "Spin" },
    { id: "fa-spin-pulse", label: "Spin pulse" },
    { id: "fa-spin-reverse", label: "Spin reverse" },
  ] },
];

const ROTATE_FLAG = /^fa-rotate-(90|180|270)$/;
const FLIP_FLAG = /^fa-flip-(horizontal|vertical|both)$/;
const STYLE_BY_CLASS = {
  "fa-solid": "solid",
  "fa-regular": "regular",
  "fa-light": "light",
  "fa-thin": "thin",
  "fa-semibold": "semibold",
};

function allowedFlags(flags) {
  if (!Array.isArray(flags)) {
    return null;
  }
  const wanted = new Set(flags);
  const out = [];
  for (const group of FA_FLAG_GROUPS) {
    for (const item of group.items) {
      if (wanted.has(item.id)) {
        out.push(item.id);
      }
    }
  }
  return out;
}

/* True when the kit is absent, or when it can draw this name in the selected family and style. */
export function faIconKnown(familyId, styleId, name, lookup) {
  const explicit = arguments.length >= 4;
  const fa = explicit ? lookup : globalThis.FontAwesome;
  if (!fa || typeof fa.findIconDefinition !== "function") {
    return true;
  }
  const catalog = explicit ? faCatalog(lookup) : faCatalog();
  const prefixes = prefixesFor(familyOf(catalog, familyId), styleId);
  if (!prefixes.length) {
    return false;
  }
  return prefixes.some((prefix) => Boolean(fa.findIconDefinition({ prefix, iconName: name })));
}

function bucketHas(defs, prefix, name) {
  if (!defs || typeof defs !== "object") {
    return false;
  }
  const bucket = defs instanceof Map ? defs.get(prefix) : defs[prefix];
  if (!bucket || typeof bucket !== "object") {
    return false;
  }
  if (bucket instanceof Map) {
    return bucket.has(name);
  }
  return Object.prototype.hasOwnProperty.call(bucket, name);
}

/* The kit's definition buckets are the sets that actually contain the name.
   findIconDefinition can answer for a prefix the icon is not in. */
function kitDraws(fa, prefixes, name) {
  const defs = fa && fa.library && fa.library.definitions;
  if (defs && typeof defs === "object") {
    return prefixes.some((prefix) => bucketHas(defs, prefix, name));
  }
  return prefixes.some((prefix) => Boolean(fa.findIconDefinition({ prefix, iconName: name })));
}

/* Where the kit can draw this name. The selected pair wins. A name that is only
   in the brands set is Brands next, so it is not drawn as another family.
   Another style of the selected family follows, then any other loaded family.
   Null when the kit has no such icon. An empty definition list is not a kit,
   so the selected pair is kept. */
export function faIconPlace(familyId, styleId, name, lookup) {
  const explicit = arguments.length >= 4;
  const fa = explicit ? lookup : globalThis.FontAwesome;
  if (!fa || typeof fa.findIconDefinition !== "function") {
    return { family: familyId, style: styleId };
  }
  const catalog = explicit ? faCatalog(lookup) : faCatalog();
  if (catalog.source !== "kit") {
    return { family: familyId, style: styleId };
  }
  const current = familyOf(catalog, familyId);
  const currentStyle = current && current.styles.some((item) => item.id === styleId)
    ? styleId
    : (current && current.styles[0] ? current.styles[0].id : "");
  if (current && kitDraws(fa, prefixesFor(current, current.styles.length ? currentStyle : ""), name)) {
    return { family: current.id, style: current.styles.length ? currentStyle : "" };
  }
  const brands = familyOf(catalog, "brands");
  if (brands && (!current || current.id !== "brands") && kitDraws(fa, prefixesFor(brands, ""), name)) {
    return { family: "brands", style: "" };
  }
  if (current) {
    for (const style of current.styles) {
      if (style.id === currentStyle) {
        continue;
      }
      if (kitDraws(fa, prefixesFor(current, style.id), name)) {
        return { family: current.id, style: style.id };
      }
    }
  }
  for (const family of catalog.families) {
    if (current && family.id === current.id) {
      continue;
    }
    if (!family.styles.length) {
      if (kitDraws(fa, prefixesFor(family, ""), name)) {
        return { family: family.id, style: "" };
      }
      continue;
    }
    for (const style of family.styles) {
      if (kitDraws(fa, prefixesFor(family, style.id), name)) {
        return { family: family.id, style: style.id };
      }
    }
  }
  return null;
}

/* Icon names the loaded kit can draw whose name contains the query. */
export function faKitNames(query, lookup) {
  const explicit = arguments.length >= 2;
  const fa = explicit ? lookup : globalThis.FontAwesome;
  const defs = fa && fa.library && fa.library.definitions;
  if (!defs || typeof defs !== "object") {
    return [];
  }
  const needle = String(query || "").trim().toLowerCase();
  const names = [];
  const seen = new Set();
  const buckets = defs instanceof Map ? [...defs.values()] : Object.values(defs);
  for (const bucket of buckets) {
    if (!bucket || typeof bucket !== "object") {
      continue;
    }
    const keys = bucket instanceof Map ? [...bucket.keys()] : Object.keys(bucket);
    for (const key of keys) {
      const name = String(key);
      if (seen.has(name) || (needle && !name.toLowerCase().includes(needle))) {
        continue;
      }
      seen.add(name);
      names.push(name);
      if (names.length >= 200) {
        return names;
      }
    }
  }
  return names;
}

const NAME_OK = /^[a-z0-9-]{1,80}$/;
const COLOR_OK = /^#[0-9a-fA-F]{6}$/;

/* The open Font Awesome icon. A flag edit rebuilds this name, not the default star. */
export function faChoiceFromHtml(html) {
  const text = String(html || "").trim();
  if (!/^<i\b/i.test(text)) {
    return null;
  }
  const open = text.match(/\bclass\s*=\s*["']([^"']*)["']/i);
  if (!open) {
    return null;
  }
  const classes = open[1].split(/\s+/).filter(Boolean);
  const families = FA_FAMILY_ROWS
    .filter((row) => row.prefix)
    .sort((a, b) => b.prefix.length - a.prefix.length);
  const flagGroup = new Map();
  for (const group of FA_FLAG_GROUPS) {
    for (const item of group.items) {
      flagGroup.set(item.id, group);
    }
  }
  let familyId = "";
  let styleId = "";
  const flags = [];
  let name = "";
  for (const cls of classes) {
    if (!familyId) {
      const row = families.find((item) => item.prefix === cls);
      if (row) {
        familyId = row.id;
        continue;
      }
    }
    if (!styleId && STYLE_BY_CLASS[cls]) {
      styleId = STYLE_BY_CLASS[cls];
      continue;
    }
    const group = flagGroup.get(cls);
    if (group) {
      if (group.exclusive) {
        for (let i = flags.length - 1; i >= 0; i -= 1) {
          if (flagGroup.get(flags[i]) === group) {
            flags.splice(i, 1);
          }
        }
      }
      if (!flags.includes(cls)) {
        flags.push(cls);
      }
      continue;
    }
    if (!name && /^fa-[a-z0-9-]{1,80}$/.test(cls)) {
      const bare = cls.slice(3);
      if (NAME_OK.test(bare)) {
        name = bare;
      }
    }
  }
  if (!name) {
    return null;
  }
  const choice = {
    name,
    family: familyId || "classic",
    flags: allowedFlags(flags) || [],
  };
  if (styleId) {
    choice.style = styleId;
  }
  const color = text.match(/\bstyle\s*=\s*["'][^"']*?\bcolor:\s*(#[0-9a-fA-F]{6})\b/i);
  if (color) {
    choice.color = color[1];
  }
  return choice;
}

export function assetSnippet(id) {
  return `<img src="pav-asset:${id}" alt="">`;
}

export function needsFoundry(choice) {
  if (!choice) {
    return false;
  }
  if (Array.isArray(choice.flags)) {
    return choice.flags.some((flag) => ROTATE_FLAG.test(flag)) && choice.flags.some((flag) => FLIP_FLAG.test(flag));
  }
  return Boolean(choice.rotate && choice.flip);
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
  /* The class follows the chosen family, including Brands and Graphite, even
     when the kit has not filled that prefix. Placement still uses faCatalog. */
  const families = faFamilyList();
  const family = families.find((item) => item.id === pick.family)
    || families.find((item) => item.id === "sharp-duotone")
    || families[0];
  const style = family.styles.find((item) => item.id === pick.style) || family.styles[0];
  const extras = [];
  const flags = allowedFlags(pick.flags);
  if (flags) {
    extras.push(...flags);
  } else {
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
  }
  const classes = [family.prefix, style && style.cls, `fa-${name}`, ...extras].filter(Boolean).join(" ");
  const color = COLOR_OK.test(pick.color || "") ? ` style="color: ${pick.color}"` : "";
  return `<i class="${classes}"${color}></i>`;
}

/* Rotate on the outside, flip on the inside. That is the Foundry nesting. */
export function foundryFromLibrary(choice) {
  const pick = choice || {};
  if (Array.isArray(pick.flags)) {
    const rotate = pick.flags.find((flag) => ROTATE_FLAG.test(flag)) || "";
    const inner = librarySnippet({ ...pick, flags: pick.flags.filter((flag) => flag !== rotate) });
    return `<span class="fa-layers${rotate ? ` ${rotate}` : ""}">${inner}</span>`;
  }
  const inner = librarySnippet({ ...pick, rotate: 0, flip: "" });
  const rot = pick.rotate ? ` fa-rotate-${pick.rotate}` : "";
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

/* The Icons column and the Wizard tab share this. pav-system is the roof-mark PNG. */
export function iconFormat(html, kind) {
  const text = String(html || "").trim();
  if (text.includes("pav-system:")) {
    return "png";
  }
  const next = guessMode(text, kind);
  if (next === "png" || next === "svg") {
    return next;
  }
  if (next === "foundry") {
    return "custom";
  }
  if (/material-symbols/.test(text) || /class="[^"]*\b(?:bi|ph|ti|ri)-/.test(text) || /iconify-icon/.test(text)) {
    return "other";
  }
  if (/fa-layers|fa-stack/.test(text)) {
    return "custom";
  }
  return "fa";
}

export function iconFormatLabel(id) {
  return {
    fa: "Font Awesome",
    other: "Other Library",
    png: "PNG",
    svg: "SVG",
    custom: "Custom",
  }[id] || "Font Awesome";
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

const ICON_EXPORT_KIND = "pavillion.icons";

/* Saved overrides only. A key equal to its shipped default is still included
   when it is stored. Recent icons and the theme are not. */
export function iconOverrideDocument(icons) {
  const map = icons && typeof icons === "object" ? icons : {};
  const out = {};
  for (const entry of REGISTRY) {
    const value = map[entry.key];
    if (typeof value === "string" && value.trim() !== "") {
      out[entry.key] = value;
    }
  }
  return `${JSON.stringify({ kind: ICON_EXPORT_KIND, icons: out }, null, 2)}\n`;
}

/* Null when the file is not an icon export. An empty object clears overrides. */
export function iconOverridesFromText(text) {
  let parsed;
  try {
    parsed = JSON.parse(String(text || ""));
  } catch {
    return null;
  }
  if (!parsed || parsed.kind !== ICON_EXPORT_KIND || !parsed.icons || typeof parsed.icons !== "object" || Array.isArray(parsed.icons)) {
    return null;
  }
  const known = new Set(REGISTRY.map((entry) => entry.key));
  const out = {};
  for (const [key, value] of Object.entries(parsed.icons)) {
    if (!known.has(key) || typeof value !== "string") {
      continue;
    }
    const trimmed = value.trim();
    if (trimmed === "") {
      continue;
    }
    const checked = sanitizeIconHtml(trimmed);
    if (checked.ok) {
      out[key] = trimmed;
    }
  }
  return out;
}

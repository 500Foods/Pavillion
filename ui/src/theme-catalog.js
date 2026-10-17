import darkCss from "../public/styles/dark.css?raw";
import lightCss from "../public/styles/light.css?raw";
import darkTemplate from "../public/styles/pavillion-dark.theme?raw";
import lightTemplate from "../public/styles/pavillion-light.theme?raw";

export const THEME_TEMPLATES = [
  { name: "Pavillion Dark.theme", text: darkTemplate },
  { name: "Pavillion Light.theme", text: lightTemplate },
];

const STARTERS = {
  "Pavillion Dark.theme": darkTemplate,
  "Pavillion Light.theme": lightTemplate,
};

const POSTER_CSS = [
  "html,body{margin:0;height:100%;background:transparent}",
  ".poster{width:1024px;height:768px;box-sizing:border-box;display:grid;grid-template-columns:220px 1fr 1fr;grid-template-rows:72px 1fr;background:var(--pav-surface-workspace);color:var(--pav-text);font-family:var(--pav-font,sans-serif)}",
  ".poster .nav{grid-row:1 / span 2;background:var(--pav-surface-nav);border-right:12px solid var(--pav-border)}",
  ".poster .mark{width:64px;height:64px;margin:28px;background:var(--pav-swatch-1)}",
  ".poster .tabs{grid-column:2 / span 2;background:var(--pav-surface-button);border-bottom:12px solid var(--pav-border)}",
  ".poster .list{background:var(--pav-surface-list);border-right:12px solid var(--pav-border)}",
  ".poster .detail{background:var(--pav-surface-detail)}",
].join("");

export function themeNameOk(name) {
  if (typeof name !== "string") {
    return false;
  }
  if (name.length < 7 || name.length > 200 || name.startsWith(".")) {
    return false;
  }
  if (!name.endsWith(".theme")) {
    return false;
  }
  return !/[\u0000-\u001f\u007f\\/:]/.test(name);
}

export function stripTags(value) {
  return String(value ?? "").replace(/<[^>]*>/g, "").replace(/\s+/g, " ").trim();
}

export function clip(value, limit = 20) {
  const clean = stripTags(value);
  if (clean.length <= limit) {
    return clean;
  }
  return clean.slice(0, limit);
}

export function unquote(value) {
  const text = String(value ?? "").trim();
  if (text.length >= 2) {
    const open = text[0];
    const close = text[text.length - 1];
    if ((open === '"' && close === '"') || (open === "'" && close === "'")) {
      return text.slice(1, -1);
    }
  }
  return text;
}

export function stripComments(css) {
  return String(css ?? "").replace(/\/\*[\s\S]*?\*\//g, "");
}

export function themeMap(css) {
  const clean = stripComments(css);
  const map = new Map();
  const scheme = /(?:^|[{;\s])color-scheme\s*:\s*([^;}]+)/.exec(clean);
  if (scheme) {
    map.set("color-scheme", scheme[1].trim());
  }
  for (const match of clean.matchAll(/(--pav-[a-z0-9-]+)\s*:\s*([^;}]+)/g)) {
    map.set(match[1], match[2].trim());
  }
  return map;
}

export function mapsEqual(left, right) {
  if (left.size !== right.size) {
    return false;
  }
  for (const [key, value] of left) {
    if (right.get(key) !== value) {
      return false;
    }
  }
  return true;
}

export function schemeOf(css) {
  const first = (themeMap(css).get("color-scheme") || "").split(/\s+/)[0];
  return first === "light" ? "light" : "dark";
}

export function parseHeader(text, filename) {
  const lines = String(text ?? "").split(/\r\n|\n|\r/).slice(0, 10);
  const found = {};
  for (const line of lines) {
    const cleaned = line.replace(/\/\*|\*\//g, " ").replace(/^\s*\*\s?/, "").trim();
    const match = /^(Title|Author|Version|Released):\s*(.*)$/.exec(cleaned);
    if (!match || found[match[1]] !== undefined) {
      continue;
    }
    found[match[1]] = stripTags(match[2]);
  }
  return {
    title: found.Title || filename,
    author: found.Author || "Not found",
    version: found.Version || "Not found",
    released: found.Released || "Not found",
  };
}

export function isHiddenStarter(name, text) {
  const canonical = STARTERS[name];
  if (canonical === undefined) {
    return false;
  }
  return mapsEqual(themeMap(text), themeMap(canonical));
}

export function isCustomStarter(name, text) {
  return STARTERS[name] !== undefined && !isHiddenStarter(name, text);
}

export function effectiveChoice(choice, themes) {
  if (choice === "dark" || choice === "light" || choice === "system") {
    return choice;
  }
  if (typeof choice !== "string" || !choice.startsWith("file:")) {
    return "system";
  }
  const name = choice.slice(5);
  const theme = (themes || []).find((item) => item && item.name === name);
  if (!theme) {
    return "system";
  }
  if (isHiddenStarter(theme.name, theme.text)) {
    return theme.name === "Pavillion Light.theme" ? "light" : "dark";
  }
  return choice;
}

function rewriteUrls(css) {
  return String(css).replace(/url\(\s*(['"]?)([^'")]+)\1\s*\)/gi, (_all, _quote, url) => {
    const clean = url.trim();
    if (clean.startsWith("data:")) {
      return `url("${clean}")`;
    }
    if (clean.startsWith("pav-theme:")) {
      const rest = clean.slice("pav-theme:".length).replace(/^\/+/, "");
      return `url("pav-theme://${rest}")`;
    }
    if (/^[a-z][a-z0-9+.-]*:/i.test(clean)) {
      return 'url("")';
    }
    if (clean.includes("..") || clean.includes("/") || clean.includes("\\") || clean.includes("?") || clean.includes("#")) {
      return 'url("")';
    }
    if (!/^[A-Za-z0-9._-]+\.(?:woff2?|ttf|otf|svg|png|jpe?g|gif|webp)$/.test(clean)) {
      return 'url("")';
    }
    return `url("pav-theme://${clean}")`;
  });
}

export function liveThemeCss(css) {
  const clean = stripComments(css);
  const decls = [];
  const scheme = /(?:^|[{;\s])color-scheme\s*:\s*([^;}]+)/.exec(clean);
  if (scheme) {
    decls.push(`color-scheme: ${rewriteUrls(scheme[1].trim())};`);
  }
  for (const match of clean.matchAll(/(--pav-[a-z0-9-]+)\s*:\s*([^;}]+)/g)) {
    decls.push(`${match[1]}: ${rewriteUrls(match[2].trim())};`);
  }
  const faces = [];
  for (const match of clean.matchAll(/@font-face\s*\{[^}]*\}/g)) {
    faces.push(rewriteUrls(match[0]));
  }
  const block = decls.length ? `.pav-root[data-theme="file"]{\n${decls.join("\n")}\n}` : "";
  return [block, ...faces].filter(Boolean).join("\n");
}

export function previewDocument(themeCss, themeAttr) {
  const attr = themeAttr === "light" || themeAttr === "dark" || themeAttr === "file" ? ` data-theme="${themeAttr}"` : "";
  const safe = String(themeCss ?? "").replace(/<\/style/gi, "<\\/style");
  return `<!DOCTYPE html><html><head><meta charset="utf-8"><style>${POSTER_CSS}${safe}</style></head><body><div class="pav-root poster"${attr}><div class="nav"><div class="mark"></div></div><div class="tabs"></div><div class="list"></div><div class="detail"></div></div></body></html>`;
}

function previewFor(id, fileText, systemIsLight) {
  if (id === "light" || (id === "system" && systemIsLight)) {
    return previewDocument(lightCss, "light");
  }
  if (id === "dark" || id === "system") {
    return previewDocument(darkCss, "dark");
  }
  return previewDocument(`${darkCss}\n${fileText || ""}`, "file");
}

export function buildCatalog(themes, systemIsLight) {
  const pinned = [
    { id: "light", title: "Light", author: "Pavillion", version: "1", released: "2026-10-17", filename: "" },
    { id: "dark", title: "Dark", author: "Pavillion", version: "1", released: "2026-10-17", filename: "" },
    { id: "system", title: "System", author: "Pavillion", system: true, filename: "" },
  ];
  const files = [];
  for (const theme of themes || []) {
    if (!theme || typeof theme.name !== "string" || typeof theme.text !== "string") {
      continue;
    }
    if (isHiddenStarter(theme.name, theme.text)) {
      continue;
    }
    const header = parseHeader(theme.text, theme.name);
    files.push({
      id: `file:${theme.name}`,
      title: header.title,
      author: header.author,
      version: header.version,
      released: header.released,
      filename: theme.name,
      custom: isCustomStarter(theme.name, theme.text),
      text: theme.text,
    });
  }
  files.sort((a, b) => a.title.localeCompare(b.title) || a.filename.localeCompare(b.filename));
  const rows = [...pinned, ...files];
  const labels = rows.map((row) => {
    const base = clip(row.title, 20);
    return row.custom ? `${base} Custom` : base;
  });
  const counts = new Map();
  for (const label of labels) {
    counts.set(label, (counts.get(label) || 0) + 1);
  }
  return rows.map((row, index) => {
    const duplicated = counts.get(labels[index]) > 1 && row.filename;
    const label = duplicated ? `${labels[index]} (${row.filename})` : labels[index];
    const fullBase = row.custom ? `${row.title} Custom` : row.title;
    return {
      id: row.id,
      label,
      titleFull: duplicated ? `${fullBase} (${row.filename})` : fullBase,
      author: clip(row.author, 20),
      authorFull: row.author,
      meta: row.system ? "Follows the desktop" : `${clip(row.version, 20)} · ${clip(row.released, 20)}`,
      metaFull: row.system ? "Follows the desktop" : `${row.version} · ${row.released}`,
      srcdoc: previewFor(row.id, row.text, systemIsLight),
    };
  });
}

export function safeAssetUrl(value) {
  const raw = unquote(value).trim();
  if (!raw || raw === "none") {
    return "";
  }
  if (/^data:image\/[a-z0-9.+-]+;base64,/i.test(raw)) {
    return raw;
  }
  if (raw.startsWith("pav-theme:")) {
    const rest = raw.slice("pav-theme:".length).replace(/^\/+/, "");
    if (!rest || rest.includes("..") || /[\\/?#]/.test(rest)) {
      return "";
    }
    return `pav-theme://${rest}`;
  }
  if (/^[A-Za-z0-9._-]+\.(?:svg|png|jpe?g|gif|webp)$/.test(raw)) {
    return `pav-theme://${raw}`;
  }
  return "";
}

export function iconFromHtml(html, documentRef = globalThis.document) {
  if (!documentRef || typeof DOMParser === "undefined") {
    return null;
  }
  const parsed = new DOMParser().parseFromString(String(html), "text/html");
  const body = parsed.body;
  if (!body || body.childElementCount !== 1) {
    return null;
  }
  const el = body.firstElementChild;
  if (!el || el.children.length > 0) {
    return null;
  }
  if (el.tagName === "I") {
    const node = documentRef.createElement("i");
    node.className = el.getAttribute("class") || "";
    return node;
  }
  if (el.tagName === "IMG") {
    const src = safeAssetUrl(el.getAttribute("src") || "");
    if (!src) {
      return null;
    }
    const node = documentRef.createElement("img");
    node.alt = "";
    node.src = src;
    return node;
  }
  return null;
}

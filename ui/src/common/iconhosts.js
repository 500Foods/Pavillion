import hosts from "./iconhosts.json";

export const SCRIPT_HOSTS = hosts.scripts;
export const STYLESHEET_HOSTS = hosts.stylesheets;
export const FONT_HOSTS = hosts.fonts;
export const LOADER_HOSTS = hosts.loader;

const PRESET_FA = "https://kit.fontawesome.com/45801522ef.js";
const PRESET_MATERIAL =
  "https://fonts.googleapis.com/css2?family=Material+Symbols+Sharp:opsz,wght,FILL,GRAD@20..48,100..700,0..1,-50..200";

export const FA_KIT_URL = PRESET_FA;
export const MATERIAL_URL = PRESET_MATERIAL;

export function defaultLibraries() {
  return [
    { id: "fontawesome", name: "Font Awesome", type: "script", url: PRESET_FA, enabled: true },
    { id: "material", name: "Material Symbols", type: "stylesheet", url: PRESET_MATERIAL, enabled: false },
  ];
}

function hostOf(url) {
  let rest = String(url || "");
  if (!rest.startsWith("https://")) {
    return "";
  }
  rest = rest.slice("https://".length);
  const slash = rest.search(/[/?#]/);
  const authority = slash === -1 ? rest : rest.slice(0, slash);
  if (authority === "" || authority.includes("@") || authority.includes(" ") || authority.includes("\\")) {
    return "";
  }
  const colon = authority.lastIndexOf(":");
  const host = colon === -1 ? authority : authority.slice(0, colon);
  return host.toLowerCase();
}

/* A library URL the page may load. Companion hosts (the kit loader, font files)
   are not values a user may type. */
export function libraryUrlError(type, url) {
  const allowed = type === "script" ? SCRIPT_HOSTS : type === "stylesheet" ? STYLESHEET_HOSTS : null;
  if (!allowed) {
    return "type must be script or stylesheet";
  }
  const raw = String(url || "").trim();
  if (!raw.startsWith("https://") || raw.includes(" ") || /[\r\n]/.test(raw)) {
    return "the URL must be https";
  }
  /* Userinfo is the only place an @ is credentials. A query @ is a font axis. */
  const rest = raw.slice("https://".length);
  const cut = rest.search(/[/?#]/);
  const authority = cut === -1 ? rest : rest.slice(0, cut);
  if (authority.includes("@")) {
    return "the URL must not carry credentials";
  }
  const host = hostOf(raw);
  if (!host || !allowed.includes(host)) {
    return `a new host needs a rebuild (${host || "missing host"} is not allowlisted)`;
  }
  return "";
}

export function librariesError(list) {
  if (!Array.isArray(list)) {
    return "icon libraries must be a list";
  }
  if (list.length > 16) {
    return "at most 16 icon libraries";
  }
  const seen = new Set();
  for (const entry of list) {
    if (!entry || typeof entry !== "object") {
      return "an icon library must be an object";
    }
    if (typeof entry.id !== "string" || !/^[a-z][a-z0-9-]{0,31}$/.test(entry.id)) {
      return "an icon library id must be a short lowercase name";
    }
    if (seen.has(entry.id)) {
      return "an icon library id is repeated";
    }
    seen.add(entry.id);
    if (typeof entry.name !== "string" || entry.name.trim() === "" || entry.name.length > 64) {
      return "an icon library needs a name";
    }
    if (typeof entry.enabled !== "boolean") {
      return "enabled must be true or false";
    }
    const urlError = libraryUrlError(entry.type, entry.url);
    if (urlError) {
      return urlError;
    }
  }
  return "";
}

/* Reduce a saved kit id, or a raw script tag the preview accepted, to the FA URL. */
export function fontAwesomeUrlFromKit(value) {
  const raw = String(value || "");
  const fromUrl = /kit\.fontawesome\.com\/([A-Za-z0-9]+)\.js/.exec(raw);
  const id = fromUrl ? fromUrl[1] : /^[A-Za-z0-9]+$/.test(raw.trim()) ? raw.trim() : "";
  if (!id) {
    return PRESET_FA;
  }
  return `https://kit.fontawesome.com/${id}.js`;
}

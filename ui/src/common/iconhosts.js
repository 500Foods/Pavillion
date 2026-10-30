import hosts from "./iconhosts.json";

export const SCRIPT_HOSTS = hosts.scripts;
export const STYLESHEET_HOSTS = hosts.stylesheets;
export const FONT_HOSTS = hosts.fonts;
export const LOADER_HOSTS = hosts.loader;
export const CONNECT_HOSTS = hosts.connect;

const FA_KIT = "45801522ef";

/* Families Pavillion ships. A style changes the URL only for Material and Phosphor.
   Font Awesome style stays on the kit. Tabler and Remix share one stylesheet. */
export const LIBRARY_CATALOG = [
  {
    id: "fontawesome",
    label: "Font Awesome",
    type: "script",
    styles: [
      { id: "sharp-duotone-solid", label: "Sharp duotone solid" },
      { id: "sharp-thin", label: "Sharp thin" },
      { id: "sharp-solid", label: "Sharp solid" },
      { id: "duotone-solid", label: "Duotone solid" },
      { id: "classic-solid", label: "Classic solid" },
      { id: "classic-regular", label: "Classic regular" },
    ],
  },
  {
    id: "material",
    label: "Material Symbols",
    type: "stylesheet",
    styles: [
      { id: "sharp", label: "Sharp" },
      { id: "outlined", label: "Outlined" },
      { id: "rounded", label: "Rounded" },
    ],
  },
  {
    id: "iconify",
    label: "Iconify",
    type: "script",
    url: "https://code.iconify.design/iconify-icon/3.0.0/iconify-icon.min.js",
    styles: [{ id: "any", label: "Any" }],
  },
  {
    id: "bootstrap",
    label: "Bootstrap Icons",
    type: "stylesheet",
    url: "https://cdn.jsdelivr.net/npm/bootstrap-icons@1.13.1/font/bootstrap-icons.min.css",
    styles: [{ id: "regular", label: "Regular" }],
  },
  {
    id: "phosphor",
    label: "Phosphor",
    type: "stylesheet",
    styles: [
      { id: "regular", label: "Regular" },
      { id: "bold", label: "Bold" },
      { id: "fill", label: "Fill" },
      { id: "light", label: "Light" },
      { id: "thin", label: "Thin" },
      { id: "duotone", label: "Duotone" },
    ],
  },
  {
    id: "tabler",
    label: "Tabler Icons",
    type: "stylesheet",
    url: "https://cdn.jsdelivr.net/npm/@tabler/icons-webfont@3.34.0/dist/tabler-icons.min.css",
    styles: [
      { id: "outline", label: "Outline" },
      { id: "filled", label: "Filled" },
    ],
  },
  {
    id: "remix",
    label: "Remix Icon",
    type: "stylesheet",
    url: "https://cdn.jsdelivr.net/npm/remixicon@4.6.0/fonts/remixicon.css",
    styles: [
      { id: "line", label: "Line" },
      { id: "fill", label: "Fill" },
    ],
  },
];

export function catalogById(id) {
  return LIBRARY_CATALOG.find((entry) => entry.id === id) || null;
}

function materialFamily(style) {
  if (style === "outlined") {
    return "Material+Symbols+Outlined";
  }
  if (style === "rounded") {
    return "Material+Symbols+Rounded";
  }
  return "Material+Symbols+Sharp";
}

export function materialUrl(style) {
  return `https://fonts.googleapis.com/css2?family=${materialFamily(style)}:opsz,wght,FILL,GRAD@20..48,100..700,0..1,-50..200`;
}

function phosphorStyle(style) {
  const names = ["regular", "bold", "fill", "light", "thin", "duotone"];
  return names.includes(style) ? style : "regular";
}

export function phosphorUrl(style) {
  return `https://cdn.jsdelivr.net/npm/@phosphor-icons/web@2.1.1/src/${phosphorStyle(style)}/style.css`;
}

/* The address a shipped group loads. An empty Font Awesome kit id returns "". */
export function libraryUrl(group, style, kitId) {
  const spec = catalogById(group);
  if (!spec) {
    return "";
  }
  if (spec.id === "fontawesome") {
    const id = String(kitId || "").trim();
    if (!/^[A-Za-z0-9]+$/.test(id)) {
      return "";
    }
    return `https://kit.fontawesome.com/${id}.js`;
  }
  if (spec.id === "material") {
    return materialUrl(style);
  }
  if (spec.id === "phosphor") {
    return phosphorUrl(style);
  }
  return spec.url || "";
}

const PRESET_FA = libraryUrl("fontawesome", "sharp-duotone-solid", FA_KIT);
const PRESET_MATERIAL = materialUrl("sharp");

export const FA_KIT_URL = PRESET_FA;
export const MATERIAL_URL = PRESET_MATERIAL;

function shippedEntry(id, enabled, style) {
  const spec = catalogById(id);
  const chosen = spec.styles.some((item) => item.id === style) ? style : spec.styles[0].id;
  const kit = id === "fontawesome" ? FA_KIT : "";
  return {
    id,
    name: spec.label,
    group: id,
    style: chosen,
    type: spec.type,
    url: libraryUrl(id, chosen, kit),
    enabled: enabled === true,
  };
}

export function defaultLibraries() {
  return [
    shippedEntry("fontawesome", true, "sharp-duotone-solid"),
    shippedEntry("material", false, "sharp"),
    shippedEntry("iconify", false, "any"),
    shippedEntry("bootstrap", false, "regular"),
    shippedEntry("phosphor", false, "regular"),
    shippedEntry("tabler", false, "outline"),
    shippedEntry("remix", false, "line"),
  ];
}

/* A list saved before groups existed gains the missing shipped rows once.
   After any row has a group, a family the user removed stays removed. */
export function mergeShippedLibraries(list) {
  if (!Array.isArray(list)) {
    return defaultLibraries();
  }
  const copy = list.map((entry) => (entry && typeof entry === "object" ? { ...entry } : entry));
  if (copy.some((entry) => entry && typeof entry.group === "string" && entry.group !== "")) {
    return copy;
  }
  const seen = new Set(copy.map((entry) => entry && entry.id));
  for (const shipped of defaultLibraries()) {
    if (copy.length >= 16) {
      break;
    }
    if (!seen.has(shipped.id)) {
      copy.push({ ...shipped });
      seen.add(shipped.id);
    }
  }
  return copy;
}

export function libraryGroupId(entry) {
  if (entry && typeof entry.group === "string" && catalogById(entry.group)) {
    return entry.group;
  }
  const url = entry && typeof entry.url === "string" ? entry.url : "";
  if (url.includes("kit.fontawesome.com")) {
    return "fontawesome";
  }
  if (url.includes("fonts.googleapis.com")) {
    return "material";
  }
  if (url.includes("code.iconify.design")) {
    return "iconify";
  }
  if (url.includes("bootstrap-icons")) {
    return "bootstrap";
  }
  if (url.includes("@phosphor-icons")) {
    return "phosphor";
  }
  if (url.includes("@tabler/icons-webfont")) {
    return "tabler";
  }
  if (url.includes("remixicon")) {
    return "remix";
  }
  return "";
}

export function libraryStyleId(entry) {
  const group = libraryGroupId(entry);
  const spec = catalogById(group);
  const stored = entry && typeof entry.style === "string" ? entry.style : "";
  if (/^[a-z0-9-]{1,32}$/.test(stored) && (!spec || spec.styles.some((style) => style.id === stored))) {
    return stored;
  }
  const url = entry && typeof entry.url === "string" ? entry.url : "";
  if (group === "material") {
    if (url.includes("Outlined")) {
      return "outlined";
    }
    if (url.includes("Rounded")) {
      return "rounded";
    }
    return "sharp";
  }
  if (group === "phosphor") {
    const match = /\/src\/(regular|bold|fill|light|thin|duotone)\//.exec(url);
    if (match) {
      return match[1];
    }
  }
  return spec ? spec.styles[0].id : "";
}

export function libraryGroupLabel(entry) {
  const spec = catalogById(libraryGroupId(entry));
  return spec ? spec.label : "Other";
}

export function libraryStyleLabel(entry) {
  const spec = catalogById(libraryGroupId(entry));
  const id = libraryStyleId(entry);
  const found = spec && spec.styles.find((style) => style.id === id);
  return found ? found.label : id;
}

export function fontAwesomeKitId(url) {
  const match = /kit\.fontawesome\.com\/([A-Za-z0-9]+)\.js/.exec(String(url || ""));
  return match ? match[1] : "";
}

/* Fields the page keeps besides the core's required set. Short tokens only. */
export function libraryExtras(entry) {
  const extra = {};
  if (entry && typeof entry.group === "string" && /^[a-z][a-z0-9-]{0,31}$/.test(entry.group)) {
    extra.group = entry.group;
  }
  if (entry && typeof entry.style === "string" && /^[a-z0-9-]{1,32}$/.test(entry.style)) {
    extra.style = entry.style;
  }
  return extra;
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

/* A library URL the page may load. Companion hosts (the kit loader, font files,
   Iconify's API) are not values a user may type. */
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
    return `that host is not shipped (${host || "missing host"})`;
  }
  return "";
}

/* What the window actually loads. A name or style that leaves the URL alone
   does not reload. */
export function libraryLoadSignature(list) {
  return JSON.stringify((list || []).map((entry) => [
    entry && entry.id,
    entry && entry.type,
    entry && entry.url,
    entry && entry.enabled === true,
  ]));
}

export function isFontAwesomeLibrary(entry) {
  return libraryGroupId(entry) === "fontawesome";
}

/* The fields a library row stores. Extra keys are dropped. */
export function libraryRecord(entry) {
  return {
    id: entry && entry.id,
    name: entry && entry.name,
    type: entry && entry.type === "stylesheet" ? "stylesheet" : "script",
    url: entry && entry.url,
    enabled: entry && entry.enabled === true,
    ...libraryExtras(entry),
  };
}

/* At most one Font Awesome row stays on. preferId wins when that row is
   Font Awesome and already on. Otherwise the first enabled Font Awesome
   row stays on. Other families are left as they are. */
export function singleFontAwesome(list, preferId) {
  if (!Array.isArray(list)) {
    return list;
  }
  const faOn = [];
  list.forEach((entry, index) => {
    if (entry && entry.enabled === true && isFontAwesomeLibrary(entry)) {
      faOn.push(index);
    }
  });
  if (faOn.length <= 1) {
    return list;
  }
  let keep = faOn[0];
  if (typeof preferId === "string") {
    const preferred = faOn.find((index) => list[index] && list[index].id === preferId);
    if (preferred !== undefined) {
      keep = preferred;
    }
  }
  return list.map((entry, index) => {
    if (!faOn.includes(index) || index === keep) {
      return entry;
    }
    return { ...entry, enabled: false };
  });
}

function librariesProblem(list, oneFontAwesome) {
  if (!Array.isArray(list)) {
    return "icon libraries must be a list";
  }
  if (list.length > 16) {
    return "at most 16 icon libraries";
  }
  const seen = new Set();
  let faOn = 0;
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
    if (typeof entry.group === "string" && entry.group !== "") {
      const spec = catalogById(entry.group);
      if (!spec) {
        return "an icon library group is not one Pavillion ships";
      }
      if (typeof entry.style === "string" && entry.style !== "" && !spec.styles.some((style) => style.id === entry.style)) {
        return "an icon library style is not in that group";
      }
    } else if (typeof entry.style === "string" && entry.style !== "" && !/^[a-z0-9-]{1,32}$/.test(entry.style)) {
      return "an icon library style must be a short name";
    }
    if (oneFontAwesome && entry.enabled === true && isFontAwesomeLibrary(entry)) {
      faOn += 1;
      if (faOn > 1) {
        return "only one Font Awesome library can be on";
      }
    }
  }
  return "";
}

export function librariesError(list) {
  return librariesProblem(list, true);
}

/* A libraries array from an icon file. An error string when the rows are
   not a libraries table. Two enabled Font Awesome rows become one: the
   first stays on. */
export function importedLibraries(raw) {
  const problem = librariesProblem(raw, false);
  if (problem) {
    return problem;
  }
  const next = singleFontAwesome(raw.map((entry) => libraryRecord(entry)));
  const again = librariesError(next);
  return again || next;
}

/* Reduce a saved kit id, or a raw script tag the preview accepted, to the FA URL.
   A blank value stays the shipped kit. A new row with an empty kit id must not use this. */
export function fontAwesomeUrlFromKit(value) {
  const raw = String(value || "");
  const fromUrl = /kit\.fontawesome\.com\/([A-Za-z0-9]+)\.js/.exec(raw);
  const id = fromUrl ? fromUrl[1] : /^[A-Za-z0-9]+$/.test(raw.trim()) ? raw.trim() : "";
  if (!id) {
    return PRESET_FA;
  }
  return `https://kit.fontawesome.com/${id}.js`;
}

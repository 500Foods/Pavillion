/* Every icon the page paints. Account icons and profile photos are callers, not rows. */
import { paintIcon, warnIconOnce } from "../iconsan.js";

const SHARP = "fa-sharp-duotone fa-solid";
const THIN = "fa-sharp fa-thin";

function fa(cls) {
  return `<i class="${cls}"></i>`;
}

function row(key, section, label, cls, extra = {}) {
  return {
    key,
    section,
    label,
    default: fa(cls),
    presets: [],
    smallChrome: extra.smallChrome === true,
    sizeHint: extra.sizeHint || 0,
    cssVar: extra.cssVar || "",
  };
}

function nav(key, label, cls, extra = {}) {
  return row(`nav.${key}`, "Nav", label, cls, {
    smallChrome: true,
    sizeHint: 20,
    cssVar: `--pav-icon-${key}`,
    ...extra,
  });
}

export const REGISTRY = [
  {
    key: "nav.mail",
    section: "Nav",
    label: "Mail",
    default: `<img src="pav-system:mark" alt="">`,
    presets: [],
    smallChrome: true,
    sizeHint: 20,
    cssVar: "--pav-icon-mail",
  },
  nav("contacts", "Contacts", `${SHARP} fa-address-book`),
  nav("calendars", "Calendars", `${SHARP} fa-calendar-days`),
  nav("tasks", "Tasks", `${SHARP} fa-list-check`),
  nav("files", "Files", `${SHARP} fa-folder`),
  nav("notes", "Notes", `${SHARP} fa-notebook`),
  nav("argent", "Argent", `${SHARP} fa-coins`),
  nav("bots", "Grok Bots", "fa-sharp fa-thin fa-ghost"),
  nav("github", "GitHub", "fa-brands fa-github"),
  nav("activity", "Activity", `${SHARP} fa-list-timeline`),
  nav("settings", "Settings", `${SHARP} fa-gear`),
  nav("update", "Update", `${SHARP} fa-up-down`),
  nav("help", "Help", `${SHARP} fa-circle-question`),

  row("mail.tree.star", "Mail", "Star", `${SHARP} fa-star`, { smallChrome: true, sizeHint: 16 }),
  row("mail.tree.heart", "Mail", "Heart", `${SHARP} fa-heart`, { smallChrome: true, sizeHint: 16 }),
  row("mail.tree.cloud", "Mail", "Cloud", `${SHARP} fa-cloud`, { smallChrome: true, sizeHint: 16 }),
  row("mail.role.inbox", "Mail", "Inbox", `${SHARP} fa-inbox`, { smallChrome: true, sizeHint: 16 }),
  row("mail.role.drafts", "Mail", "Drafts", `${SHARP} fa-file-pen`, { smallChrome: true, sizeHint: 16 }),
  row("mail.role.sent", "Mail", "Sent", `${SHARP} fa-paper-plane`, { smallChrome: true, sizeHint: 16 }),
  row("mail.role.archive", "Mail", "Archive", `${SHARP} fa-box-archive`, { smallChrome: true, sizeHint: 16 }),
  row("mail.role.junk", "Mail", "Junk", `${SHARP} fa-ban`, { smallChrome: true, sizeHint: 16 }),
  row("mail.role.trash", "Mail", "Trash", `${SHARP} fa-trash`, { smallChrome: true, sizeHint: 16 }),
  row("mail.role.account", "Mail", "Account", `${SHARP} fa-user`, { smallChrome: true, sizeHint: 16 }),
  row("mail.role.folder", "Mail", "Folder", `${SHARP} fa-folder`, { smallChrome: true, sizeHint: 16 }),
  row("mail.role.error", "Mail", "Error", `${SHARP} fa-circle-exclamation`, { smallChrome: true, sizeHint: 16 }),
  row("mail.list.flag", "Mail list", "Flag", `${SHARP} fa-flag`, { smallChrome: true, sizeHint: 16 }),
  row("mail.list.reply", "Mail list", "Reply", `${SHARP} fa-reply`, { smallChrome: true, sizeHint: 16 }),
  row("mail.list.share", "Mail list", "Share", `${SHARP} fa-share`, { smallChrome: true, sizeHint: 16 }),
  row("mail.list.file", "Mail list", "Attachment", `${SHARP} fa-paperclip`, { smallChrome: true, sizeHint: 16 }),

  row("tree.account", "Tree", "Account", `${SHARP} fa-user`, { smallChrome: true, sizeHint: 16 }),
  row("tree.mailbox", "Tree", "Mailbox", `${SHARP} fa-envelope`, { smallChrome: true, sizeHint: 16 }),
  row("tree.folder", "Tree", "Folder", `${SHARP} fa-folder`, { smallChrome: true, sizeHint: 16 }),
  row("tree.calendar", "Tree", "Calendar", `${SHARP} fa-calendar-days`, { smallChrome: true, sizeHint: 16 }),
  row("tree.addressbook", "Tree", "Address book", `${SHARP} fa-address-book`, { smallChrome: true, sizeHint: 16 }),
  row("tree.group", "Tree", "Group", `${SHARP} fa-users`, { smallChrome: true, sizeHint: 16 }),
  row("tree.error", "Tree", "Error", `${SHARP} fa-circle-exclamation`, { smallChrome: true, sizeHint: 16 }),
  row("tree.disclosure", "Tree", "Disclosure", `${SHARP} fa-chevron-right`, { smallChrome: true, sizeHint: 16 }),
  row("tree.group.chevron", "Tree", "Group disclosure", `${SHARP} fa-chevron-down`, { smallChrome: true, sizeHint: 16 }),

  row("table.sort", "Tables", "Sort", `${SHARP} fa-sort`, { smallChrome: true, sizeHint: 14 }),
  row("table.sort.asc", "Tables", "Sort ascending", `${SHARP} fa-sort-up`, { smallChrome: true, sizeHint: 14 }),
  row("table.sort.desc", "Tables", "Sort descending", `${SHARP} fa-sort-down`, { smallChrome: true, sizeHint: 14 }),
  row("table.current", "Tables", "Current row", `${SHARP} fa-caret-right`, { smallChrome: true, sizeHint: 14 }),
  row("table.editing", "Tables", "Editing", `${SHARP} fa-pen`, { smallChrome: true, sizeHint: 14 }),
  row("table.menu", "Tables", "Columns", `${SHARP} fa-ellipsis-vertical`, { smallChrome: true, sizeHint: 14 }),

  row("tab.message", "Tabs", "Message", `${SHARP} fa-envelope`, { smallChrome: true, sizeHint: 16 }),
  row("tab.contact", "Tabs", "Contact", `${SHARP} fa-address-card`, { smallChrome: true, sizeHint: 16 }),
  row("tab.event", "Tabs", "Event", `${SHARP} fa-calendar-day`, { smallChrome: true, sizeHint: 16 }),
  row("tab.file", "Tabs", "File", `${SHARP} fa-file`, { smallChrome: true, sizeHint: 16 }),
  row("tab.draft", "Tabs", "Draft", `${SHARP} fa-pen-to-square`, { smallChrome: true, sizeHint: 16 }),
  row("tab.browse", "Tabs", "Browse", `${SHARP} fa-globe`, { smallChrome: true, sizeHint: 16 }),

  row("features.grip", "Features", "Drag", `${SHARP} fa-bars`, { smallChrome: true, sizeHint: 16 }),
  row("features.check", "Features", "Shown", `${SHARP} fa-square-check`, { smallChrome: true, sizeHint: 16 }),
  row("features.check.off", "Features", "Hidden", `${SHARP} fa-square`, { smallChrome: true, sizeHint: 16 }),

  row("help.back", "Help", "Back", `${SHARP} fa-arrow-left`),
  row("help.copy", "Help", "Copy", `${SHARP} fa-copy`),
  row("help.save", "Help", "Save", `${SHARP} fa-save`),
  row("help.print", "Help", "Print", `${SHARP} fa-print`),
  row("help.chevron", "Help", "Expand", `${SHARP} fa-chevron-right`, { smallChrome: true, sizeHint: 16 }),
  row("help.chevron.left", "Help", "Collapse", `${SHARP} fa-chevron-left`, { smallChrome: true, sizeHint: 16 }),
  row("help.filter.all", "Help", "All", `${SHARP} fa-heart`, { smallChrome: true, sizeHint: 16 }),
  row("help.filter.pages", "Help", "Pages", `${SHARP} fa-star`, { smallChrome: true, sizeHint: 16 }),
  row("help.filter.dev", "Help", "Dev", `${SHARP} fa-flag`, { smallChrome: true, sizeHint: 16 }),
  row("help.filter.settings", "Help", "Settings", `${SHARP} fa-bookmark`, { smallChrome: true, sizeHint: 16 }),
  row("help.filter.mail", "Help", "Mail", `${SHARP} fa-tag`, { smallChrome: true, sizeHint: 16 }),

  row("module.refresh", "Module bars", "Refresh", `${SHARP} fa-arrows-rotate`),
  row("module.first", "Module bars", "First", `${SHARP} fa-backward-step`),
  row("module.prev", "Module bars", "Previous", `${SHARP} fa-chevron-left`),
  row("module.next", "Module bars", "Next", `${SHARP} fa-chevron-right`),
  row("module.last", "Module bars", "Last", `${SHARP} fa-forward-step`),

  row("accounts.add", "Accounts", "Add", `${SHARP} fa-plus`),
  row("accounts.clone", "Accounts", "Clone", `${SHARP} fa-clone`),
  row("accounts.delete", "Accounts", "Delete", `${SHARP} fa-trash`),
  row("accounts.export", "Accounts", "Export", `${SHARP} fa-file-export`),
  row("accounts.column.mail", "Accounts", "Mail column", `${SHARP} fa-paper-plane-top fa-rotate-270`, { smallChrome: true, sizeHint: 16 }),

  row("settings.help", "Settings", "Help", `${SHARP} fa-circle-question`, { smallChrome: true, sizeHint: 16 }),
  row("settings.apply", "Settings", "Apply", `${SHARP} fa-check`),
  row("ui.check", "UI", "Check", `${SHARP} fa-check`),
  row("ui.cancel", "UI", "Cancel", `${SHARP} fa-xmark`),
  row("ui.export", "UI", "Export", `${SHARP} fa-download`),
  row("ui.import", "UI", "Import", `${SHARP} fa-upload`),
  row("ui.revert", "UI", "Revert", `${SHARP} fa-rotate-left`),
  row("log.settings", "Logs", "Log settings", `${SHARP} fa-gear`, { smallChrome: true, sizeHint: 16 }),

  row("activity.mail", "Activity", "Mail", `${SHARP} fa-paper-plane-top fa-rotate-270`, { smallChrome: true, sizeHint: 16 }),
  row("activity.account", "Activity", "Account", `${THIN} fa-circle`, { smallChrome: true, sizeHint: 16 }),
  row("activity.sync.mail", "Activity", "Mail sync", `${THIN} fa-envelope`),
  row("activity.sync.contacts", "Activity", "Contacts sync", `${THIN} fa-address-book`),
  row("activity.sync.calendar", "Activity", "Calendar sync", `${THIN} fa-calendar-days`),
  row("activity.sync.files", "Activity", "Files sync", `${THIN} fa-folder`),
  row("activity.body.fetch", "Activity", "Fetch body", `${THIN} fa-file-lines`),
  row("activity.attachment", "Activity", "Attachment", `${THIN} fa-paperclip`),
  row("activity.files.upload", "Activity", "Upload", `${THIN} fa-arrow-up-from-bracket`),
  row("activity.files.download", "Activity", "Download", `${THIN} fa-arrow-down-to-bracket`),
  row("activity.send", "Activity", "Send", `${THIN} fa-paper-plane`),
  row("activity.index", "Activity", "Index", `${THIN} fa-magnifying-glass`),
  row("activity.rules", "Activity", "Rules", `${THIN} fa-filter`),
  row("activity.import", "Activity", "Import", `${THIN} fa-file-import`),
  row("activity.export", "Activity", "Export", `${THIN} fa-file-export`),
  row("activity.retention", "Activity", "Retention", `${THIN} fa-broom`),
  row("activity.cache", "Activity", "Evict cache", `${THIN} fa-box-archive`),
  row("activity.search", "Activity", "Server search", `${THIN} fa-magnifying-glass`),
  row("activity.queue", "Activity", "Flush queue", `${THIN} fa-list-check`),
  row("activity.discover", "Activity", "Configure account", `${THIN} fa-wand-magic-sparkles`),
  row("activity.test", "Activity", "Test account", `${THIN} fa-plug`),
];

const BY_KEY = new Map(REGISTRY.map((entry) => [entry.key, entry]));

export function registryEntry(key) {
  return BY_KEY.get(key) || null;
}

export function navEntries() {
  return REGISTRY.filter((entry) => entry.section === "Nav");
}

/* Class string from a shipped <i>. Empty when the default is not an <i>. */
export function iconClass(key) {
  const entry = registryEntry(key);
  if (!entry) {
    return "";
  }
  const match = /^<i class="([^"]+)"><\/i>$/.exec(entry.default);
  return match ? match[1] : "";
}

export function resolveIcon(key, options = {}) {
  const entry = registryEntry(key);
  if (!entry) {
    return { html: "", source: "missing" };
  }
  const icons = options.icons && typeof options.icons === "object" ? options.icons : {};
  const override = typeof icons[key] === "string" ? icons[key].trim() : "";
  if (override) {
    return { html: override, source: "override" };
  }
  const theme = typeof options.themeHtml === "string" ? options.themeHtml.trim() : "";
  if (theme && theme !== "none") {
    return { html: theme, source: "theme" };
  }
  return { html: entry.default, source: "shipped" };
}

function addIconClass(node) {
  try {
    node.classList.add("icon");
  } catch {
    const prev = node.getAttribute("class") || "";
    if (!prev.split(/\s+/).includes("icon")) {
      node.setAttribute("class", `${prev} icon`.trim());
    }
  }
}

/* One painted node. A failed override falls back to the shipped default. */
export function iconNode(key, options = {}) {
  const doc = options.documentRef || globalThis.document;
  const entry = registryEntry(key);
  if (!doc || !entry) {
    return null;
  }
  const resolved = resolveIcon(key, options);
  let painted = paintIcon(resolved.html, doc);
  let source = resolved.source;
  if (!painted.ok) {
    warnIconOnce(key, () => {
      if (typeof options.log === "function") {
        options.log(key);
      }
    });
    painted = paintIcon(entry.default, doc);
    source = "shipped";
  }
  const node = painted.ok && painted.fragment.firstElementChild
    ? painted.fragment.firstElementChild
    : doc.createElement("i");
  addIconClass(node);
  node.setAttribute("aria-hidden", "true");
  if (node.dataset) {
    node.dataset.iconKey = key;
    node.dataset.iconSource = source;
  }
  return node;
}

export const SYSTEM_ICONS = [{ id: "mark", label: "Roof mark", html: `<img src="pav-system:mark" alt="">` }];

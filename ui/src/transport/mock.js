function fail(code, message) {
  const error = new Error(message);
  error.code = code;
  error.retryable = false;
  return error;
}

function isObject(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

const CAP_WORDS = ["mail", "submission", "contacts", "calendars", "sieve", "files", "websocket"];
const PREF_KEYS = [
  "timezone",
  "weekStart",
  "timeFormat",
  "theme",
  "density",
  "windowMaximized",
  "windowWidth",
  "windowHeight",
  "windowX",
  "windowY",
  "chrome",
  "logLevel",
  "cache",
  "mail",
];

function defaultPrefs() {
  return {
    timezone: "America/Vancouver",
    weekStart: "monday",
    timeFormat: "24h",
    theme: "system",
    density: "comfortable",
    windowMaximized: false,
    windowWidth: 1200,
    windowHeight: 800,
    windowX: null,
    windowY: null,
    chrome: "system",
    logLevel: "info",
    cache: { bodiesMb: 2048, blobsMb: 4096, listRows: 10000 },
    mail: { cssExtra: [] },
  };
}

const CSS_EXTRA_MAX = 32;
const CSS_EXTRA_NAME = /^[a-z][a-z0-9-]{0,39}$/;

function takeMail(value) {
  if (!isObject(value)) {
    throw fail("bad_args", "mail must be an object");
  }
  const keys = Object.keys(value);
  if (keys.length === 0) {
    throw fail("bad_args", "mail is empty");
  }
  for (const key of keys) {
    if (key !== "cssExtra") {
      throw fail("bad_args", `unknown preference mail.${key}`);
    }
  }
  const names = value.cssExtra;
  if (!Array.isArray(names)) {
    throw fail("bad_args", "cssExtra must be an array");
  }
  if (names.length > CSS_EXTRA_MAX) {
    throw fail("bad_args", "cssExtra has too many names");
  }
  for (const name of names) {
    if (typeof name !== "string") {
      throw fail("bad_args", "cssExtra names must be strings");
    }
    if (!CSS_EXTRA_NAME.test(name) || name === "position" || name === "behavior") {
      throw fail("bad_args", "cssExtra has a name outside the rule");
    }
  }
  return { cssExtra: names.slice() };
}

function defaultUi() {
  return {
    nav: { width: 320 },
    zoom: { shell: 1 },
    fontawesome_kit: "45801522ef",
  };
}

/* Shell tests open these four mailboxes by id. A real profile has none of
   them; mailbox.list on the mock is this stand-in, and a scoped account id
   matches nothing here. */
const SAMPLE_MAILBOXES = [
  { id: "inbox", parentId: null, kind: "mailbox", label: "Inbox", role: "inbox", order: 0, unread: 3, total: 3, hasChildren: false, swatch: null },
  { id: "drafts", parentId: null, kind: "mailbox", label: "Drafts", role: "drafts", order: 1, unread: 0, total: 0, hasChildren: false, swatch: null },
  { id: "sent", parentId: null, kind: "mailbox", label: "Sent", role: "sent", order: 2, unread: 0, total: 0, hasChildren: false, swatch: null },
  { id: "archive", parentId: null, kind: "mailbox", label: "Archive", role: "archive", order: 3, unread: 12, total: 12, hasChildren: false, swatch: null },
];

/* In-memory stand-in for the real commands. emit() is for Vitest and the
   G-4 harness. It is not part of the transport the screens call. */
export function mockTransport() {
  const prefs = defaultPrefs();
  let ui = defaultUi();
  const listeners = new Set();
  const seen = [];
  const themeFiles = new Map();
  const themeFallback = "/pavillion";
  let themeFolder = "";
  let accounts = [];
  let nextNumber = 1;
  let accountTestState = "idle";
  const savedSecrets = new Map();
  /* log.db stand-in (H-4): every line ever logged, oldest first, each
     {seq, ts, level, subsystem, text, session}. The ring is the last 10,000. */
  let logHistory = [];
  let logCapMb = 10;
  const LOG_ROW_BYTES = 200;
  const exportFiles = new Map();

  async function call(cmd, args) {
    const input = args ?? {};
    seen.push({ cmd, args: input });
    if (!isObject(input)) {
      throw fail("bad_args", "arguments are not an object");
    }
    if (cmd === "system.status") {
      return {
        profileName: "Default",
        profileId: "default",
        state: "ready",
        error: "",
        debug: false,
        selftest: false,
      };
    }
    if (cmd === "cache.usage") {
      if (input.measure !== undefined && typeof input.measure !== "boolean") {
        throw fail("bad_args", "measure is not a boolean");
      }
      if (input.cancel !== undefined && typeof input.cancel !== "boolean") {
        throw fail("bad_args", "cancel is not a boolean");
      }
      if (input.cancel === true && input.measure !== undefined) {
        throw fail("bad_args", "cancel cannot be combined with measure");
      }
      const extra = Object.keys(input).filter((key) => key !== "measure" && key !== "cancel");
      if (extra.length > 0) {
        throw fail("bad_args", `unknown argument ${extra[0]}`);
      }
      if (input.cancel === true) {
        return { cancelled: true };
      }
      return { bodiesBytes: 0, blobsBytes: 0, sizesCheckedAt: null, accounts: [] };
    }
    if (cmd === "prefs.get") {
      return structuredClone(prefs);
    }
    if (cmd === "prefs.set") {
      const keys = Object.keys(input);
      if (keys.length === 0) {
        throw fail("bad_args", "nothing to save");
      }
      const next = {};
      for (const key of keys) {
        if (key === "mail") {
          next.mail = takeMail(input.mail);
          continue;
        }
        if (!PREF_KEYS.includes(key)) {
          throw fail("bad_args", `unknown preference ${key}`);
        }
        next[key] = input[key];
      }
      Object.assign(prefs, structuredClone(next));
      return structuredClone(prefs);
    }
    if (cmd === "prefs.ui.get") {
      return structuredClone(ui);
    }
    if (cmd === "prefs.ui.set") {
      ui = structuredClone(input);
      return structuredClone(ui);
    }
    if (cmd === "themes.list") {
      const themes = [...themeFiles.entries()]
        .sort((a, b) => a[0].localeCompare(b[0]))
        .map(([name, text]) => ({ name, text }));
      if (themeFolder === "missing") {
        return { folder: themeFolder, chosen: "", fallback: themeFallback, themes: [], missing: true };
      }
      const chosen = themeFolder;
      return {
        folder: chosen === "" ? themeFallback : chosen,
        chosen,
        fallback: themeFallback,
        themes,
      };
    }
    if (cmd === "themes.ensure") {
      if (!Array.isArray(input.files)) {
        throw fail("bad_args", "files is required");
      }
      const wrote = [];
      for (const file of input.files) {
        if (!file || typeof file.name !== "string" || typeof file.text !== "string" || !file.name.endsWith(".theme") || file.name.includes("/") || file.name.includes("\\") || file.name.includes("..")) {
          throw fail("bad_args", "bad theme name");
        }
        if (!themeFiles.has(file.name)) {
          themeFiles.set(file.name, file.text);
          wrote.push(file.name);
        }
      }
      return { wrote };
    }
    if (cmd === "themes.folder.set") {
      if (typeof input.folder !== "string") {
        throw fail("bad_args", "folder is required");
      }
      themeFolder = input.folder;
      return { folder: input.folder };
    }
    if (cmd === "activity.list") {
      if (input.includeHistory !== undefined && typeof input.includeHistory !== "boolean") {
        throw fail("bad_args", "includeHistory must be a boolean");
      }
      return { activities: [] };
    }
    if (cmd === "activity.cancel" || cmd === "activity.dismiss") {
      if (typeof input.id !== "string" || input.id === "") {
        throw fail("bad_args", "id is required");
      }
      throw fail("not_found", "unknown activity");
    }
    if (cmd === "mailbox.list") {
      if (input.accountId !== undefined && (typeof input.accountId !== "string" || input.accountId === "")) {
        throw fail("bad_args", "accountId must be a non-empty string");
      }
      if (typeof input.accountId === "string") {
        return { nodes: [] };
      }
      return { nodes: structuredClone(SAMPLE_MAILBOXES) };
    }
    if (cmd === "message.list") {
      const view = input.view;
      const extra = Object.keys(input).filter((key) => key !== "view");
      if (extra.length > 0) {
        throw fail("bad_args", `unknown argument ${extra[0]}`);
      }
      if (!isObject(view) || view.kind !== "mailbox" || typeof view.id !== "string" || view.id === "") {
        throw fail("bad_args", "view.kind must be mailbox");
      }
      return { accountId: "default", messages: [], total: 0 };
    }
     if (cmd === "sync.now") {
       if (input.accountId !== undefined && (typeof input.accountId !== "string" || input.accountId === "")) {
         throw fail("bad_args", "accountId must be a non-empty string");
       }
       return {};
     }
     if (cmd === "account.list") {
       return { accounts: structuredClone(accounts) };
     }
     if (cmd === "account.add") {
       if (typeof input.name !== "string" || input.name.trim() === "") {
         throw fail("bad_args", "name is required");
       }
       if (input.kind !== "jmap") {
         throw fail("bad_args", "only jmap is supported");
       }
       if (typeof input.sessionUrl !== "string" || !input.sessionUrl.startsWith("https://")) {
         throw fail("bad_args", "sessionUrl must be https://");
       }
       if (typeof input.username !== "string" || input.username.trim() === "") {
         throw fail("bad_args", "username is required");
       }
       if (typeof input.secret !== "string" || input.secret === "") {
         throw fail("bad_args", "secret is required");
       }
       const id = input.id || input.name.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-+|-+$/g, "");
       const number = String(nextNumber).padStart(3, "0");
       nextNumber += 1;
       const account = {
         id,
         number,
         name: input.name,
         kind: "jmap",
         sessionUrl: input.sessionUrl,
         username: input.username,
         email: typeof input.email === "string" ? input.email : (/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(input.username) ? input.username : ""),
         auth: "basic",
         color: input.color || "#2a7",
         enabled: input.enabled !== false,
         syncMail: input.syncMail !== false,
         syncContacts: input.syncContacts !== false,
         syncCalendars: input.syncCalendars !== false,
         fetchBodies: input.fetchBodies || "recent",
         icon: typeof input.icon === "string" ? input.icon : "",
         capabilities: Array.isArray(input.capabilities) ? input.capabilities.filter((c) => CAP_WORDS.includes(c)) : [],
         capabilitiesChecked: typeof input.capabilitiesChecked === "string" ? input.capabilitiesChecked : null,
         ...(typeof input.caFile === "string" && input.caFile !== "" ? { caFile: input.caFile } : {}),
       };
       accounts = [...accounts, account];
       savedSecrets.set(id, input.secret);
       accountTestState = { accountId: id, capabilities: ["mail", "submission", "contacts", "calendars", "files"] };
       return structuredClone(account);
     }
     if (cmd === "account.update") {
       const account = accounts.find((a) => a.id === input.id);
       if (!account) {
         throw fail("not_found", "account not found");
       }
       const updated = { ...account };
       if (input.patch.name !== undefined) {
         updated.name = input.patch.name;
       }
       if (input.patch.sessionUrl !== undefined && !input.patch.sessionUrl.startsWith("https://")) {
         throw fail("bad_args", "sessionUrl must be https://");
       }
       if (input.patch.sessionUrl !== undefined) {
         updated.sessionUrl = input.patch.sessionUrl;
       }
       if (input.patch.username !== undefined) {
         updated.username = input.patch.username;
       }
       if (input.patch.email !== undefined) {
         updated.email = input.patch.email;
       }
       if (input.patch.icon !== undefined) {
         updated.icon = input.patch.icon;
       }
       if (input.patch.color !== undefined) {
         updated.color = input.patch.color;
       }
       if (input.patch.enabled !== undefined) {
         updated.enabled = input.patch.enabled;
       }
       if (input.patch.syncMail !== undefined) {
         updated.syncMail = input.patch.syncMail;
       }
       if (input.patch.syncContacts !== undefined) {
         updated.syncContacts = input.patch.syncContacts;
       }
       if (input.patch.syncCalendars !== undefined) {
         updated.syncCalendars = input.patch.syncCalendars;
       }
       if (input.patch.fetchBodies !== undefined) {
         updated.fetchBodies = input.patch.fetchBodies;
       }
       if (input.patch.caFile !== undefined) {
         if (input.patch.caFile === "") {
           delete updated.caFile;
         } else {
           updated.caFile = input.patch.caFile;
         }
       }
       accounts = accounts.map((a) => (a.id === input.id ? updated : a));
       return structuredClone(updated);
     }
     if (cmd === "account.remove") {
       const before = accounts.length;
       accounts = accounts.filter((a) => a.id !== input.id);
       if (accounts.length === before) {
         throw fail("not_found", "account not found");
       }
       return { id: input.id };
     }
     if (cmd === "account.discover") {
       const m = typeof input.email === "string" ? /^[^@\s]+@([a-z0-9.-]+)$/i.exec(input.email) : null;
       if (!m) {
         throw fail("bad_args", "email must look like name@domain");
       }
       const domain = m[1].toLowerCase();
       if (domain === "missing.example") {
         throw fail("not_found", `no JMAP session found for ${domain} (server returned 404)`);
       }
       return { sessionUrl: `https://mail.${domain}/jmap/session`, via: "srv" };
     }
     if (cmd === "account.test") {
       let secret = typeof input.secret === "string" ? input.secret : "";
       if (secret === "" && typeof input.id === "string" && input.id !== "") {
         if (!accounts.find((a) => a.id === input.id)) {
           throw fail("not_found", "account not found");
         }
         secret = savedSecrets.get(input.id) || "";
         if (secret === "") {
           throw fail("bad_args", "no saved password for this account");
         }
       }
       if (secret === "") {
         throw fail("bad_args", "secret is required");
       }
       if (typeof input.username !== "string" || input.username.trim() === "") {
         throw fail("bad_args", "username is required");
       }
       if (typeof input.sessionUrl !== "string" || !input.sessionUrl.startsWith("https://")) {
         throw fail("bad_args", "sessionUrl must be https://");
       }
       if (secret === "wrong") {
         throw fail("auth", "The credentials were rejected by the server.");
       }
       /* K-Q7: https://redirect.<host>/... answers from https://mail.<host>/jmap/session,
          a cross-host redirect, so Authorization was dropped. */
       const hop = /^https:\/\/redirect\.([^/]+)\//.exec(input.sessionUrl);
       const sessionUrl = hop ? `https://mail.${hop[1]}/jmap/session` : input.sessionUrl;
       const caps = ["mail", "submission", "contacts", "calendars", "files"];
       /* K-Q10: a verified test of a saved account is stored. */
       const saved = typeof input.id === "string" ? accounts.find((a) => a.id === input.id) : null;
       if (saved && !hop) {
         saved.capabilities = [...caps];
         saved.capabilitiesChecked = new Date().toISOString();
       }
       return {
         ok: true,
         capabilities: caps,
         accounts: hop ? [] : [{ id: "test-account", name: "Test Account" }],
         sessionUrl,
         authSent: !hop,
       };
     }
     if (cmd === "debug.activity_demo") {
      if (!Number.isInteger(input.steps) || !Number.isInteger(input.ms)) {
        throw fail("bad_args", "steps and ms are required");
      }
      return { id: "act-1" };
    }
    if (cmd === "log.client") {
      if (typeof input.subsystem !== "string" || typeof input.level !== "string" || typeof input.message !== "string") {
        throw fail("bad_args", "log.client requires subsystem, level, and message");
      }
      return {};
    }
    if (cmd === "log.tail") {
      return { lines: logHistory.slice(-10000).map(({ session, ...line }) => line) };
    }
    if (cmd === "log.query") {
      if (!Number.isInteger(input.limit) || input.limit < 1 || input.limit > 10000) {
        throw fail("bad_args", "limit must be an integer from 1 to 10000");
      }
      const current = logHistory.reduce((max, line) => Math.max(max, line.session || 1), 1);
      const wanted = input.session === "current" ? current : input.session === "previous" ? current - 1 : null;
      const kept = logHistory.filter((line) => {
        const ms = Date.parse(line.ts);
        return (input.beforeSeq === undefined || line.seq < input.beforeSeq)
          && (input.since === undefined || ms >= input.since)
          && (input.until === undefined || ms < input.until)
          && (wanted === null || (line.session || 1) === wanted)
          && (!Array.isArray(input.levels) || input.levels.includes(line.level));
      });
      const page = kept.slice(-input.limit).map((line) => ({ ...line, session: line.session || 1 }));
      const sessions = {};
      for (const line of page) {
        const first = logHistory.find((row) => (row.session || 1) === line.session);
        sessions[String(line.session)] = first ? Date.parse(first.ts) : 0;
      }
      return { lines: page, sessions, currentSession: current };
    }
    if (cmd === "log.stats" || cmd === "log.trim") {
      if (cmd === "log.trim") {
        if (input.mb !== undefined) {
          if (!Number.isInteger(input.mb) || input.mb < 1 || input.mb > 100) {
            throw fail("bad_args", "mb must be an integer from 1 to 100");
          }
          logCapMb = input.mb;
        }
        const keep = Math.floor((logCapMb * 1024 * 1024) / LOG_ROW_BYTES);
        logHistory = logHistory.slice(-keep);
      }
      const oldest = logHistory[0];
      return {
        bytes: logHistory.length * LOG_ROW_BYTES,
        records: logHistory.length,
        oldestSeq: oldest ? oldest.seq : null,
        oldestTs: oldest ? Date.parse(oldest.ts) : null,
        capMb: logCapMb,
      };
    }
    if (cmd === "log.trim_preview") {
      if (!Number.isInteger(input.mb) || input.mb < 1 || input.mb > 100) {
        throw fail("bad_args", "mb must be an integer from 1 to 100");
      }
      const kept = logHistory.slice(-Math.floor((input.mb * 1024 * 1024) / LOG_ROW_BYTES));
      return {
        bytes: kept.length * LOG_ROW_BYTES,
        records: kept.length,
        oldestTs: kept.length > 0 ? Date.parse(kept[0].ts) : null,
      };
    }
    if (cmd === "export.write_text") {
      if (typeof input.path !== "string" || typeof input.text !== "string") {
        throw fail("bad_args", "export.write_text requires path and text");
      }
      exportFiles.set(input.path, input.text);
      return { bytes: new TextEncoder().encode(input.text).length };
    }
    if (cmd === "export.read_text") {
      if (typeof input.path !== "string" || input.path === "") {
        throw fail("bad_args", "export.read_text requires path");
      }
      if (!exportFiles.has(input.path)) {
        throw fail("bad_args", "file does not exist");
      }
      return { text: exportFiles.get(input.path) };
    }
    if (cmd === "log.export") {
      if (typeof input.path !== "string" || !isObject(input.filter)) {
        throw fail("bad_args", "log.export requires path and filter");
      }
      return { lines: 0 };
    }
    throw fail("bad_args", `unknown command ${cmd}`);
  }

  return {
    seen,
    setLogHistory(lines) {
      logHistory = Array.isArray(lines) ? lines.slice() : [];
    },
    call,
    on(handler) {
      listeners.add(handler);
      return () => listeners.delete(handler);
    },
    emit(name, payload) {
      for (const handler of listeners) {
        handler(name, payload);
      }
    },
  };
}

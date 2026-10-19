function fail(code, message) {
  const error = new Error(message);
  error.code = code;
  error.retryable = false;
  return error;
}

function isObject(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

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
  };
}

function defaultUi() {
  return {
    nav: { width: 320 },
    zoom: { shell: 1 },
    fontawesome_kit: "45801522ef",
  };
}

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
    if (cmd === "prefs.get") {
      return structuredClone(prefs);
    }
    if (cmd === "prefs.set") {
      const keys = Object.keys(input);
      if (keys.length === 0) {
        throw fail("bad_args", "nothing to save");
      }
      for (const key of keys) {
        if (!PREF_KEYS.includes(key)) {
          throw fail("bad_args", `unknown preference ${key}`);
        }
      }
      Object.assign(prefs, structuredClone(input));
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
         auth: "basic",
         color: input.color || "#2a7",
         enabled: input.enabled !== false,
         syncMail: input.syncMail !== false,
         syncContacts: input.syncContacts !== false,
         syncCalendars: input.syncCalendars !== false,
         fetchBodies: input.fetchBodies || "recent",
       };
       accounts = [...accounts, account];
       accountTestState = { accountId: id, capabilities: ["mail", "submission", "contacts", "calendars"] };
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
     if (cmd === "account.test") {
       if (typeof input.secret !== "string" || input.secret === "") {
         throw fail("bad_args", "secret is required");
       }
       if (typeof input.username !== "string" || input.username.trim() === "") {
         throw fail("bad_args", "username is required");
       }
       if (typeof input.sessionUrl !== "string" || !input.sessionUrl.startsWith("https://")) {
         throw fail("bad_args", "sessionUrl must be https://");
       }
       if (input.secret === "wrong") {
         throw fail("auth", "The credentials were rejected by the server.");
       }
       return {
         ok: true,
         capabilities: ["mail", "submission", "contacts", "calendars"],
         accounts: [{ id: "test-account", name: "Test Account" }],
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
      return { lines: [] };
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

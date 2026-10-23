/* Settings → Mail (N Lock): preferences.mail.cssExtra. One property name
   per line. The built-in CSS list stays fixed. Refresh reloads the saved
   names. Apply writes them through prefs.set. The core rejects a name
   outside ^[a-z][a-z0-9-]{0,39}$, position, behavior, a leading hyphen,
   a non-string, and more than 32 names. */

import { moduleFooter, moduleToolbar, mountBar } from "../common/module-bar.js";

function messageOf(err) {
  return err instanceof Error ? err.message : String(err);
}

export function namesFromText(text) {
  const names = [];
  for (const line of String(text).split(/\r?\n/)) {
    const name = line.trim();
    if (name !== "") {
      names.push(name);
    }
  }
  return names;
}

export function textFromNames(names) {
  return Array.isArray(names) ? names.filter((name) => typeof name === "string").join("\n") : "";
}

export function createMailSettings(api, root, options = {}) {
  const field = root.querySelector("#mail-css-extra");
  if (!field) {
    return {
      refresh: () => Promise.resolve(),
      apply: () => Promise.resolve(),
      destroy() {},
    };
  }

  let saved = "";
  let ticket = 0;

  function report(err) {
    if (typeof options.onError === "function") {
      options.onError(messageOf(err));
    }
  }

  function paint(names) {
    saved = textFromNames(names);
    field.value = saved;
  }

  function refresh() {
    const mine = ticket + 1;
    ticket = mine;
    return api.prefs.get().then((prefs) => {
      if (mine !== ticket) {
        return null;
      }
      const mail = prefs && prefs.mail;
      paint(mail && mail.cssExtra);
      return prefs;
    }).catch((err) => {
      if (mine === ticket) {
        report(err);
      }
      return null;
    });
  }

  function apply() {
    const mine = ticket + 1;
    ticket = mine;
    const cssExtra = namesFromText(field.value);
    return api.prefs.set({ mail: { cssExtra } }).then((prefs) => {
      if (mine !== ticket) {
        return null;
      }
      const mail = prefs && prefs.mail;
      paint(mail && mail.cssExtra);
      return prefs;
    }).catch((err) => {
      if (mine === ticket) {
        report(err);
      }
      return null;
    });
  }

  mountBar(root.querySelector("#mail-toolbar"), moduleToolbar({
    title: "Mail",
  }));
  mountBar(root.querySelector("#mail-footer"), moduleFooter({
    label: "Mail actions",
    onRefresh: () => {
      refresh();
    },
    end: [{
      action: "apply",
      icon: "fa-check",
      text: "Apply",
      label: "Save extra CSS properties",
      onClick: () => {
        apply();
      },
    }],
  }));

  return { refresh, apply, destroy() { ticket += 1; } };
}

import hostileHtml from "../../../tests/corpus/hostile.html?raw";
import { createHtmlPane } from "./htmlpane.js";

export const ISOLATION_PANE = "isolation";
export const ISOLATION_LINKS = ["https://example.com/pavillion-isolation-test"];

const MISSING = "This link is not in the pane list.";

export function isolationLinkUrl(paneId, n) {
  if (paneId !== ISOLATION_PANE || !Number.isInteger(n) || n < 0 || n >= ISOLATION_LINKS.length) {
    return null;
  }
  return ISOLATION_LINKS[n];
}

function linkDialog(root) {
  let dialog = root.querySelector("#link-confirm");
  if (dialog) {
    return dialog;
  }
  dialog = document.createElement("dialog");
  dialog.id = "link-confirm";
  const label = document.createElement("p");
  label.id = "link-confirm-label";
  label.textContent = "This link wants to open:";
  const url = document.createElement("p");
  url.id = "link-confirm-url";
  const form = document.createElement("form");
  form.method = "dialog";
  const close = document.createElement("button");
  close.type = "submit";
  close.value = "close";
  close.textContent = "Close";
  form.append(close);
  dialog.append(label, url, form);
  dialog.setAttribute("aria-labelledby", "link-confirm-label");
  root.append(dialog);
  return dialog;
}

function showLinkDialog(root, text) {
  const dialog = linkDialog(root);
  dialog.querySelector("#link-confirm-url").textContent = text;
  if (typeof dialog.showModal === "function" && !dialog.open) {
    dialog.showModal();
  }
}

export function installIsolationTest({ api, enabled, openMail, root }) {
  if (!enabled || !root) {
    return () => {};
  }
  const button = root.querySelector("#isolation-test");
  if (!button) {
    return () => {};
  }
  button.hidden = false;
  let shown = false;
  button.addEventListener("click", () => {
    openMail();
    const detail = root.querySelector("#panel-mail .pane-detail");
    if (!detail || shown) {
      return;
    }
    shown = true;
    const host = document.createElement("div");
    host.className = "isolation-host";
    const note = document.createElement("p");
    note.className = "isolation-note";
    note.textContent = `Window title is "${document.title}". A change means script ran.`;
    const frameHost = document.createElement("div");
    frameHost.className = "isolation-frame";
    host.append(note, frameHost);
    detail.replaceChildren(host);
    createHtmlPane(frameHost).show(hostileHtml, { allowRemote: false });
  });
  return api.on("link.clicked", (payload) => {
    const url = isolationLinkUrl(payload && payload.paneId, payload && payload.n);
    showLinkDialog(root, url == null ? MISSING : url);
  });
}

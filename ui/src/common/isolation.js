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

function showLinkDialog(confirm, text) {
  if (typeof confirm !== "function") {
    return;
  }
  Promise.resolve(confirm({
    title: "This link wants to open:",
    text,
    okLabel: "Close",
  })).then(() => {}, () => {});
}

export function installIsolationTest({ api, enabled, openMail, root, confirm }) {
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
    showLinkDialog(confirm, url == null ? MISSING : url);
  });
}

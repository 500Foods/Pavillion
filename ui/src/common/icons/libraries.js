/* Build script and link elements from validated library URLs. Never parse HTML. */
import { fontAwesomeUrlFromKit, libraryUrlError } from "../iconhosts.js";

export function loadIconLibraries(list, documentRef = globalThis.document) {
  const head = documentRef && (documentRef.head || documentRef);
  if (!head || typeof head.appendChild !== "function") {
    return;
  }
  for (const entry of list || []) {
    if (!entry || entry.enabled !== true) {
      continue;
    }
    if (libraryUrlError(entry.type, entry.url)) {
      continue;
    }
    const selector = `[data-pav-library="${entry.id}"]`;
    if (entry.type === "script") {
      const existing = head.querySelector(`script${selector}`);
      if (existing && existing.src === entry.url) {
        continue;
      }
      if (existing) {
        existing.remove();
      }
      const script = documentRef.createElement("script");
      script.src = entry.url;
      script.dataset.pavLibrary = entry.id;
      script.crossOrigin = "anonymous";
      if (entry.url.includes("kit.fontawesome.com")) {
        script.setAttribute("data-auto-replace-svg", "svg");
      }
      script.addEventListener("error", () => {
        script.dataset.pavLibraryError = "load";
      });
      head.appendChild(script);
      continue;
    }
    const existing = head.querySelector(`link${selector}`);
    if (existing && existing.href === entry.url) {
      continue;
    }
    if (existing) {
      existing.remove();
    }
    const link = documentRef.createElement("link");
    link.rel = "stylesheet";
    link.href = entry.url;
    link.dataset.pavLibrary = entry.id;
    head.appendChild(link);
  }
}

export function librariesFromKit(kit, defaults) {
  const list = (defaults || []).map((entry) => ({ ...entry }));
  if (list[0]) {
    list[0].url = fontAwesomeUrlFromKit(kit);
  }
  return list;
}

import { helpPages } from "./pages.js";
import { moduleToolbar, mountBar } from "../common/module-bar.js";

/* A blank query keeps every page. A hit on the page title keeps every section.
   Otherwise only the sections whose title, text, or sample match are kept. */
export function filterHelp(pages, query) {
  const q = String(query || "").trim().toLowerCase();
  const out = [];
  for (const page of pages) {
    if (!q) {
      out.push({ page, sectionIds: page.sections.map((section) => section.id) });
      continue;
    }
    const titleHit = page.title.toLowerCase().includes(q);
    const matched = page.sections.filter((section) => {
      if (titleHit) {
        return true;
      }
      if (section.title.toLowerCase().includes(q)) {
        return true;
      }
      if (section.paragraphs.some((paragraph) => paragraph.toLowerCase().includes(q))) {
        return true;
      }
      return typeof section.code === "string" && section.code.toLowerCase().includes(q);
    });
    if (titleHit || matched.length > 0) {
      const shown = titleHit ? page.sections : matched;
      out.push({ page, sectionIds: shown.map((section) => section.id) });
    }
  }
  return out;
}

/* options.onBack: Back pressed (D-10). The toolbar is built into #help-toolbar
   by the shared module bar: title Help, search right, Back right of search. */
export function createHelpView(root, pages = helpPages, options = {}) {
  const toolbarHost = root.querySelector("#help-toolbar");
  if (toolbarHost && !toolbarHost.querySelector("#help-search")) {
    mountBar(toolbarHost, moduleToolbar({
      title: "Help",
      search: { id: "help-search", label: "Search help", placeholder: "Search help" },
      after: [{ id: "help-back", action: "back", icon: "fa-arrow-left", text: "Back", label: "Back", onClick: () => typeof options.onBack === "function" && options.onBack() }],
    }, root.ownerDocument || document));
  }
  const backButton = root.querySelector("#help-back");
  if (backButton) {
    backButton.disabled = true;
  }
  const search = root.querySelector("#help-search");
  const index = root.querySelector("#help-index");
  const article = root.querySelector("#help-article");
  let query = "";
  let pageId = pages[0] ? pages[0].id : "";
  let sectionId = "";

  function paint() {
    if (!index || !article) {
      return;
    }
    const hits = filterHelp(pages, query);
    if (hits.length > 0 && !hits.some((hit) => hit.page.id === pageId)) {
      pageId = hits[0].page.id;
      sectionId = "";
    }
    paintIndex(hits);
    paintArticle(hits);
  }

  function paintIndex(hits) {
    index.replaceChildren();
    if (hits.length === 0) {
      const empty = document.createElement("p");
      empty.className = "empty";
      empty.textContent = "No matching pages.";
      index.append(empty);
      return;
    }
    const tree = document.createElement("ul");
    tree.className = "tree";
    tree.setAttribute("role", "tree");
    tree.setAttribute("aria-label", "Help topics");
    for (const hit of hits) {
      const item = document.createElement("li");
      const button = document.createElement("button");
      button.type = "button";
      button.setAttribute("role", "treeitem");
      button.dataset.help = hit.page.id;
      button.setAttribute("aria-expanded", "true");
      button.textContent = hit.page.title;
      if (hit.page.id === pageId) {
        button.setAttribute("aria-current", "true");
      }
      item.append(button);
      const group = document.createElement("ul");
      group.setAttribute("role", "group");
      for (const id of hit.sectionIds) {
        const section = hit.page.sections.find((entry) => entry.id === id);
        if (!section) {
          continue;
        }
        const child = document.createElement("li");
        const sectionButton = document.createElement("button");
        sectionButton.type = "button";
        sectionButton.setAttribute("role", "treeitem");
        sectionButton.dataset.help = hit.page.id;
        sectionButton.dataset.helpSection = section.id;
        sectionButton.textContent = section.title;
        if (hit.page.id === pageId && section.id === sectionId) {
          sectionButton.setAttribute("aria-current", "true");
        }
        child.append(sectionButton);
        group.append(child);
      }
      item.append(group);
      tree.append(item);
    }
    index.append(tree);
  }

  function paintArticle(hits) {
    article.replaceChildren();
    const hit = hits.find((entry) => entry.page.id === pageId);
    if (!hit) {
      const empty = document.createElement("p");
      empty.className = "empty";
      empty.textContent = "No matching pages.";
      article.append(empty);
      return;
    }
    const title = document.createElement("h2");
    title.id = "help-title";
    title.textContent = hit.page.title;
    article.append(title);
    article.setAttribute("aria-labelledby", "help-title");
    for (const section of hit.page.sections) {
      const block = document.createElement("section");
      block.id = `help-sec-${hit.page.id}-${section.id}`;
      const heading = document.createElement("h3");
      heading.textContent = section.title;
      block.append(heading);
      for (const paragraph of section.paragraphs) {
        const p = document.createElement("p");
        p.textContent = paragraph;
        block.append(p);
      }
      if (typeof section.code === "string" && section.code !== "") {
        const sample = document.createElement("pre");
        sample.className = "help-code";
        sample.textContent = section.code;
        block.append(sample);
      }
      article.append(block);
    }
    const target = sectionId === "" ? null : article.querySelector(`#help-sec-${hit.page.id}-${sectionId}`);
    if (target && typeof target.scrollIntoView === "function") {
      target.scrollIntoView({ block: "start" });
    } else {
      article.scrollTop = 0;
    }
  }

  function onInput() {
    query = search.value;
    paint();
  }

  function onKey(event) {
    if (event.key === "Escape") {
      search.value = "";
      query = "";
      paint();
      return;
    }
    if (event.key !== "Enter") {
      return;
    }
    const hits = filterHelp(pages, query);
    if (hits.length === 0) {
      return;
    }
    pageId = hits[0].page.id;
    sectionId = hits[0].sectionIds[0] || "";
    paint();
  }

  function onIndexClick(event) {
    const button = event.target instanceof Element ? event.target.closest("button[data-help]") : null;
    if (!button || !index.contains(button)) {
      return;
    }
    pageId = button.dataset.help || pageId;
    sectionId = button.dataset.helpSection || "";
    paint();
  }

  if (search) {
    search.addEventListener("input", onInput);
    search.addEventListener("keydown", onKey);
  }
  if (index) {
    index.addEventListener("click", onIndexClick);
  }
  paint();

  function open(nextPage, nextSection) {
    const page = pages.find((item) => item.id === nextPage);
    if (!page) {
      return false;
    }
    query = "";
    if (search) {
      search.value = "";
    }
    pageId = page.id;
    sectionId = page.sections.some((item) => item.id === nextSection) ? nextSection : "";
    paint();
    return true;
  }

  return {
    open,
    setBackEnabled(on) {
      if (backButton) {
        backButton.disabled = !on;
      }
    },
    destroy() {
      if (search) {
        search.removeEventListener("input", onInput);
        search.removeEventListener("keydown", onKey);
      }
      if (index) {
        index.removeEventListener("click", onIndexClick);
      }
    },
  };
}

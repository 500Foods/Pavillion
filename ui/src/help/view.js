import { iconClass } from "../common/icons/registry.js";
import { helpPages } from "./pages.js";
import { moduleToolbar, moduleFooter, mountBar } from "../common/module-bar.js";

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
   by the shared module bar: title Help, search right, Back right of search.
   A chevron toggle to the left of the title opens a filter panel that slides
   in from the left and takes the full height of the help index. The chevron
   has a 1px vertical separator to its right (matching the search border),
   and the index shifts right by the panel width when the panel is open so the
   panel's right border aligns with the chevron separator. The panel fades in
   and out along with the slide.

   The filter panel, index, split-resize, and article live in .help-content,
   a flex row inside #panel-help. Below it is #help-toolbar-bottom, a
   pav-module-footer, holding Refresh on the left and Copy/Save/Print on the
   right.

   options.tooltips: the tooltip host from shell.js, used to bind labels to the
   icon-only filter chips. */
export function createHelpView(root, pages = helpPages, options = {}) {
  const toolbarHost = root.querySelector("#help-toolbar");
  const doc = root.ownerDocument || document;
  if (toolbarHost && !toolbarHost.querySelector("#help-search")) {
    mountBar(toolbarHost, moduleToolbar({
      title: "Help",
      search: { id: "help-search", label: "Search help", placeholder: "Search help" },
      after: [{ id: "help-back", action: "back", icon: iconClass("help.back"), text: "Back", label: "Back", onClick: () => typeof options.onBack === "function" && options.onBack() }],
    }, doc));
  }
  const footerHost = root.querySelector("#help-toolbar-bottom");
  if (footerHost && !footerHost.querySelector("button")) {
    mountBar(footerHost, moduleFooter({
      label: "Help actions",
      onRefresh: () => { if (typeof options.onRefresh === "function") options.onRefresh(); },
      end: [
        { id: "help-copy", action: "copy", icon: iconClass("help.copy"), text: "Copy", label: "Copy topic", onClick: () => {} },
        { id: "help-save", action: "save", icon: iconClass("help.save"), text: "Save", label: "Save topic", onClick: () => {} },
        { id: "help-print", action: "print", icon: iconClass("help.print"), text: "Print", label: "Print topic", onClick: () => {} },
      ],
    }, doc));
  }
  const titleSpan = toolbarHost && toolbarHost.querySelector(".pav-module-title");
  const chevron = doc.createElement("button");
  chevron.type = "button";
  chevron.id = "help-filter-toggle";
  chevron.className = "help-filter-chevron";
  chevron.setAttribute("aria-label", "Toggle filter panel");
  chevron.setAttribute("aria-pressed", "false");
  const chevronIcon = doc.createElement("i");
  chevronIcon.className = `icon ${iconClass("help.chevron")}`;
  chevronIcon.setAttribute("aria-hidden", "true");
  chevron.append(chevronIcon);
  if (titleSpan && titleSpan.parentNode) {
    titleSpan.parentNode.insertBefore(chevron, titleSpan);
  }
  if (titleSpan && titleSpan.parentNode && !titleSpan.parentNode.querySelector(".help-filter-separator")) {
    const separator = doc.createElement("div");
    separator.className = "help-filter-separator";
    separator.setAttribute("aria-hidden", "true");
    titleSpan.parentNode.insertBefore(separator, titleSpan);
  }

  const split = root.querySelector(".help-content");
  const index = root.querySelector("#help-index");
  const article = root.querySelector("#help-article");
  let filterPanel = root.querySelector("#help-filter-panel");
  if (!filterPanel && split) {
    filterPanel = doc.createElement("div");
    filterPanel.id = "help-filter-panel";
    filterPanel.className = "help-filter-panel";
    filterPanel.setAttribute("aria-hidden", "true");
    const strip = doc.createElement("div");
    strip.className = "help-filter-icon-strip";
     const filters = [
       { id: "help-filter-all", label: "All", tip: "All", icon: iconClass("help.filter.all"), cat: "all" },
       { id: "help-filter-pages", label: "Pages", tip: "Pages", icon: iconClass("help.filter.pages"), cat: "pages" },
       { id: "help-filter-dev", label: "Dev", tip: "Dev", icon: iconClass("help.filter.dev"), cat: "dev" },
       { id: "help-filter-settings", label: "Settings", tip: "Settings", icon: iconClass("help.filter.settings"), cat: "settings" },
       { id: "help-filter-mail", label: "Mail", tip: "Mail", icon: iconClass("help.filter.mail"), cat: "mail" },
     ];
     for (const spec of filters) {
       const chip = doc.createElement("button");
       chip.type = "button";
       chip.id = spec.id;
       chip.className = "help-filter-chip";
       chip.dataset.helpFilter = spec.cat;
       chip.setAttribute("aria-pressed", spec.cat === "all" ? "true" : "false");
       chip.setAttribute("aria-label", spec.tip);
       const icon = doc.createElement("i");
       icon.className = `icon ${spec.icon}`;
       icon.setAttribute("aria-hidden", "true");
       chip.append(icon);
       if (options.tooltips) {
         options.tooltips.bind(chip, spec.tip);
       }
       strip.append(chip);
     }
    filterPanel.append(strip);
    split.insertBefore(filterPanel, index);
  }
   const backButton = root.querySelector("#help-back");
  if (backButton) {
    backButton.disabled = true;
  }
  const search = root.querySelector("#help-search");
  let query = "";
  let pageId = pages[0] ? pages[0].id : "";
  let sectionId = "";
  let filterActive = "all";

  const FILTER_CATEGORIES = {
    pages: new Set(["app", "introduction"]),
    dev: new Set(["developer"]),
    settings: new Set(["settings"]),
    mail: new Set(["mail"]),
  };

  function paint() {
    if (!index || !article) {
      return;
    }
    const hits = filterHelp(pages, query).filter((hit) => {
      if (filterActive === "all") {
        return true;
      }
      const set = FILTER_CATEGORIES[filterActive];
      return set ? set.has(hit.page.id) : true;
    });
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
    if (hit.page.id === "app") {
      /* Mark above the version title, as wide as it (D-UI.md Help mark). */
      const lockup = document.createElement("div");
      lockup.className = "pav-mark-lockup";
      const mark = document.createElement("img");
      mark.src = "/mark.png";
      mark.alt = "";
      lockup.append(mark, title);
      article.append(lockup);
    } else {
      article.append(title);
    }
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

  function onToggle() {
    const expanded = chevron.getAttribute("aria-pressed") === "true";
    chevron.setAttribute("aria-pressed", String(!expanded));
    if (chevronIcon) {
      chevronIcon.classList.toggle("fa-chevron-right", expanded);
      chevronIcon.classList.toggle("fa-chevron-left", !expanded);
    }
    if (filterPanel) {
      filterPanel.setAttribute("aria-hidden", String(expanded));
    }
    if (split) {
      split.classList.toggle("help-filter-open", !expanded);
    }
  }

  if (search) {
    search.addEventListener("input", onInput);
    search.addEventListener("keydown", onKey);
  }
  if (index) {
    index.addEventListener("click", onIndexClick);
  }
  if (chevron && filterPanel) {
    chevron.addEventListener("click", onToggle);
    chevron.addEventListener("keydown", (event) => {
      if (event.key === "Enter" || event.key === " ") {
        event.preventDefault();
        onToggle();
      }
    });
  }
  const filterChips = filterPanel ? [...filterPanel.querySelectorAll("[data-help-filter]")] : [];
  const TIP_MAP = { all: "All", pages: "Pages", dev: "Dev", settings: "Settings", mail: "Mail" };
  for (const chip of filterChips) {
    chip.addEventListener("click", () => {
      filterActive = chip.dataset.helpFilter;
      for (const other of filterChips) {
        other.setAttribute("aria-pressed", String(other === chip));
      }
      paint();
    });
    if (options.tooltips && !chip.getAttribute("aria-label")) {
      options.tooltips.bind(chip, TIP_MAP[chip.dataset.helpFilter]);
    }
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
      if (chevron) {
        chevron.removeEventListener("click", onToggle);
      }
    },
  };
}

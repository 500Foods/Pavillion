import { Compartment, EditorState, RangeSetBuilder, StateEffect, StateField } from "@codemirror/state";
import { Decoration, EditorView, WidgetType } from "@codemirror/view";
import { countMatches, lineVisible, normalizeFilter } from "./filter.js";
import { fileLine, parseFileLine, screenLine } from "./format.js";

const LEVELS = new Set(["ERROR", "WARN", "INFO", "DEBUG"]);
const zoneEffect = StateEffect.define();

class LocalTimeWidget extends WidgetType {
  constructor(label, utc) {
    super();
    this.label = label;
    this.utc = utc;
  }

  eq(other) {
    return other.label === this.label && other.utc === this.utc;
  }

  toDOM() {
    const span = document.createElement("span");
    span.className = "pav-log-fields";
    span.textContent = this.label;
    span.title = this.utc;
    return span;
  }
}

/* The document stays the UTC file line. One widget draws the whole
   screen line: local time, level, and subsystem at fixed widths, two
   spaces between fields, then the message. The line title is the UTC stamp. */
export function buildLogDecorations(state, timeZone) {
  const builder = new RangeSetBuilder();
  for (let number = 1; number <= state.doc.lines; number += 1) {
    const line = state.doc.line(number);
    const parsed = parseFileLine(line.text);
    if (!parsed || !LEVELS.has(parsed.level) || parsed.ts.length !== 24) {
      continue;
    }
    builder.add(line.from, line.from, Decoration.line({
      class: `pav-log-level-${parsed.level}`,
      attributes: { title: parsed.ts },
    }));
    builder.add(line.from, line.to, Decoration.replace({
      widget: new LocalTimeWidget(screenLine(parsed, timeZone), parsed.ts),
    }));
  }
  return builder.finish();
}

function orderedLines(lines, sinceSeq) {
  const fresh = [];
  for (const line of lines) {
    if (!line || typeof line.seq !== "number" || !Number.isFinite(line.seq) || line.seq <= sinceSeq) {
      continue;
    }
    fresh.push(line);
  }
  fresh.sort((a, b) => a.seq - b.seq);
  return fresh;
}

const LOG_FONT = '"Vanadium Mono", ui-monospace, monospace';

function fontTheme(scale) {
  const size = Number.isFinite(scale) ? scale : 1;
  return EditorView.theme({
    "&": {
      height: "100%",
      backgroundColor: "transparent",
      fontSize: `${0.85 * size}rem`,
    },
    ".cm-scroller, .cm-content, .cm-line": {
      fontFamily: LOG_FONT,
    },
    "&.cm-focused": {
      outline: "none",
    },
  });
}

function messageOf(err) {
  if (err instanceof Error) {
    return err.message;
  }
  return String(err);
}

export function createLogView(parent, api, options = {}) {
  let timeZone = options.timeZone || "America/Vancouver";
  let zoom = Number.isFinite(options.zoom) ? options.zoom : 1;
  let filter = normalizeFilter(options.filter);
  let generation = 0;
  let unsubscribe = null;
  let lastSeq = 0;
  let loaded = [];
  let isOpen = false;
  let pinning = false;
  let pinTimer = 0;
  let subsystemKey = null;
  const font = new Compartment();
  const wrap = new Compartment();

  const menus = [];

  function closeMenus() {
    for (const menu of menus) {
      menu.panel.hidden = true;
      menu.button.setAttribute("aria-expanded", "false");
    }
  }

  function addMenu(align) {
    const root = document.createElement("div");
    root.className = "log-menu";
    const button = document.createElement("button");
    button.type = "button";
    button.className = "log-menu-button";
    button.setAttribute("aria-haspopup", "true");
    button.setAttribute("aria-expanded", "false");
    const panel = document.createElement("div");
    panel.className = "log-menu-panel";
    panel.hidden = true;
    if (align === "end") {
      panel.dataset.align = "end";
    }
    button.addEventListener("click", (event) => {
      event.stopPropagation();
      const open = panel.hidden;
      closeMenus();
      if (open) {
        panel.hidden = false;
        button.setAttribute("aria-expanded", "true");
      }
    });
    root.append(button, panel);
    menus.push({ button, panel });
    return { root, button, panel };
  }

  function checkRow(text, onChange) {
    const label = document.createElement("label");
    label.className = "log-check";
    const box = document.createElement("input");
    box.type = "checkbox";
    box.addEventListener("change", () => onChange(box));
    label.append(box, document.createTextNode(` ${text}`));
    return { label, box };
  }

  function rule() {
    const line = document.createElement("div");
    line.className = "log-menu-rule";
    return line;
  }

  const severity = addMenu();
  severity.button.setAttribute("aria-label", "Severity");
  const severityAll = checkRow("All", (box) => {
    filter.levels = box.checked ? ["ERROR", "WARN", "INFO", "DEBUG"] : [];
    commit(true);
  });
  const severityNone = checkRow("None", (box) => {
    filter.levels = box.checked ? [] : ["ERROR", "WARN", "INFO", "DEBUG"];
    commit(true);
  });
  const severityBoxes = [];
  const severityItems = document.createElement("div");
  for (const name of ["ERROR", "WARN", "INFO", "DEBUG"]) {
    const row = checkRow(name, () => {
      filter.levels = severityBoxes.filter((item) => item.checked).map((item) => item.value);
      commit(true);
    });
    row.box.value = name;
    severityBoxes.push(row.box);
    severityItems.append(row.label);
  }
  severity.panel.append(severityAll.label, severityNone.label, rule(), severityItems);

  const subsystem = addMenu();
  subsystem.button.setAttribute("aria-label", "Subsystem");
  const subsystemAll = checkRow("All", (box) => {
    filter.subsystems = box.checked ? null : [];
    commit(true);
  });
  const subsystemNone = checkRow("None", (box) => {
    filter.subsystems = box.checked ? [] : null;
    commit(true);
  });
  const subsystemItems = document.createElement("div");
  subsystem.panel.append(subsystemAll.label, subsystemNone.label, rule(), subsystemItems);

  const topSpacer = document.createElement("div");
  topSpacer.className = "log-bar-spacer";

  const search = document.createElement("input");
  search.type = "search";
  search.setAttribute("aria-label", "Search the log");
  search.value = filter.text;
  search.maxLength = 200;
  search.placeholder = "Search";

  const gear = addMenu("end");
  gear.button.classList.add("log-gear");
  gear.button.setAttribute("aria-label", "Log options");
  const gearIcon = document.createElement("i");
  gearIcon.className = "icon fa-sharp-duotone fa-solid fa-gear";
  gearIcon.setAttribute("aria-hidden", "true");
  gear.button.append(gearIcon);
  const follow = checkRow("Follow", (box) => {
    filter.follow = box.checked;
    commit(false);
    if (filter.follow) {
      scrollToEnd();
    }
  });
  const followBox = follow.box;
  followBox.checked = filter.follow;
  const wrapRow = checkRow("Wrap", (box) => {
    filter.wrap = box.checked;
    view.dispatch({ effects: wrap.reconfigure(filter.wrap ? EditorView.lineWrapping : []) });
    commit(false);
  });
  const wrapBox = wrapRow.box;
  wrapBox.checked = filter.wrap;
  const levelField = document.createElement("label");
  levelField.className = "log-field";
  levelField.append(document.createTextNode("Session log level"));
  const levelSelect = document.createElement("select");
  levelSelect.setAttribute("aria-label", "Session log level");
  for (const name of ["error", "warn", "info", "debug"]) {
    const option = document.createElement("option");
    option.value = name;
    option.textContent = name;
    levelSelect.append(option);
  }
  levelSelect.value = ["error", "warn", "info", "debug"].includes(options.logLevel) ? options.logLevel : "info";
  levelSelect.addEventListener("change", () => {
    if (typeof options.onLogLevel === "function") {
      Promise.resolve(options.onLogLevel(levelSelect.value)).catch((err) => report(err));
    }
  });
  levelField.append(levelSelect);
  gear.panel.append(follow.label, wrapRow.label, rule(), levelField);

  const refresh = document.createElement("button");
  refresh.type = "button";
  refresh.textContent = "Refresh";
  refresh.addEventListener("click", () => {
    open().catch((err) => report(err));
  });

  const status = document.createElement("div");
  status.className = "log-status";
  const showing = document.createElement("span");
  showing.className = "log-count";
  const matches = document.createElement("span");
  matches.className = "log-count";
  matches.setAttribute("aria-live", "polite");
  status.append(showing, matches);

  const bottomSpacer = document.createElement("div");
  bottomSpacer.className = "log-bar-spacer";

  const copy = document.createElement("button");
  copy.type = "button";
  copy.textContent = "Copy";
  copy.addEventListener("click", () => {
    copyVisible().catch((err) => report(err));
  });

  const save = document.createElement("button");
  save.type = "button";
  save.textContent = "Save";
  save.addEventListener("click", () => {
    saveVisible().catch((err) => report(err));
  });

  const print = document.createElement("button");
  print.type = "button";
  print.textContent = "Print";
  print.addEventListener("click", () => printVisible());

  const topbar = options.topbar instanceof Element ? options.topbar : null;
  const bottombar = options.bottombar instanceof Element ? options.bottombar : null;
  const editorHost = document.createElement("div");
  editorHost.className = "log-editor pav-log";
  const sheet = document.createElement("pre");
  sheet.className = "log-print";
  if (topbar && bottombar) {
    topbar.append(severity.root, subsystem.root, topSpacer, search, gear.root);
    bottombar.append(refresh, status, bottomSpacer, copy, save, print);
    parent.replaceChildren(editorHost, sheet);
  } else {
    const localTop = document.createElement("div");
    localTop.className = "button-bar log-topbar";
    localTop.append(severity.root, subsystem.root, topSpacer, search, gear.root);
    const localBottom = document.createElement("div");
    localBottom.className = "button-bar log-bottombar";
    localBottom.append(refresh, status, bottomSpacer, copy, save, print);
    parent.replaceChildren(localTop, editorHost, sheet, localBottom);
  }

  search.addEventListener("input", () => {
    filter.text = search.value;
    commit(true);
  });

  function onPointerDown(event) {
    const target = event.target;
    if (target instanceof Element && target.closest(".log-menu")) {
      return;
    }
    closeMenus();
  }

  function onEscape(event) {
    if (event.key === "Escape") {
      closeMenus();
    }
  }

  document.addEventListener("pointerdown", onPointerDown);
  window.addEventListener("keydown", onEscape);
  paintSeverity();
  paintSubsystems();

  const decorations = StateField.define({
    create(state) {
      return buildLogDecorations(state, timeZone);
    },
    update(value, transaction) {
      const refresh = transaction.effects.some((effect) => effect.is(zoneEffect));
      if (transaction.docChanged || refresh) {
        return buildLogDecorations(transaction.state, timeZone);
      }
      return value;
    },
    provide: (field) => EditorView.decorations.from(field),
  });

  const view = new EditorView({
    parent: editorHost,
    state: EditorState.create({
      doc: "",
      extensions: [
        EditorState.readOnly.of(true),
        EditorView.editable.of(false),
        EditorView.contentAttributes.of({ "aria-label": "Log", spellcheck: "false" }),
        EditorView.domEventHandlers({
          scroll(_event, editor) {
            if (pinning || !filter.follow) {
              return;
            }
            const scroller = editor.scrollDOM;
            const gap = scroller.scrollHeight - scroller.scrollTop - scroller.clientHeight;
            if (gap > 8) {
              filter.follow = false;
              followBox.checked = false;
              commit(false);
            }
          },
        }),
        decorations,
        font.of(fontTheme(zoom)),
        wrap.of(filter.wrap ? EditorView.lineWrapping : []),
      ],
    }),
  });

  function report(err) {
    if (typeof options.onError === "function") {
      options.onError(messageOf(err));
    }
  }

  function pinScroll() {
    pinning = true;
    if (pinTimer) {
      window.clearTimeout(pinTimer);
    }
    pinTimer = window.setTimeout(() => {
      pinTimer = 0;
      pinning = false;
    }, 80);
  }

  function scrollToEnd() {
    pinScroll();
    view.dispatch({
      effects: EditorView.scrollIntoView(view.state.doc.length, { y: "end" }),
    });
  }

  function severityText() {
    if (filter.levels.length === 4) {
      return "All severities";
    }
    if (filter.levels.length === 0) {
      return "No severity";
    }
    if (filter.levels.length === 1) {
      return filter.levels[0];
    }
    return `${filter.levels.length} severities`;
  }

  function summaryText() {
    if (filter.subsystems === null) {
      return "All subsystems";
    }
    if (filter.subsystems.length === 0) {
      return "No subsystems";
    }
    if (filter.subsystems.length === 1) {
      return filter.subsystems[0];
    }
    return `${filter.subsystems.length} subsystems`;
  }

  function paintSeverity() {
    for (const box of severityBoxes) {
      box.checked = filter.levels.includes(box.value);
    }
    severityAll.box.checked = filter.levels.length === 4;
    severityNone.box.checked = filter.levels.length === 0;
    severity.button.textContent = severityText();
  }

  function paintSubsystems() {
    const names = [];
    for (const entry of loaded) {
      if (entry.subsystem !== "" && !names.includes(entry.subsystem)) {
        names.push(entry.subsystem);
      }
    }
    names.sort();
    const key = names.join("\0");
    if (key !== subsystemKey) {
      subsystemKey = key;
      subsystemItems.replaceChildren();
      for (const name of names) {
        const row = checkRow(name, () => {
          const boxes = [...subsystemItems.querySelectorAll("input")];
          const checked = boxes.filter((item) => item.checked).map((item) => item.value);
          filter.subsystems = checked.length === boxes.length ? null : checked;
          commit(true);
        });
        row.box.value = name;
        subsystemItems.append(row.label);
      }
    }
    const boxes = [...subsystemItems.querySelectorAll("input")];
    for (const box of boxes) {
      box.checked = filter.subsystems === null || filter.subsystems.includes(box.value);
    }
    subsystemAll.box.checked = filter.subsystems === null;
    subsystemNone.box.checked = Array.isArray(filter.subsystems) && filter.subsystems.length === 0;
    subsystem.button.textContent = summaryText();
  }

  function visibleLines() {
    return loaded.filter((entry) => lineVisible(entry, filter));
  }

  function paintChrome(visible) {
    const shown = visible.length;
    showing.textContent = `showing ${shown} of ${loaded.length}`;
    if (filter.text === "") {
      matches.textContent = "";
    } else {
      const body = shown === 0 ? "" : `${visible.map(fileLine).join("\n")}\n`;
      const count = countMatches(body, filter.text);
      matches.textContent = count === 1 ? "1 match" : `${count} matches`;
    }
    sheet.textContent = visible.map((entry) => screenLine(entry, timeZone)).join("\n");
    paintSeverity();
    paintSubsystems();
  }

  function replaceAll() {
    const visible = visibleLines();
    const text = visible.length === 0 ? "" : `${visible.map(fileLine).join("\n")}\n`;
    const end = view.state.doc.length;
    pinScroll();
    view.dispatch({
      changes: { from: 0, to: end, insert: text },
    });
    if (filter.follow) {
      scrollToEnd();
    }
    paintChrome(visible);
  }

  function appendVisible(entries) {
    const fresh = entries.filter((entry) => lineVisible(entry, filter));
    if (fresh.length > 0) {
      const chunk = `${fresh.map(fileLine).join("\n")}\n`;
      const end = view.state.doc.length;
      pinScroll();
      view.dispatch({
        changes: { from: end, insert: chunk },
        effects: filter.follow ? EditorView.scrollIntoView(end, { y: "end" }) : undefined,
      });
    }
    paintChrome(visibleLines());
  }

  function commit(repaint) {
    filter = normalizeFilter(filter);
    if (repaint) {
      replaceAll();
    }
    if (typeof options.onFilter === "function") {
      options.onFilter(normalizeFilter(filter));
    }
  }

  function remember(lines, replace) {
    const fresh = orderedLines(lines, replace ? 0 : lastSeq);
    const added = [];
    if (replace) {
      loaded = [];
      lastSeq = 0;
    }
    for (const line of fresh) {
      const entry = {
        seq: line.seq,
        ts: typeof line.ts === "string" ? line.ts : "",
        level: typeof line.level === "string" ? line.level : "",
        subsystem: typeof line.subsystem === "string" ? line.subsystem : "",
        text: typeof line.text === "string" ? line.text : "",
      };
      if (entry.seq > lastSeq) {
        lastSeq = entry.seq;
      }
      loaded.push(entry);
      added.push(entry);
    }
    return added;
  }

  function onFind(event) {
    if (!isOpen) {
      return;
    }
    if ((event.ctrlKey || event.metaKey) && !event.altKey && !event.shiftKey && event.key.toLowerCase() === "f") {
      event.preventDefault();
      search.focus();
      search.select();
    }
  }

  window.addEventListener("keydown", onFind);

  async function copyVisible() {
    const visible = visibleLines();
    const text = visible.length === 0 ? "" : `${visible.map(fileLine).join("\n")}\n`;
    if (navigator.clipboard && typeof navigator.clipboard.writeText === "function") {
      try {
        await navigator.clipboard.writeText(text);
        return;
      } catch {
        /* The textarea path covers a webview that refuses the async clipboard. */
      }
    }
    const area = document.createElement("textarea");
    area.value = text;
    document.body.append(area);
    area.select();
    document.execCommand("copy");
    area.remove();
  }

  async function saveVisible() {
    const host = options.host;
    if (!host || !host.dialog || typeof host.dialog.save !== "function") {
      throw new Error("Save is not available");
    }
    const path = await host.dialog.save({
      title: "Save log",
      defaultPath: "pavillion.log",
      filters: [{ name: "Log", extensions: ["log", "txt"] }],
    });
    if (!path) {
      return;
    }
    const spec = normalizeFilter(filter);
    await api.log.export({
      path,
      filter: {
        levels: spec.levels,
        subsystems: spec.subsystems,
        text: spec.text,
      },
    });
  }

  function printVisible() {
    const visible = visibleLines();
    sheet.textContent = visible.map((entry) => screenLine(entry, timeZone)).join("\n");
    window.print();
  }

  function open() {
    const ticket = generation + 1;
    generation = ticket;
    isOpen = true;
    if (unsubscribe) {
      unsubscribe();
      unsubscribe = null;
    }
    const pending = [];
    let ready = false;
    unsubscribe = api.on("log.lines", (payload) => {
      if (ticket !== generation) {
        return;
      }
      const lines = payload && Array.isArray(payload.lines) ? payload.lines : [];
      if (!ready) {
        pending.push(...lines);
        return;
      }
      appendVisible(remember(lines, false));
    });
    return api.log.tail({}).then((tail) => {
      if (ticket !== generation) {
        return;
      }
      ready = true;
      remember(tail && Array.isArray(tail.lines) ? tail.lines : [], true);
      replaceAll();
      appendVisible(remember(pending, false));
      view.requestMeasure();
    }).catch((err) => {
      if (ticket === generation && unsubscribe) {
        unsubscribe();
        unsubscribe = null;
        generation += 1;
        isOpen = false;
      }
      throw err;
    });
  }

  function close() {
    generation += 1;
    isOpen = false;
    if (unsubscribe) {
      unsubscribe();
      unsubscribe = null;
    }
  }

  return {
    open,
    close,
    measure() {
      view.requestMeasure();
      if (filter.follow) {
        scrollToEnd();
      }
    },
    setZoom(scale) {
      zoom = Number.isFinite(scale) ? scale : 1;
      view.dispatch({ effects: font.reconfigure(fontTheme(zoom)) });
    },
    setTimeZone(zone) {
      timeZone = zone || "America/Vancouver";
      view.dispatch({ effects: zoneEffect.of(null) });
      paintChrome(visibleLines());
    },
    destroy() {
      close();
      window.removeEventListener("keydown", onFind);
      window.removeEventListener("keydown", onEscape);
      document.removeEventListener("pointerdown", onPointerDown);
      if (pinTimer) {
        window.clearTimeout(pinTimer);
      }
      view.destroy();
    },
  };
}

import { Compartment, EditorState, RangeSetBuilder, StateEffect, StateField } from "@codemirror/state";
import { Decoration, EditorView, WidgetType } from "@codemirror/view";
import { LOG_LEVELS, SESSIONS, VIEW_LEVEL_NAMES, countMatches, levelsAtOrAbove, lineVisible, normalizeFilter, queryArgs } from "./filter.js";
import { fromLocalInput, inRange, normalizeRange, rangeBounds, rangeWidened, toLocalInput } from "./range.js";
import { fileLine, parseFileLine, screenLine } from "./format.js";

const LEVELS = new Set(LOG_LEVELS);
/* One log.query page of history (H-4). */
export const HISTORY_PAGE = 2000;
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

/* The document is the screen line (local 24 h time in the profile zone,
   level, subsystem, message; H-4 Lock). Each line gets its level class and
   the UTC stamp as its title. A UTC file line (older callers, tests) is
   still drawn through the local-time widget. */
export function buildLogDecorations(state, timeZone, stamps = []) {
  const builder = new RangeSetBuilder();
  for (let number = 1; number <= state.doc.lines; number += 1) {
    const line = state.doc.line(number);
    const parsed = parseFileLine(line.text);
    if (!parsed || !LEVELS.has(parsed.level) || parsed.ts.length !== 24) {
      const level = line.text.slice(14, 19).trim();
      if (LEVELS.has(level)) {
        const utc = typeof stamps[number - 1] === "string" ? stamps[number - 1] : "";
        builder.add(line.from, line.from, Decoration.line({
          class: `pav-log-level-${level}`,
          attributes: utc ? { title: utc } : {},
        }));
      }
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
  let session = "all";
  /* UTC stamp of each document line: the document is the local screen line
     (H-4 Lock), the stamp is the hover title. */
  let docStamps = [];
  const show = (entry) => screenLine(entry, timeZone);
  let exhausted = false;
  let olderPending = null;
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
    filter.levels = box.checked ? LOG_LEVELS.slice() : [];
    commit(true);
  });
  const severityNone = checkRow("None", (box) => {
    filter.levels = box.checked ? [] : LOG_LEVELS.slice();
    commit(true);
  });
  const severityBoxes = [];
  const severityItems = document.createElement("div");
  for (const name of LOG_LEVELS) {
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

  /* Time range (H-4 Lock): 12 h, 24 h, 7 d, All, Custom. */
  const RANGE_LABELS = { "12h": "12 h", "24h": "24 h", "7d": "7 d", all: "All", custom: "Custom" };
  const rangeSelect = document.createElement("select");
  rangeSelect.className = "log-range";
  rangeSelect.setAttribute("aria-label", "Time range");
  for (const value of ["12h", "24h", "7d", "all", "custom"]) {
    const option = document.createElement("option");
    option.value = value;
    option.textContent = RANGE_LABELS[value];
    rangeSelect.append(option);
  }
  const custom = document.createElement("span");
  custom.className = "log-range-custom";
  const sinceInput = document.createElement("input");
  sinceInput.type = "datetime-local";
  sinceInput.setAttribute("aria-label", "From");
  const untilInput = document.createElement("input");
  untilInput.type = "datetime-local";
  untilInput.setAttribute("aria-label", "To");
  custom.append(sinceInput, untilInput);

  function paintRange() {
    const range = normalizeRange(filter.range);
    rangeSelect.value = typeof range === "string" ? range : "custom";
    custom.hidden = typeof range === "string";
    if (typeof range !== "string") {
      sinceInput.value = toLocalInput(range.since);
      untilInput.value = toLocalInput(range.until);
    }
  }

  function setRange(next) {
    const prev = filter.range;
    filter.range = normalizeRange(next);
    commit(false);
    paintRange();
    if (!isOpen) {
      return;
    }
    const widened = rangeWidened(prev, filter.range);
    open().then(() => (widened ? loadOlder() : undefined)).catch((err) => report(err));
  }

  rangeSelect.addEventListener("change", () => {
    if (rangeSelect.value !== "custom") {
      setRange(rangeSelect.value);
      return;
    }
    /* Custom starts as the range that was showing, up to now. */
    const bounds = currentBounds();
    custom.hidden = false;
    sinceInput.value = toLocalInput(bounds.since !== undefined ? bounds.since : Date.now() - 12 * 3600 * 1000);
    untilInput.value = "";
    setRange({ since: fromLocalInput(sinceInput.value) });
  });
  for (const input of [sinceInput, untilInput]) {
    input.addEventListener("change", () => {
      const since = fromLocalInput(sinceInput.value);
      const until = fromLocalInput(untilInput.value);
      const next = {};
      if (since !== null) {
        next.since = since;
      }
      if (until !== null) {
        next.until = until;
      }
      setRange(Object.keys(next).length > 0 ? next : "all");
    });
  }

  const sessionSelect = document.createElement("select");
  sessionSelect.className = "log-session";
  sessionSelect.setAttribute("aria-label", "Session");
  for (const item of SESSIONS) {
    const option = document.createElement("option");
    option.value = item.value;
    option.textContent = item.label;
    sessionSelect.append(option);
  }
  sessionSelect.value = session;
  sessionSelect.addEventListener("change", () => {
    session = sessionSelect.value;
    if (isOpen) {
      open().catch((err) => report(err));
    }
  });

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
  levelField.append(document.createTextNode("Default level"));
  const levelSelect = document.createElement("select");
  levelSelect.setAttribute("aria-label", "Default level");
  for (const name of VIEW_LEVEL_NAMES) {
    const option = document.createElement("option");
    option.value = name;
    option.textContent = name;
    levelSelect.append(option);
  }
  levelSelect.value = VIEW_LEVEL_NAMES.includes(options.logLevel) ? options.logLevel : "info";
  levelSelect.addEventListener("change", () => {
    /* The default level checks that level and the more severe ones. */
    filter.levels = levelsAtOrAbove(levelSelect.value);
    commit(true);
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
  const logChrome = [severity.root, subsystem.root, rangeSelect, custom, sessionSelect, topSpacer, search, gear.root];

  function setChrome(shown) {
    for (const el of logChrome) {
      el.hidden = shown !== true;
    }
    if (shown === true) {
      paintRange();
    }
    if (shown !== true) {
      closeMenus();
    }
  }

  if (topbar && bottombar) {
    topbar.append(severity.root, subsystem.root, rangeSelect, custom, sessionSelect, topSpacer, search, gear.root);
    bottombar.append(refresh, status, bottomSpacer, copy, save, print);
    parent.replaceChildren(editorHost, sheet);
  } else {
    const localTop = document.createElement("div");
    localTop.className = "button-bar log-topbar";
    localTop.append(severity.root, subsystem.root, rangeSelect, custom, sessionSelect, topSpacer, search, gear.root);
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

  const listenRoot = options.root instanceof EventTarget ? options.root : null;
  const pointerTarget = listenRoot || document;
  const keyTarget = listenRoot || window;
  pointerTarget.addEventListener("pointerdown", onPointerDown);
  keyTarget.addEventListener("keydown", onEscape);
  paintSeverity();
  paintSubsystems();
  paintRange();

  const decorations = StateField.define({
    create(state) {
      return buildLogDecorations(state, timeZone, docStamps);
    },
    update(value, transaction) {
      const refresh = transaction.effects.some((effect) => effect.is(zoneEffect));
      if (transaction.docChanged || refresh) {
        return buildLogDecorations(transaction.state, timeZone, docStamps);
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
            if (!pinning && editor.scrollDOM.scrollTop < 40) {
              loadOlder();
            }
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
    if (filter.levels.length === LOG_LEVELS.length) {
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
    severityAll.box.checked = filter.levels.length === LOG_LEVELS.length;
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
    const bounds = currentBounds();
    return loaded.filter((entry) => lineVisible(entry, filter) && inRange(entry, bounds));
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
    const text = visible.length === 0 ? "" : `${visible.map(show).join("\n")}\n`;
    docStamps = visible.map((entry) => entry.ts);
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
    const bounds = currentBounds();
    const fresh = entries.filter((entry) => lineVisible(entry, filter) && inRange(entry, bounds));
    if (fresh.length > 0) {
      const chunk = `${fresh.map(show).join("\n")}\n`;
      docStamps.push(...fresh.map((entry) => entry.ts));
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

  function toEntry(line) {
    return {
      seq: line.seq,
      ts: typeof line.ts === "string" ? line.ts : "",
      level: typeof line.level === "string" ? line.level : "",
      subsystem: typeof line.subsystem === "string" ? line.subsystem : "",
      text: typeof line.text === "string" ? line.text : "",
    };
  }

  function rememberOlder(lines) {
    const floor = loaded.length > 0 ? loaded[0].seq : Infinity;
    const older = orderedLines(lines, 0).filter((line) => line.seq < floor).map(toEntry);
    if (loaded.length === 0) {
      for (const entry of older) {
        if (entry.seq > lastSeq) {
          lastSeq = entry.seq;
        }
      }
    }
    loaded = older.concat(loaded);
    return older;
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

  keyTarget.addEventListener("keydown", onFind);

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
    const sink = options.root instanceof Element ? options.root : document.body;
    sink.append(area);
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
    await api.log.export({ path, filter: queryArgs(filter, session) });
  }

  function printVisible() {
    const visible = visibleLines();
    sheet.textContent = visible.map((entry) => screenLine(entry, timeZone)).join("\n");
    window.print();
  }

  function currentBounds() {
    return rangeBounds(filter.range);
  }

  /* The newest PAGE lines before the oldest loaded one, from log.db
     (H-4 Lock). The filter stays client-side, so a page asks for every
     level and subsystem, within the range and session. */
  function fetchPage(beforeSeq) {
    const args = { ...currentBounds(), session, limit: HISTORY_PAGE };
    if (Number.isFinite(beforeSeq)) {
      args.beforeSeq = beforeSeq;
    }
    if (!api.log || typeof api.log.query !== "function") {
      return Promise.resolve([]);
    }
    return api.log.query(args).then((reply) => (reply && Array.isArray(reply.lines) ? reply.lines : []));
  }

  function prependVisible(entries) {
    const fresh = entries.filter((entry) => lineVisible(entry, filter) && inRange(entry, currentBounds()));
    if (fresh.length > 0) {
      const scroller = view.scrollDOM;
      const height = scroller.scrollHeight;
      const top = scroller.scrollTop;
      pinScroll();
      docStamps = fresh.map((entry) => entry.ts).concat(docStamps);
      view.dispatch({ changes: { from: 0, insert: `${fresh.map(show).join("\n")}\n` } });
      /* Keep the line that was on screen where it was. */
      view.requestMeasure({
        read: () => scroller.scrollHeight,
        write: (next) => {
          scroller.scrollTop = top + (next - height);
        },
      });
    }
    paintChrome(visibleLines());
  }

  function loadOlder() {
    if (!isOpen || exhausted || olderPending) {
      return olderPending || Promise.resolve();
    }
    const ticket = generation;
    const before = loaded.length > 0 ? loaded[0].seq : undefined;
    olderPending = fetchPage(before).then((lines) => {
      if (ticket !== generation) {
        return;
      }
      if (lines.length < HISTORY_PAGE) {
        exhausted = true;
      }
      prependVisible(rememberOlder(lines));
    }).catch(() => {
      /* No log.db (a test sink or no profile): the ring is all there is. */
      if (ticket === generation) {
        exhausted = true;
      }
    }).finally(() => {
      olderPending = null;
    });
    return olderPending;
  }

  function open() {
    const ticket = generation + 1;
    generation = ticket;
    isOpen = true;
    exhausted = false;
    olderPending = null;
    if (unsubscribe) {
      unsubscribe();
      unsubscribe = null;
    }
    const pending = [];
    let ready = false;
    const live = session !== "previous";
    unsubscribe = api.on("log.lines", (payload) => {
      if (ticket !== generation || !live) {
        return;
      }
      const lines = payload && Array.isArray(payload.lines) ? payload.lines : [];
      if (!ready) {
        pending.push(...lines);
        return;
      }
      appendVisible(remember(lines, false));
    });
    /* This run and All start from the ring inside the range; Last run is
       only in log.db. */
    const first = live
      ? api.log.tail({}).then((tail) => {
        const bounds = currentBounds();
        return (tail && Array.isArray(tail.lines) ? tail.lines : []).filter((line) => inRange(line, bounds));
      })
      : fetchPage(undefined).then((lines) => {
        if (lines.length < HISTORY_PAGE) {
          exhausted = true;
        }
        return lines;
      });
    return first.then((lines) => {
      if (ticket !== generation) {
        return;
      }
      ready = true;
      remember(lines, true);
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
      replaceAll();
      paintChrome(visibleLines());
    },
    setChrome,
    loadOlder,
    loadedCount() {
      return loaded.length;
    },
    destroy() {
      close();
      keyTarget.removeEventListener("keydown", onFind);
      keyTarget.removeEventListener("keydown", onEscape);
      pointerTarget.removeEventListener("pointerdown", onPointerDown);
      if (pinTimer) {
        window.clearTimeout(pinTimer);
      }
      view.destroy();
    },
  };
}

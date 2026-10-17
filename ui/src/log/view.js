import { Compartment, EditorState, RangeSetBuilder, StateEffect, StateField } from "@codemirror/state";
import { SearchQuery } from "@codemirror/search";
import { Decoration, EditorView, WidgetType } from "@codemirror/view";
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

export function createLogView(parent, api, options = {}) {
  if (typeof SearchQuery !== "function") {
    throw new Error("CodeMirror search is not installed");
  }
  let timeZone = options.timeZone || "America/Vancouver";
  let zoom = Number.isFinite(options.zoom) ? options.zoom : 1;
  let generation = 0;
  let unsubscribe = null;
  let lastSeq = 0;
  const font = new Compartment();

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
    parent,
    state: EditorState.create({
      doc: "",
      extensions: [
        EditorState.readOnly.of(true),
        EditorView.editable.of(false),
        EditorView.contentAttributes.of({ "aria-label": "Log", spellcheck: "false" }),
        decorations,
        font.of(fontTheme(zoom)),
      ],
    }),
  });

  function dispatchLines(lines, replace) {
    const fresh = orderedLines(lines, replace ? 0 : lastSeq);
    for (const line of fresh) {
      if (line.seq > lastSeq) {
        lastSeq = line.seq;
      }
    }
    const text = fresh.length === 0 ? "" : `${fresh.map(fileLine).join("\n")}\n`;
    if (!replace && text === "") {
      return;
    }
    const end = view.state.doc.length;
    view.dispatch({
      changes: replace ? { from: 0, to: end, insert: text } : { from: end, insert: text },
      effects: EditorView.scrollIntoView(end, { y: "end" }),
    });
  }

  function open() {
    const ticket = generation + 1;
    generation = ticket;
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
      dispatchLines(lines, false);
    });
    return api.log.tail({}).then((tail) => {
      if (ticket !== generation) {
        return;
      }
      ready = true;
      lastSeq = 0;
      dispatchLines(tail && Array.isArray(tail.lines) ? tail.lines : [], true);
      dispatchLines(pending, false);
      view.requestMeasure();
    }).catch((err) => {
      if (ticket === generation && unsubscribe) {
        unsubscribe();
        unsubscribe = null;
        generation += 1;
      }
      throw err;
    });
  }

  function close() {
    generation += 1;
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
      view.dispatch({
        effects: EditorView.scrollIntoView(view.state.doc.length, { y: "end" }),
      });
    },
    setZoom(scale) {
      zoom = Number.isFinite(scale) ? scale : 1;
      view.dispatch({ effects: font.reconfigure(fontTheme(zoom)) });
    },
    setTimeZone(zone) {
      timeZone = zone || "America/Vancouver";
      view.dispatch({ effects: zoneEffect.of(null) });
    },
    destroy() {
      close();
      view.destroy();
    },
  };
}

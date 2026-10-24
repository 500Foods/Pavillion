import { EditorState } from "@codemirror/state";
import { expect, test } from "vitest";
import { fileLine, screenLine } from "./format.js";
import { buildLogDecorations } from "./view.js";

test("level and local time are decorations over the UTC line", () => {
  const line = fileLine({
    ts: "2026-10-14T17:04:01.123Z",
    level: "WARN",
    subsystem: "config",
    text: "prefs saved",
  });
  const state = EditorState.create({ doc: `${line}\n` });
  const set = buildLogDecorations(state, "America/Vancouver");
  const found = [];
  set.between(0, state.doc.length, (from, to, value) => {
    found.push({ from, to, spec: value.spec });
  });
  expect(state.doc.line(1).text).toBe(line);
  expect(found).toHaveLength(2);
  expect(found[0].spec.class).toBe("pav-log-level-WARN");
  expect(found[0].spec.attributes.title).toBe("2026-10-14T17:04:01.123Z");
  expect(found[1].from).toBe(0);
  expect(found[1].to).toBe(line.length);
  expect(found[1].spec.widget.label).toBe(screenLine({
    ts: "2026-10-14T17:04:01.123Z",
    level: "WARN",
    subsystem: "config",
    text: "prefs saved",
  }, "America/Vancouver"));
  expect(found[1].spec.widget.utc).toBe("2026-10-14T17:04:01.123Z");
});

test("a level list colors a screen line whose time is wider than 12 characters", () => {
  const text = "10:04:01.123 AM  INFO   core      hello\n";
  const state = EditorState.create({ doc: text });
  const set = buildLogDecorations(
    state,
    "America/Vancouver",
    ["2026-10-14T17:04:01.123Z"],
    [],
    null,
    ["INFO"],
  );
  const found = [];
  set.between(0, state.doc.length, (_from, _to, value) => {
    found.push(value.spec);
  });
  expect(found.some((spec) => spec.class === "pav-log-level-INFO" && spec.attributes.title === "2026-10-14T17:04:01.123Z")).toBe(true);
});

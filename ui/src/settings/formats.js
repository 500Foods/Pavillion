/* Settings → Formats (D-8). One block per kind, a locale, and a zone.
   Each block describes the kind, shows the default pattern on the sample,
   and lists default patterns. Custom keeps any other allowed string.
   Apply writes preferences.ui.formats through the shell, and the zone
   through prefs.set. A string that matches the current default is left
   out, so time_format still switches 12 h and 24 h. Reset clears that
   row's saved string. Preview shows now and the fixed sample. */
import { iconClass } from "../common/icons/registry.js";

import { moduleFooter, moduleToolbar, mountBar } from "../common/module-bar.js";
import {
  FORMAT_KINDS,
  RELATIVE_STYLES,
  SAMPLE_DURATION_MS,
  SAMPLE_INSTANT,
  defaultPattern,
  formatInstant,
  localeError,
  patternError,
  timeZoneNames,
  zoneError,
} from "../formats.js";

const CUSTOM = "custom";

/* A time three hours before the sample, so long and short differ. */
const RELATIVE_NOW = SAMPLE_INSTANT + 3 * 60 * 60 * 1000;
/* Same year as the sample, another day, so dateShort shows its own pattern. */
const SHORT_NOW = Date.parse("2026-06-01T19:00:00.000Z");

const FIELDS = [
  {
    kind: "date",
    label: "Date",
    lead: "A calendar day. The message list uses this for a day in another year.",
    presets: ["yyyy-MM-dd", "MMM d, yyyy", "MMMM d, yyyy", "d MMM yyyy", "dd/MM/yyyy", "MM/dd/yyyy"],
  },
  {
    kind: "time",
    label: "Time",
    lead: "A clock time without seconds. The message list uses this for a message from today.",
    presets: ["HH:mm", "HH:mm:ss", "h:mm a", "h:mm:ss a"],
  },
  {
    kind: "timeSeconds",
    label: "Time with seconds",
    lead: "A clock time with seconds. The log and the activity list use this.",
    presets: ["HH:mm:ss.SSS", "HH:mm:ss", "h:mm:ss.SSS a", "h:mm:ss a"],
  },
  {
    kind: "datetime",
    label: "Date and time",
    lead: "A date and a time on one line. Limits and the log session line use this.",
    presets: ["yyyy-MM-dd HH:mm", "yyyy-MM-dd HH:mm:ss", "yyyy-MM-dd h:mm a", "MMM d, yyyy HH:mm", "MMM d, h:mm a"],
  },
  {
    kind: "dateShort",
    label: "Message list",
    lead: "The message list uses this for a day in the same year. Today uses Time, and another year uses Date.",
    presets: ["MMM d", "MMMM d", "d MMM", "MM-dd"],
  },
  {
    kind: "weekday",
    label: "Weekday",
    lead: "The name of the day.",
    presets: ["EEEE", "EEE", "EEEEE"],
  },
  {
    kind: "relative",
    label: "Relative",
    lead: "How long ago, or how soon. long is a full phrase. short and narrow are shorter.",
    presets: RELATIVE_STYLES,
  },
  {
    kind: "duration",
    label: "Duration",
    lead: "A length of time, not a clock time. The sample is 3 hours, 4 minutes, and 5.006 seconds.",
    presets: ["h:mm:ss", "h:mm:ss.SSS", "hh:mm:ss"],
  },
];

function messageOf(err) {
  return err instanceof Error ? err.message : String(err);
}

export function createFormatsSettings(api, root, options = {}) {
  const body = root.querySelector("#formats-body");
  const toolbar = root.querySelector("#formats-toolbar");
  const footer = root.querySelector("#formats-footer");
  if (!body || !toolbar || !footer) {
    return {
      refresh: () => Promise.resolve(),
      apply: () => Promise.resolve(null),
      destroy() {},
    };
  }

  const doc = body.ownerDocument;
  let timeFormat = "24h";
  let ticket = 0;
  const rows = new Map();

  function report(err) {
    if (typeof options.onError === "function") {
      options.onError(messageOf(err));
    }
  }

  function optionTimeFormat() {
    const value = typeof options.timeFormat === "function" ? options.timeFormat() : options.timeFormat;
    return value === "12h" ? "12h" : "24h";
  }

  function addBlock(title, noteText) {
    const block = doc.createElement("div");
    block.className = "formats-block";
    const heading = doc.createElement("span");
    heading.className = "formats-title";
    heading.textContent = title;
    const note = doc.createElement("p");
    note.className = "formats-note";
    note.textContent = noteText;
    block.append(heading, note);
    body.append(block);
    return { block, note };
  }

  function addPlain(title, noteText, control, errorEl) {
    const { block } = addBlock(title, noteText);
    const row = doc.createElement("div");
    row.className = "formats-row is-plain";
    control.classList.add("formats-preset");
    row.append(control, errorEl);
    block.append(row);
  }

  const localeInput = doc.createElement("input");
  localeInput.id = "formats-locale";
  localeInput.type = "text";
  localeInput.spellcheck = false;
  localeInput.autocomplete = "off";
  localeInput.setAttribute("aria-label", "Locale");
  localeInput.placeholder = "System";
  const localeErrorEl = doc.createElement("span");
  localeErrorEl.className = "formats-error";
  localeErrorEl.setAttribute("role", "alert");

  const zoneSelect = doc.createElement("select");
  zoneSelect.id = "formats-zone";
  zoneSelect.setAttribute("aria-label", "Time zone");
  const zoneErrorEl = doc.createElement("span");
  zoneErrorEl.className = "formats-error";
  zoneErrorEl.setAttribute("role", "alert");

  function fillZones(selected) {
    const names = timeZoneNames();
    if (selected && !names.includes(selected)) {
      names.unshift(selected);
    }
    zoneSelect.replaceChildren();
    for (const name of names) {
      const option = doc.createElement("option");
      option.value = name;
      option.textContent = name;
      zoneSelect.append(option);
    }
    if (selected) {
      zoneSelect.value = selected;
    }
  }

  body.replaceChildren();
  addPlain(
    "Locale",
    "Month and weekday names. Leave this empty to use the computer's locale. en-CA keeps English names, and fr-CA uses French names.",
    localeInput,
    localeErrorEl,
  );
  addPlain(
    "Time zone",
    "Every date and time on screen uses this zone. The profile default is America/Vancouver.",
    zoneSelect,
    zoneErrorEl,
  );

  for (const field of FIELDS) {
    const { block, note } = addBlock(field.label, field.lead);
    note.id = `formats-note-${field.kind}`;
    const preset = doc.createElement("select");
    preset.className = "formats-preset";
    preset.setAttribute("aria-label", `${field.label} defaults`);
    preset.setAttribute("aria-describedby", note.id);
    const input = field.kind === "relative" ? null : doc.createElement("input");
    if (input) {
      input.type = "text";
      input.spellcheck = false;
      input.autocomplete = "off";
      input.className = "formats-pattern";
      input.setAttribute("aria-label", field.label);
      input.setAttribute("aria-describedby", note.id);
      input.placeholder = "Pattern";
    } else {
      preset.setAttribute("aria-label", field.label);
    }
    const now = doc.createElement("span");
    now.className = "muted formats-now";
    const sample = doc.createElement("span");
    sample.className = "muted formats-sample";
    const errorEl = doc.createElement("span");
    errorEl.className = "formats-error";
    errorEl.setAttribute("role", "alert");
    const reset = doc.createElement("button");
    reset.type = "button";
    reset.className = "formats-reset";
    reset.textContent = "Reset";
    reset.setAttribute("aria-label", `Reset ${field.label}`);
    const rowEl = doc.createElement("div");
    rowEl.className = field.kind === "relative" ? "formats-row is-choice" : "formats-row";
    const patternCell = input || doc.createElement("span");
    if (!input) {
      patternCell.className = "formats-pattern";
      patternCell.setAttribute("aria-hidden", "true");
    }
    rowEl.append(preset, patternCell, now, sample, reset, errorEl);
    block.append(rowEl);
    const row = {
      kind: field.kind,
      presets: field.presets,
      lead: field.lead,
      note,
      preset,
      input,
      now,
      sample,
      errorEl,
      customizing: false,
    };
    rows.set(field.kind, row);
    preset.addEventListener("change", () => {
      if (input && preset.value === CUSTOM) {
        row.customizing = true;
        input.focus();
        paintOne(field.kind);
        return;
      }
      row.customizing = false;
      if (input) {
        input.value = preset.value;
      } else {
        preset.dataset.value = preset.value;
      }
      paintOne(field.kind);
    });
    if (input) {
      input.addEventListener("input", () => {
        const value = input.value.trim();
        if (field.presets.includes(value)) {
          row.customizing = false;
        }
        paintOne(field.kind);
      });
    }
    reset.addEventListener("click", () => {
      row.customizing = false;
      const fallback = defaultPattern(field.kind, timeFormat);
      if (input) {
        input.value = fallback;
      } else {
        row.preset.dataset.value = fallback;
        row.preset.value = fallback;
      }
      paintOne(field.kind);
    });
  }

  function currentValue(kind) {
    const row = rows.get(kind);
    if (row.input) {
      return row.input.value.trim();
    }
    return row.preset.value.trim();
  }

  function draftPatterns() {
    const patterns = {};
    for (const kind of FORMAT_KINDS) {
      patterns[kind] = currentValue(kind);
    }
    return patterns;
  }

  function previewPattern(kind, pattern) {
    const sample = kind === "duration" ? SAMPLE_DURATION_MS : SAMPLE_INSTANT;
    const now = kind === "relative" ? RELATIVE_NOW : kind === "dateShort" ? SHORT_NOW : SAMPLE_INSTANT;
    return formatInstant(sample, kind, {
      patterns: { [kind]: pattern },
      timeFormat,
      zone: zoneSelect.value,
      locale: localeInput.value.trim(),
      now,
    });
  }

  function optionLabel(kind, pattern, isDefault) {
    const rendered = previewPattern(kind, pattern);
    const mark = isDefault ? " (default)" : "";
    if (rendered === "") {
      return `${pattern}${mark}`;
    }
    return `${rendered} · ${pattern}${mark}`;
  }

  function fillPreset(row) {
    const current = row.input ? row.input.value.trim() : row.preset.dataset.value || "";
    const fallback = defaultPattern(row.kind, timeFormat);
    const known = row.presets.includes(current);
    const selected = row.input
      ? (known && !row.customizing ? current : CUSTOM)
      : (known ? current : fallback);
    row.preset.replaceChildren();
    for (const pattern of row.presets) {
      const option = doc.createElement("option");
      option.value = pattern;
      option.textContent = optionLabel(row.kind, pattern, pattern === fallback);
      row.preset.append(option);
    }
    if (row.input) {
      const custom = doc.createElement("option");
      custom.value = CUSTOM;
      custom.textContent = "Custom";
      row.preset.append(custom);
    }
    row.preset.value = selected;
    if (!row.input) {
      row.preset.dataset.value = row.preset.value;
    }
  }

  function syncPreset(row) {
    if (!row.input) {
      return;
    }
    const value = row.input.value.trim();
    const next = row.presets.includes(value) && !row.customizing ? value : CUSTOM;
    if (row.preset.value !== next) {
      row.preset.value = next;
    }
  }

  function paintNote(row) {
    const pattern = defaultPattern(row.kind, timeFormat);
    const rendered = previewPattern(row.kind, pattern);
    const example = rendered === "" ? "" : ` ${pattern} is ${rendered}.`;
    const custom = row.input ? " Pick a default, or choose Custom and edit the pattern." : "";
    row.note.textContent = `${row.lead}${example}${custom}`;
  }

  function paintOne(kind) {
    const row = rows.get(kind);
    syncPreset(row);
    const value = currentValue(kind);
    const err = patternError(kind, value);
    row.errorEl.textContent = err;
    if (err !== "") {
      row.now.textContent = "";
      row.sample.textContent = "";
      return;
    }
    const patterns = draftPatterns();
    const preview = (instant) => formatInstant(instant, kind, {
      patterns,
      timeFormat,
      zone: zoneSelect.value,
      locale: localeInput.value.trim(),
      now: Date.now(),
    });
    const sampleValue = kind === "duration" ? SAMPLE_DURATION_MS : SAMPLE_INSTANT;
    const nowText = preview(kind === "duration" ? SAMPLE_DURATION_MS : Date.now());
    const sampleText = preview(sampleValue);
    row.now.textContent = nowText === "" ? "" : `Now ${nowText}`;
    row.sample.textContent = sampleText === "" ? "" : `Sample ${sampleText}`;
  }

  function paintAll() {
    localeErrorEl.textContent = localeError(localeInput.value);
    zoneErrorEl.textContent = zoneError(zoneSelect.value);
    for (const kind of FORMAT_KINDS) {
      const row = rows.get(kind);
      fillPreset(row);
      paintNote(row);
      paintOne(kind);
    }
  }

  localeInput.addEventListener("input", paintAll);
  zoneSelect.addEventListener("change", paintAll);

  function readStored(prefs, ui) {
    timeFormat = prefs && prefs.timeFormat === "12h" ? "12h" : optionTimeFormat();
    const formats = ui && ui.formats && typeof ui.formats === "object" ? ui.formats : {};
    const zone = prefs && typeof prefs.timezone === "string" && zoneError(prefs.timezone) === ""
      ? prefs.timezone
      : "America/Vancouver";
    fillZones(zone);
    localeInput.value = typeof formats.locale === "string" ? formats.locale : "";
    for (const kind of FORMAT_KINDS) {
      const row = rows.get(kind);
      const stored = typeof formats[kind] === "string" ? formats[kind] : "";
      const fallback = defaultPattern(kind, timeFormat);
      const value = stored !== "" && patternError(kind, stored) === "" ? stored : fallback;
      row.customizing = false;
      if (row.input) {
        row.input.value = value;
      } else {
        row.preset.dataset.value = value;
      }
    }
    paintAll();
  }

  function refresh() {
    const mine = ticket + 1;
    ticket = mine;
    const uiPromise = typeof options.getUi === "function"
      ? Promise.resolve(options.getUi())
      : api.prefs.ui.get();
    return Promise.all([api.prefs.get(), uiPromise]).then(([prefs, ui]) => {
      if (mine !== ticket) {
        return null;
      }
      readStored(prefs, ui);
      return prefs;
    }).catch((err) => {
      if (mine === ticket) {
        report(err);
      }
      return null;
    });
  }

  function chosen() {
    const patterns = draftPatterns();
    const formats = {};
    for (const kind of FORMAT_KINDS) {
      const err = patternError(kind, patterns[kind]);
      if (err !== "") {
        rows.get(kind).errorEl.textContent = err;
        return null;
      }
      if (patterns[kind] !== defaultPattern(kind, timeFormat)) {
        formats[kind] = patterns[kind];
      }
    }
    const locale = localeInput.value.trim();
    const localeMsg = localeError(locale);
    localeErrorEl.textContent = localeMsg;
    if (localeMsg !== "") {
      return null;
    }
    if (locale !== "") {
      formats.locale = locale;
    }
    const zone = zoneSelect.value;
    const zoneMsg = zoneError(zone);
    zoneErrorEl.textContent = zoneMsg;
    if (zoneMsg !== "") {
      return null;
    }
    return { formats, timezone: zone };
  }

  function apply() {
    const next = chosen();
    if (!next) {
      return Promise.resolve(null);
    }
    if (typeof options.onSave !== "function") {
      return Promise.resolve(next);
    }
    return Promise.resolve(options.onSave(next)).then((saved) => saved === undefined ? next : saved).catch((err) => {
      report(err);
      return null;
    });
  }

  mountBar(toolbar, moduleToolbar({ title: "Formats" }));
  mountBar(footer, moduleFooter({
    label: "Formats actions",
    onRefresh: () => {
      refresh();
    },
    end: [{
      action: "apply",
      icon: iconClass("settings.apply"),
      text: "Apply",
      label: "Save formats",
      onClick: () => {
        apply();
      },
    }],
  }));

  return { refresh, apply, destroy() { ticket += 1; } };
}

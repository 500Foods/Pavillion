const SCOPES = ["global", "workspace", "tree", "list", "composer"];
const CHORD_MS = 1000;
const MODIFIER_KEYS = new Set(["Control", "Shift", "Alt", "Meta"]);

/* Letters are stored lowercase. Shift stays on letters and on named keys, and is omitted for a
   printable mark such as `*` because that character is already the shifted key. */
function normalizeToken(raw) {
  const parts = String(raw).split("+").filter((part) => part !== "");
  const mods = [];
  let key = "";
  for (const part of parts) {
    const lower = part.toLowerCase();
    if (lower === "ctrl" || lower === "control") {
      mods.push("Ctrl");
    } else if (lower === "alt" || lower === "option") {
      mods.push("Alt");
    } else if (lower === "shift") {
      mods.push("Shift");
    } else if (lower === "meta" || lower === "cmd" || lower === "command") {
      mods.push("Meta");
    } else {
      key = part.length === 1 ? part.toLowerCase() : part;
    }
  }
  if (key === "") {
    return "";
  }
  const ordered = ["Ctrl", "Alt", "Shift", "Meta"].filter((name) => mods.includes(name));
  return [...ordered, key].join("+");
}

function normalizeChord(raw) {
  const steps = Array.isArray(raw) ? raw : [raw];
  return steps.map((step) => normalizeToken(step)).filter((step) => step !== "");
}

function tokenFromEvent(event) {
  const key = event.key;
  if (typeof key !== "string" || key === "" || MODIFIER_KEYS.has(key)) {
    return "";
  }
  const printable = key.length === 1;
  const name = printable ? key.toLowerCase() : key;
  const shiftCounts = event.shiftKey === true && (!printable || /[a-z]/.test(name));
  const mods = [];
  if (event.ctrlKey === true) {
    mods.push("Ctrl");
  }
  if (event.altKey === true) {
    mods.push("Alt");
  }
  if (shiftCounts) {
    mods.push("Shift");
  }
  if (event.metaKey === true) {
    mods.push("Meta");
  }
  return [...mods, name].join("+");
}

function sameChord(left, right) {
  return left.length === right.length && left.every((step, index) => step === right[index]);
}

function chordStarts(chord, sequence) {
  return sequence.every((step, index) => chord[index] === step);
}

export function createKeys(root) {
  if (!(root instanceof Element)) {
    throw new Error("keys require an element");
  }
  const bindings = [];
  const view = root.ownerDocument.defaultView;
  let scope = "workspace";
  let pending = null;
  let timer = 0;

  function clearTimer() {
    if (!timer || !view) {
      timer = 0;
      return;
    }
    view.clearTimeout(timer);
    timer = 0;
  }

  function clearPending() {
    pending = null;
    clearTimer();
  }

  function scopes() {
    if (scope === "global") {
      return ["global"];
    }
    if (scope === "workspace") {
      return ["workspace", "global"];
    }
    return [scope, "workspace", "global"];
  }

  function ownerOf(first) {
    for (const name of scopes()) {
      const owns = bindings.some((binding) => binding.scope === name && binding.chords.some((chord) => chord[0] === first));
      if (owns) {
        return name;
      }
    }
    return "";
  }

  function matches(name, sequence) {
    const exact = [];
    let longer = false;
    for (const binding of bindings) {
      if (binding.scope !== name) {
        continue;
      }
      for (const chord of binding.chords) {
        if (!chordStarts(chord, sequence)) {
          continue;
        }
        if (chord.length === sequence.length) {
          exact.push(binding);
        } else if (chord.length > sequence.length) {
          longer = true;
        }
      }
    }
    return { exact, longer };
  }

  function fireExact(name, sequence) {
    for (const binding of bindings) {
      if (binding.scope !== name) {
        continue;
      }
      if (binding.chords.some((chord) => sameChord(chord, sequence))) {
        binding.run();
        return true;
      }
    }
    return false;
  }

  function armTimer() {
    clearTimer();
    if (!view) {
      return;
    }
    const scheduled = pending;
    timer = view.setTimeout(() => {
      timer = 0;
      if (pending !== scheduled || !scheduled) {
        return;
      }
      pending = null;
      fireExact(scheduled.scope, scheduled.sequence);
    }, CHORD_MS);
  }

  function pageFocus() {
    const doc = root.ownerDocument;
    const el = doc.activeElement;
    return !(el instanceof Element) || el === doc.body || el === doc.documentElement;
  }

  /* A click on a calendar date, or any other control that cannot take focus, leaves focus on the page.
     The shortcut still applies. It does not apply while another document control, outside this shell, has focus. */
  function applies() {
    if (!root.isConnected) {
      return false;
    }
    const el = root.ownerDocument.activeElement;
    if (el instanceof Element && root.contains(el)) {
      return true;
    }
    return pageFocus();
  }

  function blocked() {
    if (root.querySelector("dialog[open], .pav-menu")) {
      return true;
    }
    const el = root.ownerDocument.activeElement;
    if (!(el instanceof Element) || !root.contains(el)) {
      return false;
    }
    if (el.closest("input, textarea, select")) {
      return true;
    }
    const editable = el.closest("[contenteditable]");
    return editable instanceof Element && editable.getAttribute("contenteditable") !== "false";
  }

  function handleToken(token, retried) {
    const sequence = pending ? pending.sequence.concat([token]) : [token];
    const name = pending ? pending.scope : ownerOf(token);
    if (!name) {
      clearPending();
      return false;
    }
    const found = matches(name, sequence);
    if (!found.longer && found.exact.length === 0) {
      const stale = pending;
      clearPending();
      if (stale && !retried) {
        const fired = fireExact(stale.scope, stale.sequence);
        return handleToken(token, true) || fired;
      }
      return false;
    }
    if (found.longer) {
      pending = { sequence, scope: name };
      armTimer();
      return true;
    }
    clearPending();
    found.exact[0].run();
    return true;
  }

  function onKeyDown(event) {
    if (event.defaultPrevented || event.isComposing) {
      return;
    }
    if (!applies()) {
      clearPending();
      return;
    }
    if (blocked()) {
      clearPending();
      return;
    }
    const token = tokenFromEvent(event);
    if (token === "") {
      return;
    }
    if (handleToken(token, false)) {
      event.preventDefault();
    }
  }

  function register(spec) {
    const id = spec && typeof spec.id === "string" ? spec.id : "";
    const scopeName = spec && spec.scope;
    const run = spec && spec.run;
    if (id === "" || !SCOPES.includes(scopeName) || typeof run !== "function") {
      return () => {};
    }
    const chords = (Array.isArray(spec.chords) ? spec.chords : [])
      .map((chord) => normalizeChord(chord))
      .filter((chord) => chord.length > 0);
    if (chords.length === 0) {
      return () => {};
    }
    const entry = { id, scope: scopeName, chords, run };
    const index = bindings.findIndex((binding) => binding.id === id);
    if (index >= 0) {
      bindings[index] = entry;
    } else {
      bindings.push(entry);
    }
    return () => {
      const at = bindings.indexOf(entry);
      if (at >= 0) {
        bindings.splice(at, 1);
      }
    };
  }

  function list() {
    return bindings.map((binding) => ({
      id: binding.id,
      scope: binding.scope,
      chords: binding.chords.map((chord) => chord.slice()),
    }));
  }

  function setScope(next) {
    if (!SCOPES.includes(next) || next === scope) {
      return;
    }
    scope = next;
    clearPending();
  }

  const doc = root.ownerDocument;
  doc.addEventListener("keydown", onKeyDown);

  return {
    register,
    list,
    setScope,
    getScope() {
      return scope;
    },
    destroy() {
      clearPending();
      bindings.splice(0, bindings.length);
      doc.removeEventListener("keydown", onKeyDown);
    },
  };
}

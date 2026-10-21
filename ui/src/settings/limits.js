/* Settings → Limits (L Lock, J Lock): preferences.cache bodiesMb and
   blobsMb. Refresh reloads them. Apply writes both through prefs.set.
   The page clamps each value to 1..1048576. The log size cap stays on Logs. */

import { moduleFooter, moduleToolbar, mountBar } from "../common/module-bar.js";

const MIN_MB = 1;
const MAX_MB = 1048576;
const DEFAULT_BODIES = 2048;
const DEFAULT_BLOBS = 4096;

function messageOf(err) {
  return err instanceof Error ? err.message : String(err);
}

function storedMb(cache, key, fallback) {
  const value = cache && cache[key];
  return Number.isInteger(value) && value >= MIN_MB && value <= MAX_MB ? value : fallback;
}

/* A blank or non-integer field goes back to the saved value. A number
   outside 1..1048576 snaps to that end. The field shows what will be saved. */
function readMb(input, saved) {
  const raw = input.value.trim();
  if (!/^[0-9]+$/.test(raw)) {
    input.value = String(saved);
    return saved;
  }
  let n = Number(raw);
  if (n < MIN_MB) {
    n = MIN_MB;
  }
  if (n > MAX_MB) {
    n = MAX_MB;
  }
  input.value = String(n);
  return n;
}

export function createLimitsSettings(api, root, options = {}) {
  const bodies = root.querySelector("#limits-bodies");
  const blobs = root.querySelector("#limits-blobs");
  if (!bodies || !blobs) {
    return {
      refresh: () => Promise.resolve(),
      apply: () => Promise.resolve(),
      destroy() {},
    };
  }

  let savedBodies = DEFAULT_BODIES;
  let savedBlobs = DEFAULT_BLOBS;
  let ticket = 0;

  function report(err) {
    if (typeof options.onError === "function") {
      options.onError(messageOf(err));
    }
  }

  function paint() {
    bodies.value = String(savedBodies);
    blobs.value = String(savedBlobs);
  }

  function refresh() {
    const mine = ticket + 1;
    ticket = mine;
    return api.prefs.get().then((prefs) => {
      if (mine !== ticket) {
        return null;
      }
      const cache = prefs && prefs.cache;
      savedBodies = storedMb(cache, "bodiesMb", DEFAULT_BODIES);
      savedBlobs = storedMb(cache, "blobsMb", DEFAULT_BLOBS);
      paint();
      return prefs;
    }).catch((err) => {
      if (mine === ticket) {
        report(err);
      }
      return null;
    });
  }

  function apply() {
    const mine = ticket + 1;
    ticket = mine;
    const bodiesMb = readMb(bodies, savedBodies);
    const blobsMb = readMb(blobs, savedBlobs);
    return api.prefs.set({ cache: { bodiesMb, blobsMb } }).then((prefs) => {
      if (mine !== ticket) {
        return null;
      }
      const cache = prefs && prefs.cache;
      savedBodies = storedMb(cache, "bodiesMb", bodiesMb);
      savedBlobs = storedMb(cache, "blobsMb", blobsMb);
      paint();
      return prefs;
    }).catch((err) => {
      if (mine === ticket) {
        report(err);
      }
      return null;
    });
  }

  function onBlur(event) {
    const input = event.currentTarget;
    readMb(input, input === bodies ? savedBodies : savedBlobs);
  }

  function onKey(event) {
    if (event.key === "Enter") {
      event.preventDefault();
      apply();
    }
  }

  mountBar(root.querySelector("#limits-toolbar"), moduleToolbar({
    title: "Limits",
  }));
  mountBar(root.querySelector("#limits-footer"), moduleFooter({
    label: "Limits actions",
    onRefresh: () => {
      refresh();
    },
    end: [{
      action: "apply",
      icon: "fa-check",
      text: "Apply",
      label: "Save cache limits",
      onClick: () => {
        apply();
      },
    }],
  }));

  bodies.addEventListener("blur", onBlur);
  blobs.addEventListener("blur", onBlur);
  bodies.addEventListener("keydown", onKey);
  blobs.addEventListener("keydown", onKey);

  return {
    refresh,
    apply,
    destroy() {
      ticket += 1;
      bodies.removeEventListener("blur", onBlur);
      blobs.removeEventListener("blur", onBlur);
      bodies.removeEventListener("keydown", onKey);
      blobs.removeEventListener("keydown", onKey);
    },
  };
}

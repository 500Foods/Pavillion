/* Settings → Logs (H-4 Lock, Retention): the size cap of log.db. Refresh
   shows log.stats, the slider previews a trim with log.trim_preview, Apply
   saves the cap and trims (log.trim {mb}), Trim Now trims to the saved cap
   (log.trim {}). Each shows the stats that came back. */

import { formatInstant } from "../formats.js";

const MB = 1024 * 1024;
const PREVIEW_DELAY_MS = 150;

export function formatMb(bytes) {
  const mb = Number.isFinite(bytes) ? bytes / MB : 0;
  return `${mb < 10 ? mb.toFixed(2) : mb.toFixed(1)} MB`;
}

export function formatOldest(ms, timeZone) {
  if (!Number.isFinite(ms)) {
    return "none";
  }
  const zone = typeof timeZone === "string" && timeZone !== "" ? timeZone : undefined;
  const text = formatInstant(ms, "datetime", zone ? { zone } : {});
  return text === "" ? "none" : text;
}

export function statsText(stats, timeZone) {
  const records = Number.isFinite(stats && stats.records) ? stats.records : 0;
  const noun = records === 1 ? "record" : "records";
  return `${formatMb(stats && stats.bytes)}, ${records.toLocaleString("en-US")} ${noun}, oldest ${formatOldest(stats && stats.oldestTs, timeZone)}`;
}

function messageOf(err) {
  return err instanceof Error ? err.message : String(err);
}

export function createLogSettings(api, root, options = {}) {
  const slider = root.querySelector("#log-cap");
  const value = root.querySelector("#log-cap-value");
  const statsLine = root.querySelector("#log-stats");
  const previewLine = root.querySelector("#log-preview");
  const refreshButton = root.querySelector("#log-refresh");
  const applyButton = root.querySelector("#log-apply");
  const trimButton = root.querySelector("#log-trim-now");
  let savedCap = 10;
  let previewTimer = 0;
  let previewTicket = 0;
  let lastStats = null;
  let lastPreview = null;

  if (!slider || !statsLine) {
    return { refresh: () => Promise.resolve(), destroy() {} };
  }

  function zone() {
    return typeof options.timeZone === "function" ? options.timeZone() : options.timeZone;
  }

  function report(err) {
    if (typeof options.onError === "function") {
      options.onError(messageOf(err));
    }
  }

  function paintCap(mb) {
    if (value) {
      value.textContent = `${mb} MB`;
    }
  }

  function show(stats) {
    lastStats = stats;
    lastPreview = null;
    if (stats && Number.isInteger(stats.capMb)) {
      savedCap = stats.capMb;
      slider.value = String(savedCap);
      paintCap(savedCap);
    }
    statsLine.textContent = statsText(stats, zone());
    if (previewLine) {
      previewLine.textContent = "";
    }
    return stats;
  }

  function repaintTimes() {
    if (lastStats) {
      statsLine.textContent = statsText(lastStats, zone());
    }
    if (lastPreview && previewLine && previewLine.textContent !== "") {
      previewLine.textContent = `At ${sliderMb()} MB: about ${statsText(lastPreview, zone())}`;
    }
  }

  function sliderMb() {
    const mb = Number.parseInt(slider.value, 10);
    return Number.isInteger(mb) && mb >= 1 && mb <= 100 ? mb : savedCap;
  }

  function preview() {
    const mb = sliderMb();
    const ticket = previewTicket + 1;
    previewTicket = ticket;
    if (mb === savedCap) {
      if (previewLine) {
        previewLine.textContent = "";
      }
      return Promise.resolve();
    }
    return api.log.trimPreview(mb).then((est) => {
      if (ticket === previewTicket && previewLine) {
        lastPreview = est;
        previewLine.textContent = `At ${mb} MB: about ${statsText(est, zone())}`;
      }
    }).catch(report);
  }

  function refresh() {
    return api.log.stats().then(show).catch(report);
  }

  function apply() {
    previewTicket += 1;
    return api.log.trim(sliderMb()).then(show).catch(report);
  }

  function trimNow() {
    previewTicket += 1;
    return api.log.trim().then(show).catch(report);
  }

  function onInput() {
    paintCap(sliderMb());
    if (previewTimer) {
      window.clearTimeout(previewTimer);
    }
    previewTimer = window.setTimeout(() => {
      previewTimer = 0;
      preview();
    }, PREVIEW_DELAY_MS);
  }

  slider.addEventListener("input", onInput);
  refreshButton?.addEventListener("click", refresh);
  applyButton?.addEventListener("click", apply);
  trimButton?.addEventListener("click", trimNow);

  return {
    refresh,
    preview,
    repaintTimes,
    apply,
    trimNow,
    destroy() {
      if (previewTimer) {
        window.clearTimeout(previewTimer);
      }
      slider.removeEventListener("input", onInput);
      refreshButton?.removeEventListener("click", refresh);
      applyButton?.removeEventListener("click", apply);
      trimButton?.removeEventListener("click", trimNow);
    },
  };
}

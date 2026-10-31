/* One line at the right of the tab bar. The shell owns the node and the timer.
   Other modules only hand it a string. */

export const STATUS_HOLD_MS = 2500;
export const STATUS_FADE_MS = 160;

let poster = () => {};

export function setStatusPoster(fn) {
  poster = typeof fn === "function" ? fn : () => {};
}

export function postStatus(text) {
  if (typeof text !== "string" || text === "") {
    return;
  }
  poster(text);
}

/* "Filters: 90%". The name is the Zooms row label. */
export function zoomStatus(name, scale) {
  const percent = Math.round(Number(scale) * 100);
  if (typeof name !== "string" || name === "" || !Number.isFinite(percent)) {
    return;
  }
  postStatus(`${name}: ${percent}%`);
}

/* "3m4s". At most two units. A zero unit is skipped. Under a second is "1s". */
export function shortDuration(ms) {
  const value = Number(ms);
  if (!Number.isFinite(value) || value <= 0) {
    return "0s";
  }
  let seconds = Math.round(value / 1000);
  if (seconds < 1) {
    seconds = 1;
  }
  const parts = [
    ["d", Math.floor(seconds / 86400)],
    ["h", Math.floor(seconds / 3600) % 24],
    ["m", Math.floor(seconds / 60) % 60],
    ["s", seconds % 60],
  ];
  const out = [];
  for (const [unit, count] of parts) {
    if (count === 0) {
      continue;
    }
    out.push(`${count}${unit}`);
    if (out.length === 2) {
      break;
    }
  }
  return out.join("");
}

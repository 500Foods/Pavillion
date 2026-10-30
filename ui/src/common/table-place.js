/* Saved list position. A row id is a string. A scroll is holder pixels.
   Mail keeps one place for the folder that was open: the mailbox id, the
   message id, and the scroll. Garbage is dropped on read. */

const SELECTED_MAX = 512;
const SCROLL_MAX = 1000000;

export function cleanSelected(value) {
  if (typeof value !== "string") {
    return "";
  }
  const text = value.trim();
  if (text === "" || text.length > SELECTED_MAX) {
    return "";
  }
  for (let i = 0; i < text.length; i += 1) {
    const code = text.charCodeAt(i);
    if (code < 32 || code === 127) {
      return "";
    }
  }
  return text;
}

function cleanOffset(value) {
  if (typeof value !== "number" || !Number.isFinite(value) || value < 0 || value > SCROLL_MAX) {
    return null;
  }
  return Math.round(value);
}

/* Both offsets missing or unusable is null. One good offset keeps the other at 0. */
export function cleanScroll(value) {
  if (!value || typeof value !== "object") {
    return null;
  }
  const top = cleanOffset(value.top);
  const left = cleanOffset(value.left);
  if (top === null && left === null) {
    return null;
  }
  return { top: top === null ? 0 : top, left: left === null ? 0 : left };
}

/* Mail. top and left sit on the place itself. A missing mailbox drops the place. */
export function cleanPlace(value) {
  if (!value || typeof value !== "object") {
    return null;
  }
  const mailbox = cleanSelected(value.mailbox);
  if (mailbox === "") {
    return null;
  }
  const scroll = cleanScroll(value) || { top: 0, left: 0 };
  return {
    mailbox,
    selected: cleanSelected(value.selected),
    top: scroll.top,
    left: scroll.left,
  };
}

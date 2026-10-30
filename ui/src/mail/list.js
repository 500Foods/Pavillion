/* Mail list (N-3). One message.list reply for the selected folder.
   Columns are text, or an element built from a class name. The time column
   calls formats.js. j and k move the selection. Enter keeps it here.
   A mail.messages_changed for that account loads the reply again and
   keeps the selected id when that row is still there. Rows past the cap
   are not fetched. */

import { clampWidth, growWeights } from "../common/column-widths.js";
import { cleanPlace } from "../common/table-place.js";
import { iconClass } from "../common/icons/registry.js";
import { createTable } from "../common/table.js";
import { formatInstant } from "../formats.js";

const SWATCH = /^#(?:[0-9a-fA-F]{3}|[0-9a-fA-F]{6})$/;
/* Fixed columns remember a user width. From, Subject, and the preview share
   the spare width. A drag of one of them saves all three live widths, and
   the next open uses those as widthGrow weights. The address column is
   stored as "from" (To in Sent and Drafts uses the same slot). */
const FIXED_FIELDS = ["receivedAt", "flags", "hasAttachment"];
const GROW_FIELDS = ["from", "subject", "preview"];
const GROW_FALLBACK = { from: 2, subject: 3, preview: 2 };

/* Groups of three digits. Locale-independent, so 10000 is always 10,000. */
function groupDigits(n) {
  return String(n).replace(/\B(?=(\d{3})+(?!\d))/g, ",");
}

/* Plain number when the folder fits. Otherwise how many of total are listed. */
export function formatListCount(shown, total) {
  if (!Number.isInteger(shown) || !Number.isInteger(total) || shown < 0 || total < 0) {
    return "";
  }
  if (shown === total) {
    return groupDigits(shown);
  }
  return `${groupDigits(shown)} of ${groupDigits(total)}`;
}

function textCell(cell, doc) {
  const span = doc.createElement("span");
  const value = cell.getValue();
  span.textContent = typeof value === "string" ? value : "";
  return span;
}

function rowData(cell) {
  if (!cell || typeof cell.getRow !== "function") {
    return {};
  }
  const row = cell.getRow();
  if (!row || typeof row.getData !== "function") {
    return {};
  }
  const data = row.getData();
  return data && typeof data === "object" ? data : {};
}

const LIST_ICONS = {
  "fa-flag": iconClass("mail.list.flag"),
  "fa-reply": iconClass("mail.list.reply"),
  "fa-share": iconClass("mail.list.share"),
  "fa-paperclip": iconClass("mail.list.file"),
};

function icon(doc, name, mark) {
  const node = doc.createElement("i");
  const base = LIST_ICONS[name] || `fa-sharp-duotone fa-solid ${name}`;
  node.className = mark ? `${base} ${mark}` : base;
  node.setAttribute("aria-hidden", "true");
  node.draggable = false;
  node.style.pointerEvents = "none";
  node.style.setProperty("-webkit-user-drag", "none");
  return node;
}

export function createMailList(api, pane, options = {}) {
  const doc = pane.ownerDocument || globalThis.document;
  const count = doc.createElement("p");
  const host = doc.createElement("div");
  const keys = options.keys || null;
  const mailboxOf = typeof options.mailboxOf === "function" ? options.mailboxOf : () => ({});
  count.className = "mail-list-count";
  host.className = "mail-list-table";
  pane.replaceChildren(count, host);
  count.textContent = "Select a folder.";

  let table = null;
  let listScale = { width: 1, font: 1 };
  let tableAddress = "";
  let generation = 0;
  let mailboxId = "";
  let accountId = "";
  let selected = "";
  let scrollMailbox = "";
  let addressField = "from";
  let addressTitle = "From";
  let folderSwatch = "";
  let stopEvents = null;
  const stops = [];

  function readWidths(fields) {
    const raw = typeof options.tableWidths === "function" ? options.tableWidths("mail") : {};
    const out = {};
    if (raw && typeof raw === "object") {
      for (const field of fields) {
        const value = clampWidth(raw[field]);
        if (value !== null) {
          out[field] = value;
        }
      }
    }
    return out;
  }

  function savedWidths() {
    return readWidths(FIXED_FIELDS);
  }

  function savedGrow() {
    return readWidths(GROW_FIELDS);
  }

  function applyOrder(cols) {
    const raw = typeof options.tableOrder === "function" ? options.tableOrder("mail") : [];
    if (!Array.isArray(raw) || raw.length === 0) {
      return cols;
    }
    const rank = new Map();
    raw.forEach((field, index) => {
      const name = field === "from" || field === "to" ? addressField : field;
      if (!rank.has(name)) {
        rank.set(name, index);
      }
    });
    return [...cols].sort((a, b) => (rank.has(a.field) ? rank.get(a.field) : 1000) - (rank.has(b.field) ? rank.get(b.field) : 1000));
  }

  function columns() {
    const saved = savedWidths();
    const share = growWeights(savedGrow(), GROW_FIELDS, GROW_FALLBACK);
    const fixed = (field, width, minWidth, extra) => ({
      headerSort: false,
      field,
      width: saved[field] || width,
      minWidth,
      ...(saved[field] ? { lockWidth: true } : {}),
      ...extra,
    });
    return applyOrder([
      {
        title: "",
        field: "swatch",
        headerSort: false,
        width: 10,
        minWidth: 10,
        resizable: false,
        hozAlign: "center",
        cssClass: "mail-col-swatch",
        formatter: () => {
          const bar = doc.createElement("span");
          bar.className = "mail-swatch";
          if (SWATCH.test(folderSwatch)) {
            bar.style.background = folderSwatch;
          }
          return bar;
        },
      },
      {
        title: addressTitle,
        field: addressField,
        headerSort: false,
        widthGrow: share.from,
        minWidth: 96,
        formatter: (cell) => textCell(cell, doc),
      },
      {
        title: "Subject",
        field: "subject",
        headerSort: false,
        widthGrow: share.subject,
        minWidth: 120,
        formatter: (cell) => textCell(cell, doc),
      },
      {
        title: "Preview",
        field: "preview",
        headerSort: false,
        widthGrow: share.preview,
        minWidth: 80,
        formatter: (cell) => textCell(cell, doc),
      },
      fixed("receivedAt", 108, 72, {
        title: "Time",
        formatter: (cell) => {
          const span = doc.createElement("span");
          span.textContent = formatInstant(cell.getValue(), "dateShort");
          return span;
        },
      }),
      fixed("flags", 84, 72, {
        title: "Flags",
        hozAlign: "center",
        formatter: (cell) => {
          const data = rowData(cell);
          const wrap = doc.createElement("span");
          wrap.className = "mail-flags";
          if (data.seen === false) {
            const dot = doc.createElement("span");
            dot.className = "mail-unread";
            wrap.append(dot);
          }
          if (data.flagged === true) {
            wrap.append(icon(doc, "fa-flag", "mail-flag"));
          }
          if (data.answered === true) {
            wrap.append(icon(doc, "fa-reply", "mail-answered"));
          }
          if (data.forwarded === true) {
            wrap.append(icon(doc, "fa-share", "mail-forwarded"));
          }
          return wrap;
        },
      }),
      fixed("hasAttachment", 36, 36, {
        title: "",
        hozAlign: "center",
        formatter: (cell) => {
          if (cell.getValue() !== true) {
            return textCell({ getValue: () => "" }, doc);
          }
          return icon(doc, "fa-paperclip", "mail-file");
        },
      }),
    ]);
  }

  function readPlace() {
    if (typeof options.tablePlace !== "function") {
      return null;
    }
    return cleanPlace(options.tablePlace("mail"));
  }

  function savePlace(patch) {
    if (mailboxId === "" || typeof options.saveTablePlace !== "function") {
      return;
    }
    const prev = readPlace();
    const base = prev && prev.mailbox === mailboxId
      ? { mailbox: mailboxId, selected: prev.selected, top: prev.top, left: prev.left }
      : { mailbox: mailboxId, selected: "", top: 0, left: 0 };
    options.saveTablePlace("mail", { ...base, ...patch, mailbox: mailboxId });
  }

  function pinnedScroll(pos) {
    return Boolean(pos && (pos.top > 0 || pos.left > 0));
  }

  function applyMailbox(id) {
    const meta = mailboxOf(id) || {};
    const role = typeof meta.role === "string" ? meta.role : "";
    addressField = role === "sent" || role === "drafts" ? "to" : "from";
    addressTitle = addressField === "to" ? "To" : "From";
    folderSwatch = typeof meta.swatch === "string" ? meta.swatch : "";
  }

  function show(rows) {
    const switching = scrollMailbox !== mailboxId;
    const place = readPlace();
    const placeMatches = Boolean(place && place.mailbox === mailboxId);
    if (switching && placeMatches && place.selected !== "" && rows.some((row) => row && row.id === place.selected)) {
      selected = place.selected;
    }
    const scroll = placeMatches ? { top: place.top, left: place.left } : null;
    if (table && tableAddress !== addressField) {
      table.destroy();
      table = null;
    }
    tableAddress = addressField;
    if (!table) {
      table = createTable(host, {
        columns: columns(),
        data: rows,
        rowId: "id",
        select: true,
        scroll: placeMatches ? scroll : null,
        onScroll(pos) {
          savePlace({ selected, top: pos.top, left: pos.left });
        },
        rowClass(data) {
          return data && data.seen === false ? "pav-row-unread" : "";
        },
        onRowClick(data) {
          host.focus();
          selected = data && typeof data.id === "string" ? data.id : "";
          savePlace({ selected });
          if (table && selected !== "") {
            table.select(selected);
          }
        },
        onColumnResized(field, width) {
          if (typeof options.saveTableWidths !== "function") {
            return;
          }
          const growKey = field === addressField ? "from" : field;
          if (GROW_FIELDS.includes(growKey)) {
            if (!table || typeof table.widths !== "function") {
              return;
            }
            const live = table.widths();
            const address = clampWidth(live[addressField]);
            const subject = clampWidth(live.subject);
            const preview = clampWidth(live.preview);
            if (address === null || subject === null || preview === null) {
              return;
            }
            options.saveTableWidths("mail", {
              ...savedWidths(),
              from: address,
              subject,
              preview,
            });
            return;
          }
          if (!FIXED_FIELDS.includes(field)) {
            return;
          }
          const kept = clampWidth(width);
          if (kept === null) {
            return;
          }
          options.saveTableWidths("mail", { ...savedGrow(), ...savedWidths(), [field]: kept });
        },
        onColumnMoved(order) {
          if (typeof options.saveTableOrder !== "function") {
            return;
          }
          options.saveTableOrder("mail", order.map((field) => (field === addressField ? "from" : field)));
        },
      });
      if (typeof table.setScale === "function") {
        table.setScale(listScale.width, listScale.font);
      }
      scrollMailbox = mailboxId;
      selectShown(rows, switching, scroll);
      return Promise.resolve();
    }
    const stamp = mailboxId;
    scrollMailbox = mailboxId;
    const loading = table.setData(rows);
    return Promise.resolve(loading).then(() => {
      if (!table || mailboxId !== stamp) {
        return;
      }
      if (switching && typeof table.setScroll === "function") {
        table.setScroll(placeMatches && scroll ? scroll : { top: 0, left: 0 });
      }
      selectShown(rows, switching, scroll);
    });
  }

  function selectShown(rows, switching, scroll) {
    if (!table) {
      return;
    }
    if (selected !== "" && rows.some((row) => row && row.id === selected)) {
      table.select(selected, { reveal: switching ? !pinnedScroll(scroll) : false });
    } else {
      const had = selected !== "";
      selected = "";
      table.select(null, { reveal: false });
      if (had) {
        savePlace({ selected: "" });
      }
    }
  }

  function ids() {
    if (!table) {
      return [];
    }
    return table.ids().filter((id) => typeof id === "string" && id !== "");
  }

  function moveTo(index) {
    const rows = ids();
    if (rows.length === 0) {
      return;
    }
    const next = Math.max(0, Math.min(rows.length - 1, index));
    if (rows[next] === selected) {
      return;
    }
    selected = rows[next];
    table.select(selected);
    savePlace({ selected });
    host.focus();
  }

  function move(delta) {
    const rows = ids();
    if (rows.length === 0) {
      return;
    }
    const index = rows.indexOf(selected);
    if (index < 0) {
      moveTo(delta < 0 ? rows.length - 1 : 0);
      return;
    }
    moveTo(index + delta);
  }

  /* N-4 opens the message. Enter stays in the list. */
  function choose() {
    const rows = ids();
    if (selected === "" && rows.length > 0) {
      selected = rows[0];
      table.select(selected);
      savePlace({ selected });
    }
    host.focus();
  }

  function open(id) {
    const mine = generation + 1;
    const nextId = typeof id === "string" ? id : "";
    generation = mine;
    if (nextId !== mailboxId) {
      selected = "";
    }
    mailboxId = nextId;
    applyMailbox(mailboxId);
    if (mailboxId === "") {
      accountId = "";
      selected = "";
      scrollMailbox = "";
      count.textContent = "Select a folder.";
      if (table) {
        table.select(null);
        table.setData([]);
      }
      return Promise.resolve();
    }
    return Promise.resolve()
      .then(() => api.message.list({ view: { kind: "mailbox", id: mailboxId } }))
      .then((data) => {
        if (mine !== generation) {
          return;
        }
        const messages = data && Array.isArray(data.messages) ? data.messages : [];
        const total = data && Number.isInteger(data.total) && data.total >= 0 ? data.total : messages.length;
        accountId = data && typeof data.accountId === "string" ? data.accountId : "";
        count.textContent = formatListCount(messages.length, total);
        return show(messages);
      })
      .catch(() => {
        if (mine !== generation) {
          return;
        }
        accountId = "";
        selected = "";
        count.textContent = "";
        if (table) {
          table.select(null);
          table.setData([]);
        }
      });
  }

  if (typeof api.on === "function") {
    stopEvents = api.on("mail.messages_changed", (payload) => {
      if (mailboxId === "" || accountId === "" || !payload || payload.accountId !== accountId) {
        return;
      }
      void open(mailboxId);
    });
  }

  if (keys && typeof keys.setScope === "function") {
    const onFocus = () => keys.setScope("list");
    const onBlur = (event) => {
      const next = event.relatedTarget;
      if (!(next instanceof Node) || !pane.contains(next)) {
        keys.setScope("workspace");
      }
    };
    host.addEventListener("focusin", onFocus);
    host.addEventListener("focusout", onBlur);
    stops.push(() => {
      host.removeEventListener("focusin", onFocus);
      host.removeEventListener("focusout", onBlur);
    });
  }
  if (keys && typeof keys.register === "function") {
    const page = () => (table && typeof table.pageCount === "function" ? table.pageCount() : 1);
    stops.push(keys.register({ id: "list.next", scope: "list", chords: [["j"]], run: () => move(1) }));
    stops.push(keys.register({ id: "list.prev", scope: "list", chords: [["k"]], run: () => move(-1) }));
    stops.push(keys.register({ id: "list.pageNext", scope: "list", chords: [["PageDown"]], run: () => move(page()) }));
    stops.push(keys.register({ id: "list.pagePrev", scope: "list", chords: [["PageUp"]], run: () => move(-page()) }));
    stops.push(keys.register({ id: "list.home", scope: "list", chords: [["Home"]], run: () => moveTo(0) }));
    stops.push(keys.register({ id: "list.end", scope: "list", chords: [["End"]], run: () => moveTo(1e6) }));
    stops.push(keys.register({ id: "list.open", scope: "list", chords: [["Enter"]], run: () => choose() }));
  }

  return {
    open,
    setZoom(widthScale, fontScale) {
      listScale = { width: widthScale, font: fontScale };
      if (table && typeof table.setScale === "function") {
        table.setScale(widthScale, fontScale);
      }
    },
    stop() {
      generation += 1;
      mailboxId = "";
      accountId = "";
      selected = "";
      if (typeof stopEvents === "function") {
        stopEvents();
        stopEvents = null;
      }
      for (const stop of stops) {
        if (typeof stop === "function") {
          stop();
        }
      }
      stops.length = 0;
      if (table) {
        table.destroy();
        table = null;
      }
    },
  };
}

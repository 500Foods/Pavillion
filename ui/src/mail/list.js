/* Mail list (N-3). One message.list reply for the selected folder.
   Columns are text, or an element built from a class name. The time column
   calls formats.js. j and k move the selection. Enter keeps it here.
   A mail.messages_changed for that account loads the reply again and
   keeps the selected id when that row is still there. Rows past the cap
   are not fetched. */

import { iconClass } from "../common/icons/registry.js";
import { createTable } from "../common/table.js";
import { formatInstant } from "../formats.js";

const SWATCH = /^#(?:[0-9a-fA-F]{3}|[0-9a-fA-F]{6})$/;

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
  let tableAddress = "";
  let generation = 0;
  let mailboxId = "";
  let accountId = "";
  let selected = "";
  let addressField = "from";
  let addressTitle = "From";
  let folderSwatch = "";
  let stopEvents = null;
  const stops = [];

  function columns() {
    return [
      {
        title: "",
        field: "swatch",
        headerSort: false,
        width: 10,
        minWidth: 10,
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
        widthGrow: 2,
        formatter: (cell) => textCell(cell, doc),
      },
      {
        title: "Subject",
        field: "subject",
        headerSort: false,
        widthGrow: 3,
        formatter: (cell) => textCell(cell, doc),
      },
      {
        title: "Preview",
        field: "preview",
        headerSort: false,
        widthGrow: 2,
        formatter: (cell) => textCell(cell, doc),
      },
      {
        title: "Time",
        field: "receivedAt",
        headerSort: false,
        width: 108,
        minWidth: 72,
        formatter: (cell) => {
          const span = doc.createElement("span");
          span.textContent = formatInstant(cell.getValue(), "dateShort");
          return span;
        },
      },
      {
        title: "Flags",
        field: "flags",
        headerSort: false,
        width: 84,
        minWidth: 72,
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
      },
      {
        title: "",
        field: "hasAttachment",
        headerSort: false,
        width: 36,
        minWidth: 28,
        hozAlign: "center",
        formatter: (cell) => {
          if (cell.getValue() !== true) {
            return textCell({ getValue: () => "" }, doc);
          }
          return icon(doc, "fa-paperclip", "mail-file");
        },
      },
    ];
  }

  function applyMailbox(id) {
    const meta = mailboxOf(id) || {};
    const role = typeof meta.role === "string" ? meta.role : "";
    addressField = role === "sent" || role === "drafts" ? "to" : "from";
    addressTitle = addressField === "to" ? "To" : "From";
    folderSwatch = typeof meta.swatch === "string" ? meta.swatch : "";
  }

  function show(rows) {
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
        rowClass(data) {
          return data && data.seen === false ? "pav-row-unread" : "";
        },
        onRowClick(data) {
          host.focus();
          selected = data && typeof data.id === "string" ? data.id : "";
          if (table && selected !== "") {
            table.select(selected);
          }
        },
      });
    } else {
      table.setData(rows);
    }
    if (selected !== "" && rows.some((row) => row && row.id === selected)) {
      table.select(selected);
    } else {
      selected = "";
      table.select(null);
    }
  }

  function ids() {
    if (!table) {
      return [];
    }
    return table.ids().filter((id) => typeof id === "string" && id !== "");
  }

  function move(delta) {
    const rows = ids();
    if (rows.length === 0) {
      return;
    }
    let index = rows.indexOf(selected);
    if (index < 0) {
      index = delta < 0 ? rows.length : -1;
    }
    const next = index + delta;
    if (next < 0 || next >= rows.length) {
      return;
    }
    selected = rows[next];
    table.select(selected);
    host.focus();
  }

  /* N-4 opens the message. Enter stays in the list. */
  function choose() {
    const rows = ids();
    if (selected === "" && rows.length > 0) {
      selected = rows[0];
      table.select(selected);
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
        show(messages);
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
    stops.push(keys.register({ id: "list.next", scope: "list", chords: [["j"]], run: () => move(1) }));
    stops.push(keys.register({ id: "list.prev", scope: "list", chords: [["k"]], run: () => move(-1) }));
    stops.push(keys.register({ id: "list.open", scope: "list", chords: [["Enter"]], run: () => choose() }));
  }

  return {
    open,
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

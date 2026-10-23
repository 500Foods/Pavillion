/* Mail list (N-3a): one message.list reply for the selected folder.
   From and Subject are text. The count is how many of total are listed.
   A mail.messages_changed for that account loads the reply again.
   A row click does nothing. Rows past the cap are not fetched. */

import { createTable } from "../common/table.js";

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

export function createMailList(api, pane) {
  const doc = pane.ownerDocument || globalThis.document;
  const count = doc.createElement("p");
  const host = doc.createElement("div");
  count.className = "mail-list-count";
  host.className = "mail-list-table";
  pane.replaceChildren(count, host);
  count.textContent = "Select a folder.";

  let table = null;
  let generation = 0;
  let mailboxId = "";
  let accountId = "";
  let stopEvents = null;

  function columns() {
    return [
      {
        title: "From",
        field: "from",
        headerSort: false,
        widthGrow: 1,
        formatter: (cell) => textCell(cell, doc),
      },
      {
        title: "Subject",
        field: "subject",
        headerSort: false,
        widthGrow: 2,
        formatter: (cell) => textCell(cell, doc),
      },
    ];
  }

  function show(rows) {
    if (!table) {
      table = createTable(host, { columns: columns(), data: rows, rowId: "id" });
      return;
    }
    table.setData(rows);
  }

  function open(id) {
    const mine = generation + 1;
    generation = mine;
    mailboxId = typeof id === "string" ? id : "";
    if (mailboxId === "") {
      accountId = "";
      count.textContent = "Select a folder.";
      if (table) {
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
        count.textContent = "";
        if (table) {
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

  return {
    open,
    stop() {
      generation += 1;
      mailboxId = "";
      accountId = "";
      if (typeof stopEvents === "function") {
        stopEvents();
        stopEvents = null;
      }
      if (table) {
        table.destroy();
        table = null;
      }
    },
  };
}

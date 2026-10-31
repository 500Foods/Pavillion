/**
 * @vitest-environment jsdom
 */
import { expect, test, vi } from "vitest";
import { createApi } from "../api.js";
import { mount, unmount } from "../index.js";
import { mockTransport } from "../transport/mock.js";

/* Tabulator does not lay out rows under jsdom, so the shared table is a small
   fake here: one button per row (data-account-id), each cell drawn by the real
   column formatters, and a row click calls onRowClick like Tabulator does. */
const fakeTables = [];
vi.mock("../common/table.js", async (importOriginal) => {
  const actual = await importOriginal();
  return {
  ...actual,
  createTable(el, options) {
    globalThis.__lastTableOptions = options;
    let current = null;
    let rowsNow = options.data;
    let allRows = options.data;
    let filterFn = null;
    const columns = options.columns.filter((c) => c.visible !== false);
    const drawRow = (row) => {
      const button = document.createElement("button");
      button.type = "button";
      button.dataset.accountId = row.id;
      if (options.rowClass && options.rowClass(row)) {
        button.classList.add(options.rowClass(row));
      }
      for (const column of columns) {
        const cell = { getValue: () => row[column.field], getRow: () => ({ getData: () => row }) };
        const out = column.formatter ? column.formatter(cell) : String(row[column.field] ?? "");
        const span = document.createElement("span");
        span.dataset.field = column.field;
        span.append(out);
        button.append(span);
      }
      button.addEventListener("click", () => options.onRowClick(row));
      return button;
    };
    const order = (data) => {
      const groups = new Map();
      for (const row of data) {
        const key = options.groupBy ? row[options.groupBy] : "";
        if (!groups.has(key)) {
          groups.set(key, []);
        }
        groups.get(key).push(row);
      }
      return groups;
    };
    const mark = () => {
      for (const b of el.querySelectorAll("button[data-account-id]")) {
        b.classList.toggle("tabulator-selected", b.dataset.accountId === current);
      }
    };
    /* Like Tabulator: groups in first-seen order, header, rows, then the
       bottom calc row and the page's footer element. */
    const draw = (input) => {
      allRows = input;
      const data = filterFn ? input.filter(filterFn) : input;
      rowsNow = data;
      el.replaceChildren();
      for (const [key, rows] of order(data)) {
        if (options.groupBy) {
          const header = document.createElement("div");
          header.className = "tabulator-group";
          header.dataset.group = key;
          header.append(options.groupHeader ? options.groupHeader(key, rows.length, rows) : key);
          el.append(header);
        }
        for (const row of rows) {
          el.append(drawRow(row));
        }
      }
      if (columns.some((c) => c.bottomCalc)) {
        const calcs = document.createElement("div");
        calcs.className = "tabulator-calcs-bottom";
        for (const column of columns) {
          const span = document.createElement("span");
          span.dataset.field = column.field;
          span.textContent = column.bottomCalc ? String(column.bottomCalc(data.map((r) => r[column.field]), data)) : "";
          calcs.append(span);
        }
        el.append(calcs);
      }
      if (options.footerElement) {
        el.append(options.footerElement);
      }
      mark();
      if (options.onRender) {
        options.onRender();
      }
    };
    draw(options.data);
    const handle = {
      options,
      downloads: [],
      setData: draw,
      visibility: {},
      setColumnVisible(field, on) {
        handle.visibility[field] = on;
      },
      widthCalls: [],
      setColumnWidth(field, width) {
        handle.widthCalls.push([field, width]);
      },
      widths: () => handle.liveWidths || {},
      refits: 0,
      refit() {
        handle.refits += 1;
        globalThis.__lastTableRefits = handle.refits;
        return Promise.resolve();
      },
      scales: [],
      setScale(w, f) {
        handle.scales.push([w, f]);
        globalThis.__lastTableScale = [w, f];
      },
      setFilter(fn) {
        filterFn = fn;
        draw(allRows);
      },
      select(id) {
        current = id;
        mark();
      },
      ids: () => [...order(rowsNow).values()].flat().map((r) => r.id),
      pageCount: () => 2,
      download(name) {
        handle.downloads.push(name);
        return true;
      },
      destroy() {
        el.replaceChildren();
      },
    };
    fakeTables.push(handle);
    globalThis.__lastTableHandle = handle;
    return handle;
  },
  };
});

function getTransport() {
  const transport = mockTransport();
  const api = createApi(transport);
  return { transport, api };
}

function openAccountsPanel() {
  document.querySelector('[data-section="settings"]').click();
  document.querySelector('[data-heading="accounts"]').click();
}

test("accounts heading appears in the settings nav", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  try {
    const { api } = getTransport();
    await mount(el, api, {});
    openAccountsPanel();
    const heading = document.querySelector('[data-heading="accounts"]');
    expect(heading).not.toBeNull();
    expect(heading.getAttribute("aria-current")).toBe("true");
    const pane = document.querySelector('[data-settings="accounts"]');
    expect(pane.hidden).toBe(false);
  } finally {
    unmount();
    el.remove();
  }
});

test("the empty tree shows a placeholder", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  try {
    const { api } = getTransport();
    await mount(el, api, {});
    openAccountsPanel();
    const tree = document.querySelector("#accounts-tree");
    expect(tree.textContent).toContain("No accounts yet.");
  } finally {
    unmount();
    el.remove();
  }
});

test("adding an account lists it in the tree with a number", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  try {
    const { api } = getTransport();
    await mount(el, api, {});
    openAccountsPanel();
    document.querySelector("#account-name-input").value = "Test Account";
    document.querySelector("#account-session-url-input").value = "https://example.com/.well-known/jmap";
    document.querySelector("#account-username-input").value = "test@example.com";
    document.querySelector("#account-secret-input").value = "correct-password";
    document.querySelector("#account-color-input").value = "#2a7";

    document.querySelector("#account-name-input").dispatchEvent(new Event("input", { bubbles: true }));
    document.querySelector("#account-secret-input").dispatchEvent(new Event("input", { bubbles: true }));

    document.querySelector("#account-add").click();
    await vi.waitFor(() => {
      const tree = document.querySelector("#accounts-tree");
      expect(tree.textContent).toContain("Test Account");
      expect(tree.textContent).toContain("001");
    });

    const addBtn = document.querySelector("#account-add");
    const removeBtn = document.querySelector("#account-remove");
    expect(addBtn.hidden).toBe(true);
    expect(removeBtn.hidden).toBe(false);
  } finally {
    unmount();
    el.remove();
  }
});

test("Test button is disabled until secret is entered", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  try {
    const { api } = getTransport();
    await mount(el, api, {});
    openAccountsPanel();
    document.querySelector("#account-name-input").value = "Test Account";
    document.querySelector("#account-session-url-input").value = "https://example.com/.well-known/jmap";
    document.querySelector("#account-username-input").value = "test@example.com";
    document.querySelector("#account-name-input").dispatchEvent(new Event("input", { bubbles: true }));
    document.querySelector("#account-session-url-input").dispatchEvent(new Event("input", { bubbles: true }));
    document.querySelector("#account-username-input").dispatchEvent(new Event("input", { bubbles: true }));

    expect(document.querySelector("#account-test").disabled).toBe(true);

    document.querySelector("#account-secret-input").value = "correct-password";
    document.querySelector("#account-secret-input").dispatchEvent(new Event("input", { bubbles: true }));

    expect(document.querySelector("#account-test").disabled).toBe(false);
  } finally {
    unmount();
    el.remove();
  }
});

test("Test with valid credentials shows capabilities", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  try {
    const { api } = getTransport();
    await mount(el, api, {});
    openAccountsPanel();
    document.querySelector("#account-session-url-input").value = "https://example.com/.well-known/jmap";
    document.querySelector("#account-username-input").value = "test@example.com";
    document.querySelector("#account-secret-input").value = "correct-password";
    document.querySelector("#account-session-url-input").dispatchEvent(new Event("input", { bubbles: true }));
    document.querySelector("#account-username-input").dispatchEvent(new Event("input", { bubbles: true }));
    document.querySelector("#account-secret-input").dispatchEvent(new Event("input", { bubbles: true }));

    document.querySelector("#account-test").click();
    await vi.waitFor(() => {
      const caps = document.querySelector("#account-test-capabilities");
      expect(caps.textContent).toContain("mail");
    });
    expect(document.querySelector("#account-test-status").textContent).toBe("OK");
  } finally {
    unmount();
    el.remove();
  }
});

test("Test with wrong password shows an error", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  try {
    const { api } = getTransport();
    await mount(el, api, {});
    openAccountsPanel();
    document.querySelector("#account-session-url-input").value = "https://example.com/.well-known/jmap";
    document.querySelector("#account-username-input").value = "test@example.com";
    document.querySelector("#account-secret-input").value = "wrong";
    document.querySelector("#account-session-url-input").dispatchEvent(new Event("input", { bubbles: true }));
    document.querySelector("#account-username-input").dispatchEvent(new Event("input", { bubbles: true }));
    document.querySelector("#account-secret-input").dispatchEvent(new Event("input", { bubbles: true }));

    document.querySelector("#account-test").click();
    await vi.waitFor(() => {
      const err = document.querySelector("#account-test-error");
      expect(err.textContent).toContain("rejected");
    });
  } finally {
    unmount();
    el.remove();
  }
});

test("Save is disabled until Test succeeds", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  try {
    const { api } = getTransport();
    await mount(el, api, {});
    openAccountsPanel();
    document.querySelector("#account-name-input").value = "Test Account";
    document.querySelector("#account-session-url-input").value = "https://example.com/.well-known/jmap";
    document.querySelector("#account-username-input").value = "test@example.com";
    document.querySelector("#account-secret-input").value = "correct-password";
    document.querySelector("#account-name-input").dispatchEvent(new Event("input", { bubbles: true }));
    document.querySelector("#account-session-url-input").dispatchEvent(new Event("input", { bubbles: true }));
    document.querySelector("#account-username-input").dispatchEvent(new Event("input", { bubbles: true }));
    document.querySelector("#account-secret-input").dispatchEvent(new Event("input", { bubbles: true }));

    expect(document.querySelector("#account-save").disabled).toBe(true);

    document.querySelector("#account-test").click();
    await vi.waitFor(() => {
      expect(document.querySelector("#account-test-status").textContent).toBe("OK");
    });
    expect(document.querySelector("#account-save").disabled).toBe(false);
  } finally {
    unmount();
    el.remove();
  }
});

test("selecting an account in the tree populates the form", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  try {
    const { api } = getTransport();
    await mount(el, api, {});
    openAccountsPanel();

    document.querySelector("#account-name-input").value = "First Account";
    document.querySelector("#account-session-url-input").value = "https://first.com/.well-known/jmap";
    document.querySelector("#account-username-input").value = "first@example.com";
    document.querySelector("#account-secret-input").value = "pw1";
    document.querySelector("#account-color-input").value = "#2a7";
    document.querySelector("#account-name-input").dispatchEvent(new Event("input", { bubbles: true }));
    document.querySelector("#account-session-url-input").dispatchEvent(new Event("input", { bubbles: true }));
    document.querySelector("#account-username-input").dispatchEvent(new Event("input", { bubbles: true }));
    document.querySelector("#account-secret-input").dispatchEvent(new Event("input", { bubbles: true }));
    document.querySelector("#account-add").click();

    document.querySelector("#account-name-input").value = "Second Account";
    document.querySelector("#account-session-url-input").value = "https://second.com/.well-known/jmap";
    document.querySelector("#account-username-input").value = "second@example.com";
    document.querySelector("#account-secret-input").value = "pw2";
    document.querySelector("#account-name-input").dispatchEvent(new Event("input", { bubbles: true }));
    document.querySelector("#account-session-url-input").dispatchEvent(new Event("input", { bubbles: true }));
    document.querySelector("#account-username-input").dispatchEvent(new Event("input", { bubbles: true }));
    document.querySelector("#account-secret-input").dispatchEvent(new Event("input", { bubbles: true }));
    document.querySelector("#account-add").click();

    await vi.waitFor(() => {
      const buttons = document.querySelectorAll('[data-account-id]');
      expect(buttons.length).toBe(2);
    });

    const firstBtn = document.querySelector('[data-account-id="first-account"]');
    expect(firstBtn).not.toBeNull();
    firstBtn.click();

    expect(document.querySelector("#account-name-input").value).toBe("First Account");
    expect(document.querySelector("#account-session-url-input").value).toBe("https://first.com/.well-known/jmap");
    expect(document.querySelector("#account-username-input").value).toBe("first@example.com");
    expect(document.querySelector("#account-form-number").textContent).toBe("001");
  } finally {
    unmount();
    el.remove();
  }
});

test("removing an account deletes it from the tree", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  try {
    const { api } = getTransport();
    await mount(el, api, {});
    openAccountsPanel();

    document.querySelector("#account-name-input").value = "Test Account";
    document.querySelector("#account-session-url-input").value = "https://example.com/.well-known/jmap";
    document.querySelector("#account-username-input").value = "test@example.com";
    document.querySelector("#account-secret-input").value = "pw1";
    document.querySelector("#account-name-input").dispatchEvent(new Event("input", { bubbles: true }));
    document.querySelector("#account-session-url-input").dispatchEvent(new Event("input", { bubbles: true }));
    document.querySelector("#account-username-input").dispatchEvent(new Event("input", { bubbles: true }));
    document.querySelector("#account-secret-input").dispatchEvent(new Event("input", { bubbles: true }));
    document.querySelector("#account-add").click();

    await vi.waitFor(() => {
      expect(document.querySelector('[data-account-id="test-account"]')).not.toBeNull();
    });

    document.querySelector('[data-account-id="test-account"]').click();
    expect(document.querySelector("#account-remove")).not.toBeNull();

    /* D-10: the toolbar Delete asks through the app dialog first. */
    expect(document.querySelector("#account-delete").disabled).toBe(false);
    document.querySelector("#account-delete").click();
    await vi.waitFor(() => {
      expect(document.querySelector("dialog.pav-dialog .pav-dialog-ok")).not.toBeNull();
    });
    const dialog = document.querySelector("dialog.pav-dialog");
    expect(dialog.querySelector(".pav-dialog-title").textContent).toBe("Delete Test Account?");
    dialog.querySelector(".pav-dialog-ok").click();

    await vi.waitFor(() => {
      expect(document.querySelector("#accounts-tree").textContent).toContain("No accounts yet.");
    });
    expect(document.querySelector("#account-delete").disabled).toBe(true);
    expect(document.querySelector("#account-clone").disabled).toBe(true);
  } finally {
    unmount();
    el.remove();
  }
});

test("Save updates account details", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  try {
    const { api } = getTransport();
    await mount(el, api, {});
    openAccountsPanel();

    document.querySelector("#account-name-input").value = "Original Name";
    document.querySelector("#account-session-url-input").value = "https://example.com/.well-known/jmap";
    document.querySelector("#account-username-input").value = "orig@example.com";
    document.querySelector("#account-secret-input").value = "pw1";
    document.querySelector("#account-name-input").dispatchEvent(new Event("input", { bubbles: true }));
    document.querySelector("#account-session-url-input").dispatchEvent(new Event("input", { bubbles: true }));
    document.querySelector("#account-username-input").dispatchEvent(new Event("input", { bubbles: true }));
    document.querySelector("#account-secret-input").dispatchEvent(new Event("input", { bubbles: true }));
    document.querySelector("#account-add").click();

    await vi.waitFor(() => {
      expect(document.querySelector('[data-account-id="original-name"]')).not.toBeNull();
    });

    document.querySelector('[data-account-id="original-name"]').click();

    document.querySelector("#account-secret-input").value = "pw1";
    document.querySelector("#account-secret-input").dispatchEvent(new Event("input", { bubbles: true }));

    document.querySelector("#account-test").click();
    await vi.waitFor(() => {
      expect(document.querySelector("#account-test-status").textContent).toBe("OK");
    });

    document.querySelector("#account-name-input").value = "Renamed Account";
    document.querySelector("#account-name-input").dispatchEvent(new Event("input", { bubbles: true }));

    document.querySelector("#account-save").click();
    await vi.waitFor(() => {
      expect(document.querySelector("#account-form-title").textContent).toBe("Renamed Account");
    });
    expect(document.querySelector("#tabs-message").textContent).toBe("Account saved");
  } finally {
    unmount();
    el.remove();
  }
});

test("the Advanced CA file field is sent with add, test, and save", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  try {
    const transport = mockTransport();
    const callSpy = vi.spyOn(transport, "call");
    const api = createApi(transport);
    await mount(el, api, {});
    openAccountsPanel();

    const caInput = document.querySelector("#account-ca-file-input");
    expect(caInput).not.toBeNull();
    expect(caInput.closest("details.accounts-advanced")).not.toBeNull();

    document.querySelector("#account-name-input").value = "Pinned";
    document.querySelector("#account-session-url-input").value = "https://localhost:1443/.well-known/jmap";
    document.querySelector("#account-username-input").value = "andrew@test-a.local";
    document.querySelector("#account-secret-input").value = "test";
    caInput.value = "/tmp/test-ca.crt";
    document.querySelector("#account-secret-input").dispatchEvent(new Event("input", { bubbles: true }));

    document.querySelector("#account-test").click();
    await vi.waitFor(() => {
      expect(document.querySelector("#account-test-status").textContent).toBe("OK");
    });
    const testCall = callSpy.mock.calls.find(([cmd]) => cmd === "account.test");
    expect(testCall[1].caFile).toBe("/tmp/test-ca.crt");

    document.querySelector("#account-add").click();
    await vi.waitFor(() => {
      expect(document.querySelector('[data-account-id="pinned"]')).not.toBeNull();
    });
    const addCall = callSpy.mock.calls.find(([cmd]) => cmd === "account.add");
    expect(addCall[1].caFile).toBe("/tmp/test-ca.crt");

    document.querySelector('[data-account-id="pinned"]').click();
    expect(document.querySelector("#account-ca-file-input").value).toBe("/tmp/test-ca.crt");
    document.querySelector("#account-secret-input").value = "test";
    document.querySelector("#account-secret-input").dispatchEvent(new Event("input", { bubbles: true }));
    document.querySelector("#account-test").click();
    await vi.waitFor(() => {
      expect(document.querySelector("#account-test-status").textContent).toBe("OK");
    });
    document.querySelector("#account-ca-file-input").value = "";
    document.querySelector("#account-save").click();
    await vi.waitFor(() => {
      expect(callSpy.mock.calls.some(([cmd]) => cmd === "account.update")).toBe(true);
    });
    const updateCall = callSpy.mock.calls.find(([cmd]) => cmd === "account.update");
    expect(updateCall[1].patch.caFile).toBe("");
  } finally {
    unmount();
    el.remove();
  }
});

test("a blank CA file is left out of add and test", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  try {
    const transport = mockTransport();
    const callSpy = vi.spyOn(transport, "call");
    const api = createApi(transport);
    await mount(el, api, {});
    openAccountsPanel();
    document.querySelector("#account-name-input").value = "Plain";
    document.querySelector("#account-session-url-input").value = "https://example.com/.well-known/jmap";
    document.querySelector("#account-username-input").value = "p@example.com";
    document.querySelector("#account-secret-input").value = "pw";
    document.querySelector("#account-secret-input").dispatchEvent(new Event("input", { bubbles: true }));
    document.querySelector("#account-test").click();
    await vi.waitFor(() => {
      expect(document.querySelector("#account-test-status").textContent).toBe("OK");
    });
    document.querySelector("#account-add").click();
    await vi.waitFor(() => {
      expect(document.querySelector('[data-account-id="plain"]')).not.toBeNull();
    });
    const testCall = callSpy.mock.calls.find(([cmd]) => cmd === "account.test");
    const addCall = callSpy.mock.calls.find(([cmd]) => cmd === "account.add");
    expect("caFile" in testCall[1]).toBe(false);
    expect("caFile" in addCall[1]).toBe(false);
  } finally {
    unmount();
    el.remove();
  }
});

function typeInto(selector, value) {
  const input = document.querySelector(selector);
  input.value = value;
  input.dispatchEvent(new Event("input", { bubbles: true }));
}

async function addSavedAccount() {
  typeInto("#account-name-input", "Saved One");
  typeInto("#account-session-url-input", "https://example.com/.well-known/jmap");
  typeInto("#account-username-input", "saved@example.com");
  typeInto("#account-secret-input", "pw-saved");
  document.querySelector("#account-add").click();
  await vi.waitFor(() => {
    expect(document.querySelector('[data-account-id="saved-one"]')).not.toBeNull();
  });
  document.querySelector('[data-account-id="saved-one"]').click();
}

test("Test with a blank password uses the saved credential", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  try {
    const transport = mockTransport();
    const callSpy = vi.spyOn(transport, "call");
    const api = createApi(transport);
    await mount(el, api, {});
    openAccountsPanel();
    await addSavedAccount();

    expect(document.querySelector("#account-secret-input").value).toBe("");
    expect(document.querySelector("#account-credentials-status").textContent).toBe("Saved in keyring");
    expect(document.querySelector("#account-test").disabled).toBe(false);
    document.querySelector("#account-test").click();
    await vi.waitFor(() => {
      expect(document.querySelector("#account-test-status").textContent).toBe("OK");
    });
    const testCall = callSpy.mock.calls.filter(([cmd]) => cmd === "account.test").pop();
    expect(testCall[1].id).toBe("saved-one");
    expect(testCall[1].secret).toBe("");
  } finally {
    unmount();
    el.remove();
  }
});

test("a typed password wins over the saved one", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  try {
    const transport = mockTransport();
    const callSpy = vi.spyOn(transport, "call");
    const api = createApi(transport);
    await mount(el, api, {});
    openAccountsPanel();
    await addSavedAccount();
    typeInto("#account-secret-input", "wrong");
    expect(document.querySelector("#account-credentials-status").textContent).toBe("Replaces the saved password");
    document.querySelector("#account-test").click();
    await vi.waitFor(() => {
      expect(document.querySelector("#account-test-error").textContent).toContain("rejected");
    });
    const testCall = callSpy.mock.calls.filter(([cmd]) => cmd === "account.test").pop();
    expect(testCall[1].secret).toBe("wrong");
    expect(testCall[1].id).toBeUndefined();
  } finally {
    unmount();
    el.remove();
  }
});

test("a new account still needs a password to test", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  try {
    const { api } = getTransport();
    await mount(el, api, {});
    openAccountsPanel();
    typeInto("#account-session-url-input", "https://example.com/.well-known/jmap");
    typeInto("#account-username-input", "new@example.com");
    expect(document.querySelector("#account-test").disabled).toBe(true);
    expect(document.querySelector("#account-credentials-status").textContent).toBe("Not saved yet");
  } finally {
    unmount();
    el.remove();
  }
});

test("Email defaults from the username and Detect fills the session URL", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  try {
    const transport = mockTransport();
    const callSpy = vi.spyOn(transport, "call");
    const api = createApi(transport);
    await mount(el, api, {});
    openAccountsPanel();
    const detect = document.querySelector("#account-detect");
    expect(detect.disabled).toBe(true);
    typeInto("#account-username-input", "andrew");
    expect(document.querySelector("#account-email-input").value).toBe("");
    typeInto("#account-username-input", "andrew@500foods.com");
    expect(document.querySelector("#account-email-input").value).toBe("andrew@500foods.com");
    expect(detect.disabled).toBe(false);
    detect.click();
    await vi.waitFor(() => {
      expect(document.querySelector("#account-session-url-input").value).toBe("https://mail.500foods.com/jmap/session");
    });
    expect(document.querySelector("#account-detect-status").textContent).toBe("Found via SRV");
    const call = callSpy.mock.calls.find(([cmd]) => cmd === "account.discover");
    expect(call[1]).toEqual({ email: "andrew@500foods.com" });
  } finally {
    unmount();
    el.remove();
  }
});

test("a typed Email is kept and a failed Detect shows the error inline", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  try {
    const { api } = getTransport();
    await mount(el, api, {});
    openAccountsPanel();
    typeInto("#account-email-input", "me@missing.example");
    typeInto("#account-username-input", "other@example.com");
    expect(document.querySelector("#account-email-input").value).toBe("me@missing.example");
    document.querySelector("#account-detect").click();
    await vi.waitFor(() => {
      expect(document.querySelector("#account-detect-error").textContent).toContain("no JMAP session found");
    });
    expect(document.querySelector("#account-session-url-input").value).toBe("");
  } finally {
    unmount();
    el.remove();
  }
});

test("the account form groups fields in order", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  try {
    const { api } = getTransport();
    await mount(el, api, {});
    openAccountsPanel();
    const groups = [...document.querySelectorAll("#account-form fieldset.accounts-group")].map((g) => g.dataset.group);
    expect(groups).toEqual(["identity", "server", "credentials", "options"]);
    const server = document.querySelector('[data-group="server"]');
    expect(server.querySelector("#account-session-url-input").nextElementSibling.id).toBe("account-detect");
  } finally {
    unmount();
    el.remove();
  }
});

test("the email is saved with the account and read back", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  try {
    const transport = mockTransport();
    const callSpy = vi.spyOn(transport, "call");
    const api = createApi(transport);
    await mount(el, api, {});
    openAccountsPanel();
    typeInto("#account-name-input", "Mail One");
    typeInto("#account-session-url-input", "https://example.com/.well-known/jmap");
    typeInto("#account-username-input", "login-name");
    typeInto("#account-email-input", "me@example.com");
    typeInto("#account-secret-input", "pw");
    document.querySelector("#account-add").click();
    await vi.waitFor(() => {
      expect(document.querySelector('[data-account-id="mail-one"]')).not.toBeNull();
    });
    const addCall = callSpy.mock.calls.find(([cmd]) => cmd === "account.add");
    expect(addCall[1].email).toBe("me@example.com");
    document.querySelector('[data-account-id="mail-one"]').click();
    expect(document.querySelector("#account-email-input").value).toBe("me@example.com");

    typeInto("#account-email-input", "new@example.com");
    document.querySelector("#account-test").click();
    await vi.waitFor(() => {
      expect(document.querySelector("#account-test-status").textContent).toBe("OK");
    });
    document.querySelector("#account-save").click();
    await vi.waitFor(() => {
      const upd = callSpy.mock.calls.find(([cmd]) => cmd === "account.update");
      expect(upd[1].patch.email).toBe("new@example.com");
    });
  } finally {
    unmount();
    el.remove();
  }
});

async function withView(body, hostExtra = {}) {
  const el = document.createElement("div");
  document.body.append(el);
  try {
    const transport = mockTransport();
    const callSpy = vi.spyOn(transport, "call");
    const api = createApi(transport);
    const saves = [];
    const host = {
      dialog: { save: async (opts) => { saves.push(opts); return "/tmp/pav-accounts.csv"; } },
      ...hostExtra,
    };
    await mount(el, api, { hostServices: host });
    openAccountsPanel();
    await body(callSpy, saves);
  } finally {
    unmount();
    el.remove();
  }
}

test("Detect on an existing account saves the session URL at once", async () => {
  await withView(async (callSpy) => {
    await addSavedAccount();
    typeInto("#account-email-input", "me@500foods.com");
    document.querySelector("#account-detect").click();
    await vi.waitFor(() => {
      expect(document.querySelector("#account-detect-status").textContent).toBe("Found via SRV · Saved");
    });
    const update = callSpy.mock.calls.filter(([cmd]) => cmd === "account.update").pop();
    expect(update[1]).toEqual({ id: "saved-one", patch: { sessionUrl: "https://mail.500foods.com/jmap/session" } });
    expect(document.querySelector("#account-session-url-input").value).toBe("https://mail.500foods.com/jmap/session");
  });
});

test("Detect on a new account keeps the URL in the form for Add", async () => {
  await withView(async (callSpy) => {
    typeInto("#account-email-input", "me@500foods.com");
    document.querySelector("#account-detect").click();
    await vi.waitFor(() => {
      expect(document.querySelector("#account-detect-status").textContent).toBe("Found via SRV");
    });
    expect(callSpy.mock.calls.some(([cmd]) => cmd === "account.update")).toBe(false);
    expect(document.querySelector("#account-session-url-input").value).toBe("https://mail.500foods.com/jmap/session");
  });
});

test("a Test redirect to another host stores the final URL and tests it again", async () => {
  await withView(async (callSpy) => {
    await addSavedAccount();
    typeInto("#account-session-url-input", "https://redirect.500foods.com/.well-known/jmap");
    document.querySelector("#account-test").click();
    await vi.waitFor(() => {
      expect(document.querySelector("#account-test-status").textContent).toBe("OK");
    });
    expect(document.querySelector("#account-url-note").textContent).toBe("Session URL updated");
    expect(document.querySelector("#account-session-url-input").value).toBe("https://mail.500foods.com/jmap/session");
    const tests = callSpy.mock.calls.filter(([cmd]) => cmd === "account.test");
    expect(tests.at(-1)[1].sessionUrl).toBe("https://mail.500foods.com/jmap/session");
    expect(tests.at(-2)[1].sessionUrl).toBe("https://redirect.500foods.com/.well-known/jmap");
    const update = callSpy.mock.calls.filter(([cmd]) => cmd === "account.update").pop();
    expect(update[1].patch).toEqual({ sessionUrl: "https://mail.500foods.com/jmap/session" });
  });
});

test("the table row shows the swatch, protocols, and features after a Test", async () => {
  await withView(async () => {
    await addSavedAccount();
    const row = () => document.querySelector('[data-account-id="saved-one"]');
    expect(row().querySelector(".account-swatch .fa-envelope")).not.toBeNull();
    expect(row().querySelector(".accounts-badge")).toBeNull();
    expect(row().classList.contains("tabulator-selected")).toBe(true);
    const box = (field) => row().querySelector(`[data-field="${field}"] input[type="checkbox"]`);
    expect(box("mail").checked).toBe(false);
    expect(box("mail").disabled).toBe(true);
    document.querySelector("#account-test").click();
    await vi.waitFor(() => {
      expect(box("mail").checked).toBe(true);
    });
    expect(box("contacts").checked).toBe(true);
    expect(box("calendars").checked).toBe(true);
    expect(box("files").checked).toBe(true); /* filenode → files (2026-10-19) */
    expect(box("jmap").checked).toBe(true);
    expect(box("imap").checked).toBe(false);
    expect(box("smtp").checked).toBe(false);
  });
});

test("the icon button opens the Wizard and Save sends the snippet", async () => {
  await withView(async (callSpy) => {
    await addSavedAccount();
    const button = document.querySelector("#account-icon-choose");
    expect(button.querySelector(".fa-envelope")).not.toBeNull();
    button.click();
    await vi.waitFor(() => {
      expect(button.querySelector(".fa-briefcase")).not.toBeNull();
    });
    document.querySelector("#account-test").click();
    await vi.waitFor(() => {
      expect(document.querySelector("#account-save").disabled).toBe(false);
    });
    document.querySelector("#account-save").click();
    await vi.waitFor(() => {
      const update = callSpy.mock.calls.filter(([cmd]) => cmd === "account.update").pop();
      expect(update[1].patch.icon).toBe('<i class="fa-solid fa-briefcase"></i>');
    });
    await vi.waitFor(() => {
      expect(document.querySelector('[data-account-id="saved-one"] .account-swatch .fa-briefcase')).not.toBeNull();
    });
  }, {
    openIconWizard: async () => ({ html: '<i class="fa-solid fa-briefcase"></i>' }),
  });
});

/* K-Q10: the stored capabilities survive a reload (unmount, mount again on
   the same core), with the checked time as the feature tooltip. */
test("the table ticks stored capabilities after a reload", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  const transport = mockTransport();
  const box = (field) => document.querySelector(`[data-account-id="saved-one"] [data-field="${field}"] input[type="checkbox"]`);
  const tip = () => document.querySelector('[data-account-id="saved-one"] [data-field="mail"] .accounts-feature-cell').dataset.pavTip;
  try {
    await mount(el, createApi(transport), {});
    openAccountsPanel();
    await addSavedAccount();
    expect(tip()).toBe("Not checked yet");
    document.querySelector("#account-test").click();
    await vi.waitFor(() => {
      expect(box("mail").checked).toBe(true);
    });
    unmount();
    await mount(el, createApi(transport), {});
    openAccountsPanel();
    await vi.waitFor(() => {
      expect(box("mail").checked).toBe(true);
    });
    expect(box("files").checked).toBe(true);
    expect(tip()).toMatch(/^Checked \d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}$/);
  } finally {
    unmount();
    el.remove();
  }
});

test("a Test from an unsaved form sends its capabilities on Add", async () => {
  await withView(async (callSpy) => {
    typeInto("#account-name-input", "Fresh");
    typeInto("#account-session-url-input", "https://example.com/.well-known/jmap");
    typeInto("#account-username-input", "fresh@example.com");
    typeInto("#account-secret-input", "pw");
    document.querySelector("#account-test").click();
    await vi.waitFor(() => {
      expect(document.querySelector("#account-test-status").textContent).toBe("OK");
    });
    document.querySelector("#account-add").click();
    await vi.waitFor(() => {
      expect(document.querySelector('[data-account-id="fresh"] [data-field="mail"] input').checked).toBe(true);
    });
    const add = callSpy.mock.calls.filter(([cmd]) => cmd === "account.add").pop();
    expect(add[1].capabilities).toContain("files");
    expect(typeof add[1].capabilitiesChecked).toBe("string");
  });
});

/* K-Q11: number, swatch, Name, features; grouped by server; a count footer. */
test("the table orders columns, groups by server, and counts accounts", async () => {
  await withView(async () => {
    const add = async (name, url) => {
      typeInto("#account-name-input", name);
      typeInto("#account-session-url-input", url);
      typeInto("#account-username-input", `${name.toLowerCase()}@example.com`);
      typeInto("#account-secret-input", "pw");
      document.querySelector("#account-add").click();
      await vi.waitFor(() => {
        expect(document.querySelector(`[data-account-id="${name.toLowerCase()}"]`)).not.toBeNull();
      });
    };
    await add("One", "https://mail.500foods.com/jmap/session");
    expect(document.querySelector("#accounts-tree .tabulator-calcs-bottom [data-field=\"number\"]").textContent).toBe("1");
    await add("Two", "https://mail.example.com/.well-known/jmap");
    await add("Three", "https://mail.500foods.com/jmap/session");
    const fields = [...document.querySelectorAll('[data-account-id="one"] > span')].map((s) => s.dataset.field);
    expect(fields).toEqual(["number", "account", "name", "jmap", "imap", "smtp", "mail", "contacts", "calendars", "files"]);
    expect(document.querySelector('[data-account-id="one"] [data-field="number"]').textContent).toMatch(/^\d{3}$/);
    const headers = [...document.querySelectorAll("#accounts-tree .tabulator-group")];
    expect(headers.map((h) => h.querySelector(".pav-group-label").textContent)).toEqual([
      "mail.500foods.com",
      "mail.example.com",
    ]);
    expect(headers.map((h) => h.querySelector(".pav-group-count").textContent)).toEqual(["2", "1"]);
    expect(document.querySelector("#accounts-tree .tabulator-calcs-bottom [data-field=\"number\"]").textContent).toBe("3");
  });
});

/* K-Q12: calcs, the navigator, arrow keys, and Export. */
test("the footer counts checked features, and the navigator and arrows move the selection", async () => {
  await withView(async (callSpy, saves) => {
    const add = async (name, url) => {
      typeInto("#account-name-input", name);
      typeInto("#account-session-url-input", url);
      typeInto("#account-username-input", `${name.toLowerCase()}@example.com`);
      typeInto("#account-secret-input", "pw");
      document.querySelector("#account-add").click();
      await vi.waitFor(() => {
        expect(document.querySelector(`[data-account-id="${name.toLowerCase()}"]`)).not.toBeNull();
      });
    };
    await add("Alpha", "https://a.example/jmap");
    await add("Beta", "https://b.example/jmap");
    await add("Gamma", "https://a.example/jmap");
    const nav = (action) => document.querySelector(`#accounts-footer button[data-step="${action}"]`);
    const selected = () => document.querySelector("#accounts-tree .tabulator-selected")?.dataset.accountId;
    const formName = () => document.querySelector("#account-name-input").value;

    /* Display order is by group: alpha, gamma (a.example), then beta. */
    document.querySelector('[data-account-id="alpha"]').click();
    expect(selected()).toBe("alpha");
    expect(nav("first").disabled).toBe(true);
    expect(nav("prev").disabled).toBe(true);
    expect(nav("next").disabled).toBe(false);
    expect(nav("last").getAttribute("aria-label")).toBe("Last account");
    expect(nav("first").querySelector("i").className).toContain("fa-backward-step");
    nav("next").click();
    expect(selected()).toBe("gamma");
    expect(formName()).toBe("Gamma");
    nav("last").click();
    expect(selected()).toBe("beta");
    expect(nav("next").disabled).toBe(true);
    expect(nav("last").disabled).toBe(true);
    const tree = document.querySelector("#accounts-tree");
    tree.dispatchEvent(new KeyboardEvent("keydown", { key: "ArrowUp", bubbles: true }));
    expect(selected()).toBe("gamma");
    tree.dispatchEvent(new KeyboardEvent("keydown", { key: "ArrowDown", bubbles: true }));
    expect(selected()).toBe("beta");
    tree.dispatchEvent(new KeyboardEvent("keydown", { key: "Home", bubbles: true }));
    expect(selected()).toBe("alpha");
    tree.dispatchEvent(new KeyboardEvent("keydown", { key: "PageDown", bubbles: true }));
    expect(selected()).toBe("beta");
    tree.dispatchEvent(new KeyboardEvent("keydown", { key: "PageUp", bubbles: true }));
    expect(selected()).toBe("alpha");
    tree.dispatchEvent(new KeyboardEvent("keydown", { key: "End", bubbles: true }));
    expect(selected()).toBe("beta");
    nav("first").click();
    expect(formName()).toBe("Alpha");

    expect(document.querySelector('#accounts-tree .tabulator-calcs-bottom [data-field="number"]').textContent).toBe("3");
    expect(document.querySelector('#accounts-tree .tabulator-calcs-bottom [data-field="mail"]').textContent).toBe("0");

    document.querySelector('#accounts-footer button[data-module-action="export"]').click();
    await vi.waitFor(() => {
      expect(callSpy.mock.calls.some(([cmd]) => cmd === "export.write_text")).toBe(true);
    });
    expect(saves[0].defaultPath).toBe("pavillion-accounts.csv");
    const write = callSpy.mock.calls.find(([cmd]) => cmd === "export.write_text")[1];
    expect(write.path).toBe("/tmp/pav-accounts.csv");
    const lines = write.text.split("\r\n");
    expect(lines[0]).toBe("Number,Name,Server,JMAP,IMAP,SMTP,Mail,Contacts,Calendars,Files,Email");
    expect(lines[1].slice(4)).toBe("Alpha,a.example,yes,no,no,no,no,no,no,alpha@example.com");
    expect(write.text).not.toContain("pw");
    expect(document.querySelector("#tabs-message").textContent).toBe("Exported");
    await vi.waitFor(() => {
      const log = callSpy.mock.calls.filter(([cmd]) => cmd === "log.client").pop();
      expect(log[1]).toEqual({ subsystem: "accounts", level: "info", message: "exported 3 accounts to CSV" });
    });
  });
});

/* D-10: the module toolbar (Add, Clone, Delete, search) and footer. */
test("cloneAccount drops username, password, id, number and capabilities", async () => {
  const { cloneAccount, accountMatches } = await import("./accounts-table.js");
  const copy = cloneAccount({
    id: "a", number: "001", name: "Alpha", username: "u@x.example", secret: "s", password: "p",
    sessionUrl: "https://x.example/jmap", email: "u@x.example", color: "#123", icon: "<i></i>",
    capabilities: ["mail"], capabilitiesChecked: "2026-10-20T00:00:00Z", syncMail: false,
  });
  expect(copy).toEqual({ name: "Alpha (copy)", sessionUrl: "https://x.example/jmap", email: "u@x.example", color: "#123", icon: "<i></i>", syncMail: false });
  const row = { name: "Alpha", number: "001", email: "a@x.example", server: "x.example (https://x.example/jmap)", account: { username: "login7" } };
  expect(accountMatches(row, "")).toBe(true);
  expect(accountMatches(row, "ALP")).toBe(true);
  expect(accountMatches(row, "001")).toBe(true);
  expect(accountMatches(row, "login7")).toBe(true);
  expect(accountMatches(row, "x.example")).toBe(true);
  expect(accountMatches(row, "nope")).toBe(false);
});

test("toolbar Add, Clone and search, and footer Refresh first", async () => {
  await withView(async (callSpy) => {
    const toolbar = document.querySelector("#accounts-toolbar");
    expect(toolbar.querySelector(".pav-module-title").textContent).toBe("Accounts");
    expect([...toolbar.querySelectorAll("button[data-module-action]")].map((b) => b.dataset.moduleAction)).toEqual(["add", "clone", "delete"]);
    expect(toolbar.querySelector("#accounts-columns")).toBeNull();
    expect(toolbar.lastElementChild.classList.contains("help-jump")).toBe(true);
    expect(document.querySelector("#account-clone").disabled).toBe(true);
    const footerFirst = document.querySelector("#accounts-footer .pav-module-nav button");
    expect(footerFirst.dataset.moduleAction).toBe("refresh");
    expect(footerFirst.querySelector("i").className).toContain("fa-arrows-rotate");

    typeInto("#account-name-input", "Alpha");
    typeInto("#account-session-url-input", "https://a.example/jmap");
    typeInto("#account-username-input", "alpha@example.com");
    typeInto("#account-secret-input", "pw");
    document.querySelector("#account-add").click();
    await vi.waitFor(() => {
      expect(document.querySelector('[data-account-id="alpha"]')).not.toBeNull();
    });
    document.querySelector('[data-account-id="alpha"]').click();
    expect(document.querySelector("#account-clone").disabled).toBe(false);

    const secretReads = () => callSpy.mock.calls.filter(([cmd]) => /secret/.test(cmd)).length;
    const before = secretReads();
    document.querySelector("#account-clone").click();
    expect(document.querySelector("#account-name-input").value).toBe("Alpha (copy)");
    expect(document.querySelector("#account-session-url-input").value).toBe("https://a.example/jmap");
    expect(document.querySelector("#account-username-input").value).toBe("");
    expect(document.querySelector("#account-secret-input").value).toBe("");
    expect(document.querySelector("#account-clone").disabled).toBe(true);
    expect(document.querySelector("#accounts-tree .tabulator-selected")).toBeNull();
    expect(secretReads()).toBe(before);

    document.querySelector('[data-account-id="alpha"]').click();
    document.querySelector("#account-new").click();
    expect(document.querySelector("#account-name-input").value).toBe("");
    expect(document.querySelector("#account-form-title").textContent).toBe("Add account");

    const search = document.querySelector("#accounts-search");
    search.value = "zzz";
    search.dispatchEvent(new Event("input", { bubbles: true }));
    expect(document.querySelector('[data-account-id="alpha"]')).toBeNull();
    search.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape", bubbles: true }));
    expect(document.querySelector('[data-account-id="alpha"]')).not.toBeNull();

    const lists = () => callSpy.mock.calls.filter(([cmd]) => cmd === "account.list").length;
    const listed = lists();
    footerFirst.click();
    await vi.waitFor(() => {
      expect(lists()).toBe(listed + 1);
    });
  });
});

test("accountsCsv quotes, guards formulas, and carries only export fields", async () => {
  const { accountsCsv } = await import("./accounts-table.js");
  const text = accountsCsv([{ number: "001", name: "A, \"B\"", server: "s", jmap: true, imap: false, smtp: false, mail: true, calendars: false, contacts: false, files: false, email: "=cmd", account: { username: "u" } }]);
  expect(text).toBe("Number,Name,Server,JMAP,IMAP,SMTP,Mail,Contacts,Calendars,Files,Email\r\n001,\"A, \"\"B\"\"\",s,yes,no,no,yes,no,no,no,'=cmd\r\n");
});

/* The table is mocked, so stand in for the row indicator header button. */
function clickIndicatorHeader() {
  const button = document.createElement("button");
  button.className = "pav-table-indicator-button";
  document.querySelector("#accounts-tree").append(button);
  globalThis.__lastTableOptions.onIndicatorHeader(button);
}

test("the column chooser hides a column and saves it under preferences.ui.tables", async () => {
  await withView(async (callSpy) => {
    clickIndicatorHeader();
    const box = document.querySelector(".pav-column-chooser");
    expect(box).not.toBeNull();
    expect(box.querySelector('[data-field="name"]').disabled).toBe(true);
    const number = box.querySelector('[data-field="number"]');
    number.checked = false;
    number.dispatchEvent(new Event("change"));
    await vi.waitFor(() => {
      const saved = callSpy.mock.calls.filter((c) => c[0] === "prefs.ui.set").map((c) => c[1]);
      expect(saved.at(-1)?.tables?.accounts?.hidden).toEqual(["number"]);
    }, { timeout: 3000 });
    box.querySelector("[data-reset]").click();
    await vi.waitFor(() => {
      const saved = callSpy.mock.calls.filter((c) => c[0] === "prefs.ui.set").map((c) => c[1]);
      expect(saved.at(-1)?.tables?.accounts?.hidden).toEqual([]);
    }, { timeout: 3000 });
  });
});

test("the accounts table asks for the row indicator, sizes the pane for it, and keeps it out of the chooser", async () => {
  const { accountColumns, tableMinWidth, CHOOSER_COLUMNS, EXPORT_FIELDS } = await import("./accounts-table.js");
  await withView(async () => {
    await addSavedAccount();
    await vi.waitFor(() => expect(document.querySelector(".accounts-tree-pane").style.minWidth).not.toBe(""));
    const { INDICATOR_WIDTH } = await import("../common/table.js");
    expect(document.querySelector(".accounts-tree-pane").style.minWidth).toBe(`${tableMinWidth(accountColumns(document)) + INDICATOR_WIDTH + 2}px`);
    clickIndicatorHeader();
    expect(document.querySelector('.pav-column-chooser [data-field="__pav_indicator"]')).toBeNull();
  });
  expect(CHOOSER_COLUMNS.some((c) => c.field === "__pav_indicator")).toBe(false);
  expect(EXPORT_FIELDS).not.toContain("__pav_indicator");
});

test("createTable gets indicator:true from the accounts view", async () => {
  await withView(async () => {
    await addSavedAccount();
    await vi.waitFor(() => expect(globalThis.__lastTableOptions?.indicator).toBe(true));
  });
});

async function testThenSave() {
  document.querySelector("#account-test").click();
  await vi.waitFor(() => expect(document.querySelector("#account-save").disabled).toBe(false));
  document.querySelector("#account-save").click();
}

const callsOf = (spy, cmd) => spy.mock.calls.filter(([c]) => c === cmd);

test("K-Q13: toolbar Add, fill, Test, Save adds the account and selects the new row", async () => {
  await withView(async (callSpy) => {
    await addSavedAccount();
    document.querySelector("#account-new").click();
    expect(document.querySelector("#account-name-input").value).toBe("");
    typeInto("#account-name-input", "Second Box");
    typeInto("#account-session-url-input", "https://mail.example.org/.well-known/jmap");
    typeInto("#account-username-input", "two@example.org");
    typeInto("#account-secret-input", "pw-two");
    const before = callsOf(callSpy, "account.add").length;
    await testThenSave();
    await vi.waitFor(() => expect(callsOf(callSpy, "account.add").length).toBe(before + 1));
    const body = callsOf(callSpy, "account.add").at(-1)[1];
    expect(body).toMatchObject({ name: "Second Box", kind: "jmap", sessionUrl: "https://mail.example.org/.well-known/jmap", username: "two@example.org", secret: "pw-two" });
    await vi.waitFor(() => {
      const row = document.querySelector('[data-account-id="second-box"]');
      expect(row).not.toBeNull();
      expect(row.classList.contains("tabulator-selected")).toBe(true);
    });
    expect(document.querySelector('[data-account-id="saved-one"]').classList.contains("tabulator-selected")).toBe(false);
    expect(document.querySelector("#account-name-input").value).toBe("Second Box");
    expect(document.querySelector("#account-secret-input").value).toBe("");
    expect(callsOf(callSpy, "account.update").length).toBe(0);
  });
});

test("K-Q13: Clone then Save adds a new account and selects it", async () => {
  await withView(async (callSpy) => {
    await addSavedAccount();
    document.querySelector("#account-clone").click();
    typeInto("#account-name-input", "Cloned Box");
    typeInto("#account-username-input", "clone@example.com");
    typeInto("#account-secret-input", "pw-clone");
    await testThenSave();
    await vi.waitFor(() => {
      const row = document.querySelector('[data-account-id="cloned-box"]');
      expect(row).not.toBeNull();
      expect(row.classList.contains("tabulator-selected")).toBe(true);
    });
    expect(callsOf(callSpy, "account.add").at(-1)[1]).toMatchObject({ name: "Cloned Box", username: "clone@example.com" });
    expect(document.querySelector('[data-account-id="saved-one"]')).not.toBeNull();
  });
});

test("K-Q13: a missing field is an inline error and logged, with no account.add", async () => {
  await withView(async (callSpy) => {
    document.querySelector("#account-new").click();
    typeInto("#account-name-input", "No Password");
    typeInto("#account-session-url-input", "https://example.net/.well-known/jmap");
    typeInto("#account-username-input", "np@example.net");
    document.querySelector("#account-add").click();
    await vi.waitFor(() => expect(document.querySelector("#account-test-error").textContent).toBe("Cannot add the account: missing password."));
    expect(callsOf(callSpy, "account.add").length).toBe(0);
    const logs = callsOf(callSpy, "log.client").map((c) => c[1]);
    expect(logs.some((l) => l.level === "warn" && /account add failed: missing password/.test(l.message))).toBe(true);
  });
});

test("K-Q13: an add the core refuses is shown and logged without the secret", async () => {
  await withView(async (callSpy) => {
    document.querySelector("#account-new").click();
    typeInto("#account-name-input", "Bad Url");
    typeInto("#account-session-url-input", "http://insecure.example/jmap");
    typeInto("#account-username-input", "b@example.com");
    typeInto("#account-secret-input", "pw-secret-xyz");
    document.querySelector("#account-add").click();
    await vi.waitFor(() => expect(document.querySelector("#account-test-error").textContent).toMatch(/https/));
    const logs = callsOf(callSpy, "log.client").map((c) => JSON.stringify(c[1]));
    expect(logs.some((l) => l.includes("account add failed"))).toBe(true);
    expect(logs.some((l) => l.includes("pw-secret-xyz"))).toBe(false);
  });
});

test("resize round: a resized column saves its width, Reset clears widths and restores the coded ones", async () => {
  await withView(async (callSpy) => {
    await addSavedAccount();
    const saved = () => callSpy.mock.calls.filter((c) => c[0] === "prefs.ui.set").map((c) => c[1]).at(-1);
    globalThis.__lastTableOptions.onColumnResized("jmap", 131);
    globalThis.__lastTableOptions.onColumnResized("name", 400);
    await vi.waitFor(() => expect(saved()?.tables?.accounts?.widths).toEqual({ jmap: 131 }), { timeout: 3000 });
    globalThis.__lastTableHandle.liveWidths = { jmap: 180, imap: 96 };
    globalThis.__lastTableOptions.onColumnResized("name", 400);
    await vi.waitFor(() => expect(saved()?.tables?.accounts?.widths).toEqual({ jmap: 180, imap: 96 }), { timeout: 3000 });
    expect(globalThis.__lastTableOptions.fitTitles).toEqual(["jmap", "imap", "smtp", "mail", "contacts", "calendars", "files"]);
    clickIndicatorHeader();
    const box = document.querySelector('.pav-column-chooser [data-field="smtp"]');
    box.checked = false;
    box.dispatchEvent(new Event("change"));
    await vi.waitFor(() => expect(saved()?.tables?.accounts).toEqual({ selected: "saved-one", widths: { jmap: 180, imap: 96 }, hidden: ["smtp"] }), { timeout: 3000 });
    document.querySelector(".pav-column-chooser [data-reset]").click();
    await vi.waitFor(() => expect(saved()?.tables?.accounts).toEqual({ selected: "saved-one", widths: {}, hidden: [], order: [] }), { timeout: 3000 });
  });
});

async function withStoredUi(stored, body) {
  const el = document.createElement("div");
  document.body.append(el);
  try {
    const transport = mockTransport();
    await transport.call("prefs.ui.set", stored);
    const api = createApi(transport);
    await mount(el, api, { hostServices: { dialog: { save: async () => null } } });
    await body(transport);
  } finally {
    unmount();
    el.remove();
  }
}

test("K-Q14: the Accounts split ratio is restored from preferences", async () => {
  await withStoredUi({ accounts: { split: 0.62, min: 0.15, max: 0.6 } }, async () => {
    await vi.waitFor(() => expect(document.querySelector(".accounts-tree-pane").style.flexBasis).toBe("62%"));
  });
});

test("Icons, Libraries, and Assets split ratios are restored from preferences", async () => {
  await withStoredUi({
    iconPane: { split: 0.62 },
    libraryPane: { split: 0.5 },
    assetPane: { split: 0.25 },
  }, async () => {
    await vi.waitFor(() => expect(document.querySelector("#icons-tree-pane").style.flexBasis).toBe("62%"));
    expect(document.querySelector("#libraries-tree-pane").style.flexBasis).toBe("50%");
    expect(document.querySelector("#assets-tree-pane").style.flexBasis).toBe("25%");
  });
});

test("a saved Accounts zoom moves onto Tables and Details", async () => {
  await withStoredUi({ zoom: { accountsTable: 1.5, accountsForm: 0.75 } }, async () => {
    await vi.waitFor(() => {
      expect(document.querySelector("#accounts-tree").style.zoom).toBe("");
      expect(document.querySelector(".accounts-form-pane").style.zoom).toBe("0.75");
    });
    expect(document.querySelector('[data-zoom="tables"] input').value).toBe("1.5");
    expect(document.querySelector('[data-zoom="details"] input').value).toBe("0.75");
    expect(document.querySelector("#icons-form").style.zoom).toBe("");
    expect(document.querySelector('[data-zoom="accountsTable"]')).toBeNull();
  });
});

test("K-Q14 zoom in place: the table gets width and font scales, the pane minimum does not move", async () => {
  await withStoredUi({ zoom: { shell: 1.2, accountsTable: 1.5 } }, async () => {
    await vi.waitFor(() => expect(document.querySelector("#account-add")).not.toBeNull());
    typeInto("#account-name-input", "Zoomed");
    typeInto("#account-session-url-input", "https://z.example/.well-known/jmap");
    typeInto("#account-username-input", "z@example.com");
    typeInto("#account-secret-input", "pw-z");
    document.querySelector("#account-add").click();
    await vi.waitFor(() => expect(document.querySelector('[data-account-id="zoomed"]')).not.toBeNull());
    const [w, f] = globalThis.__lastTableScale;
    expect(w).toBeCloseTo(1.8);
    expect(f).toBe(1.5);
    expect(document.querySelector("#accounts-tree").style.zoom).toBe("");
    const { accountColumns, tableMinWidth } = await import("./accounts-table.js");
    const { INDICATOR_WIDTH } = await import("../common/table.js");
    expect(document.querySelector(".accounts-tree-pane").style.minWidth).toBe(`${tableMinWidth(accountColumns(document)) + INDICATOR_WIDTH + 2}px`);
  });
});

test("footer Refresh reloads the accounts, refits the table, and keeps the selected row", async () => {
  await withView(async (callSpy) => {
    await addSavedAccount();
    expect(document.querySelector('[data-account-id="saved-one"]').classList.contains("tabulator-selected")).toBe(true);
    const lists = () => callSpy.mock.calls.filter(([c]) => c === "account.list").length;
    const before = lists();
    globalThis.__lastTableRefits = 0;
    const refits = 0;
    document.querySelector('#accounts-footer button[data-module-action="refresh"]').click();
    await vi.waitFor(() => expect(globalThis.__lastTableRefits).toBe(refits + 1));
    expect(lists()).toBe(before + 1);
    expect(document.querySelector('[data-account-id="saved-one"]').classList.contains("tabulator-selected")).toBe(true);
    expect(document.querySelector("#account-name-input").value).toBe("Saved One");
  });
});

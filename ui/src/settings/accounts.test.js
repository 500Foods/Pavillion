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
vi.mock("../common/table.js", () => ({
  createTable(el, options) {
    const draw = (data) => {
      el.replaceChildren();
      for (const row of data) {
        const button = document.createElement("button");
        button.type = "button";
        button.dataset.accountId = row.id;
        if (options.rowClass && options.rowClass(row)) {
          button.classList.add(options.rowClass(row));
        }
        for (const column of options.columns) {
          const cell = { getValue: () => row[column.field], getRow: () => ({ getData: () => row }) };
          const out = column.formatter ? column.formatter(cell) : String(row[column.field] ?? "");
          const span = document.createElement("span");
          span.dataset.field = column.field;
          span.append(out);
          button.append(span);
        }
        button.addEventListener("click", () => options.onRowClick(row));
        el.append(button);
      }
    };
    draw(options.data);
    return { setData: draw, destroy() { el.replaceChildren(); } };
  },
}));

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

    const confirmMock = vi.spyOn(window, "confirm").mockReturnValue(true);
    document.querySelector("#account-remove").click();

    await vi.waitFor(() => {
      expect(document.querySelector("#accounts-tree").textContent).toContain("No accounts yet.");
    });
    confirmMock.mockRestore();
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

async function withView(body) {
  const el = document.createElement("div");
  document.body.append(el);
  try {
    const transport = mockTransport();
    const callSpy = vi.spyOn(transport, "call");
    const api = createApi(transport);
    await mount(el, api, {});
    openAccountsPanel();
    await body(callSpy);
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

test("the table row shows the swatch, J/I/S badges, and features after a Test", async () => {
  await withView(async () => {
    await addSavedAccount();
    const row = () => document.querySelector('[data-account-id="saved-one"]');
    expect(row().querySelector(".account-swatch .fa-envelope")).not.toBeNull();
    const badges = [...row().querySelectorAll(".accounts-badge")].map((b) => [b.textContent, b.classList.contains("is-disabled")]);
    expect(badges).toEqual([["J", false], ["I", true], ["S", true]]);
    expect(row().classList.contains("pav-row-selected")).toBe(true);
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
  });
});

test("the icon chooser stores a Font Awesome snippet and Save sends it", async () => {
  await withView(async (callSpy) => {
    await addSavedAccount();
    const choices = document.querySelectorAll("#account-icon-choices button[data-icon]");
    expect(choices.length).toBeGreaterThan(4);
    expect(document.querySelector('#account-icon-choices [data-icon="envelope"]').getAttribute("aria-checked")).toBe("true");
    document.querySelector('#account-icon-choices [data-icon="briefcase"]').click();
    expect(document.querySelector('#account-icon-choices [data-icon="briefcase"]').getAttribute("aria-checked")).toBe("true");
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
  });
});

/* K-Q10: the stored capabilities survive a reload (unmount, mount again on
   the same core), with the checked time as the feature tooltip. */
test("the table ticks stored capabilities after a reload", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  const transport = mockTransport();
  const box = (field) => document.querySelector(`[data-account-id="saved-one"] [data-field="${field}"] input[type="checkbox"]`);
  const tip = () => document.querySelector('[data-account-id="saved-one"] [data-field="mail"] .accounts-feature-cell').title;
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
    expect(tip()).toMatch(/^Checked \d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}$/);
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

/**
 * @vitest-environment jsdom
 */
import { expect, test, vi } from "vitest";
import { createApi } from "../api.js";
import { mount, unmount } from "../index.js";
import { mockTransport } from "../transport/mock.js";

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

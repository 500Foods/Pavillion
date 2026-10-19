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

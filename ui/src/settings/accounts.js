import { createTable, INDICATOR_WIDTH } from "../common/table.js";
import { accountSwatch, ICON_CHOICES, iconChoice, iconSnippet } from "./account-icon.js";
import { moduleFooter, moduleToolbar, mountBar, paintFooterNav } from "../common/module-bar.js";
import { openColumnChooser } from "../common/column-chooser.js";
import {
  accountColumns,
  accountMatches,
  accountRows,
  accountsCsv,
  CHOOSER_COLUMNS,
  cleanHidden,
  cloneAccount,
  navTarget,
  serverGroupHeader,
  tableMinWidth,
} from "./accounts-table.js";

export function createAccountsView(api, root, options = {}) {
  let table = null;
  let footerEl = null;
  let toolbarEl = null;
  /* D-10: the toolbar search text. */
  let query = "";
  /* K-Q10: an unsaved form keeps its last verified Test and sends it on Add. */
  let formCaps = null;
  const timeZone = () => (typeof options.timeZone === "function" ? options.timeZone() : options.timeZone);
  /* K-Q7: the inline note after Detect or Test saved the session URL. */
  let urlNote = "";
  let accounts = [];
  let selectedId = null;
  let testPending = false;
  let testResult = { ok: false, capabilities: [], error: "" };
  /* Email and Detect stay in the form; they are not account fields. */
  let email = "";
  let emailTouched = false;
  let detectPending = false;
  let detectResult = { text: "", error: "" };
  const formState = {
    name: "",
    sessionUrl: "",
    username: "",
    secret: "",
    color: "#2a7",
    icon: "",
    kind: "jmap",
    auth: "basic",
    syncMail: true,
    syncContacts: true,
    syncCalendars: true,
    fetchBodies: "recent",
    enabled: true,
    caFile: "",
  };

  function textContent(el, text) {
    if (el) {
      el.textContent = typeof text === "string" ? text : String(text ?? "");
    }
  }

  function clearForm() {
    email = "";
    emailTouched = false;
    formCaps = null;
    detectResult = { text: "", error: "" };
    formState.name = "";
    formState.sessionUrl = "";
    formState.username = "";
    formState.secret = "";
    formState.color = "#2a7";
    formState.icon = "";
    urlNote = "";
    formState.kind = "jmap";
    formState.auth = "basic";
    formState.syncMail = true;
    formState.syncContacts = true;
    formState.syncCalendars = true;
    formState.fetchBodies = "recent";
    formState.enabled = true;
    formState.caFile = "";
    selectedId = null;
    testResult = { ok: false, capabilities: [], error: "" };
  }

  function selectedAccount() {
    return accounts.find((a) => a.id === selectedId) || null;
  }

  function formValue(key) {
    return formState[key];
  }

  function looksLikeEmail(value) {
    return typeof value === "string" && /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(value.trim());
  }

  /* The Email field follows the username until the user types in it. */
  function effectiveEmail() {
    if (emailTouched) {
      return email;
    }
    return looksLikeEmail(formState.username) ? formState.username.trim() : email;
  }

  /* A blank password is fine when the selected account has one saved. */
  function isTestReady() {
    return Boolean(formState.sessionUrl) && Boolean(formState.username) && (Boolean(formState.secret) || Boolean(selectedId));
  }

  function isSaveReady() {
    if (!formState.name || formState.name.trim() === "") {
      return false;
    }
    if (!isTestReady()) {
      return false;
    }
    return testResult.ok;
  }

  function showBanner(err) {
    const banner = root.querySelector("#banner");
    if (banner) {
      banner.hidden = false;
      banner.textContent = err instanceof Error ? err.message : String(err ?? "");
    }
  }

  function paintHeader() {
    const header = root.querySelector("#accounts-form-header");
    if (!header) {
      return;
    }
    const selected = selectedAccount();
    textContent(header.querySelector("#account-form-title"), selected ? selected.name || selected.id : "Add account");
    textContent(header.querySelector("#account-form-number"), selected ? selected.number || "" : "");
    const swatchHost = header.querySelector("#account-form-swatch");
    if (swatchHost) {
      swatchHost.replaceChildren(accountSwatch({ icon: formState.icon, color: formState.color }, document));
    }
  }

  function selectAccount(id) {
    formCaps = null;
    const selected = accounts.find((a) => a.id === id);
    if (!selected) {
      return;
    }
    selectedId = id;
    syncFormState(selected);
    detectResult = { text: "", error: "" };
    urlNote = "";
    clearSecretField();
    testResult = { ok: false, capabilities: [], error: "" };
    if (table) {
      table.select(id);
      paintNavState();
    } else {
      paintTree();
    }
    paintHeader();
    paintForm();
  }

  /* K-Q8, K-Q12: the accounts table; a row click, the navigator, or Up and
     Down select a row and load the form. */
  function paintTree() {
    const treeEl = root.querySelector("#accounts-tree");
    if (!treeEl) {
      return;
    }
    if (accounts.length === 0) {
      if (table) {
        table.destroy();
        table = null;
      }
      treeEl.replaceChildren();
      const empty = document.createElement("p");
      empty.className = "accounts-empty";
      empty.textContent = "No accounts yet.";
      treeEl.append(empty);
      return;
    }
    const rows = accountRows(accounts, timeZone());
    if (!table) {
      treeEl.replaceChildren();
      const columns = accountColumns(document, hiddenColumns());
      sizePane(columns);
      table = createTable(treeEl, {
        indicator: true,
        columns,
        data: rows,
        rowId: "id",
        onRowClick: (row) => selectAccount(row.id),
        groupBy: "server",
        groupHeader: (value, count) => serverGroupHeader(value, count, document),
        groupOrder: (data) => [...new Set(data.map((row) => row.server))],
        select: true,
        filter: true,
        onRender: paintNavState,
      });
      if (query !== "") {
        table.setFilter((row) => accountMatches(row, query));
      }
      if (!treeEl.dataset.keys) {
        treeEl.dataset.keys = "1";
        treeEl.addEventListener("keydown", onTreeKey);
      }
    } else {
      table.setData(rows);
    }
    table.select(selectedId);
    paintNavState();
  }

  /* D-10: the module footer navigator, Export, and Clone follow the shown
     rows and the selection. */
  /* Column chooser (TABULATOR.md): hidden fields from preferences.ui.tables.accounts. */
  function hiddenColumns() {
    return cleanHidden(typeof options.tableHidden === "function" ? options.tableHidden("accounts") : []);
  }

  function sizePane(columns) {
    const pane = root.querySelector(".accounts-tree-pane");
    if (pane) {
      pane.style.minWidth = `${tableMinWidth(columns) + INDICATOR_WIDTH + 2}px`;
    }
  }

  function chooseColumns(anchor) {
    openColumnChooser(anchor, {
      label: "Account columns",
      columns: CHOOSER_COLUMNS,
      hidden: hiddenColumns(),
      onChange(hidden) {
        const next = cleanHidden(hidden);
        if (typeof options.saveTableHidden === "function") {
          options.saveTableHidden("accounts", next);
        }
        if (table) {
          for (const column of CHOOSER_COLUMNS) {
            if (!column.locked) {
              table.setColumnVisible(column.field, !next.includes(column.field));
            }
          }
        }
        sizePane(accountColumns(document, next));
      },
    });
  }

  function paintNavState() {
    const ids = table ? table.ids() : [];
    if (footerEl) {
      paintFooterNav(footerEl, ids, selectedId);
      const exportButton = footerEl.querySelector('button[data-module-action="export"]');
      if (exportButton) {
        exportButton.disabled = ids.length === 0;
      }
    }
    for (const id of ["#account-clone", "#account-delete"]) {
      const button = toolbarEl && toolbarEl.querySelector(id);
      if (button) {
        button.disabled = !selectedAccount();
      }
    }
  }

  function applySearch(value) {
    query = String(value || "");
    if (table) {
      table.setFilter(query.trim() === "" ? null : (row) => accountMatches(row, query));
    }
  }

  /* D-10 Clone: the selected account minus username, password, id, number,
     and capabilities, opened unsaved in the form for Add. Nothing secret is
     read: account rows never carry the password. */
  function cloneSelected() {
    const selected = selectedAccount();
    if (!selected) {
      return;
    }
    const copy = cloneAccount(selected);
    clearForm();
    clearSecretField();
    syncFormState({ ...copy, username: "" });
    if (table) {
      table.select(null);
    }
    paintHeader();
    paintForm();
    paintNavState();
  }

  /* D-10 (D-Q21) Add: a blank unsaved form; the form's Add creates it. */
  function newAccount() {
    clearForm();
    clearSecretField();
    if (table) {
      table.select(null);
    }
    paintHeader();
    paintForm();
    paintNavState();
    const name = root.querySelector("#account-name-input");
    if (name && typeof name.focus === "function") {
      name.focus();
    }
  }

  /* D-10 (D-Q21) Delete: confirm, then account.remove with purgeLocal; the
     core deletes the stored secret with the account. */
  async function removeSelected() {
    const selected = selectedAccount();
    if (!selected) {
      return;
    }
    const name = selected.name || selected.id;
    const ask = typeof options.confirm === "function"
      ? options.confirm
      : (text) => Promise.resolve(typeof window.confirm === "function" ? window.confirm(text) : true);
    const ok = await ask(`Delete ${name}? Its saved password and local mail and data are deleted.`, name);
    if (!ok) {
      return;
    }
    try {
      await api.account.remove(selected.id, true);
      clearForm();
      await refresh();
    } catch (err) {
      showBanner(err);
    }
  }

  function bindBars() {
    toolbarEl = mountBar(root.querySelector("#accounts-toolbar"), moduleToolbar({
      title: "Accounts",
      actions: [
        { id: "account-new", action: "add", icon: "fa-plus", text: "Add", label: "Add an account", onClick: newAccount },
        { id: "account-clone", action: "clone", icon: "fa-clone", text: "Clone", label: "Clone the selected account", onClick: cloneSelected },
        { id: "account-delete", action: "delete", icon: "fa-trash", text: "Delete", label: "Delete the selected account", onClick: () => void removeSelected() },
      ],
      tools: [{ id: "accounts-columns", action: "columns", icon: "fa-table-columns", label: "Columns", onClick: () => chooseColumns(root.querySelector("#accounts-columns")) }],
      search: { id: "accounts-search", label: "Search accounts", placeholder: "Search accounts", onInput: applySearch },
    }, document));
    footerEl = mountBar(root.querySelector("#accounts-footer"), moduleFooter({
      label: "Account actions",
      noun: "account",
      onRefresh: () => void refresh(),
      onNav: navigate,
      end: [{ action: "export", icon: "fa-file-export", text: "Export", label: "Export the account list as CSV", onClick: () => void exportCsv() }],
    }, document));
    paintNavState();
  }

  function navigate(action) {
    if (!table) {
      return;
    }
    const id = navTarget(table.ids(), selectedId, action);
    if (id !== null && id !== selectedId) {
      selectAccount(id);
    }
  }

  function onTreeKey(event) {
    if (event.target instanceof HTMLInputElement || event.target instanceof HTMLButtonElement) {
      return;
    }
    if (event.key === "ArrowUp" || event.key === "ArrowDown") {
      event.preventDefault();
      navigate(event.key === "ArrowUp" ? "prev" : "next");
    }
  }

  /* K-Q12: CSV of the visible columns plus server and email; never secrets. */
  /* D-10: the shown rows in display order, as CSV, to a path from the host
     save dialog, written by the core (export.write_text). */
  async function exportCsv() {
    const host = options.host;
    if (!table || !host || !host.dialog || typeof host.dialog.save !== "function") {
      showBanner("Export is not available");
      return;
    }
    const byId = new Map(accountRows(accounts, timeZone()).map((row) => [row.id, row]));
    const rows = table.ids().map((id) => byId.get(id)).filter(Boolean);
    let path;
    try {
      path = await host.dialog.save({
        title: "Export accounts",
        defaultPath: "pavillion-accounts.csv",
        filters: [{ name: "CSV", extensions: ["csv"] }],
      });
      if (!path) {
        return;
      }
      await api.exports.writeText({ path, text: accountsCsv(rows) });
    } catch (err) {
      showBanner(err);
      return;
    }
    const count = rows.length;
    void Promise.resolve(api.log && api.log.client
      ? api.log.client({ subsystem: "accounts", level: "info", message: `exported ${count} ${count === 1 ? "account" : "accounts"} to CSV` })
      : null).catch(() => {});
  }

  /* K-Q9: the interim icon chooser beside Color, drawn on the account color. */
  function paintIconChoices(formEl) {
    const host = formEl.querySelector("#account-icon-choices");
    if (!host) {
      return;
    }
    host.replaceChildren();
    const current = iconChoice(formState.icon);
    for (const name of ICON_CHOICES) {
      const button = document.createElement("button");
      button.type = "button";
      button.dataset.icon = name;
      button.setAttribute("role", "radio");
      button.setAttribute("aria-checked", name === current ? "true" : "false");
      button.setAttribute("aria-label", name);
      button.title = name;
      button.append(accountSwatch({ icon: iconSnippet(name), color: formState.color }, document));
      host.append(button);
    }
  }

  function paintForm() {
    const formEl = root.querySelector("#accounts-form");
    if (!formEl) {
      return;
    }
    const selected = selectedAccount();
    const editing = Boolean(selected);

    const nameInput = formEl.querySelector("#account-name-input");
    if (nameInput) {
      nameInput.value = formValue("name") || "";
    }
    textContent(formEl.querySelector("#account-number"), formValue("number"));

    const sessionUrlInput = formEl.querySelector("#account-session-url-input");
    if (sessionUrlInput) {
      sessionUrlInput.value = formValue("sessionUrl") || "";
    }
    const usernameInput = formEl.querySelector("#account-username-input");
    if (usernameInput) {
      usernameInput.value = formValue("username") || "";
    }
    const caFileInput = formEl.querySelector("#account-ca-file-input");
    if (caFileInput) {
      caFileInput.value = formValue("caFile") || "";
    }
    const colorInput = formEl.querySelector("#account-color-input");
    if (colorInput) {
      colorInput.value = formValue("color") || "#2a7";
    }
    const kindSelect = formEl.querySelector("#account-kind");
    if (kindSelect) {
      kindSelect.value = formValue("kind") || "jmap";
    }

    formEl.querySelector("#account-sync-mail").checked = formValue("syncMail") !== false;
    formEl.querySelector("#account-sync-contacts").checked = formValue("syncContacts") !== false;
    formEl.querySelector("#account-sync-calendars").checked = formValue("syncCalendars") !== false;
    formEl.querySelector("#account-fetch-bodies").value = formValue("fetchBodies") || "recent";
    formEl.querySelector("#account-enabled").checked = formValue("enabled") !== false;

    const testBtn = formEl.querySelector("#account-test");
    testBtn.disabled = testPending || !isTestReady();
    if (testPending) {
      textContent(formEl.querySelector("#account-test-status"), "Testing…");
    } else if (testResult.ok) {
      textContent(formEl.querySelector("#account-test-status"), "OK");
    } else {
      textContent(formEl.querySelector("#account-test-status"), "");
    }

    const saveBtn = formEl.querySelector("#account-save");
    saveBtn.disabled = !isSaveReady();

    const capabilitiesEl = formEl.querySelector("#account-test-capabilities");
    if (testResult.ok) {
      capabilitiesEl.textContent = testResult.capabilities.join(", ");
    } else {
      capabilitiesEl.textContent = "";
    }

    const errorEl = formEl.querySelector("#account-test-error");
    errorEl.textContent = testResult.error || "";

    const addBtn = formEl.querySelector("#account-add");
    const removeBtn = formEl.querySelector("#account-remove");
    addBtn.hidden = editing;
    removeBtn.hidden = !editing;
    removeBtn.disabled = !editing;

    const credentialsEl = formEl.querySelector("#account-credentials-status");
    if (!editing) {
      credentialsEl.textContent = "Not saved yet";
    } else if (formState.secret) {
      credentialsEl.textContent = "Replaces the saved password";
    } else {
      credentialsEl.textContent = "Saved in keyring";
    }

    const emailInput = formEl.querySelector("#account-email-input");
    if (emailInput && emailInput.value !== effectiveEmail()) {
      emailInput.value = effectiveEmail();
    }
    const detectBtn = formEl.querySelector("#account-detect");
    if (detectBtn) {
      detectBtn.disabled = detectPending || !looksLikeEmail(effectiveEmail());
    }
    textContent(formEl.querySelector("#account-detect-status"), detectPending ? "Detecting…" : detectResult.text);
    textContent(formEl.querySelector("#account-detect-error"), detectResult.error);
    textContent(formEl.querySelector("#account-url-note"), urlNote);
    paintIconChoices(formEl);
    paintHeader();
  }

  function updateFormState() {
    const nameInput = root.querySelector("#account-name-input");
    const sessionUrlInput = root.querySelector("#account-session-url-input");
    const usernameInput = root.querySelector("#account-username-input");
    const colorInput = root.querySelector("#account-color-input");
    const secretInput = root.querySelector("#account-secret-input");
    const kindSelect = root.querySelector("#account-kind");
    const caFileInput = root.querySelector("#account-ca-file-input");
    const emailInput = root.querySelector("#account-email-input");
    if (emailInput && emailInput.value !== effectiveEmail()) {
      email = emailInput.value;
      emailTouched = true;
    }
    if (nameInput) {
      formState.name = nameInput.value;
    }
    if (sessionUrlInput) {
      formState.sessionUrl = sessionUrlInput.value;
    }
    if (usernameInput) {
      formState.username = usernameInput.value;
    }
    if (colorInput) {
      formState.color = colorInput.value;
    }
    if (kindSelect) {
      formState.kind = kindSelect.value;
    }
    if (caFileInput) {
      formState.caFile = caFileInput.value.trim();
    }
    if (secretInput) {
      formState.secret = secretInput.value;
    }
  }

  function syncFormState(selected) {
    if (selected) {
      formState.name = selected.name;
      formState.sessionUrl = selected.sessionUrl;
      formState.username = selected.username;
      formState.color = selected.color || "#2a7";
      formState.icon = typeof selected.icon === "string" ? selected.icon : "";
      formState.kind = selected.kind || "jmap";
      formState.auth = selected.auth || "basic";
      formState.syncMail = selected.syncMail !== false;
      formState.syncContacts = selected.syncContacts !== false;
      formState.syncCalendars = selected.syncCalendars !== false;
      formState.fetchBodies = selected.fetchBodies || "recent";
      formState.enabled = selected.enabled !== false;
      formState.caFile = selected.caFile || "";
      email = selected.email || "";
      emailTouched = Boolean(selected.email);
    }
  }

  async function refresh() {
    try {
      const result = await api.account.list();
      accounts = result.accounts || [];
      if (selectedId && !accounts.find((a) => a.id === selectedId)) {
        clearForm();
      }
      paintTree();
      paintHeader();
      paintForm();
      if (!selectedId && accounts.length > 0) {
        selectedId = accounts[0].id;
        syncFormState(accounts[0]);
        if (table) {
          table.select(selectedId);
        }
        paintHeader();
        paintForm();
      }
      paintNavState();
    } catch (err) {
      showBanner(err);
    }
  }

  function clearSecretField() {
    const input = root.querySelector("#account-secret-input");
    if (input) {
      input.value = "";
    }
    formState.secret = "";
  }

  function bindAdd() {
    const addBtn = root.querySelector("#account-add");
    if (!addBtn) {
      return;
    }
    addBtn.addEventListener("click", async () => {
        updateFormState();
        try {
          const body = { ...formState };
          body.email = effectiveEmail().trim();
          if (!body.caFile) {
            delete body.caFile;
          }
          if (formCaps) {
            body.capabilities = formCaps.capabilities;
            body.capabilitiesChecked = formCaps.checked;
          }
          await api.account.add(body);
          clearForm();
          clearSecretField();
          await refresh();
        } catch (err) {
          showBanner(err);
        }
      });
  }

  function bindSave() {
    const saveBtn = root.querySelector("#account-save");
    if (!saveBtn) {
      return;
    }
    saveBtn.addEventListener("click", async () => {
      if (!selectedId) {
        return;
      }
      updateFormState();
      try {
        await api.account.update(selectedId, {
          name: formState.name,
          sessionUrl: formState.sessionUrl,
          username: formState.username,
          email: effectiveEmail().trim(),
          color: formState.color,
          icon: formState.icon,
          syncMail: formState.syncMail,
          syncContacts: formState.syncContacts,
          syncCalendars: formState.syncCalendars,
          fetchBodies: formState.fetchBodies,
          enabled: formState.enabled,
          caFile: formState.caFile,
        });
        await refresh();
        clearSecretField();
        const selected = accounts.find((a) => a.id === selectedId);
        if (selected) {
          syncFormState(selected);
          paintHeader();
          paintForm();
        }
      } catch (err) {
        showBanner(err);
      }
    });
  }

  function bindRemove() {
    const removeBtn = root.querySelector("#account-remove");
    if (!removeBtn) {
      return;
    }
    removeBtn.addEventListener("click", () => void removeSelected());
  }

  /* K-Q7: store a new session URL at once on an existing account (a new
     account keeps it in the form and saves it on Add). */
  async function storeSessionUrl(url) {
    formState.sessionUrl = url;
    if (!selectedId) {
      return false;
    }
    await api.account.update(selectedId, { sessionUrl: url });
    accounts = accounts.map((a) => (a.id === selectedId ? { ...a, sessionUrl: url } : a));
    return true;
  }

  async function testOnce(sessionUrl, secret) {
    return api.account.test({
      sessionUrl,
      username: formState.username,
      auth: formState.auth,
      secret,
      ...(selectedId && !secret ? { id: selectedId } : {}),
      ...(formState.caFile ? { caFile: formState.caFile } : {}),
    });
  }

  async function runTest() {
    updateFormState();
    const secret = formState.secret;
    if (!isTestReady()) {
      return;
    }
    testPending = true;
    urlNote = "";
    testResult = { ok: false, capabilities: [], error: "" };
    paintForm();
    try {
      let result = await testOnce(formState.sessionUrl, secret);
      /* K-Q7: a redirect ended elsewhere: keep the final URL, and when the
         hop changed host (Authorization dropped) test it once more so the
         password is checked. */
      if (typeof result.sessionUrl === "string" && result.sessionUrl !== "" && result.sessionUrl !== formState.sessionUrl) {
        const saved = await storeSessionUrl(result.sessionUrl);
        urlNote = saved ? "Session URL updated" : "Session URL updated; saved on Add";
        if (result.authSent === false) {
          result = await testOnce(formState.sessionUrl, secret);
        }
      }
      testResult = { ok: true, capabilities: result.capabilities || [], error: "" };
      if (selectedId) {
        /* K-Q10: the core stored a verified result; reload the list. */
        if (result.authSent !== false) {
          await refresh();
        }
      } else if (result.authSent !== false) {
        formCaps = { capabilities: testResult.capabilities, checked: new Date().toISOString() };
      }
    } catch (err) {
      testResult = { ok: false, capabilities: [], error: err instanceof Error ? err.message : String(err ?? "") };
    } finally {
      testPending = false;
      paintForm();
    }
  }

  async function runDetect() {
    updateFormState();
    const address = effectiveEmail().trim();
    if (!looksLikeEmail(address) || detectPending) {
      return;
    }
    detectPending = true;
    detectResult = { text: "", error: "" };
    urlNote = "";
    paintForm();
    try {
      const result = await api.account.discover({
        email: address,
        ...(formState.caFile ? { caFile: formState.caFile } : {}),
      });
      const via = `Found via ${result.via === "srv" ? "SRV" : "well-known"}`;
      testResult = { ok: false, capabilities: [], error: "" };
      const saved = await storeSessionUrl(result.sessionUrl || "");
      detectResult = { text: saved ? `${via} · Saved` : via, error: "" };
    } catch (err) {
      detectResult = { text: "", error: err instanceof Error ? err.message : String(err ?? "") };
    } finally {
      detectPending = false;
      paintForm();
    }
  }

  function bindDetect() {
    const detectBtn = root.querySelector("#account-detect");
    if (detectBtn) {
      detectBtn.addEventListener("click", (event) => {
        event.preventDefault();
        void runDetect();
      });
    }
  }

  function bindTest() {
    const testBtn = root.querySelector("#account-test");
    if (testBtn) {
      testBtn.addEventListener("click", () => void runTest());
    }
  }

  function bindSecretInput() {
    const secretInput = root.querySelector("#account-secret-input");
    if (secretInput) {
      secretInput.addEventListener("keydown", (event) => {
        if (event.key === "Enter") {
          event.preventDefault();
          void runTest();
        }
      });
    }
  }

  function bindFormInputs() {
    const formEl = root.querySelector("#accounts-form");
    if (!formEl) {
      return;
    }
    formEl.addEventListener("click", (event) => {
      const choice = event.target instanceof Element ? event.target.closest("button[data-icon]") : null;
      if (!choice || !formEl.contains(choice)) {
        return;
      }
      event.preventDefault();
      formState.icon = iconSnippet(choice.dataset.icon);
      paintForm();
    });
    formEl.addEventListener("input", () => {
      updateFormState();
      paintForm();
    });
    formEl.addEventListener("change", () => {
      updateFormState();
      paintForm();
    });
  }

  bindBars();
  bindAdd();
  bindSave();
  bindRemove();
  bindTest();
  bindDetect();
  bindSecretInput();
  bindFormInputs();

  return {
    refresh,
    destroy() {
      if (table) {
        table.destroy();
        table = null;
      }
      clearForm();
    },
  };
}

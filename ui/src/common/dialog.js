function textOf(value) {
  return typeof value === "string" ? value : "";
}

function focusable(dialog) {
  return [...dialog.querySelectorAll("button, input, textarea, select")].filter((el) => !el.disabled);
}

function show(dialog) {
  if (typeof dialog.showModal === "function") {
    try {
      dialog.showModal();
      return;
    } catch {
      /* jsdom and a second showModal both land here. The open attribute still displays it. */
    }
  }
  dialog.setAttribute("open", "");
}

export function createDialogs(root) {
  if (!(root instanceof Element)) {
    throw new Error("dialog requires an element");
  }
  let current = null;

  function finishCurrent() {
    if (current) {
      current.finish(current.cancelValue);
    }
  }

  function open({ title, text, okLabel, danger, field }) {
    return new Promise((resolve) => {
    finishCurrent();
    const previous = root.ownerDocument.activeElement;
    const dialog = document.createElement("dialog");
    dialog.className = "pav-dialog";
    const heading = document.createElement("h2");
    heading.className = "pav-dialog-title";
    heading.id = "pav-dialog-title";
    heading.textContent = textOf(title);
    dialog.append(heading);
    dialog.setAttribute("aria-labelledby", heading.id);
    if (textOf(text) !== "") {
      const body = document.createElement("p");
      body.className = "pav-dialog-text";
      body.textContent = textOf(text);
      dialog.append(body);
    }
    let input = null;
    if (field) {
      input = document.createElement("input");
      input.type = "text";
      input.className = "pav-dialog-input";
      input.value = textOf(field.value);
      input.setAttribute("aria-label", textOf(title) || "Value");
      input.autocomplete = "off";
      dialog.append(input);
    }
    const form = document.createElement("form");
    form.className = "pav-dialog-actions";
    const cancel = document.createElement("button");
    cancel.type = "button";
    cancel.className = "pav-dialog-cancel";
    cancel.textContent = "Cancel";
    const ok = document.createElement("button");
    ok.type = "submit";
    ok.className = "pav-dialog-ok";
    if (danger) {
      ok.classList.add("is-danger");
    }
    ok.textContent = textOf(okLabel) || "OK";
    form.append(cancel, ok);
    dialog.append(form);
    root.append(dialog);

    let settled = false;
    const session = {
      cancelValue: field ? null : false,
      finish(value) {
        if (settled) {
          return;
        }
        settled = true;
        if (current === session) {
          current = null;
        }
        dialog.removeEventListener("cancel", onCancel);
        dialog.removeEventListener("keydown", onKeyDown);
        try {
          if (typeof dialog.close === "function") {
            dialog.close();
          }
        } catch {
          dialog.removeAttribute("open");
        }
        dialog.remove();
        if (previous && previous.isConnected && previous !== dialog) {
          previous.focus();
        }
        resolve(value);
      },
    };

    function onCancel(event) {
      event.preventDefault();
      session.finish(field ? null : false);
    }

    function onKeyDown(event) {
      if (event.key === "Escape") {
        event.preventDefault();
        event.stopPropagation();
        session.finish(field ? null : false);
        return;
      }
      if (event.key !== "Tab") {
        return;
      }
      const items = focusable(dialog);
      if (items.length === 0) {
        event.preventDefault();
        return;
      }
      const first = items[0];
      const last = items[items.length - 1];
      const active = dialog.ownerDocument.activeElement;
      if (event.shiftKey && (active === first || !dialog.contains(active))) {
        event.preventDefault();
        last.focus();
      } else if (!event.shiftKey && (active === last || !dialog.contains(active))) {
        event.preventDefault();
        first.focus();
      }
    }

    dialog.addEventListener("cancel", onCancel);
    dialog.addEventListener("keydown", onKeyDown);
    cancel.addEventListener("click", () => {
      session.finish(field ? null : false);
    });
    form.addEventListener("submit", (event) => {
      event.preventDefault();
      session.finish(field ? input.value : true);
    });

    current = session;
    show(dialog);
    const initial = danger ? cancel : (input || ok);
    initial.focus();
    });
  }

  return {
    confirm(options = {}) {
      return open({
        title: options.title,
        text: options.text,
        okLabel: options.okLabel,
        danger: options.danger === true,
        field: null,
      });
    },
    prompt(options = {}) {
      return open({
        title: options.title,
        text: options.text,
        okLabel: options.okLabel,
        danger: false,
        field: { value: options.value },
      });
    },
    destroy() {
      finishCurrent();
    },
  };
}

export function createToaster(root) {
  if (!(root instanceof Element)) {
    throw new Error("toast requires an element");
  }
  const stack = document.createElement("div");
  stack.className = "pav-toasts";
  stack.setAttribute("aria-live", "polite");
  root.append(stack);
  const timers = new Set();
  const view = root.ownerDocument.defaultView;

  function toast(text, options = {}) {
    const item = document.createElement("div");
    item.className = "pav-toast";
    const message = document.createElement("p");
    message.textContent = typeof text === "string" ? text : "";
    item.append(message);
    let fired = false;
    let timer = 0;
    function dismiss() {
      if (timer) {
        view.clearTimeout(timer);
        timers.delete(timer);
        timer = 0;
      }
      item.remove();
    }
    const action = options && options.action;
    if (typeof action === "string" && action !== "") {
      const button = document.createElement("button");
      button.type = "button";
      button.textContent = action;
      button.addEventListener("click", () => {
        if (fired) {
          return;
        }
        fired = true;
        button.disabled = true;
        const fn = options.onAction;
        dismiss();
        if (typeof fn === "function") {
          fn();
        }
      });
      item.append(button);
    }
    const requested = options && options.timeoutMs;
    const timeout = Number.isFinite(requested) && requested >= 0 ? requested : 5000;
    if (timeout > 0) {
      timer = view.setTimeout(dismiss, timeout);
      timers.add(timer);
    }
    stack.append(item);
    return dismiss;
  }

  return {
    toast,
    destroy() {
      for (const timer of timers) {
        view.clearTimeout(timer);
      }
      timers.clear();
      stack.remove();
    },
  };
}

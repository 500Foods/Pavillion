/* Sandboxed srcdoc frame for outside HTML. The parent CSP is inherited as
   well. This WebKit ignores navigate-to and blocks a meta refresh because
   the frame is sandboxed. frame-src pav-link: is what lets a link click
   through. The handler answers 204 with an empty body, and this WebKit still
   replaces the srcdoc. A srcdoc has no URL to reload, so a load this pane
   did not write puts the same document back. allowRemote adds https to
   img-src only. The inherited parent img-src still blocks those loads. */

/* pending is 1 after this pane sets srcdoc. The matching load clears it.
   A later load is a navigation, and the caller writes the srcdoc again. */
export function htmlPaneAfterLoad(pending) {
  if (pending > 0) return { pending: 0, restore: false };
  return { pending: 1, restore: true };
}

const BASE_STYLE =
  ":root { color-scheme: dark; } body { margin: 0; font: 16px/1.45 system-ui, sans-serif; color: CanvasText; background: Canvas; } img { max-width: 100%; height: auto; }";

export function htmlPaneSrcdoc(html, options = {}) {
  const allowRemote = options.allowRemote === true;
  const img = allowRemote ? "pav-cid: data: https:" : "pav-cid: data:";
  const csp = `default-src 'none'; img-src ${img}; style-src 'unsafe-inline'; font-src data:; frame-src pav-link:`;
  const body = typeof html === "string" ? html : "";
  return `<!DOCTYPE html><html><head><meta http-equiv="Content-Security-Policy" content="${csp}"><style>${BASE_STYLE}</style></head><body>${body}</body></html>`;
}

export function createHtmlPane(el) {
  const frame = document.createElement("iframe");
  frame.className = "html-pane";
  frame.setAttribute("sandbox", "");
  frame.setAttribute("referrerpolicy", "no-referrer");
  frame.title = "Message";

  let html = "";
  let options;
  let pending = 0;
  let nonce = 0;
  let inserted = false;

  function paint(nextHtml, nextOptions) {
    html = typeof nextHtml === "string" ? nextHtml : "";
    options = nextOptions;
    nonce += 1;
    pending = 1;
    /* The srcdoc attribute stays after a navigation. Writing the same text
       again does not load it, so the comment makes each write differ. */
    const doc = htmlPaneSrcdoc(html, options).replace("</body>", `<!--${nonce}--></body>`);
    frame.srcdoc = doc;
    if (!inserted) {
      el.replaceChildren(frame);
      inserted = true;
    }
  }

  frame.addEventListener("load", () => {
    const next = htmlPaneAfterLoad(pending);
    if (next.restore) {
      paint(html, options);
      return;
    }
    pending = next.pending;
  });

  return {
    show(nextHtml, nextOptions) {
      paint(nextHtml, nextOptions);
    },
    clear() {
      paint("", undefined);
    },
  };
}

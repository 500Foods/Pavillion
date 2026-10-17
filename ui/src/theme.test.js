/**
 * @vitest-environment jsdom
 */
import { expect, test } from "vitest";
import { createApi } from "./api.js";
import { mount, unmount } from "./index.js";
import { iconFromHtml } from "./theme-catalog.js";
import { normalizeTheme, resolveTheme } from "./theme.js";
import { mockTransport } from "./transport/mock.js";

test("unknown theme values follow the system scheme", () => {
  expect(normalizeTheme("light")).toBe("light");
  expect(normalizeTheme("dark")).toBe("dark");
  expect(normalizeTheme("system")).toBe("system");
  expect(normalizeTheme("sepia")).toBe("system");
  expect(normalizeTheme("")).toBe("system");
  expect(resolveTheme("system", true)).toBe("light");
  expect(resolveTheme("system", false)).toBe("dark");
  expect(resolveTheme("dark", true)).toBe("dark");
  expect(resolveTheme("nope", true)).toBe("light");
});

test("an icon variable accepts one i element or a local image", () => {
  const node = iconFromHtml("<i class='fa-sharp-duotone fa-solid fa-gear'></i>");
  expect(node.tagName).toBe("I");
  expect(node.className).toBe("fa-sharp-duotone fa-solid fa-gear");
  expect(iconFromHtml("<i class='a'><b></b></i>")).toBeNull();
  expect(iconFromHtml("<img src='https://example.com/a.png'>")).toBeNull();
  expect(iconFromHtml("<img src='logo.svg'>").getAttribute("src")).toBe("pav-theme://logo.svg");
});

function installMedia(matches) {
  const listeners = new Set();
  const query = {
    matches,
    media: "(prefers-color-scheme: light)",
    addEventListener(type, fn) {
      if (type === "change") {
        listeners.add(fn);
      }
    },
    removeEventListener(type, fn) {
      if (type === "change") {
        listeners.delete(fn);
      }
    },
    emit() {
      for (const fn of listeners) {
        fn();
      }
    },
  };
  const previous = window.matchMedia;
  window.matchMedia = (media) => {
    if (media === query.media) {
      return query;
    }
    return {
      matches: false,
      media,
      addEventListener() {},
      removeEventListener() {},
    };
  };
  return {
    query,
    restore() {
      window.matchMedia = previous;
    },
  };
}

async function settle() {
  await new Promise((resolve) => {
    setTimeout(resolve, 0);
  });
}

function checkedTheme(root) {
  return root.querySelector('input[name="pav-theme"]:checked');
}

test("the theme list persists and system follows the scheme", async () => {
  const media = installMedia(false);
  const el = document.createElement("div");
  document.body.append(el);
  const transport = mockTransport();
  try {
    await transport.call("prefs.set", { theme: "sepia" });
    const themes = [];
    await mount(el, createApi(transport), {
      hostServices: {
        onTheme(resolved) {
          themes.push(resolved);
        },
      },
    });
    const root = el.querySelector(".pav-root");
    const note = root.querySelector("#theme-resolved");
    const radios = [...root.querySelectorAll('input[name="pav-theme"]')].map((input) => input.value);
    expect(radios).toEqual(["light", "dark", "system"]);
    expect(root.querySelector(".theme-divider")).toBeTruthy();
    const frame = root.querySelector(".theme-preview iframe");
    expect(frame.getAttribute("sandbox")).toBe("");
    expect(frame.getAttribute("srcdoc") || frame.srcdoc).toContain("pav-root");
    expect(checkedTheme(root).value).toBe("system");
    expect(root.dataset.theme).toBe("dark");
    expect(note.textContent).toBe("Following the system: dark.");
    expect(themes).toEqual(["dark"]);

    root.querySelector('input[value="light"]').click();
    await settle();
    expect(root.dataset.theme).toBe("light");
    expect(note.textContent).toBe("Light is in use.");
    expect(themes).toEqual(["dark", "light"]);
    const saved = transport.seen.filter((entry) => entry.cmd === "prefs.set" && entry.args.theme === "light");
    expect(saved).toHaveLength(1);
    expect((await transport.call("prefs.get")).theme).toBe("light");

    media.query.matches = true;
    media.query.emit();
    expect(root.dataset.theme).toBe("light");
    expect(checkedTheme(root).value).toBe("light");

    root.querySelector('input[value="system"]').click();
    await settle();
    expect(checkedTheme(root).value).toBe("system");
    expect(root.dataset.theme).toBe("light");
    expect(note.textContent).toBe("Following the system: light.");
    expect((await transport.call("prefs.get")).theme).toBe("system");

    media.query.matches = false;
    media.query.emit();
    expect(root.dataset.theme).toBe("dark");
    expect(note.textContent).toBe("Following the system: dark.");
    expect(checkedTheme(root).value).toBe("system");

    unmount();
    media.query.matches = true;
    media.query.emit();
    expect(el.querySelector(".pav-root")).toBeNull();
    expect(document.getElementById("pav-file-theme")).toBeNull();
  } finally {
    unmount();
    el.remove();
    media.restore();
  }
});

test("a file theme paints its icons in place and the folder field stays blank", async () => {
  const el = document.createElement("div");
  document.body.append(el);
  const transport = mockTransport();
  const warm = [
    "/* Title: Warm",
    "Author: Ada",
    "Version: 2",
    "Released: 2026-10-17",
    "*/",
    "color-scheme: dark;",
    "--pav-icon-mail: \"<i class='fa-sharp-duotone fa-solid fa-envelope fa-swap-opacity'></i>\";",
  ].join("\n");
  const opened = [];
  let picked = null;
  try {
    await transport.call("prefs.ui.set", {
      iconSource: "override",
      icons: {
        mail: "fa-sharp-duotone fa-solid fa-paper-plane-top fa-rotate-270 fa-flip-horizontal",
      },
    });
    await transport.call("themes.ensure", { files: [{ name: "Warm.theme", text: warm }] });
    await mount(el, createApi(transport), {
      hostServices: {
        dialog: {
          open(options) {
            opened.push(options);
            return Promise.resolve(picked);
          },
        },
      },
    });
    const root = el.querySelector(".pav-root");
    const folder = root.querySelector("#theme-folder");
    const bar = root.querySelector(".theme-bar");
    expect(bar.firstElementChild.id).toBe("theme-refresh");
    expect(bar.contains(folder)).toBe(true);
    expect(folder.value).toBe("");
    expect(folder.placeholder).toBe("Default:/pavillion");
    expect(root.querySelector("#theme-folder-note").hidden).toBe(true);
    expect(root.querySelector('[data-section="mail"] .icon').className).toContain("fa-flip-horizontal");

    const button = root.querySelector('[data-section="mail"]');
    const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
    svg.setAttribute("class", "icon");
    svg.dataset.iconDefinition = "fa-sharp-duotone fa-solid fa-paper-plane-top fa-rotate-270";
    Object.defineProperty(svg, "className", {
      configurable: true,
      get() {
        return "";
      },
      set() {
        throw new TypeError("Attempted to assign to readonly property.");
      },
    });
    button.querySelector(".icon").replaceWith(svg);

    root.querySelector('input[value="file:Warm.theme"]').click();
    await settle();
    const icon = root.querySelector('[data-section="mail"] .icon');
    expect(icon.tagName).toBe("I");
    expect(icon.className).toContain("fa-envelope");
    expect(root.dataset.theme).toBe("file");
    const saved = await transport.call("prefs.ui.get");
    expect(saved.themeChoice).toBe("file:Warm.theme");
    expect(saved.iconSource).toBe("override");
    expect((await transport.call("prefs.get")).theme).toBe("dark");

    root.querySelector('input[value="dark"]').click();
    await settle();
    expect(root.querySelector('[data-section="mail"] .icon').className).toContain("fa-flip-horizontal");
    expect(root.dataset.theme).toBe("dark");

    root.querySelector("#theme-folder-pick").click();
    await settle();
    expect(opened).toEqual([{ directory: true, title: "Themes folder" }]);
    expect(folder.value).toBe("");
    expect(transport.seen.filter((entry) => entry.cmd === "themes.folder.set")).toHaveLength(0);

    picked = "/data/themes";
    root.querySelector("#theme-folder-pick").click();
    await settle();
    expect(folder.value).toBe("/data/themes");
    expect(folder.placeholder).toBe("Default:/pavillion");

    folder.value = "  ";
    folder.dispatchEvent(new Event("change", { bubbles: true }));
    await settle();
    expect(folder.value).toBe("");
    expect(folder.placeholder).toBe("Default:/pavillion");
  } finally {
    unmount();
    el.remove();
  }
});

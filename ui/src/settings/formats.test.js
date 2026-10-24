// @vitest-environment jsdom
import { expect, test } from "vitest";
import { createFormatsSettings } from "./formats.js";

function mount() {
  const root = document.createElement("div");
  const toolbar = document.createElement("div");
  toolbar.id = "formats-toolbar";
  const body = document.createElement("div");
  body.id = "formats-body";
  const footer = document.createElement("div");
  footer.id = "formats-footer";
  root.append(toolbar, body, footer);
  document.body.append(root);
  return root;
}

function apiFor(prefs, ui) {
  return {
    prefs: {
      get: () => Promise.resolve(prefs),
      ui: { get: () => Promise.resolve(ui) },
    },
  };
}

const prefs = { timezone: "America/Vancouver", timeFormat: "24h" };

test("the pane loads defaults, previews, refuses nope, resets, and saves a chosen string", async () => {
  const root = mount();
  let saved = null;
  const settings = createFormatsSettings(apiFor(prefs, { formats: {} }), root, {
    timeFormat: "24h",
    getUi: () => ({ formats: {} }),
    onSave(next) {
      saved = next;
      return next;
    },
  });
  await settings.refresh();
  const time = root.querySelector('[aria-label="Time"]');
  const date = root.querySelector('[aria-label="Date"]');
  expect(time.value).toBe("HH:mm");
  expect(date.value).toBe("yyyy-MM-dd");
  expect(root.querySelector('[aria-label="Relative"]').value).toBe("long");
  expect(root.querySelector("#formats-zone").value).toBe("America/Vancouver");
  expect(root.querySelector("#formats-locale").value).toBe("");

  const dateNote = date.closest(".formats-block").querySelector(".formats-note").textContent;
  expect(dateNote).toContain("another year");
  expect(dateNote).toContain("yyyy-MM-dd is 2026-10-14");
  expect(dateNote).toContain("Custom");
  const dateDefault = [...date.closest(".formats-block").querySelector("select").options]
    .find((option) => option.value === "yyyy-MM-dd");
  expect(dateDefault.textContent).toBe("2026-10-14 · yyyy-MM-dd (default)");
  expect(root.querySelector("#formats-locale").closest(".formats-block").textContent).toContain("fr-CA");
  expect(root.querySelector("#formats-zone").closest(".formats-block").textContent).toContain("America/Vancouver");

  const timeDefaults = root.querySelector('[aria-label="Time defaults"]');
  expect([...timeDefaults.options].some((option) => option.textContent === "15:04 · HH:mm (default)")).toBe(true);
  expect([...timeDefaults.options].some((option) => option.value === "h:mm a")).toBe(true);
  timeDefaults.value = "h:mm a";
  timeDefaults.dispatchEvent(new Event("change"));
  expect(time.value).toBe("h:mm a");
  const muted = time.closest(".formats-row").querySelectorAll(".muted");
  expect(muted[1].textContent).toBe("Sample 3:04 PM");

  time.value = "HH:mm:ss";
  time.dispatchEvent(new Event("input"));
  expect(muted[1].textContent).toBe("Sample 15:04:05");
  expect(timeDefaults.value).toBe("HH:mm:ss");

  time.value = "nope";
  time.dispatchEvent(new Event("input"));
  const error = time.closest(".formats-row").querySelector(".formats-error");
  expect(error.textContent).toBe("Unknown token n");
  expect(muted[0].textContent).toBe("");
  expect(muted[1].textContent).toBe("");
  expect(await settings.apply()).toBeNull();
  expect(saved).toBeNull();

  time.closest(".formats-row").querySelector("button").click();
  expect(time.value).toBe("HH:mm");
  expect(saved).toBeNull();

  time.value = "HH:mm:ss";
  const chosen = await settings.apply();
  expect(chosen.formats.time).toBe("HH:mm:ss");
  expect(chosen.formats.date).toBeUndefined();
  expect(chosen.timezone).toBe("America/Vancouver");
  expect(saved).toEqual(chosen);

  time.value = "HH:mm";
  const cleared = await settings.apply();
  expect(cleared.formats.time).toBeUndefined();
  expect(saved.formats.time).toBeUndefined();
  settings.destroy();
  root.remove();
});

test("12 h marks its own default, and a saved pattern that is not a default stays chosen", async () => {
  const root = mount();
  const settings = createFormatsSettings(apiFor(
    { timezone: "America/Vancouver", timeFormat: "12h" },
    { formats: { time: "HH:mm:ss", date: "d MMM yyyy" } },
  ), root, {
    timeFormat: "12h",
    getUi: () => ({ formats: { time: "HH:mm:ss", date: "d MMM yyyy" } }),
  });
  await settings.refresh();
  const time = root.querySelector('[aria-label="Time"]');
  const date = root.querySelector('[aria-label="Date"]');
  expect(time.value).toBe("HH:mm:ss");
  expect(date.value).toBe("d MMM yyyy");
  const timeDefaults = root.querySelector('[aria-label="Time defaults"]');
  expect(timeDefaults.value).toBe("HH:mm:ss");
  expect([...timeDefaults.options].find((option) => option.value === "h:mm a").textContent).toBe("3:04 PM · h:mm a (default)");
  const dateDefaults = date.closest(".formats-block").querySelector("select");
  expect(dateDefaults.value).toBe("d MMM yyyy");
  expect([...dateDefaults.options].find((option) => option.value === "yyyy-MM-dd").textContent).toContain("(default)");

  time.closest(".formats-row").querySelector("button").click();
  expect(time.value).toBe("h:mm a");
  expect(timeDefaults.value).toBe("h:mm a");
  const relative = root.querySelector('[aria-label="Relative"]');
  relative.value = "narrow";
  relative.dispatchEvent(new Event("change"));
  relative.closest(".formats-row").querySelector("button").click();
  expect(relative.value).toBe("long");
  settings.destroy();
  root.remove();
});

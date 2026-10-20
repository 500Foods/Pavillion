/**
 * @vitest-environment jsdom
 */
import { expect, test } from "vitest";
import { accountSwatch, iconChoice, iconSnippet } from "./account-icon.js";
import { featureOn, FEATURES } from "./accounts-table.js";

test("interim icons are FA snippets; envelope is empty", () => {
  expect(iconSnippet("envelope")).toBe("");
  expect(iconSnippet("star")).toBe('<i class="fa-solid fa-star"></i>');
  expect(iconChoice("")).toBe("envelope");
  expect(iconChoice('<i class="fa-solid fa-star"></i>')).toBe("star");
  expect(iconChoice('<span class="fa-layers"></span>')).toBeNull();
});

test("the swatch draws on the account color and refuses unsafe markup", () => {
  const ok = accountSwatch({ icon: '<i class="fa-solid fa-star"></i>', color: "#123456" });
  expect(ok.style.backgroundColor).toBe("rgb(18, 52, 86)");
  expect(ok.querySelector("i").className).toContain("fa-star");
  const bad = accountSwatch({ icon: '<img src="x" onerror="alert(1)">', color: "#123456" });
  expect(bad.querySelector("img")).toBeNull();
  expect(bad.querySelector("i").className).toContain("fa-envelope");
});

test("a feature box needs the tested capability and the sync flag", () => {
  const mail = FEATURES.find((f) => f.key === "mail");
  const files = FEATURES.find((f) => f.key === "files");
  expect(featureOn({ syncMail: true }, mail, null)).toBe(false);
  expect(featureOn({ syncMail: true }, mail, ["mail"])).toBe(true);
  expect(featureOn({ syncMail: false }, mail, ["mail"])).toBe(false);
  expect(featureOn({}, files, ["files"])).toBe(true);
});

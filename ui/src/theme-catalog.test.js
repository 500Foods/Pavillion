import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { expect, test } from "vitest";
import {
  buildCatalog,
  effectiveChoice,
  isHiddenStarter,
  liveThemeCss,
  mapsEqual,
  parseHeader,
  previewDocument,
  themeMap,
} from "./theme-catalog.js";

const styles = join(dirname(fileURLToPath(import.meta.url)), "../public/styles");

test("default theme files match the built-in sheets", () => {
  const dark = themeMap(readFileSync(join(styles, "dark.css"), "utf8"));
  const light = themeMap(readFileSync(join(styles, "light.css"), "utf8"));
  const darkFile = themeMap(readFileSync(join(styles, "pavillion-dark.theme"), "utf8"));
  const lightFile = themeMap(readFileSync(join(styles, "pavillion-light.theme"), "utf8"));
  expect(mapsEqual(dark, darkFile)).toBe(true);
  expect(mapsEqual(light, lightFile)).toBe(true);
  expect(dark.get("color-scheme")).toBe("dark");
  expect(light.get("color-scheme")).toBe("light");
  expect(dark.get("--pav-icon-mail")).toContain("pav-system:mark");
  expect(dark.get("--pav-mark")).toBe("none");
  expect(dark.get("--pav-icon-kit")).toBe('"45801522ef"');
});

test("the header is the title, and duplicate titles keep the filename", () => {
  const header = parseHeader("/* Title: <b>Warm</b>\n   Author:\n   Version: 2\n   Released: 2026-01-01\n*/\n", "Warm.theme");
  expect(header.title).toBe("Warm");
  expect(header.author).toBe("Not found");
  expect(header.version).toBe("2");
  const rows = buildCatalog([
    { name: "One.theme", text: "/* Title: Warm\nAuthor: Ada\nVersion: 2\nReleased: 2026-01-01\n*/\n.pav-root { --pav-text: red; }" },
    { name: "Two.theme", text: "/* Title: Warm\nAuthor: Bea Lane With A Long Name\nVersion: 3\nReleased: 2026-02-02\n*/\n.pav-root { --pav-text: blue; }" },
    { name: "Plain.theme", text: ".pav-root { --pav-text: green; }" },
  ], false);
  const warm = rows.filter((row) => row.label.startsWith("Warm"));
  expect(warm.map((row) => row.label)).toEqual(["Warm (One.theme)", "Warm (Two.theme)"]);
  expect(warm[1].author).toBe("Bea Lane With A Long");
  expect(warm[1].authorFull).toBe("Bea Lane With A Long Name");
  const plain = rows.find((row) => row.id === "file:Plain.theme");
  expect(plain.label).toBe("Plain.theme");
  expect(plain.author).toBe("Not found");
  expect(plain.meta).toBe("Not found · Not found");
  const titledLight = buildCatalog([
    { name: "Other.theme", text: "/* Title: Light\nAuthor: Pavillion\nVersion: 1\nReleased: 2026-10-17\n*/\n.pav-root { --pav-text: red; }" },
  ], false);
  expect(titledLight.find((row) => row.id === "light").label).toBe("Light");
  expect(titledLight.find((row) => row.id === "file:Other.theme").label).toBe("Light (Other.theme)");
});

test("identical starter files stay hidden and an edit is marked custom", () => {
  const darkText = readFileSync(join(styles, "pavillion-dark.theme"), "utf8");
  const commented = darkText.replace("--pav-text:", "/* spaced */\n  --pav-text:");
  expect(isHiddenStarter("Pavillion Dark.theme", commented)).toBe(true);
  expect(isHiddenStarter("Other.theme", darkText)).toBe(false);
  const edited = darkText.replace("--pav-mark: none;", "--pav-mark: logo.svg;");
  const rows = buildCatalog([
    { name: "Pavillion Dark.theme", text: darkText },
    { name: "Pavillion Light.theme", text: readFileSync(join(styles, "pavillion-light.theme"), "utf8") },
    { name: "Copy.theme", text: darkText },
    { name: "Pavillion Dark.theme", text: edited },
  ], false);
  expect(rows.some((row) => row.id === "file:Pavillion Dark.theme" && row.label === "Pavillion Dark")).toBe(false);
  expect(rows.some((row) => row.id === "file:Copy.theme")).toBe(true);
  const custom = buildCatalog([{ name: "Pavillion Dark.theme", text: edited }], false);
  expect(custom.find((row) => row.id === "file:Pavillion Dark.theme").label).toBe("Pavillion Dark Custom");
  expect(effectiveChoice("file:Missing.theme", [])).toBe("system");
  expect(effectiveChoice("file:Pavillion Dark.theme", [{ name: "Pavillion Dark.theme", text: darkText }])).toBe("dark");
});

test("a preview cannot close its style tag, and live css keeps only variables", () => {
  const doc = previewDocument("/* </style><script>1</script> */ .x{--pav-text:red}</style>", "file");
  expect(doc).not.toContain("</style><script>");
  expect(doc).toContain("<\\/style><script>");
  const live = liveThemeCss(`
    /* @font-face { src: url("secret.woff2"); } */
    .pav-root { color-scheme: light; --pav-text: red; --pav-mark: url(logo.svg); }
    @font-face { src: url("Example.woff2"); }
    @font-face { src: url("https://example.com/a.woff2"); }
  `);
  expect(live).toContain("color-scheme: light;");
  expect(live).toContain("--pav-text: red;");
  expect(live).toContain('url("pav-theme://logo.svg")');
  expect(live).toContain('url("pav-theme://Example.woff2")');
  expect(live).toContain('url("")');
  expect(live).not.toContain("https://example.com");
  expect(live).not.toContain("secret.woff2");
  expect(live.startsWith('.pav-root[data-theme="file"]')).toBe(true);
});

export function normalizeTheme(value) {
  if (value === "dark" || value === "light" || value === "system") {
    return value;
  }
  return "system";
}

export function resolveTheme(choice, prefersLight) {
  const theme = normalizeTheme(choice);
  if (theme === "light" || theme === "dark") {
    return theme;
  }
  return prefersLight ? "light" : "dark";
}

export function schemeQuery() {
  if (typeof window === "undefined" || typeof window.matchMedia !== "function") {
    return null;
  }
  try {
    return window.matchMedia("(prefers-color-scheme: light)");
  } catch {
    return null;
  }
}

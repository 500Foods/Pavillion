/* Search a library. Font Awesome refreshes once when facebook is not stored as
   a brand. Material refreshes once when its catalog is empty. */

const REFRESH_IDS = new Set(["fontawesome", "material"]);

function iconRows(payload) {
  if (Array.isArray(payload)) {
    return payload;
  }
  return payload && Array.isArray(payload.icons) ? payload.icons : [];
}

function catalogFresh(id, rows) {
  if (id === "fontawesome") {
    return rows.some((row) => row && row.name === "facebook" && row.family === "brands");
  }
  return rows.length > 0;
}

export function createIconSearch(api) {
  const ready = new Set();
  return async function searchIcons(body) {
    const request = body && typeof body === "object" ? body : {};
    const id = typeof request.id === "string" ? request.id : "";
    const iconlib = api && api.iconlib;
    if (!iconlib || typeof iconlib.search !== "function") {
      throw new Error("Search failed.");
    }
    if (REFRESH_IDS.has(id) && !ready.has(id)) {
      const probe = id === "fontawesome"
        ? { id, query: "facebook", limit: 5 }
        : { id, query: "", limit: 1 };
      const rows = iconRows(await iconlib.search(probe));
      if (catalogFresh(id, rows)) {
        ready.add(id);
      } else if (typeof iconlib.refresh === "function") {
        try {
          await iconlib.refresh({ id });
        } finally {
          ready.add(id);
        }
      }
    }
    return iconlib.search(request);
  };
}

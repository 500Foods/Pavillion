/* Search a library. Font Awesome and Material fetch their catalog once when none is cached. */

const REFRESH_IDS = new Set(["fontawesome", "material"]);

function iconRows(payload) {
  if (Array.isArray(payload)) {
    return payload;
  }
  return payload && Array.isArray(payload.icons) ? payload.icons : [];
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
      const probe = await iconlib.search({ id, query: "", limit: 1 });
      if (iconRows(probe).length) {
        ready.add(id);
      } else if (typeof iconlib.refresh === "function") {
        await iconlib.refresh({ id });
        ready.add(id);
      }
    }
    return iconlib.search(request);
  };
}

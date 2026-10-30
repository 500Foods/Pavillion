/* Saved column widths are base pixels (already divided by the table zoom).
   A value outside this range is dropped. Several widthGrow columns store
   their live widths here and come back as weights, so the share returns
   and the row still fills the pane. */

export const COLUMN_WIDTH_MIN = 30;
export const COLUMN_WIDTH_MAX = 4000;

export function clampWidth(value) {
  if (typeof value !== "number" || !Number.isFinite(value) || value < COLUMN_WIDTH_MIN || value > COLUMN_WIDTH_MAX) {
    return null;
  }
  return Math.round(value);
}

/* Every listed field must be a saved width. Otherwise the coded weights
   stay. One grow column is not a share: pass fewer than two fields and
   the fallback is returned. */
export function growWeights(saved, fields, fallback) {
  const base = { ...(fallback || {}) };
  if (!saved || typeof saved !== "object" || !Array.isArray(fields) || fields.length < 2) {
    return base;
  }
  const next = {};
  for (const field of fields) {
    const value = clampWidth(saved[field]);
    if (value === null) {
      return base;
    }
    next[field] = value;
  }
  return next;
}

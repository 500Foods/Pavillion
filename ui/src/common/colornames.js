/* CSS color names, and the theme roles a picker swatch can match.
   A swatch with neither stays unnamed. */

const NAMED = [
  ["#f0f8ff", "alice blue"],
  ["#faebd7", "antique white"],
  ["#00ffff", "aqua"],
  ["#7fffd4", "aquamarine"],
  ["#f0ffff", "azure"],
  ["#f5f5dc", "beige"],
  ["#ffe4c4", "bisque"],
  ["#000000", "black"],
  ["#ffebcd", "blanched almond"],
  ["#0000ff", "blue"],
  ["#8a2be2", "blue violet"],
  ["#a52a2a", "brown"],
  ["#deb887", "burlywood"],
  ["#5f9ea0", "cadet blue"],
  ["#7fff00", "chartreuse"],
  ["#d2691e", "chocolate"],
  ["#ff7f50", "coral"],
  ["#6495ed", "cornflower blue"],
  ["#fff8dc", "cornsilk"],
  ["#dc143c", "crimson"],
  ["#00008b", "dark blue"],
  ["#008b8b", "dark cyan"],
  ["#b8860b", "dark goldenrod"],
  ["#a9a9a9", "dark gray"],
  ["#006400", "dark green"],
  ["#bdb76b", "dark khaki"],
  ["#8b008b", "dark magenta"],
  ["#556b2f", "dark olive green"],
  ["#ff8c00", "dark orange"],
  ["#9932cc", "dark orchid"],
  ["#8b0000", "dark red"],
  ["#e9967a", "dark salmon"],
  ["#8fbc8f", "dark sea green"],
  ["#483d8b", "dark slate blue"],
  ["#2f4f4f", "dark slate gray"],
  ["#00ced1", "dark turquoise"],
  ["#9400d3", "dark violet"],
  ["#ff1493", "deep pink"],
  ["#00bfff", "deep sky blue"],
  ["#696969", "dim gray"],
  ["#1e90ff", "dodger blue"],
  ["#b22222", "firebrick"],
  ["#fffaf0", "floral white"],
  ["#228b22", "forest green"],
  ["#ff00ff", "fuchsia"],
  ["#dcdcdc", "gainsboro"],
  ["#f8f8ff", "ghost white"],
  ["#ffd700", "gold"],
  ["#daa520", "goldenrod"],
  ["#808080", "gray"],
  ["#008000", "green"],
  ["#adff2f", "green yellow"],
  ["#f0fff0", "honeydew"],
  ["#ff69b4", "hot pink"],
  ["#cd5c5c", "indian red"],
  ["#4b0082", "indigo"],
  ["#fffff0", "ivory"],
  ["#f0e68c", "khaki"],
  ["#e6e6fa", "lavender"],
  ["#fff0f5", "lavender blush"],
  ["#7cfc00", "lawn green"],
  ["#fffacd", "lemon chiffon"],
  ["#add8e6", "light blue"],
  ["#f08080", "light coral"],
  ["#e0ffff", "light cyan"],
  ["#fafad2", "light goldenrod yellow"],
  ["#d3d3d3", "light gray"],
  ["#90ee90", "light green"],
  ["#ffb6c1", "light pink"],
  ["#ffa07a", "light salmon"],
  ["#20b2aa", "light sea green"],
  ["#87cefa", "light sky blue"],
  ["#778899", "light slate gray"],
  ["#b0c4de", "light steel blue"],
  ["#ffffe0", "light yellow"],
  ["#00ff00", "lime"],
  ["#32cd32", "lime green"],
  ["#faf0e6", "linen"],
  ["#800000", "maroon"],
  ["#66cdaa", "medium aquamarine"],
  ["#0000cd", "medium blue"],
  ["#ba55d3", "medium orchid"],
  ["#9370db", "medium purple"],
  ["#3cb371", "medium sea green"],
  ["#7b68ee", "medium slate blue"],
  ["#00fa9a", "medium spring green"],
  ["#48d1cc", "medium turquoise"],
  ["#c71585", "medium violet red"],
  ["#191970", "midnight blue"],
  ["#f5fffa", "mint cream"],
  ["#ffe4e1", "misty rose"],
  ["#ffe4b5", "moccasin"],
  ["#ffdead", "navajo white"],
  ["#000080", "navy"],
  ["#fdf5e6", "old lace"],
  ["#808000", "olive"],
  ["#6b8e23", "olive drab"],
  ["#ffa500", "orange"],
  ["#ff4500", "orange red"],
  ["#da70d6", "orchid"],
  ["#eee8aa", "pale goldenrod"],
  ["#98fb98", "pale green"],
  ["#afeeee", "pale turquoise"],
  ["#db7093", "pale violet red"],
  ["#ffefd5", "papaya whip"],
  ["#ffdab9", "peach puff"],
  ["#cd853f", "peru"],
  ["#ffc0cb", "pink"],
  ["#dda0dd", "plum"],
  ["#b0e0e6", "powder blue"],
  ["#800080", "purple"],
  ["#663399", "rebecca purple"],
  ["#ff0000", "red"],
  ["#bc8f8f", "rosy brown"],
  ["#4169e1", "royal blue"],
  ["#8b4513", "saddle brown"],
  ["#fa8072", "salmon"],
  ["#f4a460", "sandy brown"],
  ["#2e8b57", "sea green"],
  ["#fff5ee", "seashell"],
  ["#a0522d", "sienna"],
  ["#c0c0c0", "silver"],
  ["#87ceeb", "sky blue"],
  ["#6a5acd", "slate blue"],
  ["#708090", "slate gray"],
  ["#fffafa", "snow"],
  ["#00ff7f", "spring green"],
  ["#4682b4", "steel blue"],
  ["#d2b48c", "tan"],
  ["#008080", "teal"],
  ["#d8bfd8", "thistle"],
  ["#ff6347", "tomato"],
  ["#40e0d0", "turquoise"],
  ["#ee82ee", "violet"],
  ["#f5deb3", "wheat"],
  ["#ffffff", "white"],
  ["#f5f5f5", "white smoke"],
  ["#ffff00", "yellow"],
  ["#9acd32", "yellow green"],
];

const BY_HEX = new Map(NAMED);

const ROLES = [
  ["--pav-accent", "Accent"],
  ["--pav-swatch-1", "Roof red"],
  ["--pav-swatch-2", "Roof rose"],
  ["--pav-text", "Text"],
  ["--pav-text-muted", "Muted"],
  ["--pav-surface-canvas", "Canvas"],
  ["--pav-status-error", "Error"],
  ["--pav-status-warn", "Warning"],
  ["--pav-status-ok", "Success"],
  ["--pav-mail-unread", "Unread"],
  ["--pav-mail-flag", "Flag"],
  ["--pav-mail-answered", "Answered"],
  ["--pav-mail-forwarded", "Forwarded"],
];

function titleWords(name) {
  return name.replace(/\b[a-z]/g, (letter) => letter.toUpperCase());
}

/* #rgb and #rrggbb, lowercased. Eight-digit hex drops the alpha. Empty otherwise. */
function hexOnly(value) {
  const text = String(value || "").trim();
  const short = /^#([0-9a-f]{3})$/i.exec(text);
  if (short) {
    const [r, g, b] = short[1];
    return `#${r}${r}${g}${g}${b}${b}`.toLowerCase();
  }
  const long = /^#([0-9a-f]{6})([0-9a-f]{2})?$/i.exec(text);
  return long ? `#${long[1].toLowerCase()}` : "";
}

/* The CSS name for this hex, or "" when it is not one. */
export function cssColorName(value) {
  const hex = hexOnly(value);
  const phrase = hex ? BY_HEX.get(hex) : "";
  return phrase ? titleWords(phrase) : "";
}

/* CSS name first. A theme role when the hex equals that variable and has no CSS name. */
export function colorTip(value, read) {
  const named = cssColorName(value);
  if (named) {
    return named;
  }
  const hex = hexOnly(value);
  if (!hex || typeof read !== "function") {
    return "";
  }
  for (const [variable, label] of ROLES) {
    if (hexOnly(read(variable)) === hex) {
      return label;
    }
  }
  return "";
}

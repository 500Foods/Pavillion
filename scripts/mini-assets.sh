#!/usr/bin/env bash
# Shrink the embedded page before `make mini` packs it.
# The nav mark is drawn at 1.15rem. The source PNG is 1024x1024 and already
# compressed, so UPX cannot shrink those bytes. This rewrites only
# ui/dist/mark.png. The source file and `make build` are left alone.

set -euo pipefail

max_side=256
root="$(cd "$(dirname "$0")/.." && pwd)"
mark="$root/ui/dist/mark.png"

if [[ ! -f "$mark" ]]; then
  printf 'missing %s\n' "$mark" >&2
  exit 1
fi

if command -v magick >/dev/null 2>&1; then
  identify=(magick identify)
  convert=(magick)
elif command -v convert >/dev/null 2>&1 && command -v identify >/dev/null 2>&1; then
  identify=(identify)
  convert=(convert)
else
  printf '%s\n' "make mini needs ImageMagick" >&2
  exit 1
fi

before="$(stat -c%s "$mark")"
# identify's format has no newline, and read returns 1 at that EOF.
size="$("${identify[@]}" -format '%w %h' "$mark")"
width="${size%% *}"
height="${size##* }"
if [[ ! "$width" =~ ^[1-9][0-9]*$ || ! "$height" =~ ^[1-9][0-9]*$ ]]; then
  printf 'cannot read the size of %s\n' "$mark" >&2
  exit 1
fi

long="$width"
if (( height > long )); then
  long="$height"
fi
if (( long <= max_side )); then
  printf '%s %s bytes, left as-is\n' "$(basename "$mark")" "$before" >&2
  exit 0
fi

if (( width >= height )); then
  new_w="$max_side"
  new_h=$(( (height * max_side + width / 2) / width ))
else
  new_h="$max_side"
  new_w=$(( (width * max_side + height / 2) / height ))
fi
if (( new_w < 1 )); then
  new_w=1
fi
if (( new_h < 1 )); then
  new_h=1
fi

temporary="${mark%.png}.mini-tmp.png"
"${convert[@]}" "$mark" \
  -filter Lanczos \
  -resize "${new_w}x${new_h}!" \
  -alpha on \
  -define png:compression-level=9 \
  PNG32:"$temporary"
after="$(stat -c%s "$temporary")"
if (( after >= before )); then
  rm -f "$temporary"
  printf '%s %s bytes, left as-is\n' "$(basename "$mark")" "$before" >&2
  exit 0
fi
mv -f "$temporary" "$mark"
printf '%s %s -> %s bytes (%sx%s)\n' "$(basename "$mark")" "$before" "$after" "$new_w" "$new_h"

#!/usr/bin/env bash
# Shrink the embedded page before `make mini` packs it.
# The nav mark is drawn at 1.15rem. The source PNG is 1024x1024 and already
# compressed, so UPX cannot shrink those bytes. This rewrites only
# ui/dist/mark.png. The source file and `make build` are left alone.
#
# ImageMagick is preferred. If it is missing or crashes (seen: MSYS2
# magick segfault on Windows runners), fall back to the committed 256 px
# splash mark so the release build still finishes.

set -euo pipefail

max_side=256
root="$(cd "$(dirname "$0")/.." && pwd)"
mark="$root/ui/dist/mark.png"
fallback="$root/src-tauri/icons/128x128@2x.png"

if [[ ! -f "$mark" ]]; then
  printf 'missing %s\n' "$mark" >&2
  exit 1
fi

use_fallback() {
  local why=$1
  if [[ ! -f "$fallback" ]]; then
    printf '%s; no fallback at %s, leaving mark as-is\n' "$why" "$fallback" >&2
    exit 0
  fi
  local before after
  before="$(stat -c%s "$mark")"
  cp -f "$fallback" "$mark"
  after="$(stat -c%s "$mark")"
  printf '%s; used 256 px splash mark (%s -> %s bytes)\n' "$why" "$before" "$after" >&2
  exit 0
}

identify=()
convert=()
if command -v magick >/dev/null 2>&1; then
  identify=(magick identify)
  convert=(magick)
elif command -v convert >/dev/null 2>&1 && command -v identify >/dev/null 2>&1; then
  identify=(identify)
  convert=(convert)
else
  use_fallback "ImageMagick not found"
fi

before="$(stat -c%s "$mark")"
# identify's format has no newline, and read returns 1 at that EOF.
size="$("${identify[@]}" -format '%w %h' "$mark" 2>/dev/null)" || use_fallback "identify failed"
width="${size%% *}"
height="${size##* }"
if [[ ! "$width" =~ ^[1-9][0-9]*$ || ! "$height" =~ ^[1-9][0-9]*$ ]]; then
  use_fallback "cannot read mark size"
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
set +e
"${convert[@]}" "$mark" \
  -filter Lanczos \
  -resize "${new_w}x${new_h}!" \
  -alpha on \
  -define png:compression-level=9 \
  PNG32:"$temporary"
rc=$?
set -e
if (( rc != 0 )) || [[ ! -f "$temporary" ]]; then
  rm -f "$temporary"
  use_fallback "convert failed (exit $rc)"
fi
after="$(stat -c%s "$temporary")"
if (( after >= before )); then
  rm -f "$temporary"
  printf '%s %s bytes, left as-is\n' "$(basename "$mark")" "$before" >&2
  exit 0
fi
mv -f "$temporary" "$mark"
printf '%s %s -> %s bytes (%sx%s)\n' "$(basename "$mark")" "$before" "$after" "$new_w" "$new_h"

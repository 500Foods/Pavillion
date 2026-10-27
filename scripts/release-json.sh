#!/usr/bin/env bash
# Add or replace one release in web/releases.json (newest first).
# Usage: release-json.sh <version X.Y> <commit sha> <build X.Y.Z> <asset dir> [notes]
# Each file in <asset dir> (not *.sha256) becomes one asset. Platform comes
# from the extension. URL is the GitHub Release download link for tag vX.Y.
#
# Schema 1:
#   releases[]: { version, build, date (YYYY-MM-DD, UTC), commit, commitUrl,
#                 releaseUrl, notes, assets[] }
#   assets[]:   { platform (linux-rpm|linux-deb|linux-appimage|windows-msi|macos-dmg),
#                 arch, filename, url, size, sha256 }
set -euo pipefail
version="$1" commit="$2" build="$3" dir="$4" notes="${5:-}"
repo="${GITHUB_REPOSITORY:-500Foods/Pavillion}"
json="$(cd "$(dirname "$0")/.." && pwd)/web/releases.json"
base="https://github.com/$repo/releases/download/v$version"

assets='[]'
for f in "$dir"/*; do
  [[ -f "$f" && "$f" != *.sha256 ]] || continue
  name="$(basename "$f")"
  case "$name" in
    *.rpm) platform=linux-rpm ;;
    *.deb) platform=linux-deb ;;
    *.AppImage) platform=linux-appimage ;;
    *.msi) platform=windows-msi ;;
    *.dmg) platform=macos-dmg ;;
    *) continue ;;
  esac
  case "$name" in *aarch64*|*arm64*) arch=aarch64 ;; *) arch=x86_64 ;; esac
  sha="$(sha256sum "$f" | cut -d' ' -f1)"
  size="$(stat -c %s "$f")"
  assets="$(jq -c --arg p "$platform" --arg a "$arch" --arg n "$name" \
    --arg u "$base/$name" --arg s "$sha" --argjson z "$size" \
    '. + [{platform:$p, arch:$a, filename:$n, url:$u, size:$z, sha256:$s}]' <<<"$assets")"
done

tmp="$(mktemp)"
jq --arg v "$version" --arg b "$build" --arg c "$commit" --arg n "$notes" \
   --arg d "$(date -u +%F)" --arg r "https://github.com/$repo" --argjson a "$assets" '
  .releases |= ([{version:$v, build:$b, date:$d, commit:$c,
                  commitUrl:($r+"/commit/"+$c),
                  releaseUrl:($r+"/releases/tag/v"+$v),
                  notes:$n, assets:$a}]
                + map(select(.version != $v)))' "$json" >"$tmp"
mv "$tmp" "$json"

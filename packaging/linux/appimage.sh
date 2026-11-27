#!/usr/bin/env bash
# Builds dist/Pavillion-<version>-x86_64.AppImage (W-SHIP Packaging Lock).
# Full portable bundle: Tauri's bundler (linuxdeploy + its GTK plugin) copies
# WebKitGTK, its helper processes, GTK, libsoup, GMime, and the rest into the
# AppDir. Same shape as the msi: bundle.active is switched on for this run only.
# Usage: packaging/linux/appimage.sh [binary]   (default src-tauri/target/release/pavillion)
# The binary must not be UPX-packed; linuxdeploy patches it.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
bin=${1:-src-tauri/target/release/pavillion}
[ -x "$bin" ] || { printf '%s\n' "appimage needs $bin (run make build)" >&2; exit 1; }
if upx -t -q "$bin" >/dev/null 2>&1; then
	printf '%s\n' "$bin is UPX-packed; pass the unpacked binary" >&2; exit 1
fi
version=${PAVILLION_VERSION:-dev}
arch=$(uname -m)
rel=src-tauri/target/release
mkdir -p "$rel"
[ "$bin" -ef "$rel/pavillion" ] || cp -f "$bin" "$rel/pavillion"
rm -rf "$rel/bundle/appimage"
# linuxdeploy is itself an AppImage; this lets it run without FUSE (CI containers).
export APPIMAGE_EXTRACT_AND_RUN=1 NO_STRIP=1
./ui/node_modules/.bin/tauri bundle --bundles appimage \
	--config '{"bundle":{"active":true,"targets":["appimage"],"linux":{"appimage":{"bundleMediaFramework":false}}}}'
out=dist/Pavillion-$version-$arch.AppImage
mkdir -p dist
cp -f "$rel"/bundle/appimage/*.AppImage "$out"
chmod +x "$out"
echo "appimage $(stat -c%s "$out") bytes $out"

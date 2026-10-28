#!/usr/bin/env bash
# Stage the MinGW UCRT64 runtime DLLs pavillion.exe needs, for the MSI.
# Run under MSYS2 UCRT64 after `tauri build --no-bundle`.
#
# Discovery: MSYS2's ldd walks the PE import table recursively (like ntldd -R),
# so one pass over the built exe yields every transitive DLL. We keep only the
# ones resolved from /ucrt64/bin; Windows system DLLs (/c/Windows/...) are
# already on every box. Nothing is hand-listed, so the set follows the deps.
#
# Usage: collect-dlls.sh <exe> <outdir>
set -euo pipefail
exe=${1:?exe}; out=${2:?outdir}
mkdir -p "$out"
ldd "$exe" | awk '$3 ~ /^\/ucrt64\/bin\// {print $3}' | sort -u | while read -r dll; do
  cp -v "$dll" "$out/"
done
n=$(ls "$out"/*.dll 2>/dev/null | wc -l)
[ "$n" -gt 0 ] || { echo "no UCRT64 DLLs found for $exe" >&2; exit 1; }
# Fail loudly if ldd left anything unresolved.
if ldd "$exe" | grep -q 'not found'; then ldd "$exe" | grep 'not found' >&2; exit 1; fi
echo "staged $n DLLs in $out"

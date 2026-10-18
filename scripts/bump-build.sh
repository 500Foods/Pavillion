#!/usr/bin/env bash
# Add one to PAVILLION_BUILD.
# make dev, make build, and make mini run this before they compile.
# make test does not. The shell and the page read the file and compose
# 0.phase.build. This does not edit Cargo.toml or tauri.conf.json.

set -euo pipefail

if [[ $# -ge 1 ]]; then
  root="$(cd "$1" && pwd)"
else
  root="$(cd "$(dirname "$0")/.." && pwd)"
fi

path="$root/PAVILLION_BUILD"
if [[ ! -r "$path" ]]; then
  printf 'cannot read %s\n' "$path" >&2
  exit 1
fi

raw="$(<"$path")"
value="${raw#"${raw%%[![:space:]]*}"}"
value="${value%"${value##*[![:space:]]}"}"
if [[ ! "$value" =~ ^[1-9][0-9]*$ ]]; then
  printf '%s must be a positive integer\n' "$(basename "$path")" >&2
  exit 1
fi

next=$((value + 1))
printf '%s\n' "$next" >"$path"
printf 'PAVILLION_BUILD %s\n' "$next"

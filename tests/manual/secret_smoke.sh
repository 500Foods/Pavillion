#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
bin="$root/build/secret_smoke"
work=$(mktemp -d)
cleanup() { rm -rf "$work"; }
trap cleanup EXIT
if [ ! -x "$bin" ]; then
    cc -std=c11 -Wall -Wextra -I"$root/core/include" -o "$bin" "$root/tests/manual/secret_smoke.c" \
        "$root/build/libpavillion_core.a" -ljansson -lsqlite3 -lpthread \
        $(pkg-config --libs libsecret-1) \
        "$root/core/platform/stronghold/target/release/libpav_stronghold.a" -ldl -lm
fi
user="smoke/dummy"
value="pav-smoke-dummy"
"$bin" "$work" set "$user" "$value"
check_keyring() {
    found=$(secret-tool search service pavillion 2>&1 || true)
    printf '%s\n' "$found" | grep -q stronghold
    printf '%s\n' "$found" | grep -q 'Pavillion snapshot'
    if printf '%s\n' "$found" | grep -q "$value"; then
        echo "dummy secret visible in secret-tool" >&2
        exit 1
    fi
}
check_keyring
"$bin" "$work" get "$user" "$value"
"$bin" "$work" delete "$user"
check_keyring
"$bin" "$work" missing "$user"

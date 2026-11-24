#!/usr/bin/env bash
# Run the full test suite with the JMAP fixture server.
set -euo pipefail

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)

bash scripts/run-fixture.sh start
trap 'bash scripts/run-fixture.sh stop' EXIT

state="$root/build/fixture-state.txt"
port=$(sed -n '1p' < "$state")
ca=$(sed -n '2p' < "$state")

PAV_FIXTURE_URL="https://127.0.0.1:$port" \
PAV_FIXTURE_CA="$ca" \
"$root/build/core_test"

# Stronghold wrapper (K Lock: work factor 0, old snapshots re-saved). One
# thread: the tests set the library-wide work factor.
#
# windows-core inherits the runner's MSVC rustc. libsodium-sys-stable then
# links the moving libsodium-*-stable-msvc zip, which is built /MT and
# started aborting these tests (0xc0000005) on 2026-11-23. When the UCRT64
# toolchain is installed, use it and the packaged libsodium instead.
if [[ -x /ucrt64/bin/rustc && -x /ucrt64/bin/cargo ]]; then
    export PATH="/ucrt64/bin:${PATH}"
    unset RUSTUP_TOOLCHAIN
    host=$(/ucrt64/bin/rustc -vV | sed -n 's/^host: //p')
    case "$host" in
        *windows-gnu*) ;;
        *)
            printf '%s\n' "ucrt64 rustc host is '${host}', want a windows-gnu host" >&2
            exit 1
            ;;
    esac
    export SODIUM_USE_PKG_CONFIG=1
    if [[ -n "${PKG_CONFIG_PATH:-}" ]]; then
        export PKG_CONFIG_PATH="/ucrt64/lib/pkgconfig:${PKG_CONFIG_PATH}"
    else
        export PKG_CONFIG_PATH="/ucrt64/lib/pkgconfig"
    fi
    printf '%s\n' "stronghold tests: $(/ucrt64/bin/rustc --version) (${host}), libsodium via pkg-config"
fi
if command -v cargo >/dev/null 2>&1; then
    (cd "$root/core/platform/stronghold" && cargo test --release --quiet -- --test-threads=1)
fi

# cd rather than --prefix: Windows npm ignores --prefix for install.
(cd "$root/ui" && npm install && npm test)

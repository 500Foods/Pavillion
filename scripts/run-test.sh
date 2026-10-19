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

# cd rather than --prefix: Windows npm ignores --prefix for install.
(cd "$root/ui" && npm install && npm test)

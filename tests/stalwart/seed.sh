#!/usr/bin/env bash
# Seed check for the Stalwart test container.
#
# Posts a Capability/get JMAP call over the mapped HTTPS port with Basic auth
# and prints the HTTP status and the first 1000 characters of the reply. The
# container's config.toml defines the W-2 domains and five accounts; creating
# mailboxes and messages from tests/corpus/ is still to come.
#
# Environment (defaults): PAV_STALWART_HOST (localhost), PAV_STALWART_PORT
# (1443), PAV_STALWART_USER (andrew@test-a.local), PAV_STALWART_PASS (test),
# PAV_STALWART_CA (build/stalwart-tls/test-ca.crt).
# Needs curl and jq.
set -euo pipefail

host=${PAV_STALWART_HOST:-localhost}
port=${PAV_STALWART_PORT:-1443}
user=${PAV_STALWART_USER:-andrew@test-a.local}
pass=${PAV_STALWART_PASS:-test}
base_url="https://$host:$port"
ca=${PAV_STALWART_CA:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)/build/stalwart-tls/test-ca.crt}
[ -f "$ca" ] || { echo "seed: $ca is missing; run make it-up" >&2; exit 1; }

for tool in curl jq; do
  command -v "$tool" >/dev/null 2>&1 || { echo "seed: $tool is required" >&2; exit 1; }
done

body=$(mktemp)
trap 'rm -f "$body"' EXIT

# The container serves the self-signed test CA that `make it-up` writes to
# build/stalwart-tls/ (PAV_STALWART_CA overrides). The password goes to curl
# on stdin as a config line so it never shows in the process list.
status=$(printf 'user = "%s:%s"\n' "$user" "$pass" | curl -sS --cacert "$ca" --max-time 10 \
  --config - \
  -X POST -H 'Content-Type: application/json' \
  --data '{"methodCalls": [["Capability/get", {}, "0"]]}' \
  -o "$body" -w '%{http_code}' \
  "$base_url/jmap/api")

echo "Stalwart session: $status"
jq -a . < "$body" | head -c 1000
echo

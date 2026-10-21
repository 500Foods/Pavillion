#!/usr/bin/env bash
# Record a scrubbed Session, Mailbox/get, and Identity/get from the W-2
# Stalwart container into tests/support/fixtures/. Does not start the
# container. Run it after `make it-seed`, against https://localhost:1443.
#
# Environment matches tests/stalwart/seed.sh. The password is passed to curl
# on stdin and is never written to the fixture files.
# Needs curl and jq. No Python. No flock.
set -euo pipefail

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../.." && pwd)
host=${PAV_STALWART_HOST:-localhost}
port=${PAV_STALWART_PORT:-1443}
pass=${PAV_STALWART_PASS:-pavtest-pw}
admin_pw=${PAV_STALWART_ADMIN_PW:-pavtest-admin}
ca=${PAV_STALWART_CA:-$root/build/stalwart-tls/test-ca.crt}
user=${PAV_STALWART_USER:-andrew@test-a.local}
base_url="https://$host:$port"
out_dir="$root/tests/support/fixtures"
body=$(mktemp)
trap 'rm -f "$body"' EXIT

[ -f "$ca" ] || { echo "record: $ca is missing; run make it-up" >&2; exit 1; }
for tool in curl jq; do
  command -v "$tool" >/dev/null 2>&1 || { echo "record: $tool is required" >&2; exit 1; }
done
[ "${#pass}" -ge 8 ] || { echo "record: PAV_STALWART_PASS must be at least 8 characters" >&2; exit 1; }

# strip_secrets: drop secret-named keys. Values are checked afterwards, in
# the shell, so the password never becomes an argument.
strip_secrets() {
  jq '
    def bad: ascii_downcase | test("password|secret|authorization|refresh_token|token");
    walk(if type == "object" then with_entries(select((.key | bad) | not)) else . end)
  '
}

reject_secrets() {
  local file=$1
  local text needle
  text=$(cat "$file")
  for needle in "$pass" "$admin_pw" pavtest-pw pavtest-admin; do
    case "$text" in
      *"$needle"*)
        echo "record: $file contains a secret; not written" >&2
        return 1
        ;;
    esac
  done
  case "$text" in
    *Authorization*)
      echo "record: $file contains an Authorization header; not written" >&2
      return 1
      ;;
  esac
  return 0
}

write_fixture() {
  local dest=$1
  local tmp
  tmp=$(mktemp)
  cat > "$tmp"
  if ! reject_secrets "$tmp"; then
    rm -f "$tmp"
    return 1
  fi
  mv "$tmp" "$dest"
  echo "record: wrote ${dest#"$root"/}"
}

user_session() {
  printf 'user = "%s:%s"\n' "$user" "$pass" | curl -sS --fail --max-time 10 --cacert "$ca" --config - \
    -L "$base_url/.well-known/jmap"
}

user_jmap() {
  jq -nc --argjson mc "$1" --argjson using "$2" '{using:$using, methodCalls:$mc}' > "$body"
  printf 'user = "%s:%s"\n' "$user" "$pass" | curl -sS --fail-with-body --max-time 20 --cacert "$ca" --config - \
    -H 'Content-Type: application/json' --data-binary "@$body" "$base_url/jmap/"
}

record_method() {
  local method=$1 name=$2 using=$3 reply got
  reply=$(user_jmap "$(jq -nc --arg m "$method" --argjson a "$args" '[[$m, $a, "0"]]')" "$using")
  got=$(jq -r '.methodResponses[0][0] // empty' <<<"$reply")
  [ "$got" = "$method" ] || {
    echo "record: $method returned ${got:-nothing} ($(jq -r '.methodResponses[0][1].type // .methodResponses[0][1].code // "unknown"' <<<"$reply"))" >&2
    return 1
  }
  jq -n --arg method "$method" --argjson arguments "$args" \
    --argjson response "$(jq '.methodResponses[0][1]' <<<"$reply" | strip_secrets)" \
    '{method:$method, arguments:$arguments, response:$response}' | jq -S . |
    write_fixture "$out_dir/$name"
}

mkdir -p "$out_dir"
sess=$(user_session)
aid=$(jq -r '.primaryAccounts["urn:ietf:params:jmap:mail"] // empty' <<<"$sess")
[ -n "$aid" ] || { echo "record: $user has no mail account" >&2; exit 1; }
api_url=$(jq -r '.apiUrl // empty' <<<"$sess")
[ "$api_url" = "$base_url/jmap/" ] || { echo "record: expected apiUrl $base_url/jmap/" >&2; exit 1; }

jq -n --arg account "$user" --argjson response "$(printf '%s' "$sess" | strip_secrets)" \
  '{recorded:"session", account:$account, response:$response}' | jq -S . | write_fixture "$out_dir/session.json"

args=$(jq -nc --arg id "$aid" '{accountId:$id, ids:null}')
record_method Mailbox/get mailbox-get.json \
  '["urn:ietf:params:jmap:core","urn:ietf:params:jmap:mail"]'
record_method Identity/get identity-get.json \
  '["urn:ietf:params:jmap:core","urn:ietf:params:jmap:mail","urn:ietf:params:jmap:submission"]'

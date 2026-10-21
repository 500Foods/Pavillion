#!/usr/bin/env bash
# Seed the Stalwart v0.16 test container. Idempotent: existing objects are
# kept, missing ones are created. Steps: wait for /healthz/live; create the
# TLS certificate (files from build/stalwart-tls) and the two domains through
# the admin API; set SystemSettings (hostname, default domain, default
# certificate); restart the container only when the certificate or settings
# changed, since 443 picks up a new certificate on restart; create missing
# accounts; check andrew's Session with --cacert; as each user, Mailbox/get
# then Mailbox/set for any missing role (inbox, drafts, sent, junk, trash,
# archive) and for a mailbox named Later under Inbox.
#
# Environment (defaults): PAV_STALWART_ADMIN_PW (required; the Makefile sets
# it), PAV_STALWART_PASS (pavtest-pw, used for all five accounts; Stalwart
# needs at least 8 characters), PAV_STALWART_HOST (localhost),
# PAV_STALWART_PORT (1443), PAV_STALWART_CA (build/stalwart-tls/test-ca.crt),
# PAV_STALWART_COMPOSE (tests/stalwart/compose.yaml).
# Needs curl, jq, and docker compose.
set -euo pipefail

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
host=${PAV_STALWART_HOST:-localhost}
port=${PAV_STALWART_PORT:-1443}
admin_pw=${PAV_STALWART_ADMIN_PW:?seed: set PAV_STALWART_ADMIN_PW}
pass=${PAV_STALWART_PASS:-pavtest-pw}
ca=${PAV_STALWART_CA:-$here/../../build/stalwart-tls/test-ca.crt}
compose=${PAV_STALWART_COMPOSE:-$here/compose.yaml}
base_url="https://$host:$port"
hostname=mail.test-a.local
domains=(test-a.local test-b.local)
accounts=(andrew@test-a.local alice@test-a.local bob@test-b.local carol@test-b.local dave@test-b.local)

[ -f "$ca" ] || { echo "seed: $ca is missing; run make it-up" >&2; exit 1; }
for tool in curl jq docker; do
  command -v "$tool" >/dev/null 2>&1 || { echo "seed: $tool is required" >&2; exit 1; }
done
[ "${#pass}" -ge 8 ] || { echo "seed: PAV_STALWART_PASS must be at least 8 characters" >&2; exit 1; }

body=$(mktemp)
trap 'rm -f "$body"' EXIT

# Until the seeded certificate is served, 443 has Stalwart's self-signed one,
# so the admin calls skip verification only in that case.
tls=(--cacert "$ca")
wait_health() {
  local i
  for i in $(seq 60); do
    curl -sf -o /dev/null --max-time 2 "${tls[@]}" "$base_url/healthz/live" && return 0
    sleep 1
  done
  echo "seed: $base_url/healthz/live did not answer" >&2
  return 1
}
pick_tls() {
  if curl -sf -o /dev/null --max-time 2 --cacert "$ca" "$base_url/healthz/live"; then
    tls=(--cacert "$ca")
  else
    tls=(-k)
  fi
}

# call METHOD_CALLS_JSON: posts one JMAP request as admin and prints the reply.
# Credentials go to curl on stdin as a config line, never in argv.
call() {
  jq -nc --argjson mc "$1" '{using: ["urn:ietf:params:jmap:core", "urn:stalwart:jmap"], methodCalls: $mc}' > "$body"
  printf 'user = "admin:%s"\n' "$admin_pw" | curl -sS --fail-with-body --max-time 10 "${tls[@]}" --config - \
    -H 'Content-Type: application/json' --data-binary "@$body" "$base_url/jmap/"
}
# first_id TYPE FILTER_JSON: id of the first match, or empty.
first_id() {
  call "$(jq -nc --arg m "x:$1/query" --argjson f "$2" '[[$m, {filter: $f}, "0"]]')" |
    jq -r '.methodResponses[0][1].ids[0] // empty'
}
# create TYPE VALUE_JSON: creates one object and prints its id.
create() {
  local r id
  r=$(call "$(jq -nc --arg m "x:$1/set" --argjson v "$2" '[[$m, {create: {n: $v}}, "0"]]')")
  id=$(jq -r '.methodResponses[0][1].created.n.id // empty' <<<"$r")
  [ -n "$id" ] || { echo "seed: creating $1 failed: $(jq -c '.methodResponses[0][1].notCreated // .' <<<"$r")" >&2; return 1; }
  echo "$id"
}

tls=(-k)
wait_health
pick_tls
changed=0

cert_id=$(first_id Certificate '{}')
if [ -z "$cert_id" ]; then
  cert_id=$(create Certificate '{"certificate": {"@type": "File", "filePath": "/etc/stalwart/tls/test-ca.crt"},
    "privateKey": {"@type": "File", "filePath": "/etc/stalwart/tls/test-ca.key"}}')
  echo "seed: created certificate $cert_id"
  changed=1
else
  echo "seed: certificate exists ($cert_id)"
fi

declare -A domain_id
for d in "${domains[@]}"; do
  id=$(first_id Domain "$(jq -nc --arg n "$d" '{name: $n}')")
  if [ -z "$id" ]; then
    id=$(create Domain "$(jq -nc --arg n "$d" '{name: $n}')")
    echo "seed: created domain $d ($id)"
  else
    echo "seed: domain $d exists ($id)"
  fi
  domain_id[$d]=$id
done

want=$(jq -nc --arg c "$cert_id" --arg h "$hostname" --arg d "${domain_id[test-a.local]}" \
  '{defaultCertificateId: $c, defaultHostname: $h, defaultDomainId: $d}')
have=$(call '[["x:SystemSettings/get", {"ids": ["singleton"]}, "0"]]' |
  jq -c '.methodResponses[0][1].list[0] | {defaultCertificateId, defaultHostname, defaultDomainId}')
if [ "$(jq -S . <<<"$have")" != "$(jq -S . <<<"$want")" ]; then
  r=$(call "$(jq -nc --argjson w "$want" '[["x:SystemSettings/set", {update: {singleton: $w}}, "0"]]')")
  jq -e '.methodResponses[0][1].updated | has("singleton")' >/dev/null <<<"$r" ||
    { echo "seed: SystemSettings update failed: $(jq -c '.methodResponses[0][1].notUpdated // .' <<<"$r")" >&2; exit 1; }
  echo "seed: updated SystemSettings"
  changed=1
else
  echo "seed: SystemSettings already set"
fi

if [ "$changed" = 1 ]; then
  echo "seed: restarting the container to serve the test certificate"
  docker compose -f "$compose" restart >/dev/null
  sleep 2
  tls=(--cacert "$ca")
  wait_health
fi
pick_tls
[ "${tls[0]}" = --cacert ] || { echo "seed: $base_url does not serve the test certificate" >&2; exit 1; }

for a in "${accounts[@]}"; do
  n=${a%@*}
  d=${domain_id[${a#*@}]}
  id=$(first_id Account "$(jq -nc --arg n "$n" --arg d "$d" '{name: $n, domainId: $d}')")
  if [ -n "$id" ]; then
    echo "seed: account $a exists ($id)"
    continue
  fi
  id=$(create Account "$(jq -nc --arg n "$n" --arg d "$d" --arg p "$pass" \
    '{"@type": "User", name: $n, domainId: $d, credentials: {"0": {"@type": "Password", secret: $p}},
      roles: {"@type": "User"}, permissions: {"@type": "Inherit"}, encryptionAtRest: {"@type": "Disabled"}}')")
  echo "seed: created account $a ($id)"
done

printf 'user = "%s:%s"\n' "${accounts[0]}" "$pass" | curl -sS --fail --max-time 10 --cacert "$ca" --config - \
  -L "$base_url/.well-known/jmap" -o "$body"
api_url=$(jq -r '.apiUrl' "$body")
echo "seed: ${accounts[0]} Session apiUrl $api_url"
[ "$api_url" = "$base_url/jmap/" ] || { echo "seed: expected apiUrl $base_url/jmap/" >&2; exit 1; }

# user_session USER: GET the Session as that user. The password stays on curl's stdin.
user_session() {
  printf 'user = "%s:%s"\n' "$1" "$pass" | curl -sS --fail --max-time 10 --cacert "$ca" --config - \
    -L "$base_url/.well-known/jmap"
}
# user_jmap USER METHOD_CALLS_JSON: one mail JMAP request as that user.
user_jmap() {
  jq -nc --argjson mc "$2" \
    '{using: ["urn:ietf:params:jmap:core", "urn:ietf:params:jmap:mail"], methodCalls: $mc}' > "$body"
  printf 'user = "%s:%s"\n' "$1" "$pass" | curl -sS --fail-with-body --max-time 20 --cacert "$ca" --config - \
    -H 'Content-Type: application/json' --data-binary "@$body" "$base_url/jmap/"
}
method_name() {
  jq -r '.methodResponses[0][0] // empty' <<<"$1"
}
method_error() {
  jq -r '.methodResponses[0][1].type // .methodResponses[0][1].code // "unknown"' <<<"$1"
}

# ensure_mailboxes USER: create missing role mailboxes, then Later under Inbox.
ensure_mailboxes() {
  local user=$1
  local sess aid got kind missing n inbox has_later created bad
  sess=$(user_session "$user")
  aid=$(jq -r '.primaryAccounts["urn:ietf:params:jmap:mail"] // empty' <<<"$sess")
  [ -n "$aid" ] || { echo "seed: $user has no mail account" >&2; return 1; }
  got=$(user_jmap "$user" "$(jq -nc --arg id "$aid" '[["Mailbox/get", {accountId:$id}, "m"]]')")
  kind=$(method_name "$got")
  [ "$kind" = "Mailbox/get" ] || { echo "seed: $user Mailbox/get failed ($(method_error "$got"))" >&2; return 1; }
  missing=$(jq -c '
    .methodResponses[0][1].list as $list
    | ["inbox","drafts","sent","junk","trash","archive"] as $roles
    | ["Inbox","Drafts","Sent","Junk","Trash","Archive"] as $names
    | [ range(0; 6) as $i
        | select( [$list[].role] | index($roles[$i]) | not )
        | {key: $roles[$i], value: {name: $names[$i], role: $roles[$i], parentId: null}} ]
    | from_entries
  ' <<<"$got")
  inbox=$(jq -r '[.methodResponses[0][1].list[]? | select(.role == "inbox") | .id][0] // empty' <<<"$got")
  has_later=$(jq -r '[(.methodResponses[0][1].list // [])[].name] | index("Later") | if . == null then "no" else "yes" end' <<<"$got")
  n=$(jq 'length' <<<"$missing")
  if [ "$n" != "0" ]; then
    created=$(user_jmap "$user" "$(jq -nc --arg id "$aid" --argjson c "$missing" \
      '[["Mailbox/set", {accountId:$id, create:$c}, "c"]]')")
    kind=$(method_name "$created")
    [ "$kind" = "Mailbox/set" ] || { echo "seed: $user Mailbox/set failed ($(method_error "$created"))" >&2; return 1; }
    bad=$(jq -r '.methodResponses[0][1].notCreated // {} | keys | length' <<<"$created")
    [ "$bad" = "0" ] || {
      echo "seed: $user Mailbox/set rejected $(jq -r '.methodResponses[0][1].notCreated // {} | to_entries[] | "\(.key):\(.value.type // .value.code // "error")"' <<<"$created")" >&2
      return 1
    }
    echo "seed: $user created $n role mailbox(es)"
    if [ -z "$inbox" ]; then
      inbox=$(jq -r '.methodResponses[0][1].created.inbox.id // empty' <<<"$created")
    fi
  else
    echo "seed: $user role mailboxes present"
  fi
  if [ "$has_later" = "yes" ]; then
    echo "seed: $user mailbox Later present"
    return 0
  fi
  [ -n "$inbox" ] || { echo "seed: $user has no Inbox for Later" >&2; return 1; }
  created=$(user_jmap "$user" "$(jq -nc --arg id "$aid" --arg p "$inbox" \
    '[["Mailbox/set", {accountId:$id, create:{later:{name:"Later", parentId:$p}}}, "l"]]')")
  kind=$(method_name "$created")
  [ "$kind" = "Mailbox/set" ] || { echo "seed: $user Later failed ($(method_error "$created"))" >&2; return 1; }
  bad=$(jq -r '.methodResponses[0][1].notCreated // {} | keys | length' <<<"$created")
  [ "$bad" = "0" ] || { echo "seed: $user Later was rejected" >&2; return 1; }
  echo "seed: $user created mailbox Later"
}

for a in "${accounts[@]}"; do
  ensure_mailboxes "$a"
done

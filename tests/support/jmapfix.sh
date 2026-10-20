#!/usr/bin/env bash
# JMAP fixture server for Pavillion tests.
#
# Bash, socat, the openssl CLI, and jq. Started and stopped by `make test`
# through scripts/run-fixture.sh; the URL and test CA reach the Unity runner
# through PAV_FIXTURE_URL and PAV_FIXTURE_CA (build/fixture-state.txt holds
# the port on line 1 and the CA path on line 2).
#
# socat terminates TLS and forks one `jmapfix.sh --handle` per connection.
# Each connection carries one request and is closed after the response.
# State shared across requests lives in build/fixture-run/ and is reset on
# every start: proxy-cut (flag file), events.count, blobs/<blobId>.
#
# Scripted cases (query parameters on the request URL):
#
#   ?error=401           -> HTTP 401 with WWW-Authenticate
#   ?error=429           -> HTTP 429 with Retry-After: 1
#   ?error=503           -> HTTP 503 with Retry-After: 1
#   ?error=cannotCalculateChanges -> on POST /jmap/api, every method call
#                           answers the cannotCalculateChanges error
#   ?error=stateMismatch -> same, with stateMismatch
#   ?slow=1              -> delay the response by 2 seconds
#   ?proxy=cut           -> set the cut and close without responding
#   ?proxy=restore       -> clear the cut, then serve the request
#
# A method call whose arguments carry "__error": "cannotCalculateChanges" or
# "stateMismatch" answers that error for that call only.
#
# While the cut is set every request is closed without a response, except
# POST /jmap/proxy and any request carrying proxy=restore.
#
# Routes:
#   GET  /.well-known/jmap     -> 302 to https://<Host>/jmap/session
#   GET  /rel/well-known      -> 307, lowercase `location: /jmap/session` (relative, like Stalwart)
#   GET  /rel/insecure        -> 302 to http://<Host>/jmap/session (not https)
#   GET  /html                -> 200 text/html page (a portal, not a session)
#   GET  /xhost               -> 302 to https://localhost:<port>/authcheck (another host)
#   GET  /authcheck           -> Session only when no Authorization header came; else 400
#   GET  /jmap/session         -> Session for the Host's domain (test-a.local
#                                 when the Host is not a fixture domain)
#   POST /jmap/api             -> canned method responses
#   GET  /jmap/events          -> `event: StateChange` lines with drops
#   POST /jmap/upload          -> store the body, return a blobId
#   GET  /jmap/download/<id>   -> the stored blob bytes
#   POST /jmap/proxy?cut=1     -> set the cut; ?restore=1 clears it
#   OPTIONS *                  -> 204 with CORS headers
#
# tests/support/fixtures/ is reserved for recorded responses (record mode is
# not implemented yet); nothing reads it today.

set -u
set -f
export LC_ALL=C

self_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
self="$self_dir/$(basename -- "$0")"
root=$(CDPATH='' cd -- "$self_dir/../.." && pwd)
build="$root/build"
run="$build/fixture-run"
ca_dir="$build/test-ca"
key_file="$ca_dir/test-ca.key"
cert_file="$ca_dir/test-ca.crt"
state_file="$build/fixture-state.txt"

DOMAINS="test-a.local test-b.local"
# id email name
ACCOUNTS="a1 andrew@test-a.local Andrew
a2 alice@test-a.local Alice
b1 bob@test-b.local Bob
b2 carol@test-b.local Carol
b3 dave@test-b.local Dave"

# JSON in the same byte form as a default json.dumps: ", " and ": "
# separators, non-ASCII escaped as lowercase \uXXXX.
JQ_LIB='
def hex4: [(. / 4096 | floor) % 16, (. / 256 | floor) % 16, (. / 16 | floor) % 16, . % 16]
  | map("0123456789abcdef"[.:.+1]) | join("");
def asc: explode | map(
    if . < 128 then [.] | implode
    elif . < 65536 then "\\u" + hex4
    else (. - 65536) as $v
      | "\\u" + ((55296 + ($v / 1024 | floor)) | hex4)
      + "\\u" + ((56320 + ($v % 1024)) | hex4)
    end) | join("");
def py0: if type == "object" then
    "{" + ([to_entries[] | (.key | tojson) + ": " + (.value | py0)] | join(", ")) + "}"
  elif type == "array" then "[" + (map(py0) | join(", ")) + "]"
  else tojson end;
def py: py0 | asc;
'

# ---------------------------------------------------------------- responses

reason() {
  case "$1" in
    100) echo "Continue" ;;
    200) echo "OK" ;;
    204) echo "No Content" ;;
    302) echo "Found" ;;
    307) echo "Temporary Redirect" ;;
    400) echo "Bad Request" ;;
    401) echo "Unauthorized" ;;
    404) echo "Not Found" ;;
    429) echo "Too Many Requests" ;;
    501) echo "Not Implemented" ;;
    503) echo "Service Unavailable" ;;
    *) echo "Unknown" ;;
  esac
}

status_line() {
  printf 'HTTP/1.1 %s %s\r\nServer: PavillionFixture/0.1\r\nDate: %s\r\n' \
    "$1" "$(reason "$1")" "$(date -u '+%a, %d %b %Y %H:%M:%S GMT')"
}

# send_json CODE BODY [HEADER-LINE...]
send_json() {
  local code=$1 body=$2 h
  shift 2
  status_line "$code"
  printf 'Content-Type: application/json\r\nContent-Length: %s\r\nConnection: close\r\n' "${#body}"
  for h in "$@"; do printf '%s\r\n' "$h"; done
  printf '\r\n%s' "$body"
}

# send_error CODE MESSAGE EXPLANATION (HTML error page, as for 400 and 501)
send_error() {
  local body
  body=$(printf '<!DOCTYPE HTML>\n<html lang="en">\n    <head>\n        <meta charset="utf-8">\n        <title>Error response</title>\n    </head>\n    <body>\n        <h1>Error response</h1>\n        <p>Error code: %s</p>\n        <p>Message: %s.</p>\n        <p>Error code explanation: %s - %s.</p>\n    </body>\n</html>' "$1" "$2" "$1" "$3")
  body="$body"$'\n'
  status_line "$1"
  printf 'Connection: close\r\nContent-Type: text/html;charset=utf-8\r\nContent-Length: %s\r\n\r\n%s' "${#body}" "$body"
}

not_found() {
  send_json 404 '{"error": {"code": "notFound", "description": "not found"}}'
}

# --------------------------------------------------------------- the server

session_json() {
  local domain=$1 list
  list=$(printf '%s\n' "$ACCOUNTS" | while read -r id email name; do
    case "$email" in (*"@$domain") printf '%s\t%s\t%s\n' "$id" "$email" "$name" ;; esac
  done)
  printf '%s' "$list" | jq -R -s -r --arg domain "$domain" "$JQ_LIB"'
    [split("\n")[] | select(length > 0) | split("\t") | {id: .[0], email: .[1], name: .[2]}] as $acc
    | {
        apiUrl: "https://\($domain)/jmap/api",
        downloadUrl: "https://\($domain)/jmap/download",
        uploadUrl: "https://\($domain)/jmap/upload",
        eventSourceUrl: "https://\($domain)/jmap/events",
        state: "state-1",
        primaryAccounts: {mail: (if ($acc | length) > 0 then $acc[0].id else "" end)},
        accounts: (reduce $acc[] as $a ({}; .[$a.id] = {
          id: $a.id, name: $a.name, isPersonal: true, isSystemAccount: true,
          username: $a.email, from: $a.email,
          capabilities: {
            "urn:ietf:params:jmap:mail": {},
            "urn:ietf:params:jmap:submission": {},
            "urn:ietf:params:jmap:contacts": {},
            "urn:ietf:params:jmap:calendars": {},
            "urn:ietf:params:jmap:filenode": {}
          }
        })),
        capabilities: {
          "urn:ietf:params:jmap:mail": {
            maxSizeAttachment: 26214400, maxConcurrentUpload: 6,
            maxConcurrentDownload: 6, maxSizeRequest: 10485760,
            maxSizeUpload: 26214400, maxCallsInRequest: 64,
            maxObjectsInGet: 500, maxObjectsInSet: 500, maxObjectsInQuery: 500,
            collapsable: false, maxScheduledSends: 0
          },
          "urn:ietf:params:jmap:submission": {},
          "urn:ietf:params:jmap:contacts": {},
          "urn:ietf:params:jmap:calendars": {},
          "urn:ietf:params:jmap:filenode": {}
        },
        limits: {
          maxSizeRequest: 10485760, maxSizeUpload: 26214400,
          maxConcurrentRequests: 64, maxCallsInRequest: 64,
          maxObjectsInGet: 500, maxObjectsInSet: 500, maxObjectsInQuery: 500,
          collapsable: false, maxScheduledSends: 0
        }
      } | py'
}

socat_pid=""

stop_server() {
  trap - TERM INT HUP EXIT
  if [ -n "$socat_pid" ]; then
    if command -v pkill >/dev/null 2>&1; then pkill -TERM -P "$socat_pid" 2>/dev/null || true; fi
    kill -TERM "$socat_pid" 2>/dev/null || true
    wait "$socat_pid" 2>/dev/null || true
  fi
  exit 0
}

serve() {
  local d i port ready

  for tool in socat openssl jq curl; do
    command -v "$tool" >/dev/null 2>&1 || { echo "jmapfix: $tool is required" >&2; exit 1; }
  done

  bash "$self_dir/testca.sh" "$ca_dir" || exit 1

  rm -rf "$run"
  mkdir -p "$run/blobs"
  for d in $DOMAINS; do
    session_json "$d" > "$run/session-$d.json" || { echo "jmapfix: session JSON failed" >&2; exit 1; }
  done

  trap stop_server TERM INT HUP EXIT

  ready=0
  for _ in $(seq 1 50); do
    port=$(( 20000 + RANDOM % 30000 ))
    socat OPENSSL-LISTEN:"$port",bind=127.0.0.1,reuseaddr,fork,cert="$cert_file",key="$key_file",verify=0 \
      EXEC:"$BASH $self --handle" 2>>"$run/socat.log" &
    socat_pid=$!
    for i in $(seq 1 50); do
      kill -0 "$socat_pid" 2>/dev/null || break
      if curl -sk --max-time 1 -o /dev/null "https://127.0.0.1:$port/jmap/session"; then
        ready=1
        break
      fi
      sleep 0.1
    done
    [ "$ready" = 1 ] && break
    kill "$socat_pid" 2>/dev/null || true
    wait "$socat_pid" 2>/dev/null || true
    socat_pid=""
  done
  [ "$ready" = 1 ] || { echo "jmapfix: could not listen on a free port" >&2; exit 1; }

  printf '%s\n%s\n' "$port" "$cert_file" > "$state_file.tmp"
  mv "$state_file.tmp" "$state_file"
  echo "JMAP fixture server listening on https://127.0.0.1:$port"
  wait "$socat_pid"
}

# ------------------------------------------------------- one request handler

query=""

# qget KEY: first non-blank value of KEY in the query string, URL-decoded.
qget() {
  local pair v IFS='&'
  for pair in $query; do
    case "$pair" in
      "$1="*)
        v=${pair#*=}
        [ -n "$v" ] || continue
        v=${v//+/ }
        printf '%b' "${v//%/\\x}"
        return 0
        ;;
    esac
  done
  return 1
}

is_cut() { [ -f "$run/proxy-cut" ]; }

serve_events() {
  local i
  status_line 200
  printf 'Content-Type: text/event-stream\r\nCache-Control: no-cache\r\nConnection: keep-alive\r\n\r\n'
  echo x >> "$run/events.count"
  for i in 0 1 2 3 4; do
    is_cut && exit 0
    if [ $((i % 3)) -eq 0 ]; then
      printf ': drop\n\n' || exit 0
    else
      printf 'event: StateChange\ndata: {"type": "StateChange", "accountId": "a1"}\n\n' || exit 0
    fi
    sleep 0.5
  done
  exit 0
}

serve_download() {
  local id=$1 f size
  case "$id" in
    ''|*[!A-Za-z0-9_-]*) f="" ;;
    *) f="$run/blobs/$id" ;;
  esac
  if [ -z "$f" ] || [ ! -f "$f" ]; then
    send_json 404 '{"error": {"code": "notFound", "description": "unknown blob"}}'
    return
  fi
  size=$(wc -c < "$f" | tr -d ' ')
  status_line 200
  printf 'Content-Type: application/octet-stream\r\nContent-Length: %s\r\nConnection: close\r\n\r\n' "$size"
  cat "$f"
}

read_body() {
  local out=$1
  if [ "${content_length:-0}" -gt 0 ] 2>/dev/null; then
    head -c "$content_length" > "$out"
  else
    : > "$out"
  fi
}

serve_upload() {
  local tmp id size
  tmp=$(mktemp "$run/upload.XXXXXX")
  read_body "$tmp"
  id=$(openssl dgst -sha256 -r "$tmp" | cut -c1-32)
  size=$(wc -c < "$tmp" | tr -d ' ')
  mv "$tmp" "$run/blobs/$id"
  send_json 200 "{\"blobId\": \"$id\", \"size\": $size, \"type\": \"application/octet-stream\"}"
}

serve_api() {
  local tmp out qerr
  tmp=$(mktemp "$run/api.XXXXXX")
  read_body "$tmp"
  qerr=$(qget error || true)
  out=$(jq -r -s --arg qerr "$qerr" "$JQ_LIB"'
    def errbody($c):
      if $c == "cannotCalculateChanges" then {error: {code: "cannotCalculateChanges", description: "server cannot calculate changes"}}
      else {error: {code: "stateMismatch", description: "state has changed"}} end;
    def acct: if has("accountId") then .accountId else "" end;
    def pystr: if type == "string" then . elif . == null then "None"
      elif . == true then "True" elif . == false then "False" else tojson end;
    def handle($m; $a0):
      ($a0 | if type == "object" then . else {} end) as $a
      | if $m == "Identity/get" or $m == "Mailbox/get" or $m == "Email/get" then
          [$m, {accountId: ($a | acct), list: [], notFound: [], state: "state-1"}]
        elif $m == "Email/query" then
          [$m, {accountId: ($a | acct), queryState: "qs-1", ids: [], total: 0, canCalculateChanges: true}]
        elif $m == "Mailbox/changes" or $m == "Email/changes" then
          [$m, {accountId: ($a | acct), hasMoreChanges: false, sinceState: "state-1"}]
        elif $m == "Account/set" or $m == "Account/subscribe" then
          [$m, {accountId: ($a | acct), created: {}, updated: {}, destroyed: {}, notFound: {}, state: "state-1"}]
        elif $m == "Capability/get" then [$m, {}]
        else ["error", {code: "unknownMethod", description: ("unknown method " + ($m | pystr))}, null]
        end;
    if length != 1 then "400"
    else .[0]
      | if type != "object" then "DROP"
        elif has("methodCalls") and (.methodCalls | type) != "array" then "DROP"
        else
          {methodResponses: [(.methodCalls // [])[] as $call
            | if ($call | type) != "array" or ($call | length) < 3 then
                ["error", {code: "invalidRequest"}, null]
              elif ($qerr == "cannotCalculateChanges" or $qerr == "stateMismatch") then
                ["error", errbody($qerr), $call[2]]
              elif ($call[1] | type) == "object"
                   and ($call[1].__error == "cannotCalculateChanges" or $call[1].__error == "stateMismatch") then
                ["error", errbody($call[1].__error), $call[2]]
              else handle($call[0]; $call[1]) + [$call[2]]
              end],
           sessionState: "state-1"} | py
        end
    end' < "$tmp" 2>/dev/null) || out="400"
  rm -f "$tmp"
  case "$out" in
    400|"") send_json 400 '{"error": {"code": "invalidRequest", "description": "invalid JSON"}}' ;;
    DROP) exit 0 ;;
    *) send_json 200 "$out" ;;
  esac
}

# Shared by GET and POST: cut, slow, scripted HTTP errors. Returns 0 when the
# request was answered (or dropped) here.
preamble() {
  local p e
  p=$(qget proxy || true)
  if [ "$p" = "cut" ]; then
    : > "$run/proxy-cut"
    exit 0
  fi
  if [ "$p" = "restore" ]; then
    rm -f "$run/proxy-cut"
  elif [ "$path" != "/jmap/proxy" ] && is_cut; then
    exit 0
  fi
  [ "$(qget slow || true)" = "1" ] && sleep 2
  e=$(qget error || true)
  case "$e" in
    401) send_json 401 '{"error": {"code": "unauthorized", "description": "auth required"}}' 'WWW-Authenticate: Basic realm="test"'; return 0 ;;
    429) send_json 429 '{"error": {"code": "rateLimit", "description": "too many requests"}}' 'Retry-After: 1'; return 0 ;;
    503) send_json 503 '{"error": {"code": "serverFail", "description": "temporarily unavailable"}}' 'Retry-After: 1'; return 0 ;;
  esac
  return 1
}

do_get() {
  local domain d
  preamble && return
  case "$path" in
    /.well-known/jmap)
      status_line 302
      printf 'Location: https://%s/jmap/session\r\nContent-Length: 0\r\nConnection: close\r\n\r\n' "${host:-localhost}"
      ;;
    /rel/well-known)
      status_line 307
      printf 'location: /jmap/session\r\nContent-Length: 0\r\nConnection: close\r\n\r\n'
      ;;
    /rel/insecure)
      status_line 302
      printf 'Location: http://%s/jmap/session\r\nContent-Length: 0\r\nConnection: close\r\n\r\n' "${host:-localhost}"
      ;;
    /html)
      body='<!DOCTYPE html><html><body>Portal</body></html>'
      status_line 200
      printf 'Content-Type: text/html; charset=utf-8\r\nContent-Length: %s\r\nConnection: close\r\n\r\n%s' "${#body}" "$body"
      ;;
    /xhost)
      status_line 302
      printf 'Location: https://localhost:%s/authcheck\r\nContent-Length: 0\r\nConnection: close\r\n\r\n' "${host##*:}"
      ;;
    /authcheck)
      if [ -n "$auth_seen" ]; then
        send_json 400 '{"error": {"code": "authLeaked", "description": "Authorization crossed hosts"}}'
      else
        send_json 200 "$(cat "$run/session-test-a.local.json")"
      fi
      ;;
    /jmap/session)
      domain=${host:-localhost}
      domain=${domain%%:*}
      d=test-a.local
      case " $DOMAINS " in *" $domain "*) d=$domain ;; esac
      send_json 200 "$(cat "$run/session-$d.json")"
      ;;
    /jmap/events) serve_events ;;
    /jmap/download/*) serve_download "${path#/jmap/download/}" ;;
    *) not_found ;;
  esac
}

do_post() {
  preamble && return
  case "$path" in
    /jmap/proxy)
      if [ "$(qget cut || true)" = "1" ]; then
        : > "$run/proxy-cut"
      elif [ "$(qget restore || true)" = "1" ]; then
        rm -f "$run/proxy-cut"
      fi
      send_json 200 '{"ok": true}'
      ;;
    /jmap/upload) serve_upload ;;
    /jmap/api) serve_api ;;
    *) not_found ;;
  esac
}

do_options() {
  status_line 204
  printf 'Access-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type, Authorization\r\n\r\n'
}

handle() {
  local line method target version h name value expect=""
  host="" content_length=0 path="" query=""
  auth_seen=""
  IFS= read -r -t 30 line || exit 0
  line=${line%$'\r'}
  # shellcheck disable=SC2086
  set -- $line
  if [ $# -lt 2 ] || [ $# -gt 3 ]; then
    send_error 400 "Bad request syntax ('$line')" "Bad request syntax or unsupported method"
    exit 0
  fi
  method=$1 target=$2 version=${3:-HTTP/0.9}

  while IFS= read -r -t 30 h; do
    h=${h%$'\r'}
    [ -z "$h" ] && break
    name=${h%%:*}
    value=${h#*:}
    value=${value#"${value%%[! 	]*}"}
    case "$name" in
      [Hh][Oo][Ss][Tt]) [ -n "$host" ] || host=$value ;;
      [Cc][Oo][Nn][Tt][Ee][Nn][Tt]-[Ll][Ee][Nn][Gg][Tt][Hh]) content_length=$value ;;
      [Ee][Xx][Pp][Ee][Cc][Tt]) expect=$value ;;
      [Aa][Uu][Tt][Hh][Oo][Rr][Ii][Zz][Aa][Tt][Ii][Oo][Nn]) auth_seen=1 ;;
    esac
  done

  case "$expect" in
    [1][0][0]-[Cc][Oo][Nn][Tt][Ii][Nn][Uu][Ee])
      [ "$version" = "HTTP/1.1" ] && printf 'HTTP/1.1 100 Continue\r\n\r\n'
      ;;
  esac

  path=${target%%\?*}
  path=${path%%#*}
  case "$target" in
    *\?*) query=${target#*\?}; query=${query%%#*} ;;
  esac

  case "$method" in
    GET) do_get ;;
    POST) do_post ;;
    OPTIONS) do_options ;;
    *) send_error 501 "Unsupported method ('$method')" "Server does not support this operation" ;;
  esac
  exit 0
}

case "${1:-}" in
  --handle) handle ;;
  "") serve ;;
  *) echo "usage: jmapfix.sh" >&2; exit 2 ;;
esac

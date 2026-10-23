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
# The handler reads the whole Content-Length body before it writes a
# response. Leaving those bytes unread makes close() send RST. Windows
# then discards the response that was already sent, and curl reports
# CURLE_PARTIAL_FILE with 0 bytes. Linux usually still delivers it.
# State shared across requests lives in build/fixture-run/ and is reset on
# every start: proxy-cut (flag file), events.count, blobs/<blobId>, and
# mailstate.json (Identity and Mailbox objects, changed by Mailbox/set).
# A directory lock covers the read-modify-write. socat forks one process
# per connection, so the lock cannot live in memory.
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
#   ?emails=N            -> on GET /jmap/session, a1's Inbox then has N generated
#                           messages (0 clears them). Capped at 5000.
#   ?emailHold=1|0       -> on POST /jmap/proxy, hold or release Email/query
#                           pages whose position is past 0. A held page waits
#                           up to 15 seconds.
#   ?emailDelta=1|0      -> on POST /jmap/proxy, save or delete
#                           build/fixture-run/email-delta.json. A saved body
#                           has to be a JSON object. Account a1 reads that
#                           file on Email, Thread, and Mailbox calls. A missing
#                           file is an empty object, and a mailbox-only object
#                           does not change Email/changes.
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
#   POST /jmap/api             -> Identity/Mailbox get, changes, and set
#                                 (other methods stay canned)
#   GET  /jmap/events          -> `event: StateChange` lines with drops
#   POST /jmap/upload          -> store the body, return a blobId
#   GET  /jmap/download/<id>   -> the stored blob bytes
#   POST /jmap/proxy?cut=1     -> set the cut; ?restore=1 clears it
#   OPTIONS *                  -> 204 with CORS headers
#
# tests/support/fixtures/*.json holds recorded responses from record.sh.
# A file with method, arguments, and response is served when that method
# call's arguments match. Every other call, including the scripted Mailbox
# and Identity state, is unchanged. A file with no method is ignored.

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
          name: $a.name, isPersonal: true, isReadOnly: false,
          username: $a.email, from: $a.email,
          accountCapabilities: {
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
  seed_mailstate > "$run/mailstate.json" || { echo "jmapfix: mail state failed" >&2; exit 1; }
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

# Set by slurp_body. read_body copies this file; it does not read stdin again.
body_file=""

# Take the request body off the socket before any reply. See the note at the
# top: an unread body becomes a RST, and Windows curl then sees a partial file.
slurp_body() {
  body_file="$run/body.$$"
  if [ "${content_length:-0}" -gt 0 ] 2>/dev/null; then
    head -c "$content_length" > "$body_file"
  else
    : > "$body_file"
  fi
}

read_body() {
  local out=$1
  if [ -n "${body_file:-}" ] && [ -f "$body_file" ]; then
    cat "$body_file" > "$out"
    return
  fi
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

# One directory lock shared by every forked handler. A dead holder is cleared.
lock_mailstate() {
  local d="$run/mailstate.lockd" i pid
  for i in $(seq 1 100); do
    if mkdir "$d" 2>/dev/null; then
      echo $$ > "$d/pid"
      return 0
    fi
    pid=$(cat "$d/pid" 2>/dev/null || true)
    if [ -n "${pid:-}" ] && ! kill -0 "$pid" 2>/dev/null; then
      rm -rf "$d"
      continue
    fi
    sleep 0.05
  done
  return 1
}

unlock_mailstate() {
  rm -rf "$run/mailstate.lockd"
}

seed_mailstate() {
  jq -n -c '
    def box($id; $name; $parent; $role; $order; $total; $unread):
      {id:$id, name:$name, parentId:$parent, role:$role, sortOrder:$order,
       totalEmails:$total, unreadEmails:$unread, totalThreads:$total,
       unreadThreads:$unread, isSubscribed:true};
    def ident($id; $name; $email):
      {id:$id, name:$name, email:$email, replyTo:null, bcc:null,
       textSignature:"", htmlSignature:"", mayDelete:false};
    def acct($boxes; $idents):
      {mbxState:"mbx-1", idnState:"idn-1", mbxSeq:1,
       mailboxes:$boxes, identities:$idents,
       mbxRevs:[{state:"mbx-1", created:[], updated:[], destroyed:[]}]};
    {
      trace: [],
      "test-a.local": {
        a1: acct(
          [box("in";"Inbox";null;"inbox";10;0;0),
           box("later";"Later";"in";null;20;3;1),
           box("sent";"Sent";null;"sent";30;0;0),
           box("drafts";"Drafts";null;"drafts";40;0;0)];
          [ident("idn-andrew";"Andrew";"andrew@test-a.local")]),
        a2: acct(
          [box("in";"Inbox";null;"inbox";10;0;0),
           box("sent";"Sent";null;"sent";20;0;0)];
          [ident("idn-alice";"Alice";"alice@test-a.local")])
      },
      "test-b.local": {
        b1: acct([box("in";"Inbox";null;"inbox";10;0;0)];
                 [ident("idn-bob";"Bob";"bob@test-b.local")]),
        b2: acct([box("in";"Inbox";null;"inbox";10;0;0)];
                 [ident("idn-carol";"Carol";"carol@test-b.local")]),
        b3: acct([box("in";"Inbox";null;"inbox";10;0;0)];
                 [ident("idn-dave";"Dave";"dave@test-b.local")])
      }
    }'
}

# load_recorded DIR OUT: one JSON array of every fixture object. An empty
# directory writes []. find, not a shell glob: this script sets noglob.
load_recorded() {
  local dir=$1 out=$2 f
  printf '%s\n' '[]' > "$out"
  [ -d "$dir" ] || return 0
  while IFS= read -r f; do
    [ -n "$f" ] || continue
    if jq -c -s '.[0] + [.[1]]' "$out" "$f" > "$out.next"; then
      mv "$out.next" "$out"
    else
      rm -f "$out.next"
    fi
  done <<EOF
$(find "$dir" -maxdepth 1 -type f -name '*.json' | sort)
EOF
}

serve_api() {
  local tmp out qerr domain d statef rec mbxcap emailn
  tmp=$(mktemp "$run/api.XXXXXX")
  read_body "$tmp"
  qerr=$(qget error || true)
  domain=${host:-localhost}
  domain=${domain%%:*}
  d=test-a.local
  case " $DOMAINS " in *" $domain "*) d=$domain ;; esac
  statef="$run/mailstate.json"
  # A backfill page waits here, before the state lock, so another connection
  # can clear the hold. The first page (position 0) does not wait. No sleep
  # unless the hold file exists.
  # Keep the select on the call. Mapping to a boolean leaves a false
  # element, and `length > 0` would then wait on a position-0 page too.
  if [ -f "$run/email-hold" ] && jq -e '
      [.methodCalls[]? | select(.[0] == "Email/query" and ((.[1].position // 0) > 0))] | length > 0
    ' "$tmp" >/dev/null 2>&1; then
    _i=0
    while [ -f "$run/email-hold" ] && [ "$_i" -lt 150 ]; do
      sleep 0.1
      _i=$((_i + 1))
    done
  fi
  if ! lock_mailstate; then
    rm -f "$tmp"
    send_json 503 '{"error": {"code": "serverFail", "description": "state lock"}}'
    return
  fi
  if [ ! -f "$statef" ]; then
    seed_mailstate > "$statef" || { unlock_mailstate; rm -f "$tmp"; send_json 500 '{"error":{"code":"serverFail"}}'; return; }
  fi
  rec=$(mktemp "$run/rec.XXXXXX")
  load_recorded "$root/tests/support/fixtures" "$rec"
  # ids:null is capped at the Session's maxObjectsInGet. The session handler
  # writes that number. Explicit ids are not capped.
  mbxcap=500
  if [ -f "$run/mbx-cap" ]; then
    read -r mbxcap < "$run/mbx-cap" || mbxcap=500
    case "$mbxcap" in
      ''|*[!0-9]*) mbxcap=500 ;;
    esac
  fi
  emailn=0
  if [ -f "$run/email-count" ]; then
    read -r emailn < "$run/email-count" || emailn=0
    case "$emailn" in
      ''|*[!0-9]*) emailn=0 ;;
    esac
  fi
  deltaf="$run/email-delta.json"
  if [ ! -f "$deltaf" ]; then
    deltaf="$run/email-delta-none.json"
    if [ ! -f "$deltaf" ]; then
      printf '%s\n' '{}' > "$deltaf"
    fi
  fi
  out=$(jq -c -s --arg qerr "$qerr" --arg domain "$d" --argjson mbxcap "$mbxcap" --argjson emailn "$emailn" --slurpfile st "$statef" --slurpfile recorded "$rec" --slurpfile delta "$deltaf" "$JQ_LIB"'
    def errbody($c):
      if $c == "cannotCalculateChanges" then {code: "cannotCalculateChanges", description: "server cannot calculate changes"}
      else {code: "stateMismatch", description: "state has changed"} end;
    def acct_of($a): if ($a | type) == "object" and ($a | has("accountId")) then $a.accountId else "" end;
    def pystr: if type == "string" then . elif . == null then "None"
      elif . == true then "True" elif . == false then "False" else tojson end;
    def note($state; $name):
      $state | .trace = (((.trace // []) + [$name])[-120:]);
    def changes_since($revs; $since):
      ($revs | to_entries | map(select(.value.state == $since)) | .[0].key) as $idx
      | if $idx == null then null
        else reduce ($revs | to_entries[] | select(.key > $idx)) as $e
          ({created:[], updated:[], destroyed:[]};
           .created += $e.value.created
           | .updated += $e.value.updated
           | .destroyed += $e.value.destroyed)
        end;
    def pick($items; $ids):
      if $ids == null then {list:$items, notFound:[]}
      elif ($ids | type) != "array" then {list:$items, notFound:[]}
      else
        {list:[ $items[] | select(.id as $id | $ids | index($id)) ],
         notFound:[ $ids[] | select(. as $id | [$items[].id] | index($id) | not) ]}
      end;
    def apply_mbx_set($acct; $a):
      ($a.update // {}) as $upd
      | ($a.destroy // []) as $des
      | reduce ($upd | keys_unsorted[]) as $id
          ({acct:$acct, updated:[], notFound:[]};
           if (.acct.mailboxes | map(.id) | index($id)) == null then .notFound += [$id]
           else .acct.mailboxes |= map(if .id == $id then . + ($upd[$id] | del(.id)) else . end)
                | .updated += [$id]
           end)
      | . as $mid
      | reduce $des[] as $id ($mid;
          if (.acct.mailboxes | map(.id) | index($id)) == null then .notFound += [$id]
          else .acct.mailboxes |= map(select(.id != $id)) | .destroyed = ((.destroyed // []) + [$id])
          end)
      | if ((.updated | length) > 0) or ((.destroyed // []) | length) > 0 then
          .acct.mbxSeq += 1
          | .acct.mbxState = ("mbx-" + (.acct.mbxSeq | tostring))
          | .acct.mbxRevs += [{state:.acct.mbxState, created:[], updated:.updated, destroyed:(.destroyed // [])}]
        else . end;
    def canned($m; $a):
      if $m == "Email/get" then
        [$m, {accountId: acct_of($a), list: [], notFound: [], state: "state-1"}]
      elif $m == "Email/query" then
        [$m, {accountId: acct_of($a), queryState: "qs-1", ids: [], total: 0, canCalculateChanges: true}]
      elif $m == "Email/changes" then
        [$m, {accountId: acct_of($a), oldState: ($a.sinceState // "em-1"), newState: "em-1",
              hasMoreChanges: false, created: [], updated: [], destroyed: []}]
      elif $m == "Thread/changes" then
        [$m, {accountId: acct_of($a), oldState: ($a.sinceState // "th-1"), newState: "th-1",
              hasMoreChanges: false, created: [], updated: [], destroyed: []}]
      elif $m == "Account/set" or $m == "Account/subscribe" then
        [$m, {accountId: acct_of($a), created: {}, updated: {}, destroyed: {}, notFound: {}, state: "state-1"}]
      elif $m == "Capability/get" then [$m, {}]
      else ["error", {code: "unknownMethod", description: ("unknown method " + ($m | pystr))}]
      end;
    def eindex($id):
      if ($id | type) != "string" or ($id | test("^e[0-9]+$") | not) then null
      else ($id[1:] | tonumber) end;
    def eobj($i):
      {
        id: ("e" + ($i | tostring)),
        threadId: ("t" + ($i | tostring)),
        mailboxIds: {in: true},
        keywords: (if ($i % 5) == 0 then {"$seen": true, "$flagged": true} else {"$seen": true} end),
        size: (1000 + $i),
        receivedAt: ((1700000000 + $i) | todate),
        sentAt: ((1700000000 + $i - 30) | todate),
        messageId: [("<e" + ($i | tostring) + "@test-a.local>")],
        inReplyTo: null,
        references: null,
        subject: ("Hello " + ($i | tostring)),
        from: [{name: "Ann", email: "ann@example.com"}],
        to: [{name: "Andrew", email: "andrew@test-a.local"}],
        cc: [{name: "Bea", email: "bea@example.com"}],
        bcc: [],
        replyTo: [{name: "Ann", email: "ann@example.com"}],
        preview: ("preview " + ($i | tostring)),
        hasAttachment: (if ($i % 7) == 0 then true else false end),
        blobId: ("b" + ($i | tostring))
      };
    def wants_body($a):
      ($a.fetchTextBodyValues // false) == true
      or ($a.fetchHTMLBodyValues // false) == true
      or ((($a.properties // []) | index("bodyValues")) != null);
    def with_body($a; $obj):
      if wants_body($a) then
        $obj + {
          textBody: [{partId:"1", type:"text/plain"}],
          htmlBody: [{partId:"2", type:"text/html"}],
          bodyValues: {
            "1": {value: ("Hello " + $obj.id), isEncodingProblem:false, isTruncated:false},
            "2": {value: ("<p>Hello " + $obj.id + "</p>"), isEncodingProblem:false, isTruncated:false}
          },
          bodyStructure: {
            partId:"0", type:"multipart/alternative",
            subParts: [{partId:"1", type:"text/plain"}, {partId:"2", type:"text/html"}]
          },
          attachments: [],
          "header:List-Id:asText": "list.example",
          "header:Authentication-Results:asText": "example.com; dkim=pass"
        }
      else $obj end;
    def qids($n; $pos; $lim; $desc):
      if $n <= 0 or $lim <= 0 then []
      else
        [range(0; $lim) |
          (if $desc then ($n - 1 - $pos - .) else ($pos + .) end) as $i |
          select($i >= 0 and $i < $n) | ("e" + ($i | tostring))]
      end;
    def D: if ($delta | length) > 0 and ($delta[0] | type) == "object" then $delta[0] else {} end;
    def as_arr($v): if ($v | type) == "array" then $v else [] end;
    def as_obj($v): if ($v | type) == "object" then $v else {} end;
    def d_mail($aid):
      $aid == "a1" and (
        (D | has("emailState")) or (D | has("cannotEmail")) or (D | has("update"))
        or (D | has("create")) or (D | has("destroy")) or (D | has("threadState"))
        or (D | has("cannotThread")) or (D | has("threadUpdate")) or (D | has("threadDestroy"))
      );
    def d_box($aid):
      $aid == "a1" and (
        (D | has("mailboxState")) or (D | has("mailbox")) or (D | has("mailboxChanges"))
      );
    def merge_email($base; $upd):
      if ($upd | type) != "object" then $base
      else ($base + $upd)
        | (if ($upd | has("keywords")) then .keywords = $upd.keywords else . end)
        | (if ($upd | has("mailboxIds")) then .mailboxIds = $upd.mailboxIds else . end)
      end;
    def live_emails($n):
      ([range(0; $n) | eobj(.)
         | select(.id as $id | (as_arr(D.destroy) | index($id)) | not)
         | merge_email(.; as_obj(D.update)[.id])]
       + [as_arr(D.create)[] | select((.id | type) == "string")
            | select(.id as $id | (as_arr(D.destroy) | index($id)) | not)]);
    def email_change_rows:
      ([as_arr(D.destroy)[] | select(type == "string") | {kind:"destroy", id:.}]
       + [as_obj(D.update) | keys_unsorted[] | {kind:"update", id:.}]
       + [as_arr(D.create)[] | select((.id | type) == "string") | {kind:"create", id:.id}]);
    def thread_change_rows:
      ([as_arr(D.threadDestroy)[] | select(type == "string") | {kind:"destroy", id:.}]
       + [as_arr(D.threadUpdate)[] | select((.id | type) == "string") | {kind:"update", id:.id}]);
    def slice_changes($rows; $since; $anchor; $prefix; $final; $max; $cannot):
      if $cannot == true or ($final | type) != "string" or $final == "" then "cannot"
      elif $since == $final then {created:[], updated:[], destroyed:[], more:false, new:$final}
      else
        (if $since == $anchor then 0
         elif ($since | type) == "string" and ($since | startswith($prefix)) then
           ($since | ltrimstr($prefix)) as $rest
           | if ($rest | test("^[0-9]+$")) then ($rest | tonumber) else -1 end
         else -1 end) as $start
        | if $start < 0 or $start > ($rows | length) then "cannot"
          else
            (if ($max | type) != "number" or $max < 1 then 1 else $max end) as $lim
            | $rows[$start:$start+$lim] as $slice
            | ($start + ($slice | length)) as $next
            | ($next < ($rows | length)) as $more
            | {created:[$slice[] | select(.kind == "create") | .id],
               updated:[$slice[] | select(.kind == "update") | .id],
               destroyed:[$slice[] | select(.kind == "destroy") | .id],
               more:$more,
               new:(if $more then ($prefix + ($next | tostring)) else $final end)}
          end
      end;
    def changes_out($state; $m; $aid; $since; $got):
      if $got == "cannot" then
        [note($state; ($m + ":cannot")),
         ["error", {type:"cannotCalculateChanges", description:"server cannot calculate changes"}]]
      else
        [note($state; ($m + ":" + (($got.created | length) | tostring) + ":"
              + (($got.updated | length) | tostring) + ":" + (($got.destroyed | length) | tostring)
              + ":" + (if $got.more then "more" else "done" end))),
         [$m, {accountId:$aid, oldState:($since // ""), newState:$got.new, hasMoreChanges:$got.more,
               created:$got.created, updated:$got.updated, destroyed:$got.destroyed}]]
      end;
    def one($state; $m; $a):
      acct_of($a) as $aid
      | ($state[$domain][$aid] // null) as $acct
      | if $m == "Mailbox/get" or $m == "Identity/get" or $m == "Mailbox/changes" or $m == "Identity/changes" or $m == "Mailbox/set" or $m == "Mailbox/query" then
          if $acct == null then
            if $m == "Mailbox/set" then
              [$state, ["error", {code: "accountNotFound", description: "unknown account"}]]
            elif $m == "Mailbox/query" then
              [$state, [$m, {accountId:$aid, queryState:"mbx-1", canCalculateChanges:false,
                             position:0, ids:[], total:0}]]
            elif $m == "Mailbox/changes" or $m == "Identity/changes" then
              [$state, [$m, {accountId:$aid, oldState:($a.sinceState // ""), newState:"mbx-1",
                             hasMoreChanges:false, created:[], updated:[], destroyed:[]}]]
            else
              [$state, [$m, {accountId:$aid, state:"mbx-1", list:[], notFound:[]}]]
            end
          elif $m == "Mailbox/query" then
            (($a.position // 0) | if . < 0 then 0 else . end) as $pos
            | (($a.limit // 500) | if . < 1 then 1 else . end) as $lim
            | ($acct.mailboxes | map(.id)) as $ids
            | ($ids | length) as $total
            | [note($state; "Mailbox/query"),
               [$m, {accountId:$aid, queryState:$acct.mbxState, canCalculateChanges:false,
                     position:$pos, ids:$ids[$pos:$pos+$lim],
                     total:(if $a.calculateTotal == true then $total else null end)}]]
          elif $m == "Mailbox/get" then
            (if ($a | has("ids")) and ($a.ids != null) then $acct.mailboxes else $acct.mailboxes[0:$mbxcap] end) as $boxes
            | (pick($boxes; (if $a | has("ids") then $a.ids else null end))) as $p
            | (if d_box($aid) then [$p.list[] | . + (as_obj(D.mailbox)[.id] // {})] else $p.list end) as $listed
            | [note($state; "Mailbox/get"),
               [$m, {accountId:$aid, state:$acct.mbxState, list:$listed, notFound:$p.notFound}]]
          elif $m == "Identity/get" then
            (pick($acct.identities; (if $a | has("ids") then $a.ids else null end))) as $p
            | [note($state; "Identity/get"),
               [$m, {accountId:$aid, state:$acct.idnState, list:$p.list, notFound:$p.notFound}]]
          elif $m == "Mailbox/changes" then
            if d_box($aid) and ((D.mailboxState | type) == "string") and ($a.sinceState // "") == D.mailboxState then
              [note($state; "Mailbox/changes"),
               [$m, {accountId:$aid, oldState:D.mailboxState, newState:D.mailboxState,
                     hasMoreChanges:false, created:[], updated:[], destroyed:[]}]]
            elif d_box($aid) and ((D.mailboxState | type) == "string")
                 and ($a.sinceState // "") == $acct.mbxState
                 and ((as_arr(D.mailboxChanges) | length) > 0) then
              [note($state; "Mailbox/changes"),
               [$m, {accountId:$aid, oldState:$acct.mbxState, newState:D.mailboxState,
                     hasMoreChanges:false, created:[], updated:as_arr(D.mailboxChanges), destroyed:[]}]]
            else
              changes_since($acct.mbxRevs; ($a.sinceState // "")) as $ch
              | if $ch == null then
                  [note($state; "Mailbox/changes"),
                   ["error", {type: "cannotCalculateChanges", description: "server cannot calculate changes"}]]
                else
                  [note($state; "Mailbox/changes"),
                   [$m, {accountId:$aid, oldState:($a.sinceState // ""), newState:$acct.mbxState,
                         hasMoreChanges:false, created:$ch.created, updated:$ch.updated, destroyed:$ch.destroyed}]]
                end
            end
          elif $m == "Identity/changes" then
            if ($a.sinceState // "") == $acct.idnState then
              [note($state; "Identity/changes"),
               [$m, {accountId:$aid, oldState:$acct.idnState, newState:$acct.idnState,
                     hasMoreChanges:false, created:[], updated:[], destroyed:[]}]]
            else
              [note($state; "Identity/changes"),
               ["error", {type: "cannotCalculateChanges", description: "server cannot calculate changes"}]]
            end
          else
            apply_mbx_set($acct; $a) as $done
            | ($state | .[$domain][$aid] = $done.acct) as $next
            | [note($next; "Mailbox/set"),
               [$m, {accountId:$aid, oldState:$acct.mbxState, newState:$done.acct.mbxState,
                     created:{}, updated:(reduce $done.updated[] as $id ({}; .[$id] = null)),
                     destroyed:($done.destroyed // []), notFound:$done.notFound}]]
          end
        elif $m == "Email/query" or $m == "Email/get" or $m == "Thread/get"
             or $m == "Email/changes" or $m == "Thread/changes" then
          (if $aid == "a1" and $emailn > 0 then $emailn else 0 end) as $n
          | if d_mail($aid) and ($m == "Email/changes" or $m == "Thread/changes") then
              (($a.maxChanges // 500) | if type != "number" then 500 else . end) as $max
              | (if $m == "Email/changes" then
                   slice_changes(email_change_rows; ($a.sinceState // ""); "em-1"; "em-part-";
                                 (D.emailState // ""); $max; (D.cannotEmail == true))
                 else
                   slice_changes(thread_change_rows; ($a.sinceState // ""); "th-1"; "th-part-";
                                 (D.threadState // ""); $max; (D.cannotThread == true))
                 end) as $got
              | changes_out($state; $m; $aid; ($a.sinceState // ""); $got)
            elif d_mail($aid) and $m == "Email/query" then
              (($a.position // 0) | if . < 0 then 0 else . end) as $pos
              | (($a.limit // 500) | if . < 1 then 1 else . end) as $ask
              | (if $ask > $mbxcap then $mbxcap else $ask end) as $lim
              | ((($a.sort // []) | if type == "array" and length > 0 then .[0] else {} end)) as $sort
              | ($sort.property == "receivedAt" and $sort.isAscending == false) as $desc
              | ($a.filter.inMailbox // "") as $box
              | (live_emails($n)
                 | map(select($box != "" and (((.mailboxIds // {})[$box] // false) == true)))
                 | sort_by(.receivedAt) | if $desc then reverse else . end) as $ordered
              | ($ordered | length) as $total
              | ($ordered[$pos:$pos+$lim] | map(.id)) as $ids
              | [note($state; ("Email/query:" + ($pos | tostring) + ":" + ($ask | tostring))),
                 [$m, {accountId:$aid, queryState:(D.emailState // "em-1"), canCalculateChanges:false,
                       position:$pos, ids:$ids, total:$total}]]
            elif d_mail($aid) and $m == "Email/get" then
              (live_emails($n)) as $all
              | (if ($a.ids | type) == "array" then $a.ids else [] end) as $ids
              | (reduce $ids[] as $id ({list:[], notFound:[]};
                  (first($all[] | select(.id == $id)) // null) as $hit
                  | if $hit == null then .notFound += [$id] else .list += [with_body($a; $hit)] end)) as $p
              | [note($state; "Email/get"),
                 [$m, {accountId:$aid, state:(D.emailState // "em-1"), list:$p.list, notFound:$p.notFound}]]
            elif d_mail($aid) then
              (live_emails($n)) as $all
              | (if ($a.ids | type) == "array" then $a.ids else [] end) as $ids
              | (reduce $ids[] as $id ({list:[], notFound:[]};
                  if ($id | type) != "string" or ((as_arr(D.threadDestroy) | index($id)) != null) then
                    .notFound += [$id]
                  else
                    (first(as_arr(D.threadUpdate)[] | select(.id == $id)) // null) as $upd
                    | if $upd != null then .list += [$upd]
                      else
                        ([$all[] | select(.threadId == $id) | .id]) as $eids
                        | if ($eids | length) == 0 then .notFound += [$id]
                          else .list += [{id:$id, emailIds:$eids}] end
                      end
                  end)) as $p
              | [note($state; "Thread/get"),
                 [$m, {accountId:$aid, state:(D.threadState // "th-1"), list:$p.list, notFound:$p.notFound}]]
            elif $m == "Email/changes" or $m == "Thread/changes" then
              [$state, canned($m; $a)]
            elif $m == "Email/query" then
              (($a.position // 0) | if . < 0 then 0 else . end) as $pos
              | (($a.limit // 500) | if . < 1 then 1 else . end) as $ask
              | (if $ask > $mbxcap then $mbxcap else $ask end) as $lim
              | ((($a.sort // []) | if type == "array" and length > 0 then .[0] else {} end)) as $sort
              | ($sort.property == "receivedAt" and $sort.isAscending == false) as $desc
              | (if $n > 0 and (($a.filter.inMailbox // "") == "in") then $n else 0 end) as $total
              | (qids($total; $pos; $lim; $desc)) as $ids
              | [note($state; ("Email/query:" + ($pos | tostring) + ":" + ($ask | tostring))),
                 [$m, {accountId:$aid, queryState:"em-1", canCalculateChanges:false,
                       position:$pos, ids:$ids, total:$total}]]
            elif $m == "Email/get" then
              (if ($a.ids | type) == "array" then $a.ids else [] end) as $ids
              | (reduce $ids[] as $id ({list:[], notFound:[]};
                  (eindex($id)) as $i
                  | if $i == null or $n <= 0 or $i < 0 or $i >= $n then .notFound += [$id]
                    else .list += [with_body($a; eobj($i))] end)) as $p
              | [note($state; "Email/get"),
                 [$m, {accountId:$aid, state:"em-1", list:$p.list, notFound:$p.notFound}]]
            else
              (if ($a.ids | type) == "array" then $a.ids else [] end) as $ids
              | (reduce $ids[] as $id ({list:[], notFound:[]};
                  if ($id | type) == "string" and ($id | test("^t[0-9]+$")) and $n > 0
                     and (($id[1:] | tonumber) < $n) then
                    .list += [{id:$id, emailIds:[("e" + $id[1:])]}]
                  else .notFound += [$id] end)) as $p
              | [note($state; "Thread/get"),
                 [$m, {accountId:$aid, state:"th-1", list:$p.list, notFound:$p.notFound}]]
            end
        else
          [$state, canned($m; $a)]
        end;
    def canon:
      if type == "object" then
        to_entries | sort_by(.key) | map({key: .key, value: (.value | canon)}) | from_entries
      elif type == "array" then map(canon)
      else . end;
    def pointer($obj; $path):
      if $obj == null or ($path | type) != "string" then null
      else reduce ($path | split("/") | map(select(length > 0)))[] as $p
        ($obj; if . == null then null else .[$p] end)
      end;
    def deref($args; $done):
      if ($args | type) != "object" then {}
      else reduce ($args | keys_unsorted[]) as $k ({};
        if ($k | startswith("#")) and (($args[$k] | type) == "object") then
          ($done[$args[$k].resultOf] // null) as $prev
          | .[$k[1:]] = (if $prev == null or ($prev.name != $args[$k].name) then null
                        else pointer($prev.args; $args[$k].path) end)
        else .[$k] = $args[$k] end)
      end;
    def recorded_for($m; $a):
      first(($recorded[0] // [])[]
        | select((.method // "") == $m)
        | select((((.arguments // {}) | canon | tojson) == ($a | canon | tojson)))
        | .response) // null;
    if ($st | length) != 1 or ($st[0] | type) != "object" then {fail:"400"}
    elif length != 1 then {fail:"400"}
    else .[0] as $req
      | if ($req | type) != "object" then {fail:"DROP"}
        elif ($req | has("methodCalls")) and ($req.methodCalls | type) != "array" then {fail:"DROP"}
        else
          reduce ($req.methodCalls // [])[] as $call ({st:$st[0], res:[], done:{}};
            if ($call | type) != "array" or ($call | length) < 3 then
              .res += [["error", {code:"invalidRequest"}, null]]
            elif (($call[0] == "Identity/get") or ($call[0] == "Identity/changes"))
                 and ((($req.using // []) | index("urn:ietf:params:jmap:submission")) == null) then
              .res += [["error", {type: "unknownMethod"}, $call[2]]]
            elif ($qerr == "cannotCalculateChanges" or $qerr == "stateMismatch") then
              .res += [["error", {error: errbody($qerr)}, $call[2]]]
            elif ($call[1] | type) == "object"
                 and ($call[1].__error == "cannotCalculateChanges" or $call[1].__error == "stateMismatch") then
              .res += [["error", {error: errbody($call[1].__error)}, $call[2]]]
            else
              (if ($call[1] | type) == "object" then $call[1] else {} end) as $raw
              | (deref($raw; .done)) as $args
              | (recorded_for($call[0]; $raw)) as $hit
              | if $hit != null then
                  .res += [[$call[0], $hit, $call[2]]]
                  | .done[$call[2]] = {name: $call[0], args: $hit}
                else
                  one(.st; $call[0]; $args) as $got
                  | .st = $got[0]
                  | .res += [$got[1] + [$call[2]]]
                  | .done[$call[2]] = {name: $got[1][0], args: $got[1][1]}
                end
            end)
          | {state:.st, body:{methodResponses:.res, sessionState:"state-1"}}
        end
    end' < "$tmp" 2>"$run/api.err") || out=""
  rm -f "$tmp" "$rec" "$rec.next"
  if [ -z "$out" ] || [ "$(printf '%s' "$out" | jq -r '.fail // empty' 2>/dev/null)" = "400" ]; then
    unlock_mailstate
    send_json 400 '{"error": {"code": "invalidRequest", "description": "invalid JSON"}}'
    return
  fi
  if [ "$(printf '%s' "$out" | jq -r '.fail // empty' 2>/dev/null)" = "DROP" ]; then
    unlock_mailstate
    exit 0
  fi
  printf '%s' "$out" | jq -c '.state' > "$statef.tmp" && mv "$statef.tmp" "$statef"
  unlock_mailstate
  send_json 200 "$(printf '%s' "$out" | jq -c '.body')"
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
      cap=500
      if qcap=$(qget maxObjectsInGet || true); then
        case "$qcap" in
          ''|*[!0-9]*) cap=500 ;;
          *) cap=$qcap ;;
        esac
      fi
      if [ "$cap" -lt 1 ] || [ "$cap" -gt 500 ]; then cap=500; fi
      printf '%s\n' "$cap" > "$run/mbx-cap"
      ecn=0
      if qn=$(qget emails || true); then
        case "$qn" in
          ''|*[!0-9]*) ecn=0 ;;
          *) ecn=$qn ;;
        esac
      fi
      if [ "$ecn" -gt 5000 ]; then ecn=5000; fi
      printf '%s\n' "$ecn" > "$run/email-count"
      # The file names apiUrl after the fixture domain. That name does not
      # resolve. Serve the host the client actually connected to, port included.
      # maxObjectsInGet lives on the core capability. A query argument can
      # lower it so a test can force a second mailbox page.
      send_json 200 "$(jq -c --arg host "${host:-localhost}" --argjson cap "$cap" \
        '.apiUrl = "https://\($host)/jmap/api"
         | .downloadUrl = "https://\($host)/jmap/download"
         | .uploadUrl = "https://\($host)/jmap/upload"
         | .eventSourceUrl = "https://\($host)/jmap/events"
         | .capabilities["urn:ietf:params:jmap:core"] = {
             maxSizeUpload: 50000000, maxConcurrentUpload: 4,
             maxSizeRequest: 10000000, maxConcurrentRequests: 4,
             maxCallsInRequest: 16, maxObjectsInGet: $cap,
             maxObjectsInSet: 500, collationAlgorithms: []
           }' \
        "$run/session-$d.json")"
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
      case "$(qget emailHold || true)" in
        1) : > "$run/email-hold" ;;
        0) rm -f "$run/email-hold" ;;
      esac
      case "$(qget emailDelta || true)" in
        1)
          if [ -n "${body_file:-}" ] && [ -f "$body_file" ] && jq -e 'type == "object"' "$body_file" >/dev/null 2>&1; then
            cp "$body_file" "$run/email-delta.json"
          fi
          ;;
        0) rm -f "$run/email-delta.json" ;;
      esac
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

  # After 100 Continue, so a client that waits for it still sends the body.
  slurp_body
  trap 'rm -f "$body_file"' EXIT

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

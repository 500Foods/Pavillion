# Pavillion API

API version: 3

`PAVILLION_API_VERSION` goes up when this contract or the C surface in the G L2 Lock changes incompatibly: a function is removed or changed, or a command or event is removed, renamed, or given a different meaning. Adding a command, an event, or an optional field does not bump it. The shell refuses to start when `pavillion_api_version()` disagrees with the number it was built against.

G-2 added `pavillion_call`, `pavillion_free`, and `pav_on_event`. G-3 removed the shell's callers of the older C entry points. G-8 removed those declarations from this header (`pavillion_status`, `pavillion_prefs_get`, `pavillion_prefs_apply`, `pavillion_ui_get`, `pavillion_ui_set`, `pavillion_icons_get`, `pavillion_icons_apply`, `pavillion_kit_get`, `pavillion_kit_apply`, `pav_ui_log`, `PavillionStatus`, and `PavShellPrefs`). That removal is version 3. The header now also has `pav_protocol` (`PavBytes`) and `pav_cli_main`.

## Conventions

Commands are `noun.verb` in lowercase with dots (`mailbox.list`). Events are `noun.change` (`mail.messages_changed`). Wire fields are camelCase. C structs stay snake_case.

`pavillion_call` blocks until the core thread replies. Arguments are one JSON object, at most 2 MB. A missing value, a non-object, or invalid JSON is `bad_args`. The reply is UTF-8 JSON, freed with `pavillion_free`:

```json
{"ok": true, "data": {}}
{"ok": false, "error": {"code": "bad_args", "message": "unknown command no.such", "retryable": false}}
```

An "absolute path" argument (`caFile`, `log.export` and `export.write_text` `path`, the themes folder) is one that starts with `/`; on Windows (2026-10-20, G-6 CI) it may also be a drive path (`C:\x` or `C:/x`) or a UNC path (`\\server\share`), and both `/` and `\` separate folders. Before this, Windows drive paths were refused as `bad_args`.

`retryable` is true only for `network` and for some `server` errors. A full command queue is `internal` and returns at once.

Commands run on the core thread unless the dispatcher marks them `worker`. `debug.activity_demo` is the worker command. Bulk bodies and attachments do not cross this boundary as JSON; they use a file path or the `pav-cid` and `pav-blob` schemes.

`pav_on_event` registers one callback. The event thread calls it, so a slow host does not block the core thread. The callback only queues work. An event posted with no callback, or before the event thread is running, is dropped and counted. The event queue holds 256 payloads of at most 64 KB each.

`test_api_md_lists_every_command` reads this file. Under `## Commands`, each command is a `### name` heading followed by a `State: real (...)` or `State: planned (...)` line. Real headings are the dispatcher table. Planned headings are not callable. A later phase adds its command here in the same shape and moves `planned` to `real` when the handler exists.

## Errors

| Code | When | Retryable |
| --- | --- | --- |
| `bad_args` | The command, the arguments, or a field is missing or invalid, including an unknown command | no |
| `not_found` | The id is not in the local store | no |
| `not_ready` | The core is stopped or the profile is not loaded | no |
| `auth` | The server rejected the credentials | no |
| `network` | The transport failed before a response | yes |
| `server` | The server returned an error. Retryable only when the handler says so | sometimes |
| `conflict` | The server state moved (`stateMismatch` and the same class) | no |
| `forbidden` | The profile contains a secret field, or the action is not allowed | no |
| `too_large` | The JSON, the UI blob, or a message exceeds its cap | no |
| `cancelled` | The caller cancelled the activity | no |
| `internal` | The queue is full, or the core failed without a more specific code | no |

## Commands

### account.add

State: real (K-2)

Creates an account and stores its secret in the keyring. When `id` is absent it is generated from `name` (lowercase, non-alphanum to `-`, deduplicated with a `-2` suffix).

Args: `{name?, kind, sessionUrl, username, auth?, secret, color?, syncMail?, syncContacts?, syncCalendars?, fetchBodies?, enabled?, caFile?}`. `caFile` is an absolute path to a PEM CA bundle trusted for this account (B Lock); it is omitted from the account object when unset. `auth` defaults to `basic` and is the only K-2 method. `kind` is `jmap` (imap is accepted but unsupported). `sessionUrl` must be `https://`. `color` is `#rgb` or `#rrggbb`. `fetchBodies` is `none`, `recent`, or `all`. Booleans default true except `enabled` which defaults true. `name` is required when `id` is absent.

From 2026-10-19 (Andrew): `email?` is the account's address, stored on every account as a string. It is `""` or `name@domain` (under 256 bytes); absent, it defaults to `username` when that looks like an address, else `""`.

From 2026-10-19 (Andrew, K-Q9): `icon?` is an HTML snippet of Font Awesome markup, `""` for the default envelope. The interim form writes `<i class="fa-solid fa-name"></i>`; the Icon Wizard (D-9) will write larger snippets in the same key. Accepted: `""`, or under 4096 bytes, starting with `<`, without `<script`, an `on…=` attribute, or `javascript:`; otherwise `bad_args`. The page sanitizes it before drawing. Stored on every account; patchable with `account.update`.

From 2026-10-19 (K-Q10): `capabilities?` (array of capability words; others dropped) and `capabilitiesChecked?` (UTC ISO 8601) store a Test made before Add; absent means `[]` and `null`.

From 2026-10-19 (Andrew): `email?` is the account's address, stored on every account as a string. It is `""` or `name@domain` (under 256 bytes); absent, it defaults to `username` when that looks like an address, else `""`.

Result: the account object (without the secret). Includes `number`.

Errors: `bad_args`, `forbidden` (secret in profile), `failed` (keyring), `internal`. A keyring failure rolls back the profile write and the `account_state` row insertion. A profile write failure deletes the keyring entry.

### account.discover

State: real (K-Q5)

`{email, caFile?}` returns `{sessionUrl, via}`. Each candidate gets one attempt (no net retries). `via` is `"srv"` or `"well-known"`. Tries the SRV record `_jmap._tcp.<domain>` (`https://<target>[:port]/.well-known/jmap`), then `https://<domain>/.well-known/jmap`. No credentials are sent. Redirects follow the `account.test` rules; the final response must be JSON with an `apiUrl`, and `sessionUrl` is that final URL. Runs as an activity of kind `account.discover` (title `Configure <domain>`); logs the domain and the found host at `INFO`. It does not write the profile.

Errors: `bad_args` (missing or invalid email), `not_found` (neither SRV nor well-known gave a JMAP session; the message names the last failure), `network`, `server`.


### account.list

State: real (K-2)

From 2026-10-19: every account in the result carries `email`; an account saved before the field existed gets the username default (K-Q6).

From 2026-10-19 (K-Q10): every account also carries `capabilities` (array of capability words) and `capabilitiesChecked` (UTC ISO 8601 or `null`); older accounts get `[]` and `null`.

Lists the accounts on the active profile. Secrets are not included.

Args: `{}`.

Result: `{accounts: [account]}`.

### account.remove

State: real (K-2)

`{id, purgeLocal?}`. Removes the keyring entry, the profile entry, and the `account_state` row. When `purgeLocal` is true, also deletes rows in `remote_account`, `identity`, `mailbox`, and `sync_state` belonging to the account (cascading).

Result: `{id}`.

Errors: `bad_args`, `not_found`.

### account.set_secret

State: real (K-2)

`{id, secret}`. Replaces the keyring value for that account. The secret is never written to the profile or the database.

Result: `{id}`.

Errors: `bad_args`, `not_found`, `failed` (keyring).

### account.test

State: real (M-2)

`{sessionUrl | email, username, auth?, secret, caFile?}` returns `{ok, capabilities[], error?}`. `caFile` pins a CA bundle for this test; without it the system store is used (the test build also honours `PAV_FIXTURE_CA`). It does not write the profile. `capabilities` holds `mail`, `submission`, `contacts`, `calendars`, `sieve`, `files`, and `websocket` for what the Session advertises: `urn:ietf:params:jmap:mail`, `…:submission`, `…:contacts`, `…:calendars`, `…:sieve`, `…:websocket`, and for `files` `urn:ietf:params:jmap:filenode` (JMAP File Storage, 2026-10-19) or the older aliases `urn:ietf:params:jmap:files`, `urn:ietf:params:jmap:files:0`, and `urn:apache:jmap:files`.

From 2026-10-19 (Andrew): any failure, including an early one (bad redirect, queue or memory failure, timeout), returns a not-ok reply with an error code; it never returns `ok` without data. Redirects (301, 302, 307, 308, at most 3) read `Location` case-insensitively and resolve a relative value (`/jmap/session`, `//host/path`, or a bare path) against the request URL; the target must still be `https://`. Each test runs as an activity of kind `account.test` (title `Test <host>`), ended `done` or `failed` with the error message. Log lines name the session host (`account.test ok: localhost:1443`); the username appears only at `DEBUG`, per the H redaction rule for email addresses.

From 2026-10-19 (Andrew): a 200 that is not JSON (an HTML `Content-Type`, or a body whose first non-space byte is `<`) fails with `server`, "the server returned a web page, not a JMAP session (check the URL)". A redirect that changes scheme, host, or port drops the `Authorization` header for the rest of the chain, logged at `DEBUG` with both hosts. Each request's state (request, headers, URL, reply) is heap-owned and shared by the waiter and the net thread with a reference count: when the 30 s wait gives up, the waiter cancels the request and lets go; a reply that arrives later is dropped and logged at `DEBUG` (`late reply dropped`), never written to freed memory.

From 2026-10-19 (Andrew, K-Q7): the result also carries `sessionUrl`, the URL that answered after redirects, and `authSent` (false when a redirect to another host dropped `Authorization`, so the credentials were not checked).

From 2026-10-19 (K-Q10): when `id` names a saved account and the test succeeds with `authSent` true, the core stores `capabilities` and `capabilitiesChecked` on that account (profile write on the core thread, before the reply). A failure or `authSent` false stores nothing.

From 2026-10-19 (Andrew): `id?` names a saved account. When `secret` is absent or empty and `id` is given, the test uses that account's saved secret from the secret store; a typed `secret` still wins. Errors: `not_found` (no such account), `locked`, `bad_args` "no saved password for this account". The secret is never returned or logged.


### account.update

State: real (K-2)

`{id, patch}`. Changes account fields other than the secret. `sessionUrl`, `username`, `color`, `syncMail`, `syncContacts`, `syncCalendars`, `fetchBodies`, `enabled`, `name`, `email`, `icon`, and `caFile` may be patched. `caFile: ""` removes the field. `kind` and `auth` are not patchable.

Result: the updated account object (without the secret).

Errors: `bad_args`, `not_found`.

### activity.cancel

State: real (I-1)

`{id}`. `id` is `act-<n>`. Sets the cancel flag when the activity is `running` or `queued` and `cancellable` is true. The state stays as it is until the operation ends itself. Cancel does not kill a thread.

Result: the activity object.

Errors: `bad_args`, `not_found`.

### activity.dismiss

State: real (I-1)

`{id}`. The activity must be `failed`. Sets dismissed. It leaves the default list and stays in history until the 500-row cap drops it.

Result: the activity object.

Errors: `bad_args`, `not_found`.

### activity.list

State: real (I-1)

`{includeHistory?}`. Omitted or false, the result is `running`, `queued`, and `failed` activities that have not been dismissed. True adds the finished history loaded from the profile database.

Result: `{activities:[activity]}`. Each activity is:

```json
{ "id": "act-42", "kind": "sync.mail", "accountId": "a-500foods-andrew",
  "title": "Sync andrew@500foods.com", "detail": "Inbox: headers 300 of 1,200",
  "state": "running", "startedMs": 1760630581452, "endedMs": null,
  "progress": { "done": 300, "total": 1200, "unit": "messages" },
  "etaMs": 4200, "cancellable": true, "error": null, "parentId": null,
  "dismissed": false }
```

`state` is `queued`, `running`, `done`, `failed`, or `cancelled`. `dismissed` is true only after `activity.dismiss`. `kind` is `sync.mail`, `sync.contacts`, `sync.calendar`, `sync.files`, `body.fetch`, `attachment.download`, `cache.evict`, `files.upload`, `files.download`, `send`, `index.rebuild`, `rules.run`, `import`, `export`, or `retention`. A later phase adds a kind here in the same sitting that first uses it. `progress` is null until the first progress report. `progress.total` and `etaMs` are null when the total is unknown. `etaMs` stays null until two progress samples exist. `parentId` is `act-<n>` or null. `endedMs` and `error` are null while the activity is unfinished. Finished rows are reloaded from SQLite on the next start. Running rows are not.

Errors: `bad_args`.

### addressbook.list

State: planned (S-1)

Lists address books for the signed-in accounts.

### attachment.open

State: planned (N-7)

Copies one part into the runtime open directory and asks the host to open it.

### attachment.save

State: planned (N-7)

`{messageId, partId, destPath?}`. Writes the part to disk. The host supplies the path when `destPath` is omitted.

### calendar.list

State: planned (T-1)

Lists calendars, including color and account.

### calendar.set

State: planned (T-4)

Creates or updates one calendar.

### contact.autocomplete

State: planned (S-4)

`{prefix, limit}`. Returns matching contacts and recent addresses.

### contact.delete

State: planned (S-3)

Deletes one contact through the op queue.

### contact.export

State: planned (S-4)

`{format:"vcard"|"jscontact"}`. Writes an export the host can save.

### contact.get

State: planned (S-1)

Returns one contact card, preserving unknown JSContact fields.

### contact.import

State: planned (S-4)

Imports vCard or JSContact.

### contact.list

State: planned (S-1)

Lists contacts for the current address-book view.

### contact.save

State: planned (S-3)

Creates or updates one contact through the op queue.

### debug.activity_demo

State: real (I-1)

Debug builds only. A build with `NDEBUG` does not register this command. `make test` builds the core without `NDEBUG`.

`{steps, ms}`. `steps` is an integer from 1 to 10000. `ms` is an integer from 0 to 60000, the pause between progress reports. This command is `worker`. It begins a cancellable `sync.mail` activity titled `Demo activity` and returns before the steps run. The stepper honors cancel. At most 8 demos are outstanding in one session.

Result: `{id}`. `id` is `act-<n>`.

Errors: `bad_args`, `internal`.

### debug.secret_smoke

State: real (K-1)

Debug builds only. A build with `NDEBUG` does not register this command. `make test` builds the core without `NDEBUG`.

`{op, user, label?, value?, expect?}`. `op` is `set`, `get`, or `delete`. `user` is the account key. `set` requires `label` and `value`. `get` compares `expect` when it is present and does not return the secret.

Result: `{state}` for `set` and `delete` (`ok` or `missing`). `{state, match}` for `get`. `state` is `stored` or `missing`. `match` is true only when the stored value equals `expect`.

Errors: `bad_args`, `locked`, `failed`, `internal`.

### draft.attach

State: planned (O-4)

`{draftId, path, inline}`. Attaches a file, or marks it inline.

### draft.discard

State: planned (O-1)

Discards a local draft and queues deletion of the server copy.

### draft.new

State: planned (O-1)

`{mode, sourceId?, identityId?}`. Starts a new, reply, reply-all, or forward draft.

### draft.save

State: planned (O-1)

Saves the draft locally and queues the server copy.

### draft.send

State: planned (O-5)

`{draftId, sendAt?}`. Queues a final save, then hands the draft to the outbox.

### event.delete

State: planned (T-4)

Deletes one event, with the same scope fields as `event.save`.

### event.export

State: planned (T-5)

Exports events as `.ics`.

### event.get

State: planned (T-1)

Returns one event.

### event.import

State: planned (T-5)

Imports `.ics`.

### event.range

State: planned (T-3)

`{calendarIds, start, end, tz}`. Returns occurrences in that window.

### event.save

State: planned (T-4)

`{event, scope:"this"|"following"|"all", occurrenceStart?}`.

### export.write_text

State: real (D-10, 2026-10-20)

Writes a page-built text export (for example the Accounts CSV) to a file the user picked in the host save dialog (`host.dialog.save`). The page never writes files itself: a WebKitGTK blob download does nothing in the Tauri window, and there is no fs plugin.

Args: `{path, text}`. `path` follows the `log.export` rules: absolute, under 4096 bytes, no CR or LF, not ending in `/`, not a directory, and its parent directory exists. `text` is a UTF-8 string of at most 8 MiB (JSON strings are UTF-8, and an embedded NUL is refused). The file is written whole through the platform private writer (`0600`, replaced if present). Callers must never pass secrets; the accounts export carries no username or password and account rows never hold a secret.

Result: `{bytes}`, the number of bytes written.

Logs one `INFO` line (`export`): `wrote <bytes> bytes to <file name>` (the name only, not the directory). Failures log `WARN` with the code.

Errors: `bad_args`, `too_large`, `internal` (cannot write).

### files.delete

State: planned (U-2)

Deletes a file or an empty folder after the UI confirms.

### files.download

State: planned (U-2)

Streams a file to a local path. The bytes are not part of the JSON reply.

### files.get

State: planned (U-1)

Returns metadata for one file node.

### files.list

State: planned (U-1)

`{accountId, parentId}`. Lists one folder.

### files.mkdir

State: planned (U-2)

Creates a folder.

### files.move

State: planned (U-2)

Moves or renames a file node.

### files.upload

State: planned (U-2)

Streams a local file into a folder. The bytes are not part of the JSON arguments.

### identity.list

State: real (M-3)

Args: `{accountId?}`. Omitted lists every account. An unknown id returns an empty list.

Result: `{identities:[{accountId, raId, id, name, email, replyTo, bcc, mayDelete}]}`. `replyTo` and `bcc` are the stored JSON or null. `mayDelete` is a boolean. Signatures are stored and are not returned.

Errors: `bad_args`, `not_ready`, `internal`.

### link.open

State: planned (N-4)

`{url}`. Opens a confirmed link in the system browser. The UI asks first.

### log.client

State: real (G-2)

Replaces `pav_ui_log` for hosts that speak JSON.

Args: `{subsystem, level, message}`. `subsystem` matches `^[a-z][a-z0-9_]{0,15}$`. `level` is `error`, `warn`, `info`, `debug`, or `trace`. `message` is a string of at most 2000 bytes. CR, LF, and CRLF become U+23CE. The formatted line keeps that message. The core accepts 50 lines per second (H-4): later lines in that second are dropped, still answer `{}`, and are counted into one `WARN ui log.client dropped N lines over 50 per second` at the next accepted second. Every line is redacted and collapses with an identical previous line like any `pav_log` line.

Result: `{}`.

Errors: `bad_args`, `too_large`, `not_ready`, `internal`.

### log.export

State: real (H-3, from H-4 reads log.db)

Writes the lines that match a filter from the profile's `log.db`, oldest first, through the log thread (H-4). The bytes are C-CORE file lines with UTC stamps, including each trailing newline. When `log.db` is not open (no profile attached) it writes the matching ring lines instead. The view uses this same predicate for the lines it shows. Copy uses those UTC lines. Print draws the on-screen line instead.

Args: `{path, filter}`. `path` is an absolute file path whose parent directory exists. `filter.levels` is an array of `ERROR`, `WARN`, `INFO`, `DEBUG`, and `TRACE`. A line is kept only when its level is in that array. An empty array keeps nothing. `filter.subsystems` is an array of subsystem names, or null. Null and omitted both keep every subsystem. An empty array keeps nothing. `filter.text` is a string of at most 2000 bytes. Omitted or empty, it keeps every line. Otherwise the line is kept when the file line, without its trailing newline, contains that text. The match is literal and ASCII case-insensitive. `filter.since`, `filter.until`, and `filter.session` are as in `log.query`, so Copy, Save, and Print use the view's query.

Result: `{lines}`. `lines` is how many lines were written.

Errors: `bad_args`, `too_large`, `internal`.

### log.query

State: real (H-4)

Reads older lines from the profile's `log.db` through the log thread ([H-LOGGING.md](H-LOGGING.md) Lock). `log.tail` stays the ring.

Args: `{beforeSeq?, since?, until?, limit, levels?, subsystems?, text?, session?}`. `since` and `until` are UTC milliseconds (`since <= ts < until`; either omitted is open), read through the `ts` index; the Log view passes its time range (default the last 12 hours) when the user widens it. The result is the newest matching lines with a `seq` below `beforeSeq`, oldest first. `session` is `current`, `previous`, `all` (default), or a session id. `limit` is required, an integer from 1 to 10000. `beforeSeq` is an integer >= 1. `levels`, `subsystems`, and `text` are as in `log.export`'s filter; `levels` omitted keeps every level.

Result: `{lines:[{seq, ts, level, subsystem, text, session}], sessions:{"<id>": startedMs}, currentSession}`: the `log.tail` row shape plus each row's session id, the start (UTC ms) of each session in the page, and this run's id (separators, H-4). Fewer than `limit` lines means there is nothing older in range. `previous` with no earlier run returns no lines.

Errors: `bad_args`, `not_ready` (no `log.db` open), `internal`.

### log.stats

State: real (H-4)

Reads the size of the profile's `log.db` through the log thread ([H-LOGGING.md](H-LOGGING.md) Lock, Retention).

Args: `{}`. Returns `{bytes, records, oldestSeq, oldestTs, capMb}`. `bytes` is `(page_count - freelist_count) * page_size`. `oldestTs` is UTC milliseconds or null when empty. `capMb` is `preferences.log_max_mb`. `oldestSeq` is null when empty.

Errors: `bad_args`, `not_ready` (no `log.db` open).

### log.tail

State: real (H-1)

Reads the in-memory ring, not the file. The ring keeps the last 10,000 lines, the same bytes the log thread wrote.

Args: `{sinceSeq?, limit?, minLevel?, subsystems?}`. `sinceSeq` is an integer; the result is lines with a greater `seq`. Omitted, it is 0. `limit` is an integer from 1 to 10000 and defaults to 10000. The result is the newest matching lines, oldest first. `minLevel` is `error`, `warn`, `info`, `debug`, or `trace` and defaults to `debug`; a line numerically above it is left out (`error` is the most severe). `subsystems` is an array of subsystem names as they appear in the file (`core`, `ui`, `config`). Omitted, every subsystem is kept.

Result: `{lines:[{seq, ts, level, subsystem, text}]}`. `seq` counts from 1 for the life of the process. `ts` is the file's UTC stamp. `level` is `ERROR`, `WARN`, `INFO`, or `DEBUG`. `text` is the message without the trailing newline. The file line is the stamp, a space, the level padded to five characters, a space, the subsystem, two spaces, and the message.

The log thread also posts `log.lines` with that same object, at most one batch per 100 ms. There is no `log.line` event. A batch waits so a burst is one event; shutdown posts whatever is still waiting.

Errors: `bad_args`, `not_ready`, `internal`.

### log.trim

State: real (H-4)

Trims `log.db` oldest first until it fits the cap, then runs `incremental_vacuum`, on the same path as shutdown.

Args: `{mb?}`. With `mb` (integer 1 to 100) it writes `preferences.log_max_mb` and trims to it (Settings Apply). Without `mb` it trims to the saved cap and writes nothing (Settings Trim Now). Returns the `log.stats` result after the trim. Other values are `bad_args`.

Errors: `bad_args`, `not_ready`, `internal`.

### log.trim_preview

State: real (H-4)

Deletes nothing. Args: `{mb}` (integer 1 to 100). Returns `{bytes, records, oldestTs}` estimated for a trim to `mb`, from per-row size estimates scaled to the measured size (about 10 % accuracy). The Settings slider calls it while moving. `oldestTs` is null when nothing would be kept.

Errors: `bad_args`, `not_ready`.

### mailbox.create

State: planned (M-6)

Creates a mailbox through `Mailbox/set`.

### mailbox.delete

State: planned (M-6)

Deletes a mailbox through `Mailbox/set`.

### mailbox.list

State: real (M-3)

Args: `{accountId?}`. Omitted lists every profile account. An unknown id returns `{nodes:[]}`.

Result: `{nodes:[{id, parentId, kind, label, role, order, unread, total, hasChildren, swatch}]}`. One root per profile account (`kind` `account`, `id` the account id, `swatch` the account color or null, `order` the account's index). `unread` and `total` on that root are the sums of that account's mailbox counts. Mailbox `id` is `mbx:<accountId>/<jmapAccountId>:<mailboxId>`, `kind` `mailbox`, `swatch` null, `role` null when the mailbox has none. A mailbox whose parent is missing hangs off the account root. `order` is the sibling index after `sortOrder`, then role (inbox, drafts, sent, archive, junk, trash, then any other role, then none), then name, then id.

Errors: `bad_args`, `not_ready`, `internal`.

### mailbox.move

State: planned (M-6)

Reparents a mailbox.

### mailbox.rename

State: planned (M-6)

Renames a mailbox.

### mailbox.subscribe

State: planned (M-6)

Subscribes or unsubscribes a mailbox.

### message.archive

State: planned (M-6)

Moves messages to the Archive mailbox, creating it when the user confirms.

### message.delete

State: planned (M-6)

`{ids, permanent}`. Moves to Trash, or destroys when `permanent` is true.

### message.get

State: planned (N-1)

`{id, allowRemote}`. Returns text, parts, and headers. `htmlSanitized` stays null until N-2.

### message.list

State: planned (N-3)

`{view:{kind:"mailbox",id}|{kind:"unified",role}|{kind:"search",query}, offset, limit, threaded}`. `search` arrives in Q-1. Threaded rows arrive in N-6.

### message.move

State: planned (M-6)

Moves messages to another mailbox.

### message.raw

State: planned (N-7)

Writes the raw `.eml` into the cache and returns its path.

### message.set_flags

State: planned (M-6)

`{ids, seen?, flagged?, answered?, keywords?:{add,remove}}`.

### outbox.list

State: planned (O-5)

Lists outbox rows and their state.

### outbox.retry

State: planned (O-5)

Retries a failed send.

### cache.usage

State: real (L-2)

Bytes on disk for cached message text and documents, each account's server counts and sizes, and free space on the storage volume.

Args: `{}`, `{measure?: boolean}`, or `{cancel: true}`. Any other key is `bad_args`. `measure` and `cancel` must be booleans. `cancel` true together with `measure` is `bad_args`. `{}` is the quick read. `{measure: true}` also walks the mail and file sizes the quota does not already split. `{cancel: true}` stops that walk and does not start another. Its result is `{"cancelled": true}`.

Result:

```json
{
  "bodiesBytes": 0,
  "blobsBytes": 0,
  "volumeFreeBytes": null,
  "sizesCheckedAt": null,
  "accounts": [
    {
      "id": "andrew-500foods-com",
      "name": "Andrew",
      "bodiesBytes": 0,
      "blobsBytes": 0,
      "serverBytes": null,
      "mail": { "count": null, "bytes": null, "bodiesBytes": null, "attachmentsBytes": null, "attachmentsCount": null },
      "contacts": { "count": null, "bytes": null },
      "calendars": { "count": null, "bytes": null },
      "files": { "count": null, "bytes": null }
    }
  ]
}
```

`bodiesBytes` and `blobsBytes` are the files the cache limits measure. A missing file counts as 0. One blob file counts once. The totals are the sum of the account rows. Each store object is that account's count and size on the server. `null` means this read does not have the figure. `serverBytes` is the account-wide octet total when the server reports one quota for more than one kind. `volumeFreeBytes` is the free space on the volume that holds the blob directory, or the profile file when that directory is not there. Null when the platform cannot read it. The page builds two columns. Database is `mail.count`, contact `count`, and calendar `count`, and `mail.bodiesBytes` plus contact and calendar `bytes` when those sizes are integers. Document is `mail.attachmentsCount` plus file `count`, and `mail.attachmentsBytes` plus file `bytes` when those sizes are integers. A pool size, and the Document entry count, stay blank until the mail split for that pool is known. A Total row sums each column on its own, leaving out a missing figure. The two columns are not added together. `serverBytes` is not shown.

For an account whose `kind` is missing or `jmap`, and whose stored session has an `https` `apiUrl`, the quick read posts `Quota/get` when `urn:ietf:params:jmap:quota` is present, and `Email/query`, `ContactCard/query`, `CalendarEvent/query`, and `FileNode/query` (`calculateTotal`, `limit` 1) for each capability the raw session advertises. A query `total` is the count and wins over a quota count. An octets or count quota is that column only when every type is the same kind: `Email` or `Mail`, `ContactCard` or `Contact`, `CalendarEvent` or `Calendar`, `FileNode` or `File`. Any other type, or a mix, stays in `serverBytes` and is not labeled as one store. A single-kind octets quota is that store's `bytes`. Calendars and contacts have no other size. A count of 0 is `bytes` 0.

`measure` walks `Email/get` and `FileNode/get` when that store's bytes are still null. The mail get asks for `size`, `textBody`, `htmlBody`, and `attachments`, with `bodyProperties` of `size` only. It does not fetch body values. `mail.bytes` stays the whole message. `mail.bodiesBytes` is the decoded text plus the decoded HTML. `mail.attachmentsBytes` is the decoded attachment sizes. `mail.attachmentsCount` is how many of those attachment parts were counted. A missing part list, or a part without an integer `size`, fails that mail walk and keeps the previous split. A count of 0 is 0 for both. A single-kind octets quota does not skip the mail walk, because a quota does not separate bodies from attachments. The page size is the session's `maxObjectsInGet`, and at most 500. One request carries several windows: half of `maxCallsInRequest`, at most 8, and at most 4 for mail. At most 1,000 windows. A window that stops early (no ids before the total is covered, a missing integer `size`, a non-empty `notFound`, a repeated id, a short window before the total, or the window cap) leaves that store's bytes unset and keeps the previous value. While the walk runs, `cache.change` reports the account, the store, and the running sum, at most every 250 ms, and once more when a store finishes. Cancel stops the walk. A store that finished is kept. A store that did not finish keeps its previous size. The result may include `"cancelled": true`. The last finished walk is stored in `kv` key `limits.server`. `sizesCheckedAt` is its unix millisecond time, or null when none has finished. The quick read fills a null mail or file size from that cache when the session still advertises the store. It also fills `mail.bodiesBytes` and `mail.attachmentsBytes` from `mailBodiesBytes` and `mailAttachmentsBytes` when both are stored. It fills `mail.attachmentsCount` from `mailAttachmentsCount` when that integer is stored. A missing count does not drop the byte split.

Any other `kind`, including `imap`, leaves the five server fields null. M-8 and the later calendar and contacts phases write the same fields. This command does not speak IMAP, CalDAV, or CardDAV. One account's failed read does not fail the command.

Errors: `bad_args`, `not_ready`, `internal`.

### prefs.get

State: real (G-2)

Reads the window and the preferences the core interprets. `preferences.ui` is not included.

Args: `{}`.

Result:

```json
{
  "timezone": "America/Vancouver",
  "weekStart": "monday",
  "timeFormat": "24h",
  "theme": "system",
  "density": "comfortable",
  "windowMaximized": false,
  "windowWidth": 1200,
  "windowHeight": 800,
  "windowX": null,
  "windowY": null,
  "chrome": "system",
  "logLevel": "info",
  "cache": { "bodiesMb": 2048, "blobsMb": 4096 }
}
```

`windowX` and `windowY` are null when the position is unset. `chrome` is `system` or `hidden`. `logLevel` is the index `log_level`: `error`, `warn`, `info`, `debug`, or `trace`. From H-4 it is the Log view's default level filter only; every level is captured.

Errors: `bad_args`, `not_ready`, `internal`.

### prefs.set

State: real (G-2)

Writes the same fields. Keys that are absent stay as stored. `{}` is `bad_args`. An unknown key is `bad_args`. Profile fields are written once. `logLevel` is written on the index and applied to the running process as the view default; it does not change what is captured. A request that only sets `logLevel` does not write the profile.

Args: any subset of the `prefs.get` result. Booleans are JSON booleans. Width, height, and coordinates are integers. `windowX` or `windowY` set to null clears that coordinate. Strings must be non-empty and fit the profile field (`timezone` 63 bytes, `weekStart` 15, `timeFormat` 7, `theme` 15, `density` 15, `chrome` 15). Width and height are 200 to 10000. A set coordinate is -32000 to 32000. `chrome` is `system` or `hidden`. `logLevel` is `error`, `warn`, `info`, or `debug`. `cache` is an object with `bodiesMb`, `blobsMb`, or both. Each is an integer from 1 to 1048576. An empty `cache` object, or a key other than those two, is `bad_args`. Absent cache keys stay as stored.

Result: the `prefs.get` object after the write.

Errors: `bad_args`, `not_ready`, `internal`.

### prefs.ui.get

State: real (G-2)

Args: `{}`.

Result: the stored `preferences.ui` object. A missing object is `{}`. The core does not add or clamp keys.

Errors: `bad_args`, `not_ready`, `internal`.

### prefs.ui.set

State: real (G-2)

Replaces `preferences.ui` with the argument object. This is not a merge. The object is at most 64 KB of compact JSON and must not contain `password`, `token`, `secret`, or `refresh_token`.

Args: the new object.

Result: the stored object.

Errors: `bad_args`, `forbidden`, `too_large`, `not_ready`, `internal`.

### reminder.snooze

State: planned (T-5)

Snoozes a reminder by 5, 10, or 30 minutes.

### retention.get

State: planned (N-9)

Returns the per-mailbox retention rules.

### retention.set

State: planned (N-9)

Stores one mailbox's retention rule.

### rule.compile_preview

State: planned (R-3)

Compiles one rule to Lua source without saving it.

### rule.delete

State: planned (R-3)

Deletes one rule.

### rule.export_sieve

State: planned (R-5)

Exports simple rules as Sieve for Stalwart.

### rule.list

State: planned (R-3)

Lists the profile's rules in order.

### rule.order

State: planned (R-3)

Sets the rule order.

### rule.run

State: planned (R-4)

`{mailboxId, dryRun}`. Runs the enabled rules on one mailbox.

### rule.save

State: planned (R-3)

Creates or updates one rule.

### script.list

State: planned (R-2)

Lists scripts in the profile manifest.

### script.run

State: planned (R-2)

Runs one script. The script thread returns intents; it does not write the store.

### script.save

State: planned (R-2)

Saves one script. `minApi` is optional.

### search.run

State: planned (Q-1)

`{query, scope, server, limit}`. Local FTS5 when `server` is false.

### sender.allow_remote

State: planned (N-4)

Remembers that remote images may load for a sender or a domain.

### signature.list

State: planned (O-4)

Lists signatures by identity.

### signature.set

State: planned (O-4)

Stores one identity's signature.

### sync.now

State: real (M-3)

`{accountId?}`. `accountId`, when present, is a non-empty string. Starts a Session sync for every enabled JMAP account, or for `accountId` when that account is enabled. An unknown or disabled id does nothing. An empty profile records no activity.

Result: `{}`.

Errors: `bad_args`, `internal`.

### system.status

State: real (G-2)

Args: `{}`.

Result: `{profileName, profileId, state, error, debug, selftest}`. `state` is `ready` or `error`. `error` is empty when the profile loaded. A profile that failed to load is still `ok: true`; `state` carries the failure. `not_ready` means the core thread is not running. `debug` and `selftest` are booleans. The shell sets `PAVILLION_DEBUG=1` for a debug build and `PAVILLION_SELFTEST=1` when `--selftest` is passed. `pavillion_start` reads each variable once. The value is true only when the variable is `1`.

Errors: `bad_args`, `not_ready`, `internal`.

### system.version

State: planned (not sequenced)

Returns the API version and the program version. No phase owns this command yet.

### thread.get

State: planned (N-6)

Returns one thread, oldest message first.

### undo.run

State: planned (M-6)

`{opId}`. Runs the undo payload for a held or applied op.

### themes.ensure

State: real (D-6)

Args: `{files}`. `files` is an array of `{name, text}`. `name` is one path component, ends with `.theme`, does not start with `.`, and is at most 200 characters. `text` is at most 256 KB.

Result: `{wrote}`. `wrote` lists the names that were created. A name that is already on disk is left unchanged, including when the text differs. The files are written into the themes folder from the index, or into the working directory when `themes_folder` is absent. A missing folder writes nothing and returns `wrote: []`. It does not create the folder and it does not fail startup.

Errors: `bad_args`, `not_ready`, `internal`.

### themes.folder.set

State: real (D-6)

Args: `{folder}`. `folder` is a string. Empty clears `themes_folder` on the index, and themes are read from the working directory. Any other value must be an existing directory. A relative path is resolved from the working directory. The index stores the string that was sent.

Result: `{folder}`. `folder` is the absolute directory that `themes.list` will read.

Errors: `bad_args`, `not_ready`, `internal`.

### themes.list

State: real (D-6)

Args: `{}`.

Result: `{folder, chosen, fallback, themes, missing?}`. `folder` is the absolute directory being read. `chosen` is the stored `themes_folder` string, or `""` when the index has none. `fallback` is the working directory, which is the directory read when `chosen` is empty. `themes` is `[{name, text}]`, sorted by name, one level only, and only names that end with `.theme`. A file over 256 KB, a file that cannot be read, or a file that is not text is skipped. At most 128 files are returned. `missing` is true when that directory cannot be read. `themes` is then empty. Startup still succeeds. `chosen` and `fallback` are still present when `missing` is true.

Errors: `bad_args`, `not_ready`, `internal`.

### window.initial

State: real (G-8)

Args: `{monitors?}`. `monitors` is an array of `{x, y, w, h}` in logical pixels, at most 16. Omit it when the host has none.

Result: `{x, y, w, h, maximized}`. `x` and `y` are null when the profile has no saved position. A new profile is `w` 1200, `h` 800, and `maximized` false. When `monitors` is present the reply is clamped onto the monitor that holds the window center. This command does not write the profile.

Errors: `bad_args`, `not_ready`, `internal`.

### window.report

State: real (G-8)

Args: `{x?, y?, w, h, maximized, monitors?}`. `w` and `h` are required integers. `maximized` is a boolean. `x` and `y` are optional integers. `monitors` matches `window.initial`.

Result: `{}`. The core clamps the rectangle onto the reported monitors, or onto the absolute ranges when none are sent. It writes `windowMaximized`, `windowWidth`, `windowHeight`, `windowX`, and `windowY` 500 ms after the last report. `maximized` true keeps the rectangle already remembered and sets only the flag. Quitting inside that 500 ms still writes. `chrome` is left as stored.

Errors: `bad_args`, `not_ready`, `internal`.

## Events

Events are not commands. Payloads are JSON objects. No event is emitted yet; G-2 only delivers what a caller posts.

### activity.changed

Kind: event
State: real (I-1)

`{activity}`. The activity object from `activity.list`. Posted on begin, on end, on dismiss, and on progress when at least 250 ms have passed since the last event for that same activity. Begin, end, and dismiss are not throttled.

### cache.change

Kind: event
State: real (L-2)

`{accountId, store, done, total, bytes, scanned, scanTotal}`. Posted while Check sizes walks one account. `store` is `mail` or `files`. `done` and `total` are that store. `bytes` is the running sum. `scanned` and `scanTotal` are the whole check. Posted at most every 250 ms, and once without that wait when a store finishes.

### accounts.changed

Kind: event
State: real (K-2)

`{accounts:[account]}`. Posted after any account mutation (add, update, remove, set_secret). Each account object omits the secret.

### app.mailto

Kind: event
State: planned (O-6)

`{url}`. A second instance forwarded a `mailto:` URL.

### calendar.changed

Kind: event
State: planned (T-1)

Calendars or events changed.

### calendar.reminder

Kind: event
State: planned (T-5)

`{eventId, occurrenceStart, title, location?}`.

### contacts.changed

Kind: event
State: planned (S-1)

Address books or contacts changed.

### core.state

Kind: event
State: planned (not sequenced)

The core published a new readiness state. No phase emits this yet.

### files.changed

Kind: event
State: planned (U-1)

A file listing changed.

### link.clicked

Kind: event
State: real (F-4)

`{paneId, n}`. Emitted by `pav-link://<paneId>/<n>`. `paneId` is 1 to 64 characters from `A-Za-z0-9_-`. `n` is an integer from 0 through 2^53−1. The scheme reply is 204 with an empty body. Any other URL is 404 and emits nothing.

### log.lines

Kind: event
State: real (H-4)

`{lines:[{seq, ts, level, subsystem, text}, ...]}`. At most one batch per 100 ms. There is no `log.line` event.

### mail.mailboxes_changed

Kind: event
State: real (M-3)

`{accountId}`. Emitted when a sync writes or destroys mailbox rows. A changes page that only advances state does not emit it.

### mail.messages_changed

Kind: event
State: planned (M-4)

`{accountId, created, updated, destroyed}`. Emitted after rules, or after the page budget.

### mail.new_mail

Kind: event
State: planned (M-4)

`{accountId, mailboxId, summaries}`. Inbox-role mail (and mailboxes marked notify) after rules have filed it.

### outbox.update

Kind: event
State: planned (O-5)

`{outboxId, state:"queued"|"sending"|"sent"|"failed", error?}`.

### queue.conflict

Kind: event
State: planned (M-6)

`{opId, kind, detail}`.

### rule.disabled

Kind: event
State: planned (R-3)

`{ruleId, reason}`. The rule failed three times in an hour.

### sync.state

Kind: event
State: real (M-3)

`{accountId, state, detail}`. `state` is `idle`, `syncing`, `offline`, `locked`, `error:auth`, `error:server`, or `disabled`. `detail` is a short string and is empty when idle.

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

State: planned (K-2)

`{name, kind, sessionUrl, username, auth, secret}`. Writes the keyring first, then the profile. The result does not include the secret.

### account.list

State: planned (K-2)

Lists the accounts on the active profile. Secrets are not included.

### account.remove

State: planned (K-2)

`{id, purgeLocal}`. Removes the account and, when asked, its local rows.

### account.set_secret

State: planned (K-2)

`{id, secret}`. Replaces the keyring value for that account.

### account.test

State: planned (M-2)

`{sessionUrl, username, auth, secret}` returns `{ok, capabilities[], error?}`. It does not write the profile.

### account.update

State: planned (K-2)

`{id, patch}`. Changes account fields other than the secret.

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

`state` is `queued`, `running`, `done`, `failed`, or `cancelled`. `dismissed` is true only after `activity.dismiss`. `kind` is `sync.mail`, `sync.contacts`, `sync.calendar`, `sync.files`, `body.fetch`, `attachment.download`, `files.upload`, `files.download`, `send`, `index.rebuild`, `rules.run`, `import`, `export`, or `retention`. A later phase adds a kind here in the same sitting that first uses it. `progress` is null until the first progress report. `progress.total` and `etaMs` are null when the total is unknown. `etaMs` stays null until two progress samples exist. `parentId` is `act-<n>` or null. `endedMs` and `error` are null while the activity is unfinished. Finished rows are reloaded from SQLite on the next start. Running rows are not.

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

State: planned (M-3)

`{accountId?}`. Lists identities synced for those accounts.

### link.open

State: planned (N-4)

`{url}`. Opens a confirmed link in the system browser. The UI asks first.

### log.client

State: real (G-2)

Replaces `pav_ui_log` for hosts that speak JSON.

Args: `{subsystem, level, message}`. `subsystem` matches `^[a-z][a-z0-9_]{0,15}$`. `level` is `error`, `warn`, `info`, or `debug`. `message` is a string of at most 2000 bytes. CR, LF, and CRLF become U+23CE. The formatted line keeps that message.

Result: `{}`.

Errors: `bad_args`, `too_large`, `not_ready`, `internal`.

### log.export

State: real (H-3)

Writes the ring lines that match a filter. The bytes are the file lines, including each trailing newline. The ring is not changed. The view uses this same predicate for the lines it shows. Copy uses those UTC lines. Print draws the on-screen line instead.

Args: `{path, filter}`. `path` is an absolute file path whose parent directory exists. `filter.levels` is an array of `ERROR`, `WARN`, `INFO`, and `DEBUG`. A line is kept only when its level is in that array. An empty array keeps nothing. `filter.subsystems` is an array of subsystem names, or null. Null and omitted both keep every subsystem. An empty array keeps nothing. `filter.text` is a string of at most 2000 bytes. Omitted or empty, it keeps every line. Otherwise the line is kept when the file line, without its trailing newline, contains that text. The match is literal and ASCII case-insensitive.

Result: `{lines}`. `lines` is how many lines were written.

Errors: `bad_args`, `too_large`, `internal`.

### log.tail

State: real (H-1)

Reads the in-memory ring, not the file. The ring keeps the last 10,000 lines, the same bytes the log thread wrote.

Args: `{sinceSeq?, limit?, minLevel?, subsystems?}`. `sinceSeq` is an integer; the result is lines with a greater `seq`. Omitted, it is 0. `limit` is an integer from 1 to 10000 and defaults to 10000. The result is the newest matching lines, oldest first. `minLevel` is `error`, `warn`, `info`, or `debug` and defaults to `debug`; a line numerically above it is left out (`error` is the most severe). `subsystems` is an array of subsystem names as they appear in the file (`core`, `ui`, `config`). Omitted, every subsystem is kept.

Result: `{lines:[{seq, ts, level, subsystem, text}]}`. `seq` counts from 1 for the life of the process. `ts` is the file's UTC stamp. `level` is `ERROR`, `WARN`, `INFO`, or `DEBUG`. `text` is the message without the trailing newline. The file line is the stamp, a space, the level padded to five characters, a space, the subsystem, two spaces, and the message.

The log thread also posts `log.lines` with that same object, at most one batch per 100 ms. There is no `log.line` event. A batch waits so a burst is one event; shutdown posts whatever is still waiting.

Errors: `bad_args`, `not_ready`, `internal`.

### mailbox.create

State: planned (M-6)

Creates a mailbox through `Mailbox/set`.

### mailbox.delete

State: planned (M-6)

Deletes a mailbox through `Mailbox/set`.

### mailbox.list

State: planned (M-3)

`{accountId?}`. Returns tree nodes (`id`, `parentId`, `kind`, `label`, `role`, `order`, `unread`, `total`, `hasChildren`, `swatch`).

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
  "logLevel": "info"
}
```

`windowX` and `windowY` are null when the position is unset. `chrome` is `system` or `hidden`. `logLevel` is the index `log_level`: `error`, `warn`, `info`, or `debug`.

Errors: `bad_args`, `not_ready`, `internal`.

### prefs.set

State: real (G-2)

Writes the same fields. Keys that are absent stay as stored. `{}` is `bad_args`. An unknown key is `bad_args`. Profile fields are written once. `logLevel` is written on the index and applied to the running process. A request that only sets `logLevel` does not write the profile.

Args: any subset of the `prefs.get` result. Booleans are JSON booleans. Width, height, and coordinates are integers. `windowX` or `windowY` set to null clears that coordinate. Strings must be non-empty and fit the profile field (`timezone` 63 bytes, `weekStart` 15, `timeFormat` 7, `theme` 15, `density` 15, `chrome` 15). Width and height are 200 to 10000. A set coordinate is -32000 to 32000. `chrome` is `system` or `hidden`. `logLevel` is `error`, `warn`, `info`, or `debug`.

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

State: real (I-2)

`{accountId?}`. `accountId`, when present, is a non-empty string. I-2 accepts it and does nothing. M-3 runs the sync.

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
State: planned (H-1)

`{lines:[{seq, ts, level, subsystem, text}, ...]}`. At most one batch per 100 ms. There is no `log.line` event.

### mail.mailboxes_changed

Kind: event
State: planned (M-3)

The mailbox tree changed.

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
State: planned (M-3)

`{accountId, state:"idle"|"syncing"|"offline"|"error", detail?}`.

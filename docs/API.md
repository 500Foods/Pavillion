# Pavillion API

API version: 2

`PAVILLION_API_VERSION` goes up when this contract or the C surface in the G L2 Lock changes incompatibly: a function is removed or changed, or a command or event is removed, renamed, or given a different meaning. Adding a command, an event, or an optional field does not bump it. The shell refuses to start when `pavillion_api_version()` disagrees with the number it was built against.

G-2 added `pavillion_call`, `pavillion_free`, and `pav_on_event`. The older C entry points (`pavillion_status`, `pavillion_prefs_get`, `pavillion_prefs_apply`, `pavillion_ui_get`, `pavillion_ui_set`, `pavillion_icons_get`, `pavillion_icons_apply`, `pavillion_kit_get`, `pavillion_kit_apply`, `pav_ui_log`) stay until G-3 removes their callers. Removing them is the G-8 bump.

## Conventions

Commands are `noun.verb` in lowercase with dots (`mailbox.list`). Events are `noun.change` (`mail.messages_changed`). Wire fields are camelCase. C structs stay snake_case.

`pavillion_call` blocks until the core thread replies. Arguments are one JSON object, at most 2 MB. A missing value, a non-object, or invalid JSON is `bad_args`. The reply is UTF-8 JSON, freed with `pavillion_free`:

```json
{"ok": true, "data": {}}
{"ok": false, "error": {"code": "bad_args", "message": "unknown command no.such", "retryable": false}}
```

`retryable` is true only for `network` and for some `server` errors. A full command queue is `internal` and returns at once.

Commands run on the core thread unless the dispatcher marks them `worker`. No command is `worker` yet. Bulk bodies and attachments do not cross this boundary as JSON; they use a file path or the `pav-cid` and `pav-blob` schemes.

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

State: planned (I-1)

`{id}`. Cancels a running activity.

### activity.list

State: planned (I-1)

`{includeHistory}`. Returns the in-memory activity registry.

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

Args: `{subsystem, level, message}`. `subsystem` matches `^[a-z][a-z0-9_]{0,15}$`. `level` is `error`, `warn`, `info`, or `debug`. `message` is a string of at most 2000 bytes. CR, LF, and CRLF become U+23CE. The formatted log line is still capped by `pav_log`'s buffer until H-1.

Result: `{}`.

Errors: `bad_args`, `too_large`, `not_ready`, `internal`.

### log.tail

State: planned (H-1)

`{sinceSeq?, limit<=10000, minLevel?, subsystems?}`. Reads the in-memory ring, not the file.

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
  "chrome": "system"
}
```

`windowX` and `windowY` are null when the position is unset. `chrome` is `system` or `hidden`.

Errors: `bad_args`, `not_ready`, `internal`.

### prefs.set

State: real (G-2)

Writes the same fields. Keys that are absent stay as stored. `{}` is `bad_args`. An unknown key is `bad_args`. The profile is written once.

Args: any subset of the `prefs.get` result. Booleans are JSON booleans. Width, height, and coordinates are integers. `windowX` or `windowY` set to null clears that coordinate. Strings must be non-empty and fit the profile field (`timezone` 63 bytes, `weekStart` 15, `timeFormat` 7, `theme` 15, `density` 15, `chrome` 15). Width and height are 200 to 10000. A set coordinate is -32000 to 32000. `chrome` is `system` or `hidden`.

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

State: planned (I-2)

`{accountId?}`. I-2 returns success and does nothing. M-3 runs the sync.

### system.status

State: real (G-2)

Args: `{}`.

Result: `{profileName, profileId, state, error}`. `state` is `ready` or `error`. `error` is empty when the profile loaded. A profile that failed to load is still `ok: true`; `state` carries the failure. `not_ready` means the core thread is not running.

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

## Events

Events are not commands. Payloads are JSON objects. No event is emitted yet; G-2 only delivers what a caller posts.

### activity.changed

Kind: event
State: planned (I-1)

The activity registry changed. Coalesced to at most one event per 250 ms.

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
State: planned (F-4)

`{paneId, n}`. Emitted by `pav-link`. The scheme reply is 204.

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

# B — CONFIG

**Status:** B-2 complete (2026-10-14). Next phase in this document is B-3.
**Resume:** The table of contents names the one phase that may be worked. If that id is not in this file, do not start here.

The working directory, `Pavillion.json`, and profile files. The loader is C, called from the core. The shell never reads these files.

## Phases

One phase is one session. Do not start the next one in the same conversation.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| B-1 | Parse the index and a profile | Easy | Complete |
| B-2 | First-run files | Easy | Complete |
| B-3 | Scan, kind, and a broken active profile | Medium | Not started |
| B-4 | Paths and file modes | Medium | Not started |
| B-5 | An unknown engine is an error | Easy | Not started |

Kind: **Easy** is one module and a Unity test. **Medium** is the filesystem rules. **Hard** is unused in this document.

## How a session works

1. Read Resume in `PAVILLION_PLAN.md`, the Status column above, and the previous phase's Status, Accomplished, Lessons learned, and Handoff.
2. Re-read only this phase: Goal, Work items, Done means, Exit gate. Open the lock below when the phase cites it.
3. Ask the qualifying questions. Wait for approval before editing source.
4. Do this phase only. Check a work item `[x]` only after its check has passed. Defer with `[~]`, one line, and the phase it moves to.
5. Before marking complete, write Status, Accomplished, Lessons learned, and Handoff, and append the Working log. Then stop.
6. A sitting that ends early sets Status to **in progress**, names the last finished work item, and leaves the rest unchecked.

Leave Accomplished, Lessons learned, and Handoff off a phase that has not started.

## Lock

This lock is the contract for every phase below. A phase that needs a field added stops and asks. It does not invent one.

### Layout

```
<workdir>/
  Pavillion.json
  profiles/
    default.json
  logs/
    default.log
  default.sqlite
  blobs/
    default/
  cache/
    default/
```

`Pavillion.json` is the index. It is never a profile. The scan looks in `profile_dir`, default `profiles` under the working directory. A relative `profile_dir` resolves against the working directory. An absolute one is used as given.

A file is a profile when it ends in `.json`, is not the index, and contains `"kind": "pavillion-profile"`. Anything else is ignored and logged at `warn`. The `id` must match the filename without `.json`. A mismatch is logged and that file is skipped.

### Index

The app writes this on first run. Unknown keys are preserved on a later rewrite.

```json
{
  "kind": "pavillion-index",
  "schema": 1,
  "active": "default.json",
  "profile_dir": "profiles",
  "log_level": "info"
}
```

`log_level` is `error`, `warn`, `info`, or `debug`. Default `info`. It lives on the index because logging starts before a profile is usable.

When the index is absent, create it and create `profiles/default.json`. When the index exists and `active` is missing or broken, start in state `error`. Do not invent a replacement profile.

### Profile

One profile is loaded for the life of the process. Switching is a restart. Schema 1:

```json
{
  "kind": "pavillion-profile",
  "schema": 1,
  "id": "default",
  "name": "Default",
  "storage": {
    "engine": "sqlite",
    "path": "default.sqlite"
  },
  "logs": "logs/default.log",
  "blobs": "blobs/default",
  "cache": "cache/default",
  "accounts": [],
  "preferences": {
    "timezone": "America/Vancouver",
    "week_start": "monday",
    "time_format": "24h",
    "theme": "system",
    "density": "comfortable",
    "zoom": { "shell": 1 },
    "window": {
      "maximized": false,
      "width": 1200,
      "height": 800,
      "x": null,
      "y": null,
      "chrome": "system"
    }
  }
}
```

Relative paths are under the working directory. A path beginning with `/` is absolute. `storage.engine` defaults to `sqlite`. `storage.connection` is present only for another engine, and it holds no password. Message bodies stay files under `blobs` for every engine.

`preferences.zoom` is an object of panel id to a scale. Schema 1 has one key, `shell`, from `0.5` to `3` in steps of `0.25`. A later panel (the message list, the reading pane) is another key on the same object. Unknown keys are kept. `preferences.window` is how the shell opens: maximized or a size, position when the compositor reports one, and `chrome` of `system` or `hidden`. `x` and `y` are null when unknown. The shell writes this object on move, resize, and quit. The hello-world phases do not read it yet.

Modes: the index, the profile, the log, and the database file are `0600`. `blobs` is `0700`.

### Accounts and secrets

An account object in `accounts`:

```json
{
  "id": "a-500foods-andrew",
  "name": "andrew@500foods.com",
  "kind": "jmap",
  "session_url": "https://mail.500foods.com/.well-known/jmap",
  "username": "andrew@500foods.com",
  "auth": "basic",
  "color": "#2a7",
  "enabled": true,
  "sync_mail": true,
  "sync_contacts": true,
  "sync_calendars": true,
  "fetch_bodies": "recent"
}
```

`kind` is `jmap` or `imap`. An `imap` account uses `host`, `port`, `smtp_host`, and `smtp_port` instead of `session_url`. `auth` is `basic` or `bearer`. `fetch_bodies` is `none`, `recent`, or `all`.

No password, token, or connection secret is a JSON field. A profile that contains a key named `password`, `token`, `secret`, or `refresh_token` is rejected. The keyring is service `pavillion`, user `<profile id>/<account id>`. Keyring calls are not part of B. Rejecting the key is.

The core is the only writer. Hand-edits are allowed while the process is stopped.

## B-1: Parse the index and a profile

**Kind:** Easy
**Status:** Complete (2026-10-14). No variances.

**Goal:** Jansson turns fixture text into the lock's structures, in memory.

**Entry gate:** A-2 complete. The Unity runner exists.

**Work items:**

- [x] Parse an index fixture and a profile fixture that match the lock.
- [x] Reject schema other than `1`, a missing `kind`, and an account object that contains `password`, `token`, `secret`, or `refresh_token`.
- [x] Preserve unknown keys on a rewrite of the in-memory object back to text.
- [x] Empty `accounts` parses as an empty list.

**Done means:** Unity tests cover the four cases above and pass.

**Exit gate:** `make test` exits 0. Passed: 9 tests, 0 failures. The profile fixture's `accounts` length is 0. `editor` and `note` are still in the rewritten text. Schema `2`, a missing `kind`, and each forbidden key are rejected.

**Accomplished:** `pav_index_parse`, `pav_profile_parse`, and the rewrite functions are in `core/src/config.c`. The parsed object is kept, so a rewrite dumps that object, unknown keys included. A forbidden key anywhere in the tree is rejected. That covers an account field and matches the lock's rule for the whole profile.

**Lessons learned:** Unity's double asserts stay off unless the test build defines `UNITY_INCLUDE_DOUBLE`. `zoom.shell` is a number, so the test binary defines it. Jansson 2.14 is the system library. The shell link line names `-ljansson` even though the window does not call the parser yet.

**Handoff:** B-2 writes the default index and `profiles/default.json` when the index is missing. Use these parsers on the bytes it writes. Do not create a profile when the index is already there.

## B-2: First-run files

**Kind:** Easy
**Status:** Complete (2026-10-14). No variances.

**Goal:** A missing index creates the lock's default index and `profiles/default.json`.

**Entry gate:** B-1 complete.

**Work items:**

- [x] Given an empty temp directory, write both files with the lock's default contents.
- [x] Set mode `0600` on both.
- [x] A second call, when the index exists, does not rewrite `default.json`.

**Done means:** A Unity test on a temp directory asserts the bytes and the modes, and asserts the second call leaves the profile untouched.

**Exit gate:** `make test` exits 0. Passed: 10 tests, 0 failures. The two files match the lock's JSON, mode `0600`. After the profile is overwritten with `changed`, a second `pav_config_ensure` leaves that text in place.

**Accomplished:** `pav_config_ensure` creates `profiles/` and both files when `Pavillion.json` is absent. The index is written last. If that write fails, the new profile is removed so a retry can create both. An existing index makes the call return without opening the profile.

**Lessons learned:** `open` with mode `0600` is still passed through `fchmod`, so the mode does not depend on a lucky umask. The `profiles` directory is created mode `0700`. The lock names `0600` for the two files and does not name this directory.

**Handoff:** B-3 scans `profile_dir`. `pav_config_ensure` does not scan, and it does not create a profile when the index is already present. A missing active file stays missing.

## B-3: Scan, kind, and a broken active profile

**Kind:** Medium
**Status:** Not started

**Goal:** The scan rules and the "do not invent a profile" rule are tested.

**Entry gate:** B-2 complete.

**Work items:**

- [ ] Scan `profile_dir`. Ignore a file whose `kind` is wrong. Ignore the index if it appears in that directory.
- [ ] Skip a profile whose `id` does not match its filename, and record a warning the test can see.
- [ ] When `active` names a missing or unparsable file, return state `error` and do not create a file.
- [ ] `profile_dir` omitted means `profiles`. An absolute `profile_dir` is used as given.

**Done means:** Unity fixtures cover a stray JSON file, an id mismatch, a missing active file, and an absolute profile directory.

**Exit gate:** `make test` exits 0.

## B-4: Paths and file modes

**Kind:** Medium
**Status:** Not started

**Goal:** Relative paths resolve under the working directory, and the directories exist with the lock's modes.

**Entry gate:** B-3 complete.

**Work items:**

- [ ] Resolve `logs`, `blobs`, `cache`, and `storage.path` against the working directory. A path beginning with `/` stays absolute.
- [ ] Create those directories. `blobs` is `0700`. The log file is created `0600`.
- [ ] Do not create the SQLite file. [C-CORE.md](C-CORE.md) C-4 opens it.
- [ ] A path that cannot be created becomes state `error` and names the path.

**Done means:** A Unity test asserts the resolved paths and the modes, and asserts that no `*.sqlite` file was created.

**Exit gate:** `make test` exits 0.

## B-5: An unknown engine is an error

**Kind:** Easy
**Status:** Not started

**Goal:** A profile whose `storage.engine` is not `sqlite` fails with an error that names the engine. No driver is loaded.

**Entry gate:** B-3 complete. B-4 may still be open. This phase only reads the field.

**Work items:**

- [ ] `sqlite` is accepted, including when the key is omitted (default `sqlite`).
- [ ] Any other string returns an error that contains that string.
- [ ] The test links no database client other than SQLite, and this phase does not call SQLite.

**Done means:** Unity covers the default, an explicit `sqlite`, and `db2`.

**Exit gate:** `make test` exits 0.

## Working log

- 2026-10-14 B-1: Jansson parses the lock's index and profile. Schema other than 1, a missing kind, and a forbidden key are rejected. Unknown keys survive a rewrite. Empty `accounts` is an empty list.
- 2026-10-14 B-2: A missing index creates `Pavillion.json` and `profiles/default.json`, both mode 0600. A second call leaves the profile file untouched.

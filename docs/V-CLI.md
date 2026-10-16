# V — CLI

**Status:** Not started (scoped 2026-10-16 from old plan §11 and §16).
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

A command line for AI agents and scripts, as a thin client of the running app. Not MCP: MCP is a wrapper with its own protocol, process, and transport to maintain; a comprehensive CLI is a contract any agent can already drive, and a thin MCP wrapper later only needs `help --json`.

## Phases

One phase is one session. Do not start the next one in the same conversation.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| V-1 | The socket, subcommand dispatch in `main.rs`, `status`, `help --json`, exit codes | Hard | Not started |
| V-2 | Bot keys, scopes, the pause switch, and the audit trail | Medium | Not started |
| V-3 | Mail commands: `mail list/get/search`, `draft create`, `send` | Medium | Not started |
| V-4 | `watch --json`, `config get/list/set`, contacts and calendar commands | Medium | Not started |

Kind: **Easy** is one module and a test. **Medium** crosses one boundary. **Hard** is a second process talking to the first.

## How a session works

1. Read Resume in [PAVILLION_PLAN.md](../PAVILLION_PLAN.md), the Status column above, and the previous phase's Status, Accomplished, Lessons learned, and Handoff.
2. Re-read only this phase: Goal, Entry gate, Work items, Done means, Exit gate. Open the lock section this phase cites.
3. Ask the qualifying questions. Wait for approval before editing source.
4. Do this phase only. Check a work item `[x]` only after its check has passed. Defer with `[~]`, one line, and the phase it moves to.
5. Before marking complete, write Status, Accomplished, Lessons learned, and Handoff, update the Status column here and in the index, and append the Working log. Then stop.
6. A sitting that ends early sets Status to **in progress**, names the last finished work item, and leaves the rest unchecked.

Leave Accomplished, Lessons learned, and Handoff off a phase that has not started.

## Lock

- **One engine, the app is the only writer.** CLI commands go through the dispatcher, so their writes reach SQLite only through the L Lock's write queue, the same path as the UI's. The CLI never opens SQLite, never calls JMAP, never reads the keyring. The running app owns the store, sync, the op queue, and the keyring. One command implementation (the G-2 dispatcher), two transports: Tauri for the window, the socket for the CLI. A change through the CLI shows in the window at once, and the window sees the CLI's drafts, sends, and audit entries.
- **The CLI requires a running instance.** The app listens on a Unix domain socket `$XDG_RUNTIME_DIR/pavillion/cli.sock` (via `PAV_DIR_RUNTIME`), mode 0600 in a 0700 directory. Protocol: JSON-RPC 2.0, one JSON object per line. If the app is not running, every command fails at once with exit 6. The CLI never launches the app and never works on its own: with the app closed, no bot acts and no mail goes out. The trade-off is laptop use; a later `pavillion daemon` (same core, no window) may keep the same socket contract.
- **Same binary, subcommand mode.** Dispatch order in `main.rs`: a known flag (`--help`, `--version`) prints and exits; then a known subcommand runs the socket client (C, `pav_cli_main`) and exits before `pavillion_lib::run`, so Tauri never builds, the WebView never starts, `pavillion_start` is never called, and single-instance never sees the process; then exactly one positional argument is the working directory (A-5). The subcommand check comes before the workdir check because `pavillion status` is one non-flag argument. A separate client binary is lighter, but one file is simpler on the update path; split later only if startup cost proves real.
- One socket path for every caller in v1 (one process, one profile). The profile id in the path is a later seam.
- **Bots paused:** a toolbar toggle and a setting, the same value. While on, the socket rejects every token with exit 7 and only the human's own session works. Pause on screen lock or idle, and a tray state, are later options.
- **Handshake.** The first message on every socket connection is `handshake`. The client sends `{"jsonrpc":"2.0","method":"handshake","params":{"protocol":1,"apiVersion":<PAVILLION_API_VERSION built in>,"capabilities":["watch","json"]},"id":0}`. The app replies with its `protocol`, `apiVersion`, `appVersion`, and `capabilities` (the subset it supports).
  - The `protocol` number is the socket framing and the method set. It starts at 1 and bumps along with `PAVILLION_API_VERSION` whenever a CLI-visible command changes incompatibly.
  - A different `protocol` or `apiVersion` closes the connection with error `protocol_mismatch`, and the client prints `pavillion: CLI protocol <a>/API <b> does not match the running app (<c>/<d>); restart the app or use the matching binary` and exits 9.
  - Any other method sent before the handshake gets `protocol_mismatch`, and the connection closes.
- **Exit codes**, defined once (`core/include/pav_exit.h`) and tested: 0 ok, 1 generic failure, 2 usage error, 3 credentials missing or token request unanswered, 4 retryable server or network failure (`ApiError.retryable`), 5 conflict (`stateMismatch`), 6 app not running, 7 paused by the user, 8 permission denied, 9 protocol mismatch (the handshake failed). Permission is split from credentials so a bot can tell "my key cannot send" from "the keyring has no password".
- **Bot keys, not a shared token.** Each bot has its own key in `PAVILLION_TOKEN`. A key records its name (what the audit shows), the accounts it may use (number, id, or name), scopes (`read`, `draft`, `send`, `calendar:write`, `contacts:write`, `config`, `delete`, `keys:issue`), an expiry, and its creator. `--account 001` is checked against the key, the same number people see in the window. Only a hash of the key is stored (`bot_key`, HMAC-SHA-256 with a per-profile secret kept in the keyring); the key itself is a secret under the K and H redaction rules.
- A key can only issue keys within its own scopes, and revoking a key revokes everything it created. `token request` (asking for more) shows an approve or deny prompt in the app; unanswered, it times out with exit 3. Escalation of `send`, `delete`, `config`, or `keys:issue` always needs the human. In v1 keys are issued and revoked by the human in Settings → Bots; delegation and `token request` are the schema's seam for later.
- The human's own CLI session uses a master key unlocked by OS login (a keyring entry readable only in the user session) or a passkey later, never an environment variable.
- **Never send by default.** `draft create` writes a draft the user sees in the app. Only `send <draft-id>` with the `send` scope enqueues it through the outbox; the undo-send delay applies; failed sends stay visible. Destructive commands (permanent delete, account removal, purge) refuse without `--yes`.
- **Non-interactive.** Commands never prompt or open a dialog. Missing credentials fail with a JSON error and exit 3.
- **JSON first.** `--json` on every command, stable camelCase fields matching `docs/API.md`, `--quiet` for people. `help --json` lists every command and option, one line each. Output is ids, sizes, and headers; a body only with `--body`. Redaction applies to output and audit alike.
- **Audit:** `cli_audit` records key name, command, target id, time, and result for every mutating command. A message a bot drafted or sent carries a **local** marker (`email.local_marks.bot`), never a JMAP keyword because other clients must not see it, and the window shows it.
- **`watch --json`** streams the core's events from the app's own sync; no second EventSource and no polling.
- Timing: the socket, `status`, `help --json`, keys, and pause could land as soon as the dispatcher exists; they are sequenced after the initial working copy because nothing else depends on them. The A-6 flags stay in front of everything.

## V-1: Socket, dispatch, status, help

**Kind:** Hard
**Status:** Not started.

**Goal:** `pavillion status --json` answers from the running app and exits 6 when it is not running.

**Entry gate:** U-2 complete.

**Work items:**

- [ ] Core: a socket listener thread in `core/src/cli_sock.c` (not `src-tauri`, per the G L3 Lock). It reads JSON-RPC lines and hands them to the dispatcher with a CLI caller identity, starting after `core ready`. The client is `pav_cli_main` in `core/src/cli_client.c`.
- [ ] `main.rs` dispatch order per the Lock. The client is `pav_cli_main` in `core/src/cli_client.c`, with no Rust beyond the call (G L3 Lock).
- [ ] The `handshake` per the Lock, done by the client before every command and checked by the listener.
- [ ] `status`, `help --json`, `version`; the exit code table.
- [ ] Tests: a Unity integration test starting the core on a temp workdir with the fake platform and a socket in a temp runtime dir: `test_cli_app_not_running_exit_6`, `test_cli_handshake_mismatch_exit_9` (a listener built with `apiVersion` + 1), `test_cli_method_before_handshake_rejected`, `test_cli_status_json`, `test_cli_subcommand_before_workdir`, `test_cli_never_starts_core`.

**Done means:** The CLI talks to a running app and never starts one.

**Exit gate:** `make test` exits 0; with the built app running, `dist/pavillion status --json` prints the accounts; with it closed, exit 6.

## V-2: Keys, pause, audit

**Kind:** Medium
**Status:** Not started.

**Goal:** Each bot has its own scoped key, the human can pause all bots, and every mutation is audited.

**Entry gate:** V-1 complete.

**Work items:**

- [ ] `bot_key` handling: create (shows the key once), list, revoke with cascade; Settings → Bots page; the pause toggle in the toolbar and in Settings.
- [ ] Scope and account checks in the socket path; exits 7 and 8.
- [ ] `cli_audit` writes; an Audit view in Settings → Bots.
- [ ] Tests: `test_key_hash_only_stored`, `test_scope_denied_exit_8`, `test_paused_exit_7`, `test_revoke_cascades`, `test_audit_has_no_key`.

**Done means:** A read-only key cannot draft, and pausing stops every key.

**Exit gate:** `make test` exits 0; manual check of the three cases with the built app.

## V-3: Mail commands

**Kind:** Medium
**Status:** Not started.

**Goal:** A bot can read, search, draft, and (with the scope) send.

**Entry gate:** V-2 complete.

**Work items:**

- [ ] `mail list --account --mailbox --limit`, `mail get <id> [--body]`, `mail search <query>`, `draft create --from --to --subject --body-file`, `send <draft-id>`, `mail move`, `mail flag`, `mail delete [--permanent --yes]`.
- [ ] Local bot marker on drafted and sent messages; the window shows a small bot badge with the key name in the list and reader.
- [ ] Tests over the fixture server.

**Done means:** A bot drafts a reply the user sees in the app, and `send` works only with the scope.

**Exit gate:** `make test` exits 0; with the built app, a draft from the CLI appears in the window with the bot badge.

## V-4: Watch, config, contacts, calendar

**Kind:** Medium
**Status:** Not started.

**Goal:** Bots can follow events and manage settings, contacts, and events within their scopes.

**Entry gate:** V-3 complete.

**Work items:**

- [ ] `watch --json [--types]` streaming events over the socket.
- [ ] `config get|list|set` with validation and `--dry-run`, scope `config`, `--as` showing the audit name.
- [ ] `contacts list|get|save`, `calendar list|range|save` within `contacts:write` and `calendar:write`.
- [ ] Tests for each, including `--dry-run` changing nothing.

**Done means:** A bot watches for new mail and adds a calendar event.

**Exit gate:** `make test` exits 0; manual `watch` shows a new message within 5 s of delivery.

## Working log

- 2026-10-16 Opened from old plan §11 and §16 (two Grok Build sessions' sketch, endorsed with changes). Decisions: JSON-RPC over the socket in the host, the dispatch order, an exit code header, HMAC-hashed keys, Settings → Bots, a bot badge from a local marker, sequencing after the initial working copy.
- 2026-10-16 With Rust minimized, the socket listener and client move from the host into the C core. The `main.rs` dispatch order is unchanged, and it calls `pav_cli_main`.
- 2026-10-16 Added a `handshake` as the first message (protocol, `apiVersion` tied to `PAVILLION_API_VERSION`, and capabilities), with exit 9 on a mismatch. The client is C (`pav_cli_main`), and CLI writes go through the L write queue.

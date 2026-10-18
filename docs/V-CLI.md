# V — CLI

**Status:** V-5 complete (2026-10-17), step 116. Andrew accepted the window. The rest of this file is not started. J-9 is in progress, step 124, in [J-TABS.md](J-TABS.md), and moves Grok to the footer, with GitHub beside it. L-1 waits until that window is accepted. Do not start L-1 from this file.
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

A command line for AI agents and scripts, as a thin client of the running app. Not MCP. MCP is a wrapper with its own protocol, process, and transport, and a Grok Bot reaches this machine only through its local-computer path, which does not suit that. A comprehensive CLI is the whole surface. `help --json` lists every command. JSON in and out is the fields that command needs, and no more.

## Phases

One phase is one session. Do not start the next one in the same conversation. The index is the order, not the numbers below. V-5 comes after J-6. V-6 and V-7 follow V-2. V-8 follows V-3. V-9 follows U-2. V-10 follows R-5. V-11 follows V-9. V-4 stays after milestone 4 and stays contacts and calendars. Task commands are X-3. Note commands are Y-3. Both follow V-4. They are not added to V-4.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| V-1 | The socket, subcommand dispatch in `main.rs`, `status`, `help --json`, exit codes | Hard | Not started |
| V-2 | Bot keys, scopes, the pause switch, and the audit trail | Medium | Not started |
| V-3 | Mail commands: `mail list/get/search`, `draft create`, `send` | Medium | Not started |
| V-4 | `watch --json`, `config get/list/set`, contacts and calendar commands | Medium | Not started |
| V-5 | Features toggles and the Grok Bots section shell | Medium | Done |
| V-6 | Webhook, hello, and request states | Medium | Not started |
| V-7 | Bot transcript, prod, and the Help instruction | Medium | Not started |
| V-8 | Contextual composer on the open item | Medium | Not started |
| V-9 | CLI file commands | Medium | Not started |
| V-10 | Tell a Grok Bot when mail arrives or a mailbox changes | Medium | Not started |
| V-11 | Tell a Grok Bot when a files folder changes | Medium | Not started |

Kind: **Easy** is one module and a test. **Medium** crosses one boundary. **Hard** is a second process talking to the first.

## How a session works

1. Read Resume in [PAVILLION_PLAN.md](../PAVILLION_PLAN.md), the Status column above, and the previous phase's Status, Accomplished, Lessons learned, and Handoff.
2. Re-read only this phase: Goal, Entry gate, Work items, Done means, Exit gate. Open the lock section this phase cites.
3. Ask the qualifying questions. Wait for approval before editing source.
4. Do this phase only. Check a work item `[x]` only after its check has passed. Defer with `[~]`, one line, and the phase it moves to.
5. Before marking complete, write Status, Accomplished, Lessons learned, and Handoff, update the Status column here and in the index, and append the Working log. Before that, update `ui/src/help/pages.js` for what this phase changed: augment, replace, fix, or extend the user page, or add a Developer section when the user page has nothing new. Then stop.
6. A sitting that ends early sets Status to **in progress**, names the last finished work item, and leaves the rest unchecked.

Leave Accomplished, Lessons learned, and Handoff off a phase that has not started.

## Lock

- **One engine, the app is the only writer.** CLI commands go through the dispatcher, so their writes reach SQLite only through the L Lock's write queue, the same path as the UI's. The CLI never opens SQLite, never calls JMAP, never reads the keyring. The running app owns the store, sync, the op queue, and the keyring. One command implementation (the G-2 dispatcher), two transports: Tauri for the window, the socket for the CLI. A change through the CLI shows in the window at once, and the window sees the CLI's drafts, sends, and audit entries.
- **The CLI requires a running instance.** The app listens on a Unix domain socket `$XDG_RUNTIME_DIR/pavillion/cli.sock` (via `PAV_DIR_RUNTIME`), mode 0600 in a 0700 directory. Protocol: JSON-RPC 2.0, one JSON object per line. If the app is not running, every command fails at once with exit 6. The CLI never launches the app and never works on its own: with the app closed, no bot acts and no mail goes out. The trade-off is laptop use; a later `pavillion daemon` (same core, no window) may keep the same socket contract.
- **The CLI must not load WebKitGTK.** Returning before `gtk_init` is not enough: the dynamic linker maps every `NEEDED` library at process start, and a Tauri binary needs `libwebkit2gtk`. V-1 ships two ELFs in one package. `/usr/bin/pavillion` is the C client (`pav_cli_main`): flags, subcommands, and the socket. It links the core's client objects, Jansson, and nothing from GTK or WebKit. With no subcommand it execs `pavillion-gui` and does not open the socket. That is the only launch path. `/usr/bin/pavillion-gui` is the Tauri binary and the only ELF with a dynamic `NEEDED` entry for system `libwebkit2gtk`. WebKit bytes are never inside either ELF: no static link, no vendored WebKit, no AppImage. The desktop file runs `pavillion`, not the GUI ELF. The rpm installs both and depends on the system WebKit package; there is no second package. G-8's `pav_cli_main` stub is the interim; V-1 replaces it with this entry point.
- One socket path for every caller in v1 (one process, one profile). The profile id in the path is a later seam.
- **Bots paused:** a toolbar toggle and a setting, the same value. While on, the socket rejects every token with exit 7 and only the human's own session works. Pause on screen lock or idle, and a tray state, are later options.
- **Handshake.** The first message on every socket connection is `handshake`. The client sends `{"jsonrpc":"2.0","method":"handshake","params":{"protocol":1,"apiVersion":<PAVILLION_API_VERSION built in>,"capabilities":["watch","json"]},"id":0}`. The app replies with its `protocol`, `apiVersion`, `appVersion`, and `capabilities` (the subset it supports).
  - The `protocol` number is the socket framing and the method set. It starts at 1 and bumps along with `PAVILLION_API_VERSION` whenever a CLI-visible command changes incompatibly.
  - A different `protocol` or `apiVersion` closes the connection with error `protocol_mismatch`, and the client prints `pavillion: CLI protocol <a>/API <b> does not match the running app (<c>/<d>); restart the app or use the matching binary` and exits 9.
  - Any other method sent before the handshake gets `protocol_mismatch`, and the connection closes.
- **Exit codes**, defined once (`core/include/pav_exit.h`) and tested: 0 ok, 1 generic failure, 2 usage error, 3 credentials missing or token request unanswered, 4 retryable server or network failure (`ApiError.retryable`), 5 conflict (`stateMismatch`), 6 app not running, 7 paused by the user, 8 permission denied, 9 protocol mismatch (the handshake failed). Permission is split from credentials so a bot can tell "my key cannot send" from "the keyring has no password".
- **Bot keys, not a shared token.** Each bot has its own key in `PAVILLION_TOKEN`. There is no `--token` flag. A key records its name (what the audit shows), the accounts it may use (number, id, or name), scopes (`read`, `draft`, `send`, `calendar:write`, `contacts:write`, `files:write`, `config`, `delete`, `keys:issue`), an expiry, and its creator. `--account 001` is checked against the key, the same number people see in the window. Only a hash of the key is stored (`bot_key`, HMAC-SHA-256 with a per-profile secret kept in the keyring); the key itself is a secret under the K and H redaction rules.
- A key can only issue keys within its own scopes, and revoking a key revokes everything it created. `token request` (asking for more) shows an approve or deny prompt in the app; unanswered, it times out with exit 3. Escalation of `send`, `delete`, `config`, or `keys:issue` always needs the human. In v1 the human issues and revokes keys from the bot's own menu (V-7). A new key defaults to `read`. `send` stays off unless it is ticked. Delegation and `token request` are the schema's seam for later.
- The human's own CLI session uses a master key unlocked by OS login (a keyring entry readable only in the user session) or a passkey later, never an environment variable.
- **Never send by default.** `draft create` writes a draft the user sees in the app. Only `send <draft-id>` with the `send` scope enqueues it through the outbox; the undo-send delay applies; failed sends stay visible. Destructive commands (permanent delete, account removal, purge) refuse without `--yes`.
- **Non-interactive.** Commands never prompt or open a dialog. Missing credentials fail with a JSON error and exit 3.
- **JSON first.** `--json` on every command, stable camelCase fields matching `docs/API.md`, `--quiet` for people. `help --json` lists every command and option, one line each. Output is ids, sizes, and headers; a body only with `--body`. Redaction applies to output and audit alike.
- **Audit:** `cli_audit` records key name, command, target id, time, and result for every mutating command. A message a bot drafted or sent carries a **local** marker (`email.local_marks.bot`), never a JMAP keyword because other clients must not see it, and the window shows it.
- **`watch --json`** streams the core's events from the app's own sync; no second EventSource and no polling.
- Timing: the socket, `status`, `help --json`, keys, and pause need the dispatcher and a running app. They are sequenced just after milestone 3 (V-1, V-2, V-3) so an agent can drive mail as soon as mail exists. V-4 (contacts, calendar, `watch`, `config`) waits until S-4 and T-5. It does not grow task or note commands. Those are X-3 and Y-3, after V-4. V-9 (files) waits until U-2. The A-6 flags stay in front of everything and move into the C `pavillion` binary in V-1. V-5 is the Features toggles and the empty Grok section, after J-6 and before accounts. J-9 moves that section to the footer. V-6 and V-7 follow V-2. V-8 follows V-3. V-10 follows the mail rules. V-11 follows files.

### Grok Bots (2026-10-17)

Andrew's fifth section. V-5 is complete: the empty home and the Features toggles. The webhook, the CLI, and the transcript are not started. J-2 was not reopened, and no completed phase was rewritten.

- **Two secrets.** The webhook URL and its key let Pavillion wake the bot. The CLI key lets the bot act here. The webhook material is stored through K, never in the profile, the log, or a command result. The CLI key is the V-2 hash. A bot with no webhook is still a valid script key.
- **Outbound is a POST.** Pavillion calls the bot. The bot does not dial Pavillion except through `pavillion`. The POST body is one JSON object: `requestId`, `kind` (`hello`, `task`, or `prod`), and, for a task, `text` plus `context`. `context` is `{type, id, accountNumber}`. `type` is `message`, `contact`, `event`, or `file`. X-2 and Y-2 add `task` and `note` when those screens exist. A task from the Grok Bots screen itself omits `context`. The body never carries a message body, headers, or a file's bytes. HTTP 200 means the bot accepted the run. The finished work comes back through the CLI.
- **Hello is immediate.** Saving a URL and key POSTs `kind:"hello"` before the add dialog finishes. The bot answers with `bot hello --name --icon --color`. The icon passes the existing sanitizer. The color is `#rrggbb` or it is refused. It paints that bot's bar button and its bubbles. It does not change the theme. Until hello arrives the button says Connecting. A non-200 or a transport error fails the add at once.
- **Reply commands,** each scoped to the key that was issued for that bot: `bot hello`, `bot ack <requestId>`, `bot progress <requestId> --text`, `bot done <requestId> --text`, `bot fail <requestId> --text`. The bot is instructed to ack first, then do the task, then done or fail. Text is a short status line.
- **Request states** on `bot_request`: `queued` (waiting to POST), `sending` (POST in flight), `sent` (HTTP 200, no ack yet), `working` (`bot ack` or `bot progress`), `completed` (`bot done`), `failed` (POST failed, or `bot fail`), `noReply` (still `sent` when the quiet period ends). A typed request goes straight to `sending`. A trigger match waits in `queued` while that bot already has two POSTs in flight. Several requests may be in flight. Each keeps its own id. Pavillion does not run a second execution queue.
- **Quiet period** defaults to 2 minutes (`preferences.ui.bots.quietMinutes`, the page clamps it, minimum 1). When it fires on a `sent` request the state becomes `noReply` and the toast says to check the bot. A `working` request is not failed by the timer. **Prod** is enabled once a `sent` or `working` request is older than the quiet period. Prod POSTs `{requestId, kind:"prod"}` on the same id. The bot answers with `bot progress`.
- **Where the state shows.** The bot's bar button, the composer that sent the request, the transcript row, and the Activity table. V-6 adds activity kind `bot.request` to the I list and to `docs/API.md`. The activity is `queued` while the request is `queued`, `running` through `sending`, `sent`, and `working`, `done` on `completed`, and `failed` on `failed` and `noReply`. A typed request toasts on `completed`, `failed`, and `noReply`. A trigger request toasts on `failed` and `noReply` only.
- **The transcript** is one bubble list per bot, a status log so a person can see what was asked and what the bot said it did. It is stored in `bot_message`. It is not a person-to-person chat.
- **The section.** First footer cell, Sharp Thin `fa-ghost`, accessible name Grok Bots, theme key `--pav-icon-bots`. J-9 moved it off the top rail. It opens like Help: the home tab comes forward and the nav body stays. The bar lists bots. Add asks for the URL and key. Each bot's menu is Reconnect (hello again), Remove (revoke the key, delete the webhook secret, and delete that bot's triggers), and New token (shown once). The home is a button bar and a transcript that says No bots yet. Add is disabled, and its tooltip says a bot cannot be connected yet. V-6 enables Add and asks for the URL and key. V-5's accomplished text still describes the top-rail cell it shipped. That record stays.
- **Features.** Settings → Features lists every rail button. `preferences.ui.features` booleans are `mail`, `contacts`, `calendars`, `tasks`, `files`, `notes`, `argent`, `bots`, `github`, `activity`, `update`, and `help`. Defaults are on, on, on, off, on, off, off, off, off, on, on, on. Settings is not in that list and cannot be hidden. The page clamps them. Hiding a section hides its button and does not offer its home tab. An open home of that section closes. A saved home for a hidden section is left out of restore, with no notice. Update has no home tab. `preferences.ui.rail` is `top` and `bottom`, the button order. A missing list uses the catalog order. A missing button is appended to its default side. The default top is Mail, Contacts, Calendars, Tasks, Files, Notes, Argent. The default bottom is Grok Bots, GitHub, Activity, Settings, Update, Help. Dragging a row across the divider moves that button to the other bar. `preferences.ui.chords` maps each button to one digit `1` through `9`, or to none. Defaults are Ctrl+1 Mail through Ctrl+7 Argent. The other buttons start with none. Assigning a digit clears it on the other button. An absent chords object uses those defaults. A present object that omits a button means none. A repeated digit stays on the earlier button, top then bottom. A hidden button's chord does nothing. The digit stays with the button when the button moves. Update's chord runs the same sync as the Update button. All of the toggleable buttons may be off. Settings stays reachable. If the current section is hidden, the nav falls through to the first visible section in the top group whose nav is true, otherwise Settings. Grok, GitHub, Activity, and Update are not that fallback. Settings can sit in the top group. GitHub's placeholder is [GH-GITHUB.md](GH-GITHUB.md). It is not a bot. Later per-feature settings go on that feature's row when a phase has one. Andrew set the Settings order on 2026-10-17: Features, Themes, Icons, Fonts, Tabs, Zooms. The stored heading ids stay `features`, `theme`, `icons`, `fonts`, `tabs`, and `zoom`.
- **The composer** elsewhere uses the same icon, a bot menu, and one short field. It sends the sentence and the open item's id. While bots are paused it refuses. Mail registers `message` in V-8. Contacts, calendars, and files register their types in S-2, T-3, and U-2. Tasks and notes register `task` and `note` in X-2 and Y-2. Those types are not added before those screens exist.
- **Triggers.** A trigger tells one bot when something happens. It is a profile entry `{id, botId, event, accountId, mailboxId or folderId, text, enabled}`, not a mail rule and not Lua. The row reads "Tell Charlie when new mail arrives" or "Tell Betty when <folder> is updated". Events are `mail.arrived` (any new mail, or one mailbox), `mailbox.updated`, and, from V-11, `file.updated`. The POST is a `task` with `source:"trigger"`, `triggerId`, the standing `text` (or "New mail arrived" / "Folder updated" when text is empty), and the same `context` as a typed task. It fires after filing rules and after the message is announced, on delta sync and push only. Initial sync does not fire. "Apply rules to this folder" does not fire. The app being closed means no trigger runs, and a trigger is never exported to Sieve. While bots are paused, matches are not posted and the transcript gets one line that N were skipped. Unpausing does not replay them. At most two POSTs are in flight per bot. That cap is a stand-in until V-Q2. The mailbox menu item "Tell a Grok Bot…" opens the dialog with that folder filled in. The bot's screen lists its triggers. `P.bot.tell` enqueues the same request from raw Lua and is documented in `docs/LUA_API.md` by V-10.
- **The Help text** is a section named Grok Bots. It is the note Andrew pastes to a bot: the app must be running, the token is `PAVILLION_TOKEN` and never a command argument, `pavillion help --json` is the command list, JSON is only the fields that command needs, a webhook is answered with `bot ack` then the task then `bot done` or `bot fail`, `hello` is answered with `bot hello`, `prod` is answered with `bot progress`, context ids are fetched with the CLI rather than expected in the webhook body, and a task with `source:"trigger"` is answered the same way as one the person just typed.
- **Header.** V-Q1 is answered at the start of V-6 by copying the Authorization header from one saved Grok Bot routine. The client uses that header. It is written into this Lock in that phase.

## V-1: Socket, dispatch, status, help

**Kind:** Hard
**Status:** Not started.

**Goal:** `pavillion status --json` answers from the running app and exits 6 when it is not running.

**Entry gate:** N-9 complete. Mail exists, and the dispatcher has existed since G-2. Files are not a dependency.

**Work items:**

- [ ] Core: a socket listener thread in `core/src/cli_sock.c` (not `src-tauri`, per the G L3 Lock). It reads JSON-RPC lines and hands them to the dispatcher with a CLI caller identity, starting after `core ready`. The client is `pav_cli_main` in `core/src/cli_client.c`.
- [ ] `/usr/bin/pavillion` is `pav_cli_main` (C). No subcommand execs `pavillion-gui`. `main.rs` keeps no subcommand logic (G L3 Lock). `ldd` on the `pavillion` ELF lists no `libwebkit` and no `libgtk`. `readelf -d` on `pavillion-gui` shows `libwebkit2gtk-4.1.so` as `NEEDED`, and neither ELF contains a WebKit archive or a copied `.so`. `pavillion status` with the app closed exits 6 in under 100 ms.
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

- [ ] Migration `003_cli.sql` (`bot_key`, `cli_audit`) applied by the L runner. L-3's PIM migration is numbered after this one and does not create these tables. `bot_request` and `bot_message` are V-6 and V-7, not this migration. Then `bot_key` handling: create (shows the key once), list, revoke with cascade, on the Grok Bots section V-5 built. The pause toggle stays in the toolbar and in Settings. Webhook URL and key are V-6.
- [ ] Scope and account checks in the socket path; exits 7 and 8.
- [ ] `cli_audit` writes; an Audit view on the Grok Bots section.
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

**Entry gate:** V-3, S-4, and T-5 complete. Contacts and calendar commands need those stores. `watch` and `config` can be the first work items; the PIM commands follow in the same phase only because the gate already holds.

**Work items:**

- [ ] `watch --json [--types]` streaming events over the socket.
- [ ] `config get|list|set` with validation and `--dry-run`, scope `config`, `--as` showing the audit name.
- [ ] `contacts list|get|save`, `calendar list|range|save` within `contacts:write` and `calendar:write`.
- [ ] Tests for each, including `--dry-run` changing nothing.

**Done means:** A bot watches for new mail and adds a calendar event.

**Exit gate:** `make test` exits 0; manual `watch` shows a new message within 5 s of delivery.

## V-5: Features toggles and the Grok Bots shell

**Kind:** Medium
**Status:** Complete (2026-10-17). Andrew accepted the window. The phase stamp is `0.44`. `PAVILLION_BUILD` stays 152. `make test` is 69 Unity tests and 119 Vitest tests. This sitting did not push. Do not start L-1.

**Goal:** The five top sections can be shown or hidden, and Grok Bots has an empty home.

**Entry gate:** J-6 complete. Do not reopen J-2. At the start of this phase, amend the J shell lock with the fifth cell, then build it.

**Work items:**

- [x] Amend the J shell lock: a fifth top cell, Sharp Thin `fa-ghost`, accessible name Grok Bots, `--pav-icon-bots` in `dark.css`, `light.css`, and both starter theme files. `Ctrl+5` selects the section. Name `ui.colors.bots` in the section-color sentence so J-5 paints it.
- [x] `preferences.ui.features` as in the Lock. Settings → Features lists the five rows and nothing else yet.
- [x] The Grok Bots home is a button bar and an empty transcript. Add is present and does not call the network. The default profile hides the cell.
- [x] Vitest: bots defaults off; hiding Mail removes that cell and its home; the bottom rail is unchanged when all five are off.

**Done means:** The fifth icon is absent until Features turns it on, and the home it opens does not talk to a bot.

**Exit gate:** `make test` exits 0. In the window, toggle each of the five, restart, and the choices remain.

**Accomplished:** Settings → Features is the first heading. The order on screen is Features, Themes, Icons, Fonts, Tabs, Zooms. The stored heading ids stay `features`, `theme`, `icons`, `fonts`, `tabs`, and `zoom`. `preferences.ui.features` is five booleans, `mail`, `contacts`, `calendars`, `files`, and `bots`. A missing value is on, except bots, which is off. A non-boolean is replaced by that default. Unknown keys on the object are kept. The page clamps them. The core does not read them. `PAVILLION_API_VERSION` stays 3. No new core command. Hiding a section hides its top button and closes its home. When that home is not the active tab, the close stays on the current tab. `Ctrl+Shift+T` cannot reopen a hidden home. A saved home for a hidden section is left out of restore, with no notice. `Ctrl+1` through `Ctrl+5` stay on those five sections, and a hidden chord does nothing. The bottom rail is unchanged when all five are off. Settings stays reachable. The first launch opens Mail only when Mail is visible. Grok Bots is the fifth top cell, Sharp Thin `fa-ghost`, accessible name Grok Bots, `--pav-icon-bots` in `dark.css`, `light.css`, and both starter theme files. The default profile hides it. The home is a button bar and a transcript that says No bots yet. The left nav is empty. Add is disabled. Its tooltip says "A bot cannot be connected yet." The click does nothing, and nothing calls the network. The J shell lock names `ui.colors.bots` for J-5. J-5 was not built. Help has a Features section and a short Grok Bots stub. The paste-ready bot instruction stays in V-7. Ctrl+C runs that same quit and waits until the core has stored `preferences.ui.session.shutdown` as `clean`. Another Ctrl+C during that wait does not exit. The wait gives up after two seconds. A forced kill still leaves shutdown open. `src-tauri/src/lib.rs` is 318 lines. The phase stamp is `0.44.0`. `PAVILLION_BUILD` is 152. The release date in `build.rs` was already 2026-10-17, so it stayed. Help already described Features, the empty bot home, and that Ctrl+C finishes the session save.

**Lessons learned:** A disabled button does not receive the pointer, so the Add tooltip is bound to the span around it and the button has `pointer-events: none`. A feature checkbox is a text field for the shortcut filter. A chord fired while that box is focused does nothing. Tests move focus to the Settings tab first. Starting fake timers before mount can hang the session timer. The full Vitest run once timed out `zoom-actions.test.js` under parallel load. Alone it passed. The suite was not given a longer timeout. The launch marker writes an empty tab list with `shutdown` `open` before any tab opens, and the restored list is flushed on the next layout save or within 30 seconds. A kill in that window drops the list. The core stays a library in this process. A second core process would not hold the open tabs, and the terminal already delivers Ctrl+C to every member of the foreground group, so a separate process would not receive it more easily. The 2026-10-17 note that WebKit's page was the process Ctrl+C killed was wrong. GLib still starts that page with SIGINT reset to terminate and does not clear a blocked mask, and bubblewrap still does not pass `--new-session`, so the page inherits the block, stays in the terminal group, and stays up. The thread that died is bmalloc's scavenger inside the UI process (`pas_scavenger_notify_eligibility_if_needed` in libjavascriptcoregtk). It waits with the mask cleared and SIGINT at the default action, so the kernel kills the app before `sigwait` runs. The block stays, on the main thread before `pavillion_start` and before the window, so later threads and the page inherit it. `run()` then installs a `sigaction` with `SA_RESTART` whose handler only increments an atomic. The watcher accepts that count or a 50 ms `sigtimedwait`. The handler is not inside `block_quit_signals`, so `--help` and `--version` stay killable. `signal-hook` stays gone. Do not drop the block, and do not drop the counting handler. `prefs.ui.set` is the profile fsync. The open tabs are not a core structure. Launch stores an empty list with `shutdown` `open` before the restored tabs exist, and the live list reaches disk only when the page submits it. The shell treats that command's return, when `session.shutdown` is `clean`, as the save finishing. It also accepts `pav-quit-done`. A failed write leaves `shutdown` open. Waiting on Tauri's window-destroyed event never fired for a programmatic close. Another Ctrl+C during the two-second wait only increments that count and does not cut the write off. A normal window close printed `free(): corrupted unsorted chunks` after the profile was already `shutdown` `clean`. That is WebKitGTK 2.52's web process aborting during EGL teardown (bug 303336), on the terminal the GUI inherited. The parent had already exited 0. `run()` sets `WEBKIT_DISABLE_COMPOSITING_MODE=1` when that variable is unset, before the webview exists, so there is no GL context in that race. An inherited value wins. Software compositing can change how scroll and animation feel. Do not hook `exit` to `_exit`. The dev watcher still uses SIGKILL, and that path still quick-starts. Adding `--pav-icon-bots` makes a saved Pavillion Dark.theme or Pavillion Light.theme list as Custom until that file is deleted. The middle number is the count of completed plan steps. It was 43 while this phase was open and 44 once Andrew accepted it. The build count was already 152 from his own builds and was left alone.

**Handoff:** L-1 is next, step 42, in [L-STORE.md](L-STORE.md). Do not start it in this sitting. Its entry gate is J-6, which is met. A new session reads that phase and asks before editing. V-6 is not next. It waits for V-2, K-1, and M-1, and V-Q1 is answered at its start. J-4 (step 107) and J-5 (step 108) stay at milestone 6. Do not reopen J-2, the tab registry, the shortcut registry, the tab menu, close, middle-click, pointer drag, the nav split, the Help reader, or session restore. The SIGINT block stays, and so does the counting `sigaction` installed from `run()` after `block_quit_signals`. Do not stop blocking SIGINT and SIGTERM before the window starts, do not install that handler inside `block_quit_signals`, and do not bring `signal-hook` back. The page process inherits the block. The scavenger thread inside the UI process does not, which is why the handler exists. Leave `WEBKIT_DISABLE_COMPOSITING_MODE` as `run()` sets it when the variable is unset. Do not touch this quit path in L-1. A forced kill, including the dev watcher's rebuild kill, still leaves `shutdown` open. That is not a missed logout write. Add stays disabled until V-6. The paste-ready Grok Bots Help note is V-7. `PAVILLION_API_VERSION` stays 3. No new core command. Served sheets are `ui/public/styles`. `ui/styles` stays unlinked. The phase stamp is `0.44.0`. `PAVILLION_BUILD` was 152 at the V-5 close and is 156 now. The next `make dev`, `make build`, or `make mini` shows `0.44.157` and dirties that file. `dist/pavillion-mini` does not contain the quit fix until that `make mini`. `src-tauri/src/lib.rs` is 373 lines. Do not shrink it back to a forwarder. `make test` does not bump the build file. Neither sitting committed.

## V-6: Webhook, hello, and request states

**Kind:** Medium
**Status:** Not started.

**Goal:** A webhook URL and key are tried at once, and every later request has a state.

**Entry gate:** V-5, V-2, K-1, and M-1 complete. Answer V-Q1 before writing the client.

**Work items:**

- [ ] Bot record: a V-2 key plus an optional webhook. The URL and webhook key go through the K secret store. The profile keeps ids and labels.
- [ ] Add POSTs `kind:"hello"` on the net thread before the dialog finishes. Record the routine's Authorization header in this Lock.
- [ ] The reply commands in the Lock. A hello that passes sets the bar button's name, icon, and accent. A failed POST fails the add.
- [ ] Table `bot_request` with the Lock's states, the quiet period, and prod on the same id. Activity kind `bot.request` added to the I list and to `docs/API.md`.
- [ ] Tests: a refused POST leaves no named bot; ack moves `sent` to `working`; the quiet period marks `noReply`; prod reuses the id; the webhook key is absent from the profile and the log.

**Done means:** A wrong URL fails immediately. A right URL stays on Connecting until `bot hello`.

**Exit gate:** `make test` exits 0.

## V-7: Bot transcript, prod, and the Help instruction

**Kind:** Medium
**Status:** Not started.

**Goal:** The Grok Bots tab shows what was asked and what the bot reported, and Help can tell a bot how to connect.

**Entry gate:** V-6 complete.

**Work items:**

- [ ] Table `bot_message`. Bubbles per bot: the person's request, then ack, progress, done, and fail lines.
- [ ] Toast on `completed`, `failed`, and `noReply`. Prod is a button on a request older than the quiet period, hidden before that.
- [ ] Bot menu: Reconnect, Remove, New token, per the Lock.
- [ ] Help section Grok Bots, the paste-ready instruction in the Lock.
- [ ] Vitest: the six state labels, and Prod hidden before the quiet period.

**Done means:** A finished request toasts, and the bubble shows the bot's text. Help contains the instruction.

**Exit gate:** `make test` exits 0. In the window, prod a request that has gone quiet and the same id is posted again.

## V-8: Contextual composer on the open item

**Kind:** Medium
**Status:** Not started.

**Goal:** A short message from the open item wakes the chosen bot with that item's id.

**Entry gate:** V-7, V-3, and N-4 complete.

**Work items:**

- [ ] The Grok Bots icon, a bot menu, and one text field on the reading pane. The POST is a `task` with `context.type` `message`, the message id, and the account number. The field shows that request's state. Several may be in flight.
- [ ] Paused bots: the field refuses and says the bots are paused.
- [ ] Vitest: the posted object has the id and does not have a body; two sends have two request ids.

**Done means:** From an open message, the webhook receives the sentence and that message's id.

**Exit gate:** `make test` exits 0. With a bot connected, the request moves Sent, then Working, then Completed, and the bot's `mail get` uses the id from the webhook.

## V-9: CLI file commands

**Kind:** Medium
**Status:** Not started.

**Goal:** A bot can list and fetch files, and write them only with `files:write`.

**Entry gate:** U-2 and V-2 complete.

**Work items:**

- [ ] `files list` and `files stat` under `read`. Download streams to a path the caller names. `files mkdir`, upload, and `files delete --yes` require `files:write`.
- [ ] JSON is ids, names, and sizes. Bytes stay out of the JSON result.
- [ ] Tests on the file fixtures, including a `read` key refused on delete (exit 8).

**Done means:** A read-only key can list a folder and cannot delete a file.

**Exit gate:** `make test` exits 0.

## V-10: Tell a Grok Bot when mail arrives or a mailbox changes

**Kind:** Medium
**Status:** Not started.

**Goal:** A saved phrase wakes one bot when new mail arrives or a mailbox changes.

**Entry gate:** V-6, R-3, and M-5 complete. Filing rules already exist, and delta sync is the event source. Before the first work item, talk through V-Q2 and V-Q3. Do not add queue machinery or a Lua sample suite until that talk.

**Work items:**

- [ ] Profile `botTriggers` as in the Lock. The bot screen lists them. The mailbox menu item "Tell a Grok Bot…" opens the dialog with that mailbox filled in. Fields are the bot, the event, and an optional instruction.
- [ ] The hook fires after rules and after announce, on delta and push only. A match creates a `task` request with `source:"trigger"`. Two POSTs in flight per bot. The rest stay `queued`.
- [ ] Paused bots skip matches and write one transcript line for the count. Removing a bot deletes its triggers.
- [ ] `P.bot.tell` enqueues the same request. Document it in `docs/LUA_API.md`. It does not POST from the script thread.
- [ ] Tests: initial sync creates no request; a delta arrival does; a paused bot creates none; the third match while two are in flight is `queued`.

**Done means:** "Tell Charlie when new mail arrives" posts one task per new message after the initial sync, with that message's id.

**Exit gate:** `make test` exits 0.

## V-11: Tell a Grok Bot when a files folder changes

**Kind:** Medium
**Status:** Not started.

**Goal:** The same phrase works on a files folder.

**Entry gate:** V-10 and U-2 complete.

**Work items:**

- [ ] Event `file.updated` on a folder id. The files folder menu uses the same dialog. Context type is `file` for the folder id.
- [ ] A full listing does not fire. A change after the first browse does.
- [ ] Test: one change, one request; the initial listing, none.

**Done means:** "Tell Betty when this folder is updated" posts when that files folder changes.

**Exit gate:** `make test` exits 0.

## Open questions for Andrew

- V-Q1. At the start of V-6, copy the Authorization header from one saved Grok Bot routine and write it into the Lock. Public notes disagree on Bearer versus a signed webhook. The client follows the routine in hand.
- V-Q2. At the start of V-10, how far do bot queue limits go? Two POSTs in flight per bot is only the stand-in. The talk covers depth, what happens when the queue is full, and whether a burst becomes one request. Nothing is built for this before that phase.
- V-Q3. At the start of V-10, how much of this is a suite of Lua tools and examples on `P.bot.tell`, and how much is C? The enqueue stays in the core. Samples and extra actions wait on that talk. Nothing is built for this before that phase.

## Working log

- 2026-10-16 Opened from old plan §11 and §16 (two Grok Build sessions' sketch, endorsed with changes). Decisions: JSON-RPC over the socket in the host, the dispatch order, an exit code header, HMAC-hashed keys, Settings → Bots, a bot badge from a local marker, sequencing after the initial working copy.
- 2026-10-16 With Rust minimized, the socket listener and client move from the host into the C core. The `main.rs` dispatch order is unchanged, and it calls `pav_cli_main`.
- 2026-10-16 Added a `handshake` as the first message (protocol, `apiVersion` tied to `PAVILLION_API_VERSION`, and capabilities), with exit 9 on a mismatch. The client is C (`pav_cli_main`), and CLI writes go through the L write queue.
- 2026-10-17 Andrew scoped Grok Bots. V-5 is the fifth nav cell and Features visibility, after J-6. V-6 is the webhook and the request states. V-7 is the transcript, prod, and the Help instruction. V-8 is the composer on the open item. V-9 is file commands after U-2. Nothing here is started. J-2 is not reopened. The token stays in `PAVILLION_TOKEN`.
- 2026-10-17 Andrew added triggers. "Tell Charlie when new mail arrives" and "Tell Betty when this folder is updated" are V-10 and V-11. They reuse a bot request. They are not mail rules and they are not Sieve. Initial sync does not fire them.
- 2026-10-17 Andrew accepted the trigger shape. V-Q2 (queue limits) and V-Q3 (Lua examples rather than more C) are talks at the start of V-10. Nothing is built for them before then.
- 2026-10-17 V-5 is in progress. Add stays disabled, with a tooltip. Settings headings are Features, Themes, Icons, Fonts, Tabs, Zooms. The phase stamp is 0.43, the count of completed plan steps. The window check is open.
- 2026-10-17 V-5 complete. Andrew accepted the window. Features toggles and the empty Grok Bots home are in. Ctrl+C requests the clean session write. A forced kill still quick-starts. The phase stamp is 0.44. `PAVILLION_BUILD` stays 152. `make test` is 69 Unity tests and 119 Vitest tests. L-1 is next, step 42. Do not start it in this sitting.
- 2026-10-17 Ctrl+C correction. The first signal still asks for the window close. The shell no longer exits on the next signal before that write returns. Another Ctrl+C during the two-second wait is ignored. A forced kill still quick-starts. `src-tauri/src/lib.rs` is 246 lines. L-1 was not started.
- 2026-10-17 Ctrl+C again. Waiting on Tauri's window-destroyed event timed out, and the process exited before the core write. Ctrl+C now asks the page for the same quit the title bar uses and waits for `prefs.ui.set` to return. The core is in-process. L-1 was not started.
- 2026-10-17 Ctrl+C page process. A rebuild of the page-quit wait still printed `session save did not finish` after two seconds. WebKit's page is in the terminal group. GLib resets its SIGINT to terminate and leaves a blocked mask alone, so the page was the process that died and the script never ran. The shell now blocks SIGINT and SIGTERM before any child and receives them with `sigwait`. The save finishes when `prefs.ui.set` returns `shutdown` `clean`. `signal-hook` is gone. `src-tauri/src/lib.rs` is 318 lines. A separate core process was not started. L-1 was not started. The window was not run.
- 2026-10-18 Ctrl+C and the glibc line. The scavenger thread in the UI process, not WebKit's page, took SIGINT with the default action and killed the app before the save. The block stays. A counting `sigaction` covers that thread, and `sigtimedwait` still covers a fully blocked delivery. A second Ctrl+C during the two-second wait does not cut the write off. A normal window close printed `free(): corrupted unsorted chunks` from WebKitGTK 2.52's web process during EGL teardown (bug 303336), after the profile was already `shutdown` `clean`. `run()` sets `WEBKIT_DISABLE_COMPOSITING_MODE=1` when that variable is unset. Andrew confirmed Ctrl+C restores tabs and a window close stays quiet. `src-tauri/src/lib.rs` is 373 lines. `dist/pavillion-mini` from before this change does not contain it until the next `make mini`. `PAVILLION_BUILD` is 156. This sitting did not bump it. L-1 was not started. This sitting did not commit.
- 2026-10-18 The living lock moves Grok to the first footer cell and widens Features. J-9 does that move. V-5's accomplished text still says top rail. That record stays. Task and note CLI commands are X-3 and Y-3, after V-4. L-1 was not started.
- 2026-10-18 GitHub is the second footer cell and a Features key, default off. It is not a bot and not a V phase. The placeholder is docs/GH-GITHUB.md. V-5's accomplished text stays. L-1 was not started.

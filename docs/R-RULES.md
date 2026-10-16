# R — RULES

**Status:** Not started (opened as G-5 and G-7 on 2026-10-15; moved here and scoped 2026-10-16).
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

Embedded Lua 5.5 in the core, its sandbox and the `P` API, and mail rules as Lua's first job. Lua is a fixed, documented API, not a plugin system: plan §1's "no add-ons" stands. Hydrogen's references: [LUA_FEATURES.md](https://github.com/500Foods/Philement/blob/main/docs/H/LUA_FEATURES.md) (Philement repo, `docs/H/LUA_FEATURES.md`) for the language and `docs/H/core/subsystems/scripting/lua_api.md` in the Philement repo (not yet on GitHub `main`, so no link) for the `H` table this `P` table follows. Link them; do not copy them.

## Phases

One phase is one session. Do not start the next one in the same conversation.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| R-1 | Lua 5.5 vendored, the sandboxed state, caps, the script thread (was G-5) | Medium | Not started |
| R-2 | The `P` API: `P.log`, `P.mail` reads, async handles and `P.wait`, `docs/LUA_API.md` | Medium | Not started |
| R-3 | Mail rules engine: declarative JSON compiled to Lua, on-arrival run, intents (was G-7) | Hard | Not started |
| R-4 | Rules editor, dry run, apply to folder, run log and undo, disable banner | Medium | Not started |
| R-5 | Sieve export of simple rules to Stalwart | Medium | Not started |

Kind: **Easy** is one module and a test. **Medium** crosses one boundary. **Hard** hooks into the sync path.

## How a session works

1. Read Resume in [PAVILLION_PLAN.md](../PAVILLION_PLAN.md), the Status column above, and the previous phase's Status, Accomplished, Lessons learned, and Handoff.
2. Re-read only this phase: Goal, Entry gate, Work items, Done means, Exit gate. Open the lock section this phase cites.
3. Ask the qualifying questions. Wait for approval before editing source.
4. Do this phase only. Check a work item `[x]` only after its check has passed. Defer with `[~]`, one line, and the phase it moves to.
5. Before marking complete, write Status, Accomplished, Lessons learned, and Handoff, update the Status column here and in the index, and append the Working log. Then stop.
6. A sitting that ends early sets Status to **in progress**, names the last finished work item, and leaves the rest unchecked.

Leave Accomplished, Lessons learned, and Handoff off a phase that has not started.

## Lock

### Lua

- Lua 5.5, vendored as source under `third_party/lua/` at a tagged release, built into the core library by CMake, so every platform has the same version. Not the system Lua.
- Uses: mail rules and filters, computed views and saved searches beyond the Q syntax, templates and signatures, small automations, the hooks behind the CLI, and user-defined actions in the UI.
- Sandbox: only `base` (without `dofile`, `loadfile`, `load` of binary chunks, `collectgarbage` options other than `count`), `string`, `table`, `math`, `utf8`, and `coroutine`. No `os`, `io`, `package`, `require`, `debug`. Text chunks only (`load(..., "t")`). An instruction-count hook (`lua_sethook` every 1,000 instructions) enforces a per-run budget (default 5,000,000 instructions, about 50 ms) and a custom allocator enforces a memory cap (default 16 MB). Exceeding either aborts the run with a named error.
- One fresh state per run by default (simple and safe). **Decision metric (R-Q1):** R-3 runs the enabled rules over a 1,000-message initial sync on the fixture corpus. If fresh states cost more than **2 ms per message** on average (state creation and teardown included, measured on Andrew's machine in a release build), bulk runs switch to a pooled state that's reset between messages. Otherwise fresh states stay. Single-message arrival runs always use a fresh state.
- Threading: Lua runs only on the script thread, fed by its own queue from the core. Never on the UI, the core thread, or the net thread.
- The network and the store are reached only through `P.*`, which posts to the core and logs. Reads use the L read pool. Every write a rule causes (intents, `rule_run` rows) is submitted as a `pav_db_write` job to the store writer (L Lock), never written directly. Async calls return a handle; `P.wait(h)` (or `P.wait_all{...}`) yields the coroutine until the core replies, as `H.wait` does in Hydrogen.
- `P.mail.send` creates a **draft** only. Nothing a script does sends mail without a user action. Destructive actions (permanent delete, mailbox delete) are not in the API.
- Scripts are files in `<workdir>/scripts/<profile id>/*.lua` with a manifest in the profile's `scripts` array (`{ "id", "name", "file", "enabled" }`). (Plan G-LAYERS said a manifest in `Pavillion.json`; the index is never a profile, so the manifest is on the profile.) Edited in CodeMirror 6 with Lua highlighting. Every run is logged with script name and run id.
- `docs/LUA_API.md` documents the `P` table. A function is not real until it is listed there.
- **API stability:**
  - The `P` API has a version, `P.version`, a string `"<major>.<minor>"` that starts at `"1.0"` and is also recorded at the top of `docs/LUA_API.md`.
  - New functions arrive only in a minor version. Nothing changes meaning within a major version.
  - A function is deprecated for at least one minor version before it is removed. While deprecated, it still works, and each script that calls it logs one `warn` line per run (`lua: P.x is deprecated since 1.N, removed in 1.N+1; use P.y`). Removal, or any change in meaning, needs a major version.
  - A script may declare `"minApi": "1.2"` in its manifest entry. A script whose `minApi` is newer than `P.version` isn't loaded, and its rules show "needs Pavillion's Lua API 1.2".
  - `docs/LUA_API.md` marks each function with the version it was added in and, when deprecated, the version it goes away.

### Mail rules

- **Two tiers, one engine.** Declarative rules are JSON in the profile `rules` array, built in the rules editor. Conditions on from, to, cc, list-id, subject, any header, size, account, mailbox, has-attachment, with `equals`, `contains`, `glob`, `regex` (a small safe regex subset compiled in C, not Lua patterns, so declarative rules cannot backtrack catastrophically), combined with `all` or `any`. Actions: move, copy, add or remove keyword, flag, mark read, mark junk, set priority keyword, draft a reply or forward, stop. The core compiles each to a Lua function. Raw-Lua rules are scripts that define `function rule(msg) ... end` against the same `P.rule` API.

```json
{ "id": "r-willard-reports", "name": "Willard reports", "enabled": true, "where": "client",
  "accounts": ["007"], "match": "all",
  "conditions": [ { "field": "from", "op": "contains", "value": "willard@500foods.com" },
                  { "field": "subject", "op": "glob", "value": "Report *" } ],
  "actions": [ { "do": "move", "mailbox": "Reports/Willard" }, { "do": "mark_read" }, { "do": "stop" } ] }
```

- **When rules run:** on arrival, after sync stores a new message in an Inbox-role mailbox (or a mailbox the rule names) and **before** `mail.messages_changed` and `mail.new_mail` announce it, so a moved message never flashes in the Inbox and is never notified. Also on demand ("Apply rules to this folder") and as a dry run that lists intents without changing anything.
- **Effects are intents.** A rule returns intents (`move X to Y`, `keyword +Z`). The core applies them as batched `Email/set` through the op queue. Scripts never call the network directly. Each run writes `rule_run` with intents and op ids, so a run can be undone from the run log.
- **Order and stop:** global rules first, then per-account rules, in the user's order. `stop` ends evaluation for that message.
- **Per-message wall-clock budget:** each evaluation of the rules for one message has a 50 ms deadline (`preferences.rules.messageBudgetMs`, default 50). This is on top of the instruction cap.
  - The instruction hook checks a monotonic deadline (`clock_gettime(CLOCK_MONOTONIC)`) every 1,000 instructions and raises a `budget` error when it's passed.
  - A watchdog on the script thread's timer catches a C call that never returns to the hook. At deadline + 50 ms it sets an abort flag that every `P.*` C function checks, and it raises the error at the next hook. A state that still doesn't return within 1 s is abandoned and replaced, and the script thread goes on with a fresh state.
  - Over budget counts exactly like hitting the instruction cap: one failure toward the three-in-an-hour disable rule.
- **Safety:** same sandbox and caps (instruction, memory, and the 50 ms per-message budget). A rule that errors or hits a cap three times in an hour is disabled with a `rule.disabled` banner; the message is left untouched. No rule sends. Reply and forward actions create drafts.
- **Arrival budget (per page, separate from the per-message budget):** if rules for one sync page take more than 2 s in total, the remaining messages are announced unfiltered and filtered in the background, logged at warn, so sync never stalls behind Lua. The 50 ms limit protects against one bad rule. The 2 s limit protects sync from many slow but legal ones. Neither replaces the other.
- **Server side:** a declarative rule that maps cleanly to Sieve (`fileinto`, `addflag`, `header`/`address` tests with `:is`/`:contains`/`:matches`, `stop`) can be exported to Stalwart through JMAP Sieve (`urn:ietf:params:jmap:sieve`, `SieveScript/set` and `SieveScript/validate`), so it applies while Pavillion is closed. Raw-Lua and regex rules stay client-side. Each rule shows where it runs (`where`: `client` or `server`), and a rule is never active in both places: exporting sets `where: "server"` and the client skips it.
- The rules editor shows the generated Lua read-only so an advanced user can start a raw rule from it.

## R-1: Lua in the core, sandboxed

**Kind:** Medium
**Status:** Not started.

**Goal:** The core can run a Lua chunk on a script thread inside the sandbox and caps.

**Entry gate:** M-8 complete (milestone 3 plus the IMAP backend).

**Work items:**

- [ ] Vendor Lua 5.5 source in `third_party/lua/` with its license; CMake object library linked into the core.
- [ ] `core/src/lua_host.c`: state creation with the allowed libraries, the instruction hook (with the monotonic deadline check from the Mail rules Lock), the script-thread watchdog, the allocator cap, text-only `load`, error capture with traceback.
- [ ] Script thread and queue; `script.run {source|scriptId, args}` returns the result as JSON (tables to objects or arrays).
- [ ] Unity: `test_lua_runs_chunk`, `test_lua_no_os_io_debug_package`, `test_lua_instruction_cap`, `test_lua_memory_cap`, `test_lua_binary_chunk_refused`, `test_lua_runs_off_core_thread`.

**Done means:** `return 1+1` returns 2, and every escape in the tests is refused.

**Exit gate:** `make test` exits 0 with the six tests.

## R-2: The `P` API

**Kind:** Medium
**Status:** Not started.

**Goal:** Scripts can log, read mail data, and wait on async core calls.

**Entry gate:** R-1 complete.

**Work items:**

- [ ] `P.log(level, msg)`, `P.system.version()`, `P.now()`, `P.tz`, `P.json.encode/decode`.
- [ ] `P.mail.get(id)`, `P.mail.search(query)` (Q syntax), `P.mailbox.list(account)`, `P.contacts.find(addr)` (returns nil until S), `P.mail.send(draft)` creating a draft only.
- [ ] Async handles: each `P.*` that crosses to the core returns a handle; `P.wait`, `P.wait_all`; implemented with coroutines and the script queue.
- [ ] Scripts manifest on the profile (`minApi` optional); `script.list`, `script.save`, `script.delete`. `P.version` is `"1.0"`. Unity: `test_script_min_api_too_new_not_loaded`, `test_deprecated_call_warns_once_per_run`.
- [ ] `docs/LUA_API.md` modeled on Hydrogen's `lua_api.md` layout, linking its language doc.
- [ ] Unity: `test_p_wait_returns_core_result`, `test_p_mail_search`, `test_p_mail_send_makes_draft_only`, `test_p_log_tags_run_id`.

**Done means:** A script can search mail and draft a reply, and nothing is sent.

**Exit gate:** `make test` exits 0. `docs/LUA_API.md` lists every `P` function.

## R-3: Mail rules engine

**Kind:** Hard
**Status:** Not started.

**Goal:** Declarative and Lua rules run on arrival before the UI sees the message, and their effects go through the op queue.

**Entry gate:** R-2 complete.

**Work items:**

- [ ] Profile `rules` array with validation; `rule.list`, `rule.save`, `rule.delete`, `rule.order`.
- [ ] `core/src/rules_compile.c`: JSON to Lua source (deterministic, readable), `rule.compile_preview`. Safe regex subset compiler in C exposed to Lua as `P.rule.re(pattern)`.
- [ ] Intents and `rule_run` rows are applied as `pav_db_write` jobs, one per evaluated page (L Lock). The arrival hook between M-4/M-5 store and announce; the 2 s per-page arrival budget; the 50 ms per-message budget (deadline in the hook plus the watchdog); intents to op queue; `rule_run` rows; `rule.disabled` after three failures in an hour.
- [ ] R-Q1 measurement: a benchmark (`make bench-rules`) runs the enabled fixture rules over a 1,000-message initial sync with fresh states, then reports the mean ms per message. If it's over 2 ms, add a pooled state with reset (`test_pooled_state_isolated` proves no globals leak between messages) and record both numbers. Otherwise record the number and keep fresh states. Write the result into the Lua Lock.
- [ ] Unity: fixture messages through compiled rules assert intents for each condition and op; `test_rule_stop`, `test_rule_runs_before_announce`, `test_rule_error_disables_after_three`, `test_rule_message_budget_50ms` (a busy loop that stays under the instruction cap is still stopped near 50 ms and counts as a failure), `test_rule_watchdog_stuck_c_call` (a test-only `P` function that sleeps 2 s is aborted, and the next message runs on a fresh state), `test_rule_budget_announces_rest`, `test_rule_never_sends`.

**Done means:** A rule moving Willard reports runs on arrival and the report never appears in the Inbox list or a notification.

**Exit gate:** `make test` exits 0. Against the container, deliver a matching message; it lands in the target mailbox and no `mail.new_mail` for Inbox fires.

## R-4: Rules editor and run log

**Kind:** Medium
**Status:** Not started.

**Goal:** Andrew builds, tests, and undoes rules in the window.

**Entry gate:** R-3 complete.

**Work items:**

- [ ] Settings → Rules heading: list with order (drag and keyboard), enable toggle, where it runs; editor with conditions and actions, the generated Lua read-only (CodeMirror 6 with `@codemirror/legacy-modes` Lua), and a raw Lua rule editor.
- [ ] Dry run on a chosen mailbox listing intents per message; "Apply to this folder" as activity `rules.run`.
- [ ] Run log view (from `rule_run`) with Undo per run (`undo.run` over its op ids).
- [ ] Disabled-rule banner with a link to the rule and the error.
- [ ] Mailbox context menu: "Apply rules here", "Create rule from this message" prefilling from and subject.

**Done means:** A rule built in the editor dry-runs, applies, and is undone from the log.

**Exit gate:** `make test` exits 0. In `make build`, the three steps work against real or container mail.

## R-5: Sieve export

**Kind:** Medium
**Status:** Not started.

**Goal:** A simple rule can run on the server.

**Entry gate:** R-4 complete, and the server advertises `urn:ietf:params:jmap:sieve` (else this phase records that and closes as not applicable).

**Work items:**

- [ ] `core/src/sieve.c`: which declarative rules map (the Lock's subset), Sieve generation, one managed script `pavillion` per account, `SieveScript/validate` then `SieveScript/set` and activate.
- [ ] Editor: "Run on server" toggle only for mappable rules; exporting flips `where` and the client skips it; un-exporting removes it from the script.
- [ ] Unity: `test_sieve_maps_fileinto_addflag`, `test_sieve_refuses_regex_and_lua`, `test_rule_never_active_both_places`.

**Done means:** A server rule files mail while Pavillion is closed.

**Exit gate:** `make test` exits 0. With the app closed, deliver a matching message to the container; it is filed.

## Open questions for Andrew

- R-Q1. Decided by a metric (Lua Lock): fresh state per run unless R-3 measures more than 2 ms per message over a 1,000-message initial sync. Not a question for Andrew any more.
- R-Q2. Does the Stalwart version in use advertise `urn:ietf:params:jmap:sieve`, and should simple rules default to the server or the client?
- R-Q3. Does Stalwart already hold Sieve scripts for these accounts that a `pavillion` script must coexist with?

## Working log

- 2026-10-15 Lua and mail rules designed in G-LAYERS (G-5, G-7).
- 2026-10-16 Moved here and scoped into R-1 to R-5. Decisions: vendored Lua 5.5, exact library allowlist and caps, script manifest on the profile, a C safe-regex subset for declarative rules, a 2 s arrival budget, three-strikes disable, `rule_run` log for undo, one managed Sieve script per account.
- 2026-10-16 Review: links point to the Philement repo on GitHub. R-Q1 is settled by a metric (2 ms per message over a 1,000-message sync, measured in R-3). `P.version` stability policy and manifest `minApi` added to the Lock.
- 2026-10-16 Added a 50 ms wall-clock budget per message evaluation: a deadline in the instruction hook plus a watchdog for stuck C calls, counted like the instruction cap. The 2 s per-page arrival budget is kept as a separate limit. Rule writes go through the L write queue.

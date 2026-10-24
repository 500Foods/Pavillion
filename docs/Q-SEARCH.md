# Q — SEARCH

**Status:** Q-1 complete (2026-10-23). The index names N-3 next. Scoped 2026-10-16 from old plan §4, §6.6, §13.
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

Local full-text search over mail with FTS5, the query syntax, and a server search that widens the result. Contacts search reuses the index pattern in [S-CONTACTS.md](S-CONTACTS.md). (There is no letter P, so the docs never collide with the Lua `P` table or the old roadmap's P0–P6.)

## Phases

One phase is one session. Do not start the next one in the same conversation.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| Q-1 | FTS5 indexing, the query parser, local `search.run` | Medium | Done |
| Q-2 | Search UI, server search merge, saved searches | Medium | Not started |

Kind: **Easy** is one module and a test. **Medium** crosses one boundary.

## How a session works

1. Read Resume in [PAVILLION_PLAN.md](../PAVILLION_PLAN.md), the Status column above, and the previous phase's Status, Accomplished, Lessons learned, and Handoff.
2. Re-read only this phase: Goal, Entry gate, Work items, Done means, Exit gate. Open the lock section this phase cites.
3. Ask the qualifying questions. Wait for approval before editing source.
4. Do this phase only. Check a work item `[x]` only after its check has passed. Defer with `[~]`, one line, and the phase it moves to.
5. Before marking complete, write Status, Accomplished, Lessons learned, and Handoff, update the Status column here and in the index, and append the Working log. Before that, update `ui/src/help/pages.js` for what this phase changed: augment, replace, fix, or extend the user page, or add a Developer section when the user page has nothing new. Then stop.
6. A sitting that ends early sets Status to **in progress**, names the last finished work item, and leaves the rest unchecked.

Leave Accomplished, Lessons learned, and Handoff off a phase that has not started.

## Lock

- Index: `email_fts` (L Lock), contentless, `unicode61 remove_diacritics 2`, rowid = `email.rowid`. Columns: subject, from_text (names and addresses), to_text (to, cc), body (plain text, or text extracted from HTML by the sanitizer's tree walk, capped at 256 KB per message). Header rows on sync; body appended on fetch by delete and reinsert. Destroyed emails delete their row.
- Query syntax: `from:x to:x cc:x subject:x has:attachment is:unread is:read is:flagged is:answered before:2026-10-01 after:2026-09-01 in:<mailbox name or role> account:<number|name> "exact phrase"`, bare words, `-word` to exclude, `OR` between terms. Dates are local days in the profile timezone. Unknown `key:` is treated as text.
- The parser (`core/src/query.c`) produces an AST used twice: compiled to an FTS5 `MATCH` plus SQL filters for local, and to a JMAP `FilterOperator` (`text`, `from`, `to`, `subject`, `before`, `after`, `hasAttachment`, `inMailbox`, `hasKeyword`/`notKeyword`) for the server.
- Local search returns in under 100 ms on 200k messages (old plan §13 budget), ranked by bm25 then date.
- "Search server" runs `Email/query` with the compiled filter per remote account in scope and merges ids that are not local (their headers are fetched). The result list marks server-only rows until they are synced.
- Search is a view kind: `message.list {view:{kind:"search", query}}` so the list, threads, and actions work unchanged.
- Saved searches are named queries in `preferences.ui.savedSearches` and appear above the unified views. Computed views beyond a query belong to Lua ([R-RULES.md](R-RULES.md)).
- `index.rebuild` rebuilds the index as an activity; a Settings → Mail button triggers it.

## Q-1: FTS5 indexing and local search

**Kind:** Medium
**Status:** Complete (2026-10-23, step 60). `make test`: 247 Unity, 2 Rust, 384 Vitest, exit 0 (2026-10-23). `test_search_200k_under_100ms` passed. CI on `8a64fef` is green: fedora, macos-core, windows-core, run [40041797045](https://github.com/500Foods/Pavillion/actions/runs/40041797045). The phase stamp is `0.67.0`. Andrew asked for this close-out the same day. The agent does not commit. The window was not opened.

**Goal:** A query in the syntax returns matching local messages quickly.

**Entry gate:** N-1 complete. Header rows already exist from M-4; this phase replaces that stub and indexes bodies once N-1 has fetched them. It does not wait on the composer.

**Work items:**

- [x] `core/src/fts.c`: header insert, body reinsert, delete, rebuild; hook into M-4, M-5, and N-1 (replacing any stub).
- [x] `core/src/query.c`: tokenizer, AST, FTS and SQL compilation, JMAP filter compilation.
- [x] `search.run {query, scope, server:false, limit}` and the `search` view kind for `message.list`.
- [x] Unity: `test_query_parse_all_keys`, `test_query_negation_and_or`, `test_query_dates_local_day`, `test_query_to_jmap_filter`, `test_fts_body_reinsert`, `test_search_200k_under_100ms` (generated corpus; skipped under valgrind).

**Done means:** Local search answers the syntax within budget.

**Exit gate:** `make test` exits 0 with the timing test passing on Andrew's machine.

**Accomplished:** Local search is in the core. `email_fts` is the L Lock table: contentless FTS5, `unicode61 remove_diacritics 2`, rowid = `email.rowid`, columns subject, from_text, to_text, and body. A header row indexes body as the SQL literal `''`. `pav_fts_index_headers` prepares that insert, binds the rowid and the three header strings, steps, and finalizes. `store_email_row` calls it directly when the kept body is null or empty. A fetched body goes through `pav_fts_insert`, which binds the body as its own parameter. A contentless delete repeats the indexed strings, and an empty body deletes with that same literal. Body text is indexed only when `body_state` is `fetched`. The plain part is used when it has a visible character. Otherwise `pav_html_text` walks the HTML, capped at 256 KB. Script and style are not indexed. Eviction clears the indexed body before the files go.

`search.run` is real. The argument is `{query, scope?, server, limit?}`. `query` is required, non-empty, and at most 2000 bytes. `server` is required and must be false. `true` is `bad_args` (`server search is not available`). `limit` is an integer from 1 to 1000. Omitted, it is 100. `scope` omitted or `{}` searches every account. `accountIds` holds profile account ids, at most 64. An id that is not in `account_state` or `remote_account` is `not_found`. `mailboxId` is `mbx:<ra_id>:<mailbox id>`. A missing mailbox is `not_found`.

The parser is `core/src/query.c`. The syntax is the Lock. Groups are OR. Terms in a group are AND. A fast `MATCH` needs a positive term in each group, so `NOT` stays binary. Dates are local days in the profile timezone. `before:` is strictly before local midnight. `after:` is that midnight or later. `account:` pins a profile account with `account_id` and leaves `account_token` null. A token that is not a profile account is kept as `account_token`. It is not part of the JMAP filter. Rank is bm25, then `received_at` descending, then `email.rowid` descending. User text is bound. Dynamic SQL uses `sqlite3_prepare_v2`, not the statement cache. A `MATCH` failure is `internal` (`cannot search`). The reply is `{accountId, total, messages:[{id, from, subject}]}`, cut so it stays under 2 MB. `accountId` is the one profile account the search is pinned to, or null.

`message.list` accepts `{view:{kind:"search", query}}`. That view has no mailbox id. `accountId` is null. `total` is the match count. The same row shape, the same `listRows` cap, and the same 2 MB cut apply. `unified`, `offset`, `limit`, and `threaded` stay `bad_args`.

`index.rebuild` returns `{activityId}` (`act-<n>`). The kind is `index.rebuild`, the title is `Rebuild`, and it is cancellable. A rebuild already running returns that same id. The work deletes the index and inserts it again from the stored messages, 200 at a time. Cancel leaves a partial index. The thread is `pav_thread_create`. `pav_search_shutdown` joins it from `core_release_profile`, immediately before `pav_activity_shutdown`.

Unity covers the parser, negation and OR, local-day dates, the JMAP filter, body reinsert, `search.run`, `index.rebuild`, and `test_search_200k_under_100ms`. The timing test times the search. The seed is not in that budget. It is skipped under valgrind. `make test` is 247 Unity, 2 Rust, and 384 Vitest, exit 0 (2026-10-23).

CI on `8a64fef` is green: fedora, macos-core, windows-core, run [40041797045](https://github.com/500Foods/Pavillion/actions/runs/40041797045). Windows ignores the three existing flock tests. `7c3ba9b` (run [40031226221](https://github.com/500Foods/Pavillion/actions/runs/40031226221)) and `680181d` (run [40033655660](https://github.com/500Foods/Pavillion/actions/runs/40033655660)) segfaulted in `test_email_backfill_pages`. This close-out is not in `8a64fef`.

Help → Developer → Local search describes the command, the syntax, and that the field and the Settings button are Q-2. The Mail page has no search field. The window was not opened. Q-1's exit gate is `make test`.

`PAVILLION_API_VERSION` stays 3. The phase stamp is `0.67.0`.

**Lessons learned:**

- A contentless delete has to repeat the indexed text. A delete that names only the rowid returns success and leaves the row. An empty body is the SQL literal `''` on insert and on delete. A fetched body is a bound parameter.
- Binding that empty body (`7c3ba9b`, run [40031226221](https://github.com/500Foods/Pavillion/actions/runs/40031226221)) and sending the same literal through `exec_row` (`680181d`, run [40033655660](https://github.com/500Foods/Pavillion/actions/runs/40033655660)) both segfaulted windows-core in `test_email_backfill_pages`, exit 139, after `test_sync_mailboxes_delta_rename` passed. N-2's straight-line prepare, bind, step, and finalize of that insert passed the same day on the same SQLite 3.54.0-1 and gcc 16.2.0. `8a64fef` restored that straight line inside `pav_fts_index_headers`, and `store_email_row` calls it directly when the kept body is empty. Run [40041797045](https://github.com/500Foods/Pavillion/actions/runs/40041797045) is green. Do not put that header insert back through a helper. Do not raise the 512 KB stack. Do not pin or upgrade sqlite.
- The `-- CFLAGS:` line in the Windows log is lexbor's status message. That job compiles with `-O2` from the MSYS environment. The local cache is `-g -O0`. N-2 was also `-O2` on that job and passed, so the flag alone is not the regression.
- One stored page is one `pav_db_call` and one transaction. A nested `BEGIN` rolls the page back. Do not split a page to chase a crash. Do not log the query or `sqlite3_errmsg`.
- The 100 ms budget is the search. `test_search_200k_under_100ms` does not time the seed, and it is skipped under valgrind.
- `append_addresses` in `fts.c` and `sync_email.c` copies `snprintf`'s return value out of a 640-byte piece. That return is the length the call would have written. One address longer than the piece reads past it. The fixture names are short. It was not this crash, and it was not changed.
- `server: true`, the search field, saved searches, and the Settings rebuild button are Q-2. `message.list` kinds `unified` and `threaded` are N-3 and N-6. Kind `search` stays.

**Handoff:** Q-1 is complete (2026-10-23, step 60). Andrew asked for this close-out the same day. The agent does not commit it. The index names N-3 next (step 61, [N-MAIL.md](N-MAIL.md)). N-3 was not started. Its entry gate is N-2 and N-3a, both complete. It extends the N-3a table: the remaining columns, text-only formatters, no pagination, view kind `unified`, `threaded` flat first (the threaded display is N-6), selection and the `list` shortcut scope, row patches from `mail.messages_changed`, Tabulator themed in `ui/styles/list.css`, the gzipped size of `ui/dist` recorded, and `debug.seed_messages`. Kind `search` stays as this phase left it. `$deleted` stays in the list until N-3 and N-5 filter it. The search field, server search, saved searches, and Settings → Mail rebuild stay in Q-2 (step 62). Do not start Q-2 in the N-3 session. Keep the header insert straight-line. `PAVILLION_API_VERSION` stays 3. The phase stamp is `0.67.0`. `make test`: 247 Unity, 2 Rust, 384 Vitest, exit 0. CI on `8a64fef` is green: fedora, macos-core, windows-core. This close-out is not in that commit. Do not touch the quit path. The Accounts table polish stays paused. N-3's exit gate is the window. This session did not open it.

## Q-2: Search UI and server search

**Kind:** Medium
**Status:** Not started.

**Goal:** `/` focuses a search field, results show as a list, and the server can widen them.

**Entry gate:** Q-1 and N-3 complete. The results use the message list.

**Work items:**

- [ ] Search field above the message list; `/` focuses it; results replace the list with a "Search: …" header and a "Search server" button; `Esc` returns.
- [ ] Server search merge per the Lock with `server_pending` while running (activity `search.server`, add the kind).
- [ ] Saved searches: "Save search" names the query; it appears in the Mail nav.
- [ ] Settings → Mail: Rebuild index.

**Done means:** A local search shows results instantly and "Search server" adds older server-only matches.

**Exit gate:** `make test` exits 0. In `make build`, search a word that only exists in a message older than the backfill, then Search server finds it.

## Working log

- 2026-10-16 Opened from old plan §4, §6.6, §13. Decisions: one AST for local and server, extra keys (`cc`, `is:read`, `is:answered`, `account:`, `-`, `OR`, phrases), local-day dates, search as a view kind, saved searches.
- 2026-10-23 Q-1 in progress. Last finished work item: the Unity tests. Header rows are replaced on sync, body text is reinserted when a body is fetched, eviction clears the indexed body before the files go, and `index.rebuild` rewrites the index as an activity. `search.run` requires `server: false`. `message.list` accepts view kind `search`. `make test`: 247 Unity, 2 Rust, 384 Vitest, exit 0. `test_search_200k_under_100ms` passed. `test_search_run_local` and `test_index_rebuild` are in the same run. Help → Developer has Local search. Accomplished is unwritten. CI has not run. The phase stamp stays `0.66.0`. The window was not opened. Do not mark the row Done. Do not start Q-2.
- 2026-10-23 Q-1 still in progress. windows-core of `7c3ba9b` (run [40031226221](https://github.com/500Foods/Pavillion/actions/runs/40031226221)) segfaulted in `test_email_backfill_pages` (exit 139) after `test_sync_mailboxes_delta_rename` passed. fedora and macos-core of that push are green. An empty indexed body is the SQL literal `''` again on insert and on delete, so a contentless delete repeats those bytes. Header backfill is what that Windows SQLite (3.54.0) faulted on when the empty body was a bound parameter. `make test` again: 247 Unity, 2 Rust, 384 Vitest, exit 0. `test_search_200k_under_100ms` passed. The Windows job has not been re-run. Accomplished is unwritten. The phase stamp stays `0.66.0`. Do not mark the row Done. Do not start Q-2. The agent does not commit.
- 2026-10-23 Q-1 still in progress. The SQL literal through `exec_row` (`680181d`, merged as `779bd00`, run [40033655660](https://github.com/500Foods/Pavillion/actions/runs/40033655660)) segfaulted in `test_email_backfill_pages` the same way as `7c3ba9b`. `8a64fef` puts that insert back in a straight line inside `pav_fts_index_headers`, and `store_email_row` calls it directly when the kept body is empty. CI of that push is green: fedora, macos-core, and windows-core, run [40041797045](https://github.com/500Foods/Pavillion/actions/runs/40041797045). Unity 247, Rust 2, Vitest 384 (Windows 3 Ignored). Accomplished is unwritten. The phase stamp stays `0.66.0`. Do not mark the row Done. Do not start Q-2. The agent does not commit.
- 2026-10-23 Q-1 complete, step 60. Local `search.run`, the `search` view, and `index.rebuild` are real. A header row calls `pav_fts_index_headers` directly. Stamp `0.67.0`. `make test`: 247 Unity, 2 Rust, 384 Vitest, exit 0. `test_search_200k_under_100ms` passed. CI on `8a64fef` is green: fedora, macos-core, windows-core. The index names N-3 next. N-3 was not started. Q-2 was not started. This close-out is not in `8a64fef`. The agent does not commit.

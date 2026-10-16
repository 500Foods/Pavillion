# Q — SEARCH

**Status:** Not started (scoped 2026-10-16 from old plan §4, §6.6, §13).
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

Local full-text search over mail with FTS5, the query syntax, and a server search that widens the result. Contacts search reuses the index pattern in [S-CONTACTS.md](S-CONTACTS.md). (There is no letter P, so the docs never collide with the Lua `P` table or the old roadmap's P0–P6.)

## Phases

One phase is one session. Do not start the next one in the same conversation.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| Q-1 | FTS5 indexing, the query parser, local `search.run` | Medium | Not started |
| Q-2 | Search UI, server search merge, saved searches | Medium | Not started |

Kind: **Easy** is one module and a test. **Medium** crosses one boundary.

## How a session works

1. Read Resume in [PAVILLION_PLAN.md](../PAVILLION_PLAN.md), the Status column above, and the previous phase's Status, Accomplished, Lessons learned, and Handoff.
2. Re-read only this phase: Goal, Entry gate, Work items, Done means, Exit gate. Open the lock section this phase cites.
3. Ask the qualifying questions. Wait for approval before editing source.
4. Do this phase only. Check a work item `[x]` only after its check has passed. Defer with `[~]`, one line, and the phase it moves to.
5. Before marking complete, write Status, Accomplished, Lessons learned, and Handoff, update the Status column here and in the index, and append the Working log. Then stop.
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
**Status:** Not started.

**Goal:** A query in the syntax returns matching local messages quickly.

**Entry gate:** O-6 complete.

**Work items:**

- [ ] `core/src/fts.c`: header insert, body reinsert, delete, rebuild; hook into M-4, M-5, and N-1 (replacing any stub).
- [ ] `core/src/query.c`: tokenizer, AST, FTS and SQL compilation, JMAP filter compilation.
- [ ] `search.run {query, scope, server:false, limit}` and the `search` view kind for `message.list`.
- [ ] Unity: `test_query_parse_all_keys`, `test_query_negation_and_or`, `test_query_dates_local_day`, `test_query_to_jmap_filter`, `test_fts_body_reinsert`, `test_search_200k_under_100ms` (generated corpus; skipped under valgrind).

**Done means:** Local search answers the syntax within budget.

**Exit gate:** `make test` exits 0 with the timing test passing on Andrew's machine.

## Q-2: Search UI and server search

**Kind:** Medium
**Status:** Not started.

**Goal:** `/` focuses a search field, results show as a list, and the server can widen them.

**Entry gate:** Q-1 complete.

**Work items:**

- [ ] Search field above the message list; `/` focuses it; results replace the list with a "Search: …" header and a "Search server" button; `Esc` returns.
- [ ] Server search merge per the Lock with `server_pending` while running (activity `search.server`, add the kind).
- [ ] Saved searches: "Save search" names the query; it appears in the Mail nav.
- [ ] Settings → Mail: Rebuild index.

**Done means:** A local search shows results instantly and "Search server" adds older server-only matches.

**Exit gate:** `make test` exits 0. In `make build`, search a word that only exists in a message older than the backfill, then Search server finds it.

## Working log

- 2026-10-16 Opened from old plan §4, §6.6, §13. Decisions: one AST for local and server, extra keys (`cc`, `is:read`, `is:answered`, `account:`, `-`, `OR`, phrases), local-day dates, search as a view kind, saved searches.

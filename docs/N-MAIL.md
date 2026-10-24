# N — MAIL

**Status:** N-3 complete (step 61, 2026-10-24). D-8 is complete. Scoped 2026-10-16 from plan §6, §7, §10, §13.
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

Reading and organizing mail: MIME, the sanitizer, the message list, the reading pane, threads, unified views, flags and moves, attachments, notifications, shortcuts, and folder retention. Sync is [M-SYNC.md](M-SYNC.md). Compose is [O-COMPOSE.md](O-COMPOSE.md). Search is [Q-SEARCH.md](Q-SEARCH.md). The mailbox tree is [E-TREE.md](E-TREE.md). The isolation rules are the [F-HARDEN.md](F-HARDEN.md) Lock and are not restated here.

## Phases

One phase is one session. Do not start the next one in the same conversation.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| N-1 | Body fetch and MIME with GMime; `message.get` text, parts, headers | Medium | Done |
| N-2 | The HTML sanitizer in the core and the hostile corpus | Hard | Done |
| N-3a | Message list: From and Subject for the selected folder | Medium | Done |
| N-3 | Message list: the remaining columns, selection, live row updates | Medium | Done |
| N-4 | Reading pane: sanitized HTML in the sandbox, remote content, `cid:`, links, plain text | Medium | Not started |
| N-5 | Flags, move, delete, archive, junk, undo toast, mailbox management | Medium | Not started |
| N-6 | Threads, unified views, message sub-tabs | Medium | Not started |
| N-7 | Attachments: save, open, inline display, raw `.eml` | Medium | Not started |
| N-8 | Notifications and the mail keyboard shortcuts | Medium | Not started |
| N-9 | Folder retention rules | Medium | Not started |

Kind: **Easy** is one module and a test. **Medium** crosses one boundary. **Hard** is a security boundary built from scratch.

## How a session works

1. Read Resume in [PAVILLION_PLAN.md](../PAVILLION_PLAN.md), the Status column above, and the previous phase's Status, Accomplished, Lessons learned, and Handoff.
2. Re-read only this phase: Goal, Entry gate, Work items, Done means, Exit gate. Open the lock section this phase cites.
3. Ask the qualifying questions. Wait for approval before editing source.
4. Do this phase only. Check a work item `[x]` only after its check has passed. Defer with `[~]`, one line, and the phase it moves to.
5. Before marking complete, write Status, Accomplished, Lessons learned, and Handoff, update the Status column here and in the index, and append the Working log. Before that, update `ui/src/help/pages.js` for what this phase changed: augment, replace, fix, or extend the user page, or add a Developer section when the user page has nothing new. Then stop.
6. A sitting that ends early sets Status to **in progress**, names the last finished work item, and leaves the rest unchecked.

Leave Accomplished, Lessons learned, and Handoff off a phase that has not started.

## Lock

### MIME and bodies

- GMime 3 parses and builds MIME in the core (`core/src/mime.c`). JMAP's `bodyValues` are used when present; GMime parses the raw blob when a part is missing, for `.eml` files dropped or imported, and for IMAP.
- Charsets: decode to UTF-8 with GMime's iconv; undecodable bytes become U+FFFD; the original is never lost (the raw blob stays).
- `message.get {id, allowRemote}` returns `MessageDetail`: headers (from, to, cc, bcc, replyTo, subject, date, messageId, list-id, authentication results summary), `text`, `htmlSanitized` (null when the message has no HTML), `hasRemote`, `attachments` (part id, name, mime, size, cid, disposition), `isDraft`, and `identityHint`. Strings are data; the UI renders them as text.

### Sanitizer (was plan §7)

- HTML is parsed with **lexbor** (locked 2026-10-16), never with regular expressions, and re-serialized from the tree. Why lexbor:
  - It's a C99 implementation of the full WHATWG tree-construction algorithm, so it builds the same DOM a browser would, which is what defeats mutation-XSS tricks.
  - It has no dependencies and an Apache-2.0 license.
  - It's actively released, and it includes a CSS parser the `style` allowlist can use.
  - gumbo-parser, the only other C candidate the old plan named, is archived and unmaintained.
  - Nothing in these docs argues for another parser.
- Vendored under `third_party/lexbor/` at tag **v3.0.0**, commit `2ae88a1c6b5261830eff73ee12bb3cdf805f3cfe` (2026-03-31), Apache-2.0. CMake builds `liblexbor_static.a`. The app links that archive with the core. It is not a Fedora package and not a system library. Commits after that tag stay out (Andrew, 2026-10-23). The tree keeps `source/`, the CMake files, and the license. The tag's tests, examples, benchmarks, utils, and packaging are not in the tree. `third_party/lexbor/PAVILLION_PIN` records the tag. Do not call `lxb_dom_character_data_replace` (the tag's copy can write out of bounds). Replace a style or text node by unlinking its children and inserting a new text node.
- `pav_sanitize` options are `allow_remote`, `email_rowid` (zero or greater), `pane_id` (F-4: 1 to 64 of `[A-Za-z0-9_-]`), and `extra_properties` (NULL, or a NULL-terminated list of names). N-2 does not write `body_cache` and does not change `message.get`. `htmlSanitized` stays null and `hasRemote` stays false until N-4. N-4 passes the pane id and `preferences.mail.cssExtra` when it displays or caches. The pane id is not an email rowid, and it is not stored inside one cached HTML file.
- Allowlist of elements for email layout: `a abbr b blockquote br caption center code col colgroup dd del div dl dt em font h1-h6 hr i img ins li ol p pre q s small span strike strong sub sup table tbody td tfoot th thead tr tt u ul`, plus `html head body style title` handled specially. `script iframe frame object embed applet form input button select textarea base meta link svg math template` are removed with their content. `noscript` is parsed with scripting on, so its content stays raw text, then the element is unwrapped and that text is escaped on output. Any other element is unwrapped (its children stay). Comments and processing instructions are removed. `title` stays, as text, with no attributes. `<style>` is not dropped because of where it sat in the source: lexbor's tree construction moves style the way a browser does, and the filter runs on the tree after that. A style element that survives is parsed and kept only as the CSS rule below allows. An empty style element is removed.
- Attributes: per-element allowlist (`href` on `a`; `src alt width height` on `img`; `align valign width height bgcolor background border cellpadding cellspacing colspan rowspan` on table parts and `body`; `color face size` on `font`; `dir lang title class id style` globally). `background` is a remote URL under the URL rule, the same as a remote `src`. All `on*` attributes are removed. A name that contains `:` is removed. `class` tokens and `id` must match `^[A-Za-z_][A-Za-z0-9_-]*$`; each kept token is prefixed `m-`, and the rest are dropped. The same prefix is applied to class and id selectors. A selector whose ident is not that pattern drops the whole rule. An attribute selector is kept as written.
- URLs: `href` allows `http:`, `https:`, `mailto:`; `src` and CSS `url()` allow `cid:` and `data:image/(png|gif|jpeg|webp)`; `http:`, `https:`, and a scheme-relative `//` URL are remote. `javascript:`, `vbscript:`, `data:` (non-image), and every other scheme are removed. A URL with an ASCII control, whitespace, or a backslash is removed. Values are read from the DOM after lexbor's parse, so an entity-encoded `javascript:` is still `javascript:`. Every kept `href` is rewritten to `pav-link://<paneId>/<n>` (the F-4 shape, `n` from 0). The returned link list is the real URL at that index. Further hrefs after 10,000 are removed rather than failing. The UI keeps that list for the pane. The core event is `{paneId, n}`.
- CSS is parsed with lexbor's syntax tokenizer and rebuilt from the tokens. A `style` attribute is a declaration list. A style element is a stylesheet. `@media` is kept, and its declarations use the same property filter, up to 4 nested levels. `@import`, `@font-face`, `@keyframes`, and every other at-rule are dropped. `expression()`, `behavior`, and `-moz-binding` are dropped. No `position` value is kept (`relative` and `absolute` go with `fixed`). The property allowlist is: margin, margin-top, margin-right, margin-bottom, margin-left, padding, padding-top, padding-right, padding-bottom, padding-left, border, border-width, border-style, border-color, border-top, border-right, border-bottom, border-left, border-top-width, border-right-width, border-bottom-width, border-left-width, border-top-style, border-right-style, border-bottom-style, border-left-style, border-top-color, border-right-color, border-bottom-color, border-left-color, width, height, min-width, max-width, min-height, max-height, box-sizing, vertical-align, float, clear, overflow, overflow-x, overflow-y, display, border-radius, border-top-left-radius, border-top-right-radius, border-bottom-right-radius, border-bottom-left-radius, background-repeat, background-position, background-position-x, background-position-y, background-size, color, background-color, background, background-image, opacity, font, font-family, font-size, font-weight, font-style, font-variant, font-stretch, line-height, text-align, text-decoration, text-decoration-line, text-decoration-color, text-decoration-style, text-indent, text-transform, white-space, letter-spacing, word-spacing, word-break, word-wrap, overflow-wrap, direction, unicode-bidi, border-collapse, border-spacing, caption-side, empty-cells, table-layout, list-style, list-style-type, list-style-position, list-style-image. `display` is one of: inline, block, inline-block, none, list-item, table, inline-table, table-row, table-cell, table-row-group, table-header-group, table-footer-group, table-column, table-column-group, table-caption, flex, inline-flex. `url()` on background, background-image, and list-style-image follows the URL rule. A kept declaration may keep `!important`. `content` is not in the list. A `<` that would be written back into a style element is escaped so it cannot close the element.
- `preferences.mail.cssExtra` is an array of extra property names, default `[]`, edited on Settings → Mail. A name matches `^[a-z][a-z0-9-]{0,39}$`. `position`, `behavior`, and a leading `-` are rejected. At most 32 names. Duplicates are allowed. `prefs.set` accepts a `mail` object whose only key is `cssExtra`. An empty array clears the extras. A non-string, an empty `mail` object, an unknown mail key, or a name outside that rule is `bad_args`. The built-in list does not change. `pav_sanitize` unions `extra_properties` with the built-in list. A missing `preferences.mail` or `cssExtra` on an old profile is filled with `[]` and the profile is rewritten once, the same way a missing cache key is.
- Remote content (images, CSS `url()`, `background`) is replaced with a 1×1 transparent GIF `data:` placeholder unless `allow_remote` is set. `has_remote` is 1 when any remote URL was seen, including when `allow_remote` kept it, and including a `url()` inside a dropped `@import`. N-2 does not look up `remote_allow` or the per-message choice. That choice is the caller's `allow_remote`. Loading remote content re-sanitizes with remote allowed, and the pane's CSP adds `img-src https:` for that render only (N-4).
- `cid:` becomes `pav-cid://<emailRowid>/<cid>`. The cid is one path segment: unreserved characters (`ALPHA`, `DIGIT`, `-`, `.`, `_`, `~`) stay, and every other byte is percent-encoded. N-4 decodes that segment before it looks up the blob. The C `pav_protocol` handler (G-8) serves the blob only if that cid belongs to that email. Rust only forwards the request. `pav-cid` and `pav-blob` stay 404 until N-4 and N-7.
- Output carries `sanitizer_version`. The constant starts at 1. N-2 does not cache, so a caller that does not store the HTML does not need to persist the version. A later version bump re-sanitizes cached bodies lazily (N-4).
- Plain-text view toggle always available. Setting `preferences.mail.plainForUnknown` (default false) shows plain text first for senders not in contacts or `remote_allow`.
- Never auto-open attachments; warn on executable types (`application/x-executable`, `.exe .bat .cmd .com .scr .js .jar .msi .sh .desktop .appimage`).

### Message list (was plan §6.1, §10)

- Tabulator (`tabulator-tables`), themed on `--pav-*`, with virtual row render. Pagination stays off, because it turns the virtual DOM off. The mail list loads one `message.list` reply and keeps those rows. The reply is the newest messages in the selected folder, up to `preferences.cache.listRows` (default 10,000, whole numbers from 1 to 10,000), plus `total` for the whole folder. Settings → Limits edits that integer and labels it Message list (Andrew, 2026-10-22). A folder with more messages shows the newest rows, and a count of how many of `total` are listed. The reply stays under 2 MB: the command stops before the next row would pass that, and `total` stays the real count. A message whose `keywords` contain `$deleted` is omitted from `message.list`, and `total` counts the messages that remain (Andrew, 2026-10-23). The tree's unread and total are the local recount in Views (N-Q6). Contacts (S) and files (U) use the same module (`ui/src/common/table.js`). No second table library.
- Shared table component (Andrew, 2026-10-19, pulled ahead of N-3): `tabulator-tables` 6.6.1, pinned exactly in `ui/package.json`, MIT licence, bundled by Vite from `node_modules` (no CDN, no Tabulator stylesheet). `ui/src/common/tabulator-lib.js` is the only module that imports it (named re-exports, so unused modules are tree-shaken), and `ui/src/common/table.js` loads that file by dynamic import, so Vite puts it in its own chunk loaded on first use; it registers only the Format and Interaction modules. Screens call `createTable(el, {columns, data, rowId, onRowClick, rowClass, groupBy, groupHeader, groupOrder, footerElement, select, onRender, download})` (K-Q11, K-Q12). The wrapper registers only the modules a table asks for: Format, Interaction and ResizeTable always; GroupRows for `groupBy`; Sort when a column sorts; ColumnCalcs when a column has `bottomCalc`; SelectRow for `select`; Export and Download for `download`. Never `TabulatorFull`. Tables fill their element (`height:"100%"`, virtual renderer) and redraw through ResizeTable. Sorting is tri-state with Font Awesome icons. `footerElement` is a DOM node the page owns, placed under the calc row. The handle is `setData`, `select(id)` (single selection, scrolls into view), `ids()` (display order), `download(name)`, `destroy()`. See [TABULATOR.md](TABULATOR.md). Shared tables fit their panel by default (Andrew, 2026-10-19): layout `fitColumns`, the main text column takes the rest (`widthGrow`), icon and checkbox columns keep fixed narrow widths, and a `ResizeObserver` on the table element calls `redraw()` when the panel or a divider changes its size. Its look comes only from `--pav-*` variables in `ui/public/styles/theme.css` (`.pav-table`). Cells are text or built DOM with `textContent`; no HTML formatter takes outside data. The first user is the Settings → Accounts list (K-Q8); N-3 extends the same module for the message list (virtual rows, selection).
- Row: account color bar, from (or to in Sent and Drafts), subject, preview, 24 h time today or `Oct 14` this year or `2026-10-14` older (`preferences.time_format`), flags (unread dot, flag, answered, forwarded), attachment icon. Formatters return text, or set `textContent`. An icon is an element this module creates from a class name. A formatter that returns an HTML string is an HTML sink and is forbidden.
- Threaded or flat per view (`ui.mail.threaded[viewKey]`). Sort by date desc default.
- Mark read on open after 1 s (`preferences.mail.markReadMs`, 0 = never).

### Views (was plan §6.1, §6.3)

- Unified Inbox, Unread, Flagged, Drafts, Sent (role-based across accounts) above the per-account mailbox tree. Unread counts in the tree. From N-3, `mailbox.list` counts those from local rows: unread is not seen, not draft, and not `$deleted`; total is every message in that mailbox that is not `$deleted`. The account row sums the recounted mailbox numbers. A parent folder does not add its children. A message in two mailboxes counts in each. The stored `mailbox.unread` and `mailbox.total` stay the server's `unreadEmails` and `totalEmails`, because sync uses them as the progress hint (Andrew, 2026-10-23, N-Q6). Accounts collapsible; order and hiding are tree view state.
- Threading uses the server `threadId`. The thread view shows the conversation oldest to newest with read items collapsed.

### Actions (was plan §6.4, §6.5)

- Read/unread, flagged, answered/forwarded (`$answered`, `$forwarded`), junk (`$junk` and move to the Junk role; not junk is `$notjunk` and move to Inbox).
- Delete moves to Trash; delete in Trash is permanent (`Email/set destroy`) with a confirm. Archive moves to the Archive role mailbox (offer to create it if missing, asking first).
- Undo toast for 5 s on move, delete, and archive (M Lock held ops).
- Mailboxes: create, rename, delete, move (reparent through the tree's drop intent), subscribe toggle; roles come from the server.

### Notifications (was plan §6.12)

- Desktop notification per new message in an Inbox-role mailbox (and mailboxes the user marks `notify`), grouped when more than 3 arrive in 10 s, per-account mute, skip messages already seen, quiet hours (open question N-Q1). Clicking focuses the window and opens the message. Delivered by the host (Tauri notification plugin) from the `mail.new_mail` event. Text only: sender name and subject, no preview unless `preferences.mail.notifyPreview`.

### Keyboard (was plan §6.13; remappable later through J-3's registry)

| Key | Action | Key | Action |
| --- | --- | --- | --- |
| `j` / `k` | next / previous message | `Enter` / `o` | open |
| `n` / `p` | next / previous thread item | `u` / `Esc` | back to list |
| `c` | compose | `r` | reply |
| `a` | reply all | `f` | forward |
| `e` | archive | `#` / `Del` | delete |
| `m` | move (picker) | `l` | keyword (later) |
| `s` / `*` | toggle flag (list scope) | `Shift+U` / `Shift+I` | mark unread / read |
| `/` | search | `g i` / `g s` / `g d` | go inbox / sent / drafts |
| `g c` / `g a` | go calendar / contacts | `Ctrl+Enter` | send (composer) |
| `Ctrl+S` | save draft (composer) | `?` | shortcut help |
| `Ctrl+1` through `Ctrl+9` | section chords, set in Features (defaults mail through argent on 1–7) | `F5` | sync now |

### Retention (was plan §6.10)

- Per mailbox: keep newest N and/or older than D days, action `trash` (default) or `destroy`. Enforced by the app over JMAP: `Email/query` on the mailbox sorted `receivedAt desc` with `position=N`, plus `before` for age, then move to Trash or destroy, through the op queue. Runs after a delta sync for that mailbox at most once an hour. Labeled "enforced while Pavillion is running": it is client logic, not a server policy. Use case: willard@500foods.com report folders. If Stalwart has or gains server-side per-folder retention, prefer it and never run both (open question N-Q3).

## N-1: Body fetch and MIME

**Kind:** Medium
**Status:** Complete (2026-10-23, step 58). `make test`: 230 Unity, 2 Rust, 380 Vitest, exit 0 (2026-10-23). CI on `5342f9d` is green: fedora, macos-core, windows-core. The phase stamp is `0.65.0`. Andrew committed this close-out and the splash reveal as `9b3bf9a`.

**Goal:** Opening a message fetches its body once and returns text, parts, and headers.

**Entry gate:** M-5 complete.

**Work items:**

- [x] `core/src/mime.c` on GMime 3 (`gmime30-devel`, pkg-config `gmime-3.0`; A's Fedora list and CMake): parse a blob, walk parts, pick text and HTML alternatives, list attachments with cids, decode charsets.
- [x] Body fetch through the backend's `fetch_body` with the Lock's `Email/get` properties; write `body_cache` (text, raw HTML path) and `attachment` rows; `body_state`.
- [x] Idle prefetch per `fetch_bodies` with at most 2 in flight, as activity `body.fetch`.
- [x] `message.get` returns the Lock's shape with `htmlSanitized: null` until N-2.
- [x] Corpus `tests/corpus/`: plain, multipart/alternative, HTML newsletter, cid-image signature (copy the pattern from Andrew's bot reports), huge attachment (generated), malformed MIME, ISO-8859-1, Windows-1252, Shift_JIS, missing charset.
- [x] Unity: one test per corpus file asserting text, part count, cids, and charset.

**Done means:** Every corpus message parses to the expected text and parts.

**Exit gate:** `make test` exits 0 with the corpus tests.

**Accomplished:** `message.get` is real. The argument is `{id, allowRemote}`. `id` is `msg:<ra_id>:<emailId>`, compared the same way as a mailbox id, without splitting on the colon. A missing or empty id, an id that does not start with `msg:`, a non-boolean `allowRemote`, or any other key is `bad_args`. An id that is not a stored row is `not_found`. `allowRemote` defaults to false and does not change the bytes.

A row whose `body_state` is `fetched`, and whose body files are still on disk, is returned from the cache. Any other state fetches. The request is `Email/get` for that id, with `textBody`, `htmlBody`, `bodyStructure`, `bodyValues`, `attachments`, `header:List-Id:asText`, and `header:Authentication-Results:asText`, plus `fetchTextBodyValues`, `fetchHTMLBodyValues`, and `maxBodyValueBytes` of 2 MB. Complete body values are stored. A used value that is missing or truncated leaves `body_state` unchanged, and the same fetch downloads the raw blob. GMime 3 parses that blob. Text and HTML are cut on a UTF-8 boundary at 2 MB. Undecodable bytes become U+FFFD. The raw blob stays.

The store writes `body_cache` (`text_plain_path`, `html_raw_path`, `html_sanitized_path` null, `has_remote` 0, `sanitizer_version` 0) and replaces the `attachment` rows. An attachment row is part id, name, mime, size, cid, and disposition. `local_path` stays null. Schema v3 adds `list_id` and `auth_summary` on `email`. A stored body sets `body_state` to `fetched`. A failed fetch sets `failed`.

The result is the Lock's `MessageDetail`. `htmlSanitized` is null. `hasRemote` is false. `date` is `sent_at`, then `received_at`, then null. `identityHint` is the identity on that remote account whose email equals the first From address, or null. Strings are data.

After a sync reaches idle, `fetchBodies` of `none` fetches nothing. `recent` takes messages received in the last 30 days. `all` takes every unfetched or evicted row. At most two run at once, as activity `body.fetch`. Prefetch waits while that account is syncing, and while a `message.get` for it holds the account slot. `activity.cancel` on that id stops the transfer.

Unity covers the ten corpus files (plain, alternative, newsletter, cid signature, malformed, ISO-8859-1, Windows-1252, Shift_JIS, missing charset, and a generated huge attachment), the `Email/get` request, a cached `message.get`, bad arguments, a JMAP body apply, the prefetch window, a prefetch that ends `body.fetch`, and `message.get` against the fixture. `make test` is 230 Unity, 2 Rust, and 380 Vitest, exit 0 (2026-10-23). CI on `5342f9d` is green: fedora, macos-core, windows-core.

Help's developer Mail sync page describes `message.get`, including the 2 MB cap and that attachment bytes stay on the server. The user Mail page still describes the From and Subject list. The reading pane is N-4.

**Lessons learned:**

- glibc, the current Apple SDK, and MinGW declare `iconv` with `char **`. The call passes `char **`. A `const char **` cast fails the MinGW build and warns on Apple. `iconv` is in glibc. `build.rs` links `iconv` on macOS and Windows, and the Unity link line in the Makefile does the same.
- Fedora's GMime 3 package is `gmime30-devel`, pkg-config `gmime-3.0`. `gmime-devel` is 2.6. macOS CI installs `gmime` from Homebrew and points `PKG_CONFIG_PATH` at it. Windows CI installs `mingw-w64-ucrt-x86_64-gmime`. A local 3.2.15 under `~/.local` is preferred when that `.pc` file exists.
- `submit_json` keeps a completion callback that is already set. `message.get` leaves it empty and gets `on_net`. Prefetch sets `prefetch_on_net` first. Overwriting that callback left the `body.fetch` activity running after the body was stored.
- The net thread does not call `pav_net_request` while it holds the server mutex. When a prefetch finishes, it posts kind 5 to the core thread, and `pav_body_take_core` starts the next fetch there. Kind 4 is the download continuation and uses the same post. `pav_sync_result_free` frees both kinds.
- The splash sits at `z-index: 100`, above the column chooser at 50. WebKit paints a scroller's bar over a fixed cover. While `#splash` is in the document, the nav, the pane, and the message list set `overflow: hidden`.
- The first frame was white because `show()` ran when the rectangle was applied, while WebKit's own background was still white. The window stays hidden until `pav-splash-painted`, and the webview background is `#000000`. A hidden view may not run animation frames, so a 32 ms timer sends the same signal. The page does not call `show()`. Andrew accepted that window on 2026-10-23. The longer note is in the L-STORE startup lessons.
- The fake keyring is process-wide and capped. `test_prefetch_ends_body_fetch` removes the secret it stored during `tearDown`.
- `Email/get` with body values stores the message when those values are complete. A missing or truncated value leaves `body_state` unchanged, and the same fetch downloads the blob for GMime. Render text and HTML stop at 2 MB on a UTF-8 boundary. Attachment rows are metadata. `local_path` stays null.

**Handoff:** N-1 is complete (2026-10-23, step 58). Andrew asked for this close-out the same day and committed it, with the splash reveal, as `9b3bf9a`. He accepted the splash window the same day. The index names N-2 next (step 59, [N-MAIL.md](N-MAIL.md)). N-2 was not started. `htmlSanitized` stays null until N-2. Attachment bytes stay on the server for N-7. The reading pane stays in N-4. `$deleted` stays in the list for N-3 and N-5. `PAVILLION_API_VERSION` stays 3. The phase stamp is `0.65.0`. `make test`: 230 Unity, 2 Rust, 380 Vitest, exit 0. CI on `5342f9d` is green: fedora, macos-core, windows-core. Do not show the window from the page. Do not touch the quit path. Do not fold the splash reveal into N-2.

## N-2: The HTML sanitizer

**Kind:** Hard
**Status:** Complete (2026-10-23, step 59). `make test`: 239 Unity, 2 Rust, 384 Vitest, exit 0 (2026-10-23). libFuzzer: 2350412 runs in 601 seconds, seed 899505327, exit 0. CI on `75b0c5a` is green: fedora, macos-core, windows-core, run [40016164091](https://github.com/500Foods/Pavillion/actions/runs/40016164091). The phase stamp is `0.66.0`. Andrew asked for this close-out the same day. The agent does not commit. Settings → Mail was not opened in the window.

**Goal:** The core turns any HTML into a safe document per the Lock, proven against a hostile corpus.

**Entry gate:** N-1 complete.

**Work items:**

- [x] Link lexbor v3.0.0 (commit `2ae88a1c6b5261830eff73ee12bb3cdf805f3cfe`) under `third_party/lexbor`. Not the Fedora package.
- [x] `core/src/sanitize.c`: `pav_sanitize(html, opts{allow_remote, email_rowid, pane_id, extra_properties}, out_html, out_links, out_has_remote)` per the Lock, including the CSS filter (`core/src/css_filter.c`). `message.get` stays null. N-4 passes the pane id and `preferences.mail.cssExtra`.
- [x] `tests/corpus/hostile/`: script, `onerror`, `onload` on body, `<svg><script>`, `<math>` tricks, `javascript:` and entity-encoded `javascript:` links, `<base href>`, `<meta http-equiv=refresh>`, `<form>`, `<iframe srcdoc>`, CSS `url()` tracking, `@import`, `expression()`, `position:fixed` overlays, look-alike link text, `data:text/html`, mutation-XSS samples (noscript, template, nested comments), unclosed tags, 10 MB HTML.
- [x] Unity: each hostile file's output contains no element, attribute, scheme, or property outside the allowlist (a checker walks the output tree); remote counts are right; link list matches; a benign newsletter keeps its table layout.
- [x] A fuzz target `tests/fuzz/sanitize_fuzz.c` for libFuzzer (run manually, documented in W).

**Done means:** The hostile corpus is inert by the checker, and the newsletter still looks like itself.

**Exit gate:** `make test` exits 0 with the sanitizer suite. A 10-minute fuzz run finds no crash (logged in Accomplished).

**Accomplished:** `pav_sanitize` is in the core. Lexbor tag v3.0.0, commit `2ae88a1c6b5261830eff73ee12bb3cdf805f3cfe`, is vendored at `third_party/lexbor` and linked as `liblexbor_static.a` with whole-archive. It is not a Fedora package. The options are `allow_remote`, `email_rowid` (zero or greater), `pane_id` (1 to 64 of `[A-Za-z0-9_-]`), and `extra_properties` (NULL, or a NULL-terminated list). Output is sanitized HTML, a link list, `has_remote`, and `sanitizer_version` 1. An `href` of http, https, or mailto becomes `pav-link://<paneId>/<n>`, and the list keeps the real URL at that index, up to 10000. Further hrefs are removed. `src`, CSS `url()`, and `background` allow `cid:` and `data:image` of png, gif, jpeg, or webp. http, https, and scheme-relative URLs are remote. A remote image becomes the 1×1 transparent GIF unless `allow_remote` is set. `has_remote` is 1 when any remote URL was seen, including when `allow_remote` kept it, and including a `url()` inside a dropped `@import`. An http href does not set `has_remote`.

Script and the other drop-with-content tags go away with their content. Unknown tags unwrap. `noscript` is parsed as raw text and then unwrapped, so its text is escaped. Comments and processing instructions are removed. `title` stays as text with its attributes stripped. `html`, `head`, and `body` stay. The CSS filter keeps the Lock's property list, `@media` only, and drops every `position` value. Class and id tokens are prefixed with `m-`. `preferences.mail.cssExtra` is an array, default `[]`, edited on Settings → Mail. `prefs.set` accepts a `mail` object whose only key is `cssExtra`. A name matches `^[a-z][a-z0-9-]{0,39}$`. `position`, `behavior`, and a leading `-` are `bad_args`. At most 32 names. The filter unions the extras with the built-in list and still rejects a banned name. A missing `preferences.mail` or `cssExtra` is filled with `[]` and the profile is rewritten once. Layout keys `split`, `min`, and `max` still move under `preferences.ui.mail`. `cssExtra` stays.

N-2 does not write `body_cache`. `message.get` still returns `htmlSanitized` null and `hasRemote` false. N-4 passes the pane id and `cssExtra` when it displays or caches.

The hostile corpus is 21 files under `tests/corpus/hostile/`. The 10 MB document is generated in the Unity test. The checker walks the output and rejects an element, attribute, scheme, or property outside the allowlist. Remote counts and the link list are checked. A newsletter keeps `table`, `tbody`, `tr`, `td`, and its text, and a remote image becomes the placeholder. `make test` is 239 Unity, 2 Rust, and 384 Vitest, exit 0 (2026-10-23).

`make fuzz-sanitize` builds `build/sanitize_fuzz` with clang `-fsanitize=fuzzer,address`. It is not part of `make test` or CI. `build/sanitize_fuzz -max_total_time=600` (seed 899505327, clang 22.1.8, default max_len 4096) ran 2350412 executions in 601 seconds, exit 0, no crash.

CI on `75b0c5a` is green: fedora, macos-core, windows-core, run [40016164091](https://github.com/500Foods/Pavillion/actions/runs/40016164091). The run on `e9a2e28` failed because `third_party/` was gitignored. Lexbor is in the tree as of `dce6854`. Andrew committed the in-progress notes as `b923040`. This close-out is not in that commit.

Help's Mail page and the Settings Mail topic describe `preferences.mail.cssExtra`. The developer Mail sync page says the sanitizer exists and that `message.get` still returns `htmlSanitized` null. Settings → Mail was covered by Vitest. The window was not opened.

**Lessons learned:**

- The pin is lexbor tag v3.0.0, commit `2ae88a1c6b5261830eff73ee12bb3cdf805f3cfe`. Andrew chose that tag knowing later master commits fix memory-safety bugs, including `character_data_replace` and `:lexbor-contains`. A crash is reported against this pin. The pin stays. The code does not call `lxb_dom_character_data_replace`. A style's text is replaced by unlinking its children and inserting a new text node. The code does not call the `:lexbor-contains` API. Scripting is set before parse. Serialization is `lxb_html_serialize_tree_str` on the document node, and those bytes are copied out before `lxb_html_document_destroy`. A `<` written back into a style element is escaped as the CSS hex `\3c `, because scripting serializes style text raw.
- `liblexbor_static.a` is linked after `libpavillion_core.a` with `--whole-archive` (macOS `-force_load`, rustc `static:+whole-archive`). Lexbor objects are not copied into the core archive. Duplicate `.c` basenames collide. The static library is forced on, lexbor's own tests are off, and the link needs `-lm`.
- `e9a2e28` added `third_party/` to `.gitignore`. All three CI jobs then failed in `make` because `third_party/lexbor/CMakeLists.txt` was not in the tree. The vendor tree is part of the commit. `dce6854` added it. `.gitignore` no longer ignores `third_party/`.
- `pav_profile_migrate_ui` treated `preferences.mail` as an old layout blob and deleted the whole object when `preferences.ui.mail` already existed. That wiped `cssExtra` on every start. `move_mail_layout` copies only `split`, `min`, and `max` into `ui.mail` and leaves `cssExtra`. A new key on `preferences.mail` has to stay out of that move. `fill_mail` runs after `fill_cache`, because `fill_cache` assigns `cache_filled`. A missing `preferences.mail` or `cssExtra` is filled with `[]` and the profile is rewritten once.
- Lexbor tokenizes `.1bad` as one DIMENSION. A number, percentage, dimension, or string outside a function and outside an attribute selector drops the whole rule. `position` is banned even when `cssExtra` names it. A name starting with `-`, and `behavior`, are rejected by `prefs.set`. The Unity checker does not run `pav_css_filter` a second time, or a class becomes `m-m-`. The substring `position:` is legal inside `background-position` and `list-style-position`.
- The HTML parser inserts `tbody`. A newsletter looks like itself when `table`, `tbody`, `tr`, `td`, and the text survive, and a remote image is the 1×1 GIF unless `allow_remote`. An http href does not set `has_remote`. `has_remote` is set for a remote image, a CSS `url()`, or a `background`, including a `url()` inside a dropped `@import`, and including when `allow_remote` kept the URL. `pane_id` is per call. It is not the email rowid, and it is not stored in one cached HTML file. N-2 does not write `body_cache`.
- `make fuzz-sanitize` needs clang with `-fsanitize=fuzzer,address`. It is not part of `make test` or CI. GCC rejects `fuzzer`. The 10-minute run used clang 22.1.8, seed 899505327, the default max length 4096, 2350412 runs, 601 seconds, exit 0. Tag v3.0.0 did not crash. The documented command has no `-max_len`.
- Settings → Mail was checked with Vitest. The window was not opened. N-2's exit gate is `make test` and the fuzz run.

**Handoff:** N-2 is complete (2026-10-23, step 59). Andrew asked for this close-out the same day. The agent does not commit it. The index names Q-1 next (step 60, [Q-SEARCH.md](Q-SEARCH.md)). Q-1 was not started. Its entry gate is N-1 complete. Its lock indexes `body` as plain text, or text extracted from HTML by the sanitizer's tree walk, capped at 256 KB. N-2 does not extract that text and does not write `html_sanitized_path`. `message.get` stays null until N-4. N-4 passes `pane_id` and `preferences.mail.cssExtra`. Lexbor stays tag v3.0.0. `third_party/lexbor` stays in the tree. `PAVILLION_API_VERSION` stays 3. The phase stamp is `0.66.0`. `make test`: 239 Unity, 2 Rust, 384 Vitest, exit 0. The 10-minute fuzz run found no crash. CI on `75b0c5a` is green: fedora, macos-core, windows-core. Do not show the window from the page. Do not touch the quit path. The Accounts table polish stays paused.

## N-3a: From and Subject

**Kind:** Medium
**Status:** Complete (2026-10-23, step 135). Andrew accepted the window the same day. `make test`: 213 Unity, 2 Rust, 380 Vitest, exit 0 (2026-10-22). The container check passed on 2026-10-22. The phase stamp is `0.64.0`. CI on `e30fdd1` is green: fedora, macos-core, windows-core. The agent does not commit this close-out.

**Goal:** The selected mailbox shows the messages already stored for it, From and Subject, so a sync can be checked on screen.

**Entry gate:** M-5 complete.

**Work items:**

- [x] `message.list` returns one mailbox's messages, newest `received_at` first, at most `preferences.cache.listRows` (default 10,000), plus `total`. Each row is `id`, `from` (the first address's name, or its email when the name is empty), and `subject`. Strings are data. `docs/API.md` lists the command as `State: real` in the shape the Unity test parses. Settings → Limits edits `listRows`.
- [x] The Mail home shows that reply in the existing Tabulator (`createTable`) when a mailbox is selected. Columns are From and Subject. Cells are text. There is no second page and no scroll-triggered load. A count says how many of `total` are listed. A `mail.messages_changed` for that account loads the reply again. A row click does nothing.
- [x] Unity: order, the `listRows` cap, from and subject text, an empty mailbox, and a mailbox on another account. A Vitest covers the two columns as text.
- [x] Help says the list is From and Subject for the selected folder, and that the cap is on Settings → Limits.

**Done means:** Clicking a folder lists From and Subject for the messages stored in it.

**Exit gate:** `make test` exits 0. In the window, a synced folder shows those two columns, and a change from another client shows up after the next poll when the list reloads.

Preview, flags, time, the attachment icon, the account color bar, selection, keyboard, and `debug.seed_messages` stay in N-3. Bodies, the sanitizer, and the reading pane stay in N-1, N-2, and N-4. `message.list` kinds `unified`, `search`, and `threaded` stay in N-3 and Q-1. The Tabulator package and `createTable` are already in the tree. This phase does not add a second table library. The loaded rows use the virtual renderer. Rows past `listRows` are not fetched. `$deleted` (IMAP `\Deleted`) is stored in `keywords` and this list does not filter it. Showing or hiding that flag stays with N-3 and N-5 (Andrew, 2026-10-23).

**Accomplished:** `message.list` is real. The argument is `{view:{kind:"mailbox", id}}`, and `id` is the mailbox tree node id. Any other view kind, and `offset`, `limit`, or `threaded`, is `bad_args`. An unknown id is `not_found`. The result is `{accountId, total, messages:[{id, from, subject}]}`. `from` is the first address's name, else its email, else `""`. A null subject is `""`. Order is `received_at` descending, then `email_rowid` descending. `total` is the whole folder. The cap is `preferences.cache.listRows`, default 10000, whole numbers from 1 to 10000. A missing key is filled on load. `prefs.set` rejects a non-integer or a value outside that range. The reply stops before the next row would pass 2 MB, and `total` stays the real count. The first row is kept.

The Mail home loads that one reply into the existing Tabulator, virtual rows, pagination off. Columns are From and Subject. Cells are text. The count is the plain number when the shown rows are the whole folder, and `shown of total` otherwise. `mail.messages_changed` for that account loads the reply again. A row click does nothing. Settings → Limits labels the cap Message list. Help's Mail page and the developer Mail sync page describe it.

Unity `test_message_list_folder` covers order, the cap, from and subject text, an empty mailbox, another account, a mailbox id that contains a colon, and the 2 MB stop. Vitest covers the two columns as text, the count, and the Limits field. `make test` is 213 Unity, 2 Rust, and 380 Vitest, exit 0 (2026-10-22).

The container check used a temporary profile and the fake keyring, and did not call `sync.now`. Inbox went from 1 message to 2 after the next poll, 120 seconds. `account.remove` cleared that account. The container was left up.

Andrew opened the live profile on 2026-10-23. The count is there and the list fills immediately. Sent, with more than 3700 messages, renders at once. He accepted the window. Inbox shows a few more messages than he expected because `$deleted` is stored and not filtered. He left that for later.

**Lessons learned:**

- Pagination turns Tabulator's virtual renderer off. One reply, `height: "100%"`, and `headerSort: false` lists a Sent folder of more than 3700 messages immediately.
- `listRows` is read by the core, so it lives on `preferences.cache` with `bodiesMb` and `blobsMb`. `preferences.ui` stays opaque. The default is 10000, not 100.
- A string returned from a Tabulator formatter is HTML. The cell returns an element and sets `textContent`.
- The tree node id contains more than one colon. The lookup compares `('mbx:' || ra_id || ':' || id)` and does not split the argument.
- The default profile string in the Unity test has to include `listRows`, or `test_ensure_writes_defaults_once` fails against the file the core writes.
- Limits Apply sends `listRows` only when that input is in the form. The mock replaces the whole cache object, so a fixture that has the input must expect the key.
- `$deleted` is in the stored keywords JSON. The six flag columns do not include it, and `message.list` omits rows whose keywords contain `$deleted`, for `mailbox`, `search`, and `unified` (N-3). `total` counts the rows that remain. The keyword stays stored. The UI row-filtering convention is in [UI-FILTER.md](UI-FILTER.md); N-5 still moves a message to Trash or destroys it.
- A flag does not change From or Subject. The visible check for this list is a new message, a subject edit, or a delete of a listed row.

**Handoff:** N-3a is complete (2026-10-23, step 135). Andrew accepted the window the same day. The index names N-1 next (step 58, [N-MAIL.md](N-MAIL.md)). N-1 was not started. N-3 stays step 61. `$deleted` is stored and this list does not filter it. Showing or hiding that flag belongs with N-3 and N-5. `PAVILLION_API_VERSION` stays 3. The phase stamp is `0.64.0`. The container was left up. Andrew pushed `e30fdd1`. CI on that push is green: fedora, macos-core, and windows-core. This close-out is not in that commit. The agent does not commit.

## N-3: Message list

**Kind:** Medium
**Status:** Complete (step 61, 2026-10-24). `make test`: 248 Unity, 2 Rust, 395 Vitest, exit 0 (2026-10-24). Q-1 left `message.list` view kind `search` real. This phase adds `unified` and `threaded` (flat first; threaded display in N-6) and leaves `search` in place. The list hides `$deleted` rows. Folder badges recount local rows and skip `$deleted` (N-Q6). CI for the N-3 push (`28ec6a9`), run [40064822844](https://github.com/500Foods/Pavillion/actions/runs/40064822844), is green: fedora, macos-core, windows-core. Foods sync was failing `cannot store messages` on SQLite 3.51: a contentless full-text delete whose body does not match aborts the writer; `delete_row` rolls that back and retries an empty body. The phase stamp is `0.69.0`. Gzip -9 of `ui/dist` is 292633 bytes. The agent does not commit this close-out.

**Goal:** The Mail home tab shows the selected mailbox's messages with smooth scrolling at 100k rows. From and Subject for one page are N-3a. This phase extends that table. `message.list` omits a row whose `keywords` contain `$deleted`, and `total` counts the rows that remain. The keyword stays stored. N-5 still moves a message to Trash or destroys it.

**Entry gate:** N-2 and N-3a complete. D-8 also lands first (Andrew, 2026-10-23), so this list calls `formats.js` and does not format a date itself.

**Decided (Andrew, 2026-10-23), before any N-3 code:**

- The time column calls `formats.js`. This phase does not format a date.
- `unified` is `{view:{kind:"unified", name}}` with `inbox`, `unread`, `flagged`, `drafts`, and `sent`. Unread is every unseen message. Flagged is every flagged message. Inbox, Drafts, and Sent are those roles across accounts. Drafts and Sent show To. The nav entries stay in N-6.
- `debug.seed_messages` is `{mailboxId, count}`. It is absent when `NDEBUG` is set. It inserts that many local rows into that mailbox, clamped to `listRows`, and does not use the network.
- The mail list theme extends `.pav-table` in `ui/public/styles/theme.css`. New variables ship in `dark.css`, `light.css`, and both starter `*.theme` files. There is no `ui/styles/list.css`.
- `message.list` omits a row whose `keywords` contain `$deleted`, for `mailbox`, `search`, and `unified`. `total` counts the rows that remain. The keyword stays stored.
- `mailbox.list` recounts unread and total from local rows and skips `$deleted` (N-Q6). Unread is not seen, not draft, and not `$deleted`. Total is every message in that mailbox that is not `$deleted`. The account row sums those. A parent folder does not add its children. The stored `mailbox.unread` and `mailbox.total` stay the server numbers.

**Work items:**

- [x] Extend the N-3a table with the Lock's remaining columns and text-only formatters. The rows are the one reply N-3a loaded. Do not add pagination. The time column calls `formats.js`.
- [x] `message.list` gains view kind `unified` (`search` in Q-1) and `threaded` (flat first; threaded in N-6). The `unified` argument is the decided shape above. Kind `mailbox` and `listRows` are N-3a.
- [x] Selection, keyboard focus, and the `list` shortcut scope (`j`/`k`, `Enter`).
- [x] Live updates from `mail.messages_changed` patch rows by id.
- [x] Theme the mail list on `.pav-table` in `ui/public/styles/theme.css`, using `--pav-*` variables. New variables ship in `dark.css`, `light.css`, and both starter `*.theme` files.
- [x] Record the gzipped size of `ui/dist` in Accomplished. Gzip -9 of `ui/dist` (concatenated per-file stream) is 292633 bytes. The size is a record. It is not a reason to drop Tabulator.
- [x] A debug-only fixture generator `debug.seed_messages {mailboxId, count}` to fill that folder up to the list cap. Absent when `NDEBUG` is set. Local rows only.
- [x] `message.list` omits a row whose `keywords` contain `$deleted`, for every kind it returns. `total` counts the rows that remain. The keyword stays stored.
- [x] `mailbox.list` recounts unread and total from local rows, skipping `$deleted`. Unread also requires not seen and not draft. The account row sums those counts. A parent folder does not add its children. The stored `mailbox.unread` and `mailbox.total` stay the server numbers.

**Done means:** Clicking Inbox in the tree lists its messages, and the loaded rows scroll smoothly.

**Exit gate:** `make test` exits 0. In `make build`: real Inbox lists, and scrolling the loaded rows does not draw a DOM node per message.

**Accomplished:** `message.list` gains view kinds `unified` and `threaded` (flat first), alongside the `mailbox` and `search` kinds from N-3a and Q-1. The `unified` argument is `{view:{kind:"unified", name}}` with `name` in `inbox`, `unread`, `flagged`, `drafts`, `sent`. Unread is every unseen message; flagged is every flagged message; inbox/drafts/sent are those roles across accounts; drafts and sent show To. `threaded` returns the same flat rows as `mailbox` until N-6 groups them. `message.list` omits any row whose `keywords` contain `$deleted`, for every kind it returns, and `total` counts the rows that remain (Andrew, 2026-10-23, N-Q6). The keyword stays stored.

The reply is the newest messages by `received_at` (then `email.rowid`), up to `preferences.cache.listRows` (default 10,000, same page as the cache limits). The reply stops before the next row would push it past 2 MB, and `total` stays the real count. The first row is always kept. The Mail home renders the reply with Tabulator virtual rows, pagination off, themed on `.pav-table` in `ui/public/styles/theme.css` with `--pav-*` variables shipping in `dark.css`, `light.css`, and both starter `*.theme` files. Columns are From (To in Sent/Drafts), Subject, preview, time (via `formats.js`), flags, and an attachment mark. Cells return elements with `textContent`, never HTML strings. Selection, keyboard focus, and the `list` shortcut scope (`j`/`k`, `Enter`) work. Live `mail.messages_changed` patches rows by id in place. The empty-folder state clears the table and shows an empty selection.

`mailbox.list` recounts unread and total from local rows, skipping `$deleted`. Unread requires not seen, not draft, and not `$deleted`. The account row sums those counts. Parent folders do not add their children. The stored `mailbox.unread` and `mailbox.total` stay the server numbers. `pav_cmd_seed_messages` is registered in the dispatcher table under `#ifndef NDEBUG`, takes `{mailboxId, count}`, and inserts `min(count, listRows)` local rows into that mailbox without touching the network. The result posts `mail.messages_changed` with empty arrays. `docs/API.md` lists `message.list`, `message.search`, and `debug.seed_messages` as `State: real`.

Unity `test_message_list_folder` (tests/unity/src/core_test.c:9072) covers order, the `listRows` cap, from/subject text, an empty mailbox, another account, a mailbox id containing a colon, bad-args for missing `view.id`, `view.name`, bad `view.kind`, `offset`, `unified` with an `id`, `view.name: "later"`; unified views `inbox`/`unread`/`flagged`/`drafts`/`sent`; `threaded`; the 2 MB stop; `debug.seed_messages` insert and clamp; and `$deleted` omission from `total`. Vitest covers the two columns as text, the count label, the Limits field, virtual rendering, and the `mail.messages_changed` patch. `bypass.test.js` checks no direct stderr/console calls. `make test`: 248 Unity, 2 Rust, 395 Vitest, exit 0 (2026-10-24).

Gzip -9 of `ui/dist` (concatenated per-file stream) is 292633 bytes.

CI for the N-3 push (`28ec6a9`, run [40064822844](https://github.com/500Foods/Pavillion/actions/runs/40064822844)) is green: fedora, macos-core, windows-core. The windows-core Vancouver DST fix from D-8 holds.

**Lessons learned:**

- `tabulator-tables` 6.6.1 is pinned in `ui/package.json`, bundled by Vite, and loaded by dynamic import in `ui/src/common/table.js` so it lands in its own chunk. Virtual rendering (`height: "100%"`, `virtualDom`) keeps 100k rows smooth with pagination off. A formatter that returns a string is HTML; the cell builds an element and sets `textContent`.
- `table.getRow(id)` warns on a miss. `rowPresent(id)` checks the data first; `table.js` uses it in `reformat`, `open`, and `select` so a deleted or patched-out row does not log a warning. The accounts-table test asserts `getRow` is not called for a row that was just removed.
- `$deleted` lives in `keywords` JSON. `PAV_SQL_NOT_DELETED` (`json_type(e.keywords, '$.\"$deleted\'' IS NULL`) is in the count query, the row query, and the unified/unread/flagged queries in `message_list.c`, and in the unified count in `search.c`. `mailbox.list` in `sync.c` uses the same expression inline for both the total and the unread count.
- The nav reload that started during D-8 was replaced to fix a race: a `mailboxes_changed` event arriving while `account.list` is still in flight must not replace the full tree with one account alone. `runReload` tracks a `phase` ("idle", "accounts", "boxes") and a `pending` scope; during "accounts" a change widens the in-flight scope via `cover()`, and after "boxes" a change queues a follow-up reload. `nav.test.js` holds `account.list` and `mailbox.list` behind a promise gate to force the race. The help page adds a sentence for this: "A change for one account while that first list is still waiting on account.list stays with the full list."
- The 2 MB reply cap and the `listRows` cap interact: the 2 MB check stops before the next row, and `listRows` limits the SQL `LIMIT`. The Unity test sets `listRows` to 2 to test the `LIMIT`, and uses a 1.1 MB subject to test the 2 MB stop, asserting the row before the cap is still returned.
- `debug.seed_messages` inserts rows with `received_at` set so they sort into the list. It is clamped to `preferences.cache.listRows` on read, not just on write, so changing the cap and reseeding reflects the new limit.
- The mail list theme variables (`--pav-list-row-height`, `--pav-list-from`, etc.) ship in `dark.css`, `light.css`, and both starter `*.theme` files in the same change. `ui/styles/list.css` is not created; the list theme lives in `theme.css` under `.pav-table`.

**Handoff:** N-3 is complete (step 61, 2026-10-24). `make test`: 248 Unity, 2 Rust, 395 Vitest, exit 0. CI on `28ec6a9` is green: fedora, macos-core, windows-core. The index names Q-2 next (step 62, [Q-SEARCH.md](Q-SEARCH.md)). Q-2 was not started. Its entry gate is Q-1 and N-3, both complete. Its lock: the search field focuses `search.run`, server search posts `search.results`, and saved searches live in `preferences.ui.search.saved` as `[{id, name, query, server}]`. N-4's entry gate is N-3 complete, so it starts next if Andrew pulls it forward. `PAVILLION_API_VERSION` stays 3. The phase stamp is `0.69.0`. Do not touch the quit path. The Accounts table polish stays paused.

## N-4: Reading pane

**Kind:** Medium
**Status:** Not started.

**Goal:** Selecting a message shows it in the sandboxed pane with headers, remote-content control, inline images, and safe links.

**Entry gate:** N-3 complete.

**Work items:**

- [ ] `ui/src/mail/reader.js`: header block (text), the F-4 `htmlpane` for `htmlSanitized`, a `<pre>` (text) for plain, a toggle, an attachments strip (N-7 makes it act).
- [ ] Remote-content banner when `hasRemote`: "Load once", "Always for this sender", "Always for this domain" (`sender.allow_remote`).
- [ ] `pav-cid` entry in the C `pav_protocol` table (Rust forwards only); inline cid images render (bot reports must render their images).
- [ ] `pav-link` confirm dialog with the real URL; highlight when the display text looks like a different domain; open with the system browser through opener (`link.open`).
- [ ] Mark read after the delay.
- [ ] `message.get` gains `htmlSanitized` from N-2, cached in `body_cache` with the version.

**Done means:** A bot report with cid images renders inline images, remote images are blocked until allowed, and links confirm.

**Exit gate:** `make test` exits 0. In `make build`: open a real cid report, a newsletter (blocked, then loaded), and the hostile corpus imported into the container; the network panel of the pane shows no request until "Load once".

## N-5: Flags, move, delete, archive

**Kind:** Medium
**Status:** Not started.

**Goal:** Organizing mail works instantly with undo.

**Entry gate:** M-6 complete.

**Work items:**

- [ ] Toolbar and context menu on the list and reader: read/unread, flag, junk/not junk, move (picker over `mailbox.list`), archive, delete, delete permanently in Trash with confirm.
- [ ] Undo toast (J-1) bound to `undo.run`.
- [ ] Archive-role-missing flow: ask, then `mailbox.create` with role.
- [ ] Tree context menu actions for mailboxes: create, rename, delete with confirm, subscribe (E-3 renders the menu; this phase supplies the actions).
- [ ] Shortcuts `e`, `#`, `Del`, `m`, `s`, `*`, `Shift+U`, `Shift+I`.

**Done means:** Every action in the Lock works on one or many selected messages and can be undone for 5 s.

**Exit gate:** `make test` exits 0. Against real mail in `make build`: archive and undo, move ten, flag, delete; another client shows the same result.

## N-6: Threads, unified views, message tabs

**Kind:** Medium
**Status:** Not started.

**Goal:** Conversations, cross-account views, and opening a message in its own tab.

**Entry gate:** N-5 complete.

**Work items:**

- [ ] `thread.get`; threaded list mode (one row per thread, count badge, newest date) and the thread reader (oldest to newest, read items collapsed, `n`/`p`).
- [ ] Unified views at the top of the Mail nav: Inbox, Unread, Flagged, Drafts, Sent.
- [ ] Open in a tab (`Ctrl+Enter` on a row, double-click, context menu): a `mail:msg` sub-tab titled by subject (J-2), with its own reader.
- [ ] Session restore resolver for message tabs (J-6).

**Done means:** A thread reads as a conversation, the unified Inbox merges accounts, and a message can live in its own tab.

**Exit gate:** `make test` exits 0. In `make build`, with ~20 accounts synced: unified Inbox lists all accounts; open a thread; open a message in a tab; restart, the tab returns.

## N-7: Attachments

**Kind:** Medium
**Status:** Not started.

**Goal:** Received attachments can be previewed, saved, and opened safely.

**Entry gate:** N-6 complete.

**Work items:**

- [ ] `attachment.save {messageId, partId, destPath?}` (host save dialog when no path), `attachment.open` (copy to the runtime open dir, warn on executable types, open through opener), download as activity `attachment.download`.
- [ ] Images preview in a small viewer outside the pane (an `<img>` with a `pav-blob` URL; `img-src` gains `pav-blob:` in the F Lock CSP, and that change is made to the F Lock first).
- [ ] `message.raw` writes the `.eml` to cache and offers save.
- [ ] Streaming (M Lock): attachments and raw messages download straight to blob files, and parts are extracted with GMime streams (`GMimeStreamFs` to `GMimeStreamFs`), never a whole part in memory. Only the body text and HTML for rendering is read into memory, capped by the N Lock. Unity: `test_attachment_200mb_extract_flat_memory`.
- [ ] Unity: `test_attachment_open_copies_to_runtime`, `test_executable_warning_types`.

**Done means:** A PDF opens in the system viewer, an image previews, and an `.exe` warns first.

**Exit gate:** `make test` exits 0. In `make build`, the three cases behave as stated, and the runtime open dir is empty after quit.

## N-8: Notifications and mail shortcuts

**Kind:** Medium
**Status:** Not started.

**Goal:** New mail is announced per the Lock and every mail shortcut works.

**Entry gate:** N-7 complete. Search already exists (Q-2); `/` focuses that field. Notifications do not wait on search.

**Work items:**

- [ ] Host: `tauri-plugin-notification`; grouping, mute per account (`notify` flag on the account), `notify` per mailbox, preview setting, click to open.
- [ ] Remaining shortcuts from the table, with `?` showing a help overlay generated from the registry.
- [ ] Settings → Mail heading: mark-read delay, plain text for unknown senders, notifications.

**Done means:** A new message notifies once, five at once group into one, and each shortcut in the table works.

**Exit gate:** `make test` exits 0. In `make build`: send one and then five messages to a test account; observe one notification then one grouped; walk the shortcut table.

## N-9: Folder retention

**Kind:** Medium
**Status:** Not started.

**Goal:** A mailbox can keep only its newest N messages or the last D days.

**Entry gate:** N-8 complete.

**Work items:**

- [ ] `retention.get`, `retention.set`; the scheduler after delta sync, at most hourly per mailbox, as activity `retention`.
- [ ] Selection by `Email/query` with `position` and `before`; action through the op queue (trash or destroy).
- [ ] Folder settings page in Settings → Accounts → Mail → Folder settings, with the "enforced while Pavillion is running" label.
- [ ] Unity on fixtures: `test_retention_keeps_newest_n`, `test_retention_age`, `test_retention_once_per_hour`.

**Done means:** A willard@ report folder keeps exactly N newest (plan P3 acceptance).

**Exit gate:** `make test` exits 0. Against the container, a folder with 120 messages and N=100 ends at 100 after one sync, and the 20 oldest are in Trash.

## Open questions for Andrew

- N-Q1. Notifications: every message, Inbox only, or per-folder opt-in? Quiet hours? (plan Q5)
- N-Q2. Retention: move trimmed messages to Trash or destroy? Same rule for all willard@ folders? (plan Q7)
- N-Q3. Prefer a server-side retention in Stalwart if available (the Willard watcher spec also proposes trimming)? Never two trimmers on one folder. (plan Q8)
- N-Q4. Migration: import existing mail history (mbox/Maildir from Betterbird), or rely on the server? (plan Q6, mail part)
- N-Q5. PGP or S/MIME at any point? (plan Q10) Not in this document until answered.
- N-Q6. Folder badges and `$deleted`. Answered 2026-10-23: recount. `mailbox.list` counts unread and total from local rows and skips `$deleted`. Unread stays "not seen and not draft", which is what `unreadEmails` means, and also skips `$deleted`. Total skips `$deleted` only. The account row sums the recounted mailbox numbers. A parent folder does not add its children. The stored `mailbox.unread` and `mailbox.total` stay the server's `unreadEmails` and `totalEmails`.

## Working log

- 2026-10-16 Opened from plan §6, §7, §10, §13. Decisions: a real HTML5 parser (lexbor) for the sanitizer, locked 2026-10-16 with reasons in the Lock, the element, attribute, URL, and CSS allowlists written down, `pav-link` rewriting, `sanitizer_version`, `pav-blob` for image previews, a Tabulator theme on `--pav-*`, `Ctrl+4` for Files, retention action column, notification grouping in the host.
- 2026-10-16 N-7 extracts attachments with GMime streams from disk to disk, with a flat-memory test. Only capped render bodies are held in memory.
- 2026-10-16 Andrew restored Tabulator for the message list, and for the contacts and files lists that share it. Virtual row render stays, so 100k messages do not become 100k DOM nodes.
- 2026-10-18 Section chords are Ctrl+1 through Ctrl+7: mail, contacts, calendars, tasks, files, notes, argent. Grok has no number. The old Ctrl+4-is-files line in the 2026-10-16 log stays as history.
- 2026-10-18 Those digits are the defaults. Settings → Features assigns Ctrl+1 through Ctrl+9, or none, per button. The earlier log line stays as the default that shipped that morning.
- 2026-10-18 GitHub has no number. The chord row above is unchanged.
- 2026-10-22 N-3a in progress. `message.list` returns From and Subject for one mailbox, newest first, capped by `preferences.cache.listRows` (default 10,000, Settings → Limits). The mail list loads that one reply and does not page. `make test` is 213 Unity, 2 Rust, and 380 Vitest, exit 0. A container check on a temporary profile showed a new Inbox message after the 120 s poll. The window is not accepted.
- 2026-10-23 N-3a complete, step 135. Andrew accepted the window. The count is there and the list fills immediately. Sent, with more than 3700 messages, renders at once. `$deleted` stays in the list for N-3 and N-5. Stamp `0.64.0`. `make test`: 213 Unity, 2 Rust, 380 Vitest, exit 0. CI on `e30fdd1` is green: fedora, macos-core, windows-core. The index names N-1 next. N-1 was not started. This close-out is not in that commit.
- 2026-10-23 N-1 in progress. GMime 3 parses the corpus, `message.get` returns text, parts, and headers, and idle prefetch follows `fetchBodies`. `htmlSanitized` stays null. `make test`: 229 Unity, 2 Rust, 380 Vitest, exit 0. CI is not pushed. Stamp stays `0.64.0`.
- 2026-10-23 N-1 still in progress. macOS and Windows CI failed in `mime.c`: iconv wants `char **`, and macOS needs `-liconv`. Prefetch left `body.fetch` running because `submit_json` replaced its completion callback; the callback stays, and the next prefetch is posted to the core thread. The splash is `z-index: 100`. While it is up, the nav, the pane, and the message list do not scroll, because WebKit paints those bars over a fixed cover. The prefetch test removes its fake-keyring secret in `tearDown`. `make test`: 230 Unity, 2 Rust, 380 Vitest, exit 0. Stamp stays `0.64.0`. CI is not pushed.
- 2026-10-23 N-1 complete, step 58. GMime 3 parses the corpus. `message.get` returns text, parts, and headers. `htmlSanitized` stays null. Prefetch follows `fetchBodies`, at most two, as `body.fetch`. Stamp `0.65.0`. `make test`: 230 Unity, 2 Rust, 380 Vitest, exit 0. CI on `5342f9d` is green: fedora, macos-core, windows-core. The index names N-2 next. N-2 was not started. This close-out is not in that commit.
- 2026-10-23 Andrew committed the N-1 close-out and the splash reveal as `9b3bf9a`, and accepted the splash window. The window stays hidden until the cover is laid out. WebKit's background is `#000000`. The page does not call `show()`. Lessons are in N-1, the L-STORE startup notes, and D-UI. This is not N-2. N-2 was not started.
- 2026-10-23 N-2 in progress. The Lock records lexbor v3.0.0 (`2ae88a1c6b5261830eff73ee12bb3cdf805f3cfe`), `pane_id` and `extra_properties` on `pav_sanitize`, the CSS property list, and Settings → Mail `preferences.mail.cssExtra`. `message.get` stays null until N-4. No work item is finished.
- 2026-10-23 N-2 work items checked. `make test`: 239 Unity, 2 Rust, 384 Vitest, exit 0. libFuzzer `build/sanitize_fuzz -max_total_time=600`, seed 899505327, clang 22.1.8, default max_len 4096, 2350412 runs in 601 seconds, exit 0, no crash. Lexbor stays v3.0.0. The phase stays in progress. Accomplished is unwritten. CI has not run. Stamp stays `0.65.0`. Settings → Mail was not opened in the window.
- 2026-10-23 N-2 still in progress. CI on `75b0c5a` is green: fedora, macos-core, windows-core, run [40016164091](https://github.com/500Foods/Pavillion/actions/runs/40016164091). The run on `e9a2e28` failed because `third_party/` was ignored. Lexbor landed in `dce6854`. Accomplished is unwritten. Stamp stays `0.65.0`.
- 2026-10-23 N-2 complete, step 59. `pav_sanitize` and the hostile corpus are in the tree. Lexbor stays v3.0.0. `message.get` stays null until N-4. Stamp `0.66.0`. `make test`: 239 Unity, 2 Rust, 384 Vitest, exit 0. libFuzzer 2350412 runs in 601 seconds, exit 0. CI on `75b0c5a` is green: fedora, macos-core, windows-core. The index names Q-1 next. Q-1 was not started. This close-out is not in `b923040`. The agent does not commit.
- 2026-10-23 N-3 was not started. Andrew chose D-8 first. `unified` is five names (`inbox`, `unread`, `flagged`, `drafts`, `sent`); unread is every unseen message. `debug.seed_messages` takes `{mailboxId, count}` and is absent under `NDEBUG`. The list theme extends `.pav-table` in `ui/public/styles/theme.css`. The time column will call `formats.js`.
- 2026-10-23 D-8 complete. The index names N-3 next. N-3 was not started in that close-out. `$deleted` is still unfiltered. Ask before filtering it.
- 2026-10-23 Andrew: hide `$deleted` in the N-3 message list. `message.list` omits those rows and `total` counts what remains. The keyword stays stored. He asked whether folder badges should do the same. That is N-Q6 and is not decided. N-3 was not started.
- 2026-10-23 N-Q6 answered: recount folder badges and skip `$deleted`. Unread is not seen, not draft, and not `$deleted`. Total skips `$deleted` only. The account row sums those. Stored server counts stay for sync. N-3 was not started.
- 2026-10-24 D-8 CI on `a8903db`, run [40059550732](https://github.com/500Foods/Pavillion/actions/runs/40059550732): windows-core failed in `ui/src/formats.test.js` (`01:30 PST` vs `02:30 PDT` at `2026-11-01T09:30:00.000Z`). Fedora and macos-core passed. Fix that before N-3. N-3 was not started.
- 2026-10-24 That check now allows `01:30 PST`, `02:30 PDT`, and `02:30 MST`. CI has not re-run. N-3 was not started.
- 2026-10-24 N-3 in progress. `message.list` returns the remaining columns, `unified` (five names), and a flat `threaded` view. `$deleted` is omitted and `total` matches. `mailbox.list` recounts badges and leaves the stored server counts. `debug.seed_messages` is local and clamped to `listRows`. The mail list calls `formats.js`. j and k move the selection. Enter stays in the list. `make test`: 247 Unity, 2 Rust, 397 Vitest, exit 0. The gzip record stays open until the window is accepted. Andrew committed this as `4d8b53e`. CI on `47ee864`, run [40062205611](https://github.com/500Foods/Pavillion/actions/runs/40062205611): fedora and windows-core passed; macos-core failed the zoom-wheel test at 5000 ms. The 20000 ms timeout is in `4d8b53e`. CI for that commit, run [40064822844](https://github.com/500Foods/Pavillion/actions/runs/40064822844), was still running.
- 2026-10-24 CI for `4d8b53e`, run [40064822844](https://github.com/500Foods/Pavillion/actions/runs/40064822844), is green: fedora, macos-core, windows-core. The docs merge `69d58e9`, run [40065380950](https://github.com/500Foods/Pavillion/actions/runs/40065380950), is green on the same three jobs. Foods `sync.mail` failed `cannot store messages` / `error:server` while courses went idle. Email state stayed put, so every Update fetched the same page. Nine updated messages and three destroyed messages were `body_state` fetched with an empty full-text body. SQLite 3.51.2 turns that contentless mismatch into `SQLITE_CORRUPT_VTAB` and aborts the write. `delete_row` saves a point, rolls the bad delete back, and retries the empty body a header-only row was stored with. A copy of the store deleted all 104 of that page (13 not stored yet) and the 3 destroyed ids, then rolled back. `make test`: 248 Unity, 2 Rust, 397 Vitest, exit 0. The window was not rebuilt. Accomplished, Lessons learned, and Handoff stay unwritten. Do not start N-4.
- 2026-10-24 Foods and courses both reach idle after the full-text retry. The first window after that rebuild drew 536 mail nodes (foods only). Courses is one account plus five folders, and the next launch drew 542. `mail.mailboxes_changed` for foods arrived while the first `account.list` was in flight, and that reload was dropped, so the tree was painted from the foods list alone. A change during `account.list` now widens that load. A change after `mailbox.list` was sent waits and runs once the current load has painted. Returning to Mail logged Tabulator `Find Error` for foods Inbox message `7uaaaagv` (`body_state` none). `getRow` warns on a miss, so the table asks only when that id is in the rows it was given, and clearing the folder clears the selection first. The window is not accepted. Do not start N-4.
- 2026-10-24 Pickup. The window was not opened, so Accomplished, Lessons learned, and Handoff stay unwritten. Next sitting: pull if origin has the CLOC commit `05c23d2`, then `make dev` and accept or reject the Inbox list. `make build` must show a real Inbox whose scroll does not draw a DOM node per message. On acceptance, record gzip -9 of every file in `ui/dist` (D-8 was 360042), write the close-out, and move the stamp. Do not start N-4. `$deleted` uses `json_type(e.keywords, '$."$deleted"') IS NULL`. Badge unread is not seen, not draft, and not `$deleted`. The Unread view is every unseen message, including an unseen draft. Stored `mailbox.unread` and `mailbox.total` stay the server numbers. `mail.messages_changed` reloads the one reply and keeps the selected id. jsdom stores a hex swatch as `rgb()`. Fire `tableBuilt` on a recreated Sent table before `setData` is asserted. The full-app zoom tests need 20000 ms under 60 workers. Do not change `formatInstant` for the zone-name difference. `PAVILLION_API_VERSION` stays 3.
- 2026-10-24 N-3 complete, step 61. `message.list` adds `unified` (inbox, unread, flagged, drafts, sent) and flat `threaded`; hides `$deleted` and counts the remainder. `mailbox.list` recounts unread/total from local rows, skipping `$deleted`. `debug.seed_messages {mailboxId, count}` inserts local rows clamped to `listRows`, absent under `NDEBUG`. The list uses Tabulator virtual rows, `.pav-table` theme variables, `formats.js` for time, `rowPresent` before `getRow`, and selection/keyboard/j-k/Enter. Help pages updated. `make test`: 248 Unity, 2 Rust, 395 Vitest, exit 0. CI on `28ec6a9` is green: fedora, macos-core, windows-core. Gzip -9 of `ui/dist` is 292633 bytes. Stamp `0.69.0`. The agent does not commit this close-out. Do not start N-4.

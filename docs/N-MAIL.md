# N — MAIL

**Status:** Not started (scoped 2026-10-16 from plan §6, §7, §10, §13).
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

Reading and organizing mail: MIME, the sanitizer, the message list, the reading pane, threads, unified views, flags and moves, attachments, notifications, shortcuts, and folder retention. Sync is [M-SYNC.md](M-SYNC.md). Compose is [O-COMPOSE.md](O-COMPOSE.md). Search is [Q-SEARCH.md](Q-SEARCH.md). The mailbox tree is [E-TREE.md](E-TREE.md). The isolation rules are the [F-HARDEN.md](F-HARDEN.md) Lock and are not restated here.

## Phases

One phase is one session. Do not start the next one in the same conversation.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| N-1 | Body fetch and MIME with GMime; `message.get` text, parts, headers | Medium | Not started |
| N-2 | The HTML sanitizer in the core and the hostile corpus | Hard | Not started |
| N-3 | Message list: Tabulator, columns, selection | Medium | Not started |
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
- Vendored under `third_party/lexbor/` at a tagged release (Apache-2.0), built by CMake into the core. Not a Fedora package and not linked statically from a system copy. N-2 records the tag.
- Allowlist of elements for email layout: `a abbr b blockquote br caption center code col colgroup dd del div dl dt em font h1-h6 hr i img ins li ol p pre q s small span strike strong sub sup table tbody td tfoot th thead tr tt u ul` plus `html head body style title` handled specially. Everything else is dropped. `script iframe frame object embed applet form input button select textarea base meta link svg math template` are removed with their content. `noscript` content is kept as text. `<style>` is not dropped because of where it sat in the source: lexbor's tree construction moves style the way a browser does, and the filter runs on the tree after that. A style element that survives is parsed and kept only as the CSS rule below allows.
- Attributes: per-element allowlist (`href` on `a`; `src alt width height` on `img`; `align valign width height bgcolor background border cellpadding cellspacing colspan rowspan` on table parts and `body`; `color face size` on `font`; `dir lang title class id style` globally). `background` is a remote URL under the URL rule, the same as a remote `src`. All `on*` attributes are removed.
- URLs: `href` allows `http:`, `https:`, `mailto:`; `src` allows `cid:` and `data:image/(png|gif|jpeg|webp)`; `http:`/`https:` `src` and CSS `url()` are remote. `javascript:`, `vbscript:`, `data:` (non-image), and every other scheme are removed. Every `href` is rewritten to `pav-link://<paneId>/<n>` (the F-4 shape). The UI keeps the real URL list for that pane. The core event is `{paneId, n}`.
- CSS: `style` attributes and `<style>` blocks are parsed and filtered by property allowlist (box, color, font, text, table, list, `display` except `fixed` positioning; no `position: fixed`, no `behavior`, no `expression()`, no `@import`, no `-moz-binding`). `url()` follows the URL rule. Class and id names are prefixed `m-` so they cannot target the wrapper.
- Remote content (images, CSS `url()`, `background`) is replaced with a 1×1 transparent `data:` placeholder and recorded (`hasRemote`) unless the sender address or domain is in `remote_allow` or the user chose "Load remote content" for this message. Loading remote content re-sanitizes with remote allowed and the pane's CSP adds `img-src https:` for that render only.
- `cid:` becomes `pav-cid://<emailRowid>/<cid>`; the C `pav_protocol` handler (G-8) serves the blob only if that cid belongs to that email. Rust only forwards the request.
- Output carries `sanitizer_version`; a version bump re-sanitizes cached bodies lazily.
- Plain-text view toggle always available. Setting `preferences.mail.plainForUnknown` (default false) shows plain text first for senders not in contacts or `remote_allow`.
- Never auto-open attachments; warn on executable types (`application/x-executable`, `.exe .bat .cmd .com .scr .js .jar .msi .sh .desktop .appimage`).

### Message list (was plan §6.1, §10)

- Tabulator (`tabulator-tables`), themed on `--pav-*`, with virtual row render so 100k messages stay inside the frame budget (60 fps scroll at 100k). The page does not keep a copy of the mailbox. Remote pagination over `message.list` in pages of 100 feeds the table as the user scrolls. Contacts (S) and files (U) use the same module (`ui/src/common/table.js`). No second table library.
- Shared table component (Andrew, 2026-10-19, pulled ahead of N-3): `tabulator-tables` 6.6.1, pinned exactly in `ui/package.json`, MIT licence, bundled by Vite from `node_modules` (no CDN, no Tabulator stylesheet). `ui/src/common/table.js` is the only module that imports it, by dynamic import, so Vite puts it in its own chunk loaded on first use; it registers only the Format and Interaction modules. Screens call `createTable(el, {columns, data, rowId, onRowClick, rowClass, groupBy, groupHeader, footer})`. `groupBy` (a field or function) turns on Tabulator row groups (the GroupRows module is registered too); `groupHeader(value, count, rows)` returns a DOM node; `footer(data)` returns the footer text, kept current by `setData` (Andrew, 2026-10-19, K-Q11). Shared tables fit their panel by default (Andrew, 2026-10-19): layout `fitColumns`, the main text column takes the rest (`widthGrow`), icon and checkbox columns keep fixed narrow widths, and a `ResizeObserver` on the table element calls `redraw()` when the panel or a divider changes its size. Its look comes only from `--pav-*` variables in `ui/public/styles/theme.css` (`.pav-table`). Cells are text or built DOM with `textContent`; no HTML formatter takes outside data. The first user is the Settings → Accounts list (K-Q8); N-3 extends the same module for the message list (virtual rows, selection).
- Row: account color bar, from (or to in Sent and Drafts), subject, preview, 24 h time today or `Oct 14` this year or `2026-10-14` older (`preferences.time_format`), flags (unread dot, flag, answered, forwarded), attachment icon. Formatters return text, or set `textContent`. An icon is an element this module creates from a class name. A formatter that returns an HTML string is an HTML sink and is forbidden.
- Threaded or flat per view (`ui.mail.threaded[viewKey]`). Sort by date desc default.
- Mark read on open after 1 s (`preferences.mail.markReadMs`, 0 = never).

### Views (was plan §6.1, §6.3)

- Unified Inbox, Unread, Flagged, Drafts, Sent (role-based across accounts) above the per-account mailbox tree. Unread counts in the tree. Accounts collapsible; order and hiding are tree view state.
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
**Status:** Not started.

**Goal:** Opening a message fetches its body once and returns text, parts, and headers.

**Entry gate:** M-5 complete.

**Work items:**

- [ ] `core/src/mime.c` on GMime 3 (`gmime-devel`; add to A's Fedora list and CMake): parse a blob, walk parts, pick text and HTML alternatives, list attachments with cids, decode charsets.
- [ ] Body fetch through the backend's `fetch_body` with the Lock's `Email/get` properties; write `body_cache` (text, raw HTML path) and `attachment` rows; `body_state`.
- [ ] Idle prefetch per `fetch_bodies` with at most 2 in flight, as activity `body.fetch`.
- [ ] `message.get` returns the Lock's shape with `htmlSanitized: null` until N-2.
- [ ] Corpus `tests/corpus/`: plain, multipart/alternative, HTML newsletter, cid-image signature (copy the pattern from Andrew's bot reports), huge attachment (generated), malformed MIME, ISO-8859-1, Windows-1252, Shift_JIS, missing charset.
- [ ] Unity: one test per corpus file asserting text, part count, cids, and charset.

**Done means:** Every corpus message parses to the expected text and parts.

**Exit gate:** `make test` exits 0 with the corpus tests.

## N-2: The HTML sanitizer

**Kind:** Hard
**Status:** Not started.

**Goal:** The core turns any HTML into a safe document per the Lock, proven against a hostile corpus.

**Entry gate:** N-1 complete.

**Work items:**

- [ ] Link lexbor (Lock): use the Fedora package if it's current, else vendor a tagged release under `third_party/lexbor`. Record the version.
- [ ] `core/src/sanitize.c`: `pav_sanitize(html, opts{allow_remote, email_rowid}, out_html, out_links, out_has_remote)` per the Lock, including the CSS filter (`core/src/css_filter.c`).
- [ ] `tests/corpus/hostile/`: script, `onerror`, `onload` on body, `<svg><script>`, `<math>` tricks, `javascript:` and entity-encoded `javascript:` links, `<base href>`, `<meta http-equiv=refresh>`, `<form>`, `<iframe srcdoc>`, CSS `url()` tracking, `@import`, `expression()`, `position:fixed` overlays, look-alike link text, `data:text/html`, mutation-XSS samples (noscript, template, nested comments), unclosed tags, 10 MB HTML.
- [ ] Unity: each hostile file's output contains no element, attribute, scheme, or property outside the allowlist (a checker walks the output tree); remote counts are right; link list matches; a benign newsletter keeps its table layout.
- [ ] A fuzz target `tests/fuzz/sanitize_fuzz.c` for libFuzzer (run manually, documented in W).

**Done means:** The hostile corpus is inert by the checker, and the newsletter still looks like itself.

**Exit gate:** `make test` exits 0 with the sanitizer suite. A 10-minute fuzz run finds no crash (logged in Accomplished).

## N-3: Message list

**Kind:** Medium
**Status:** Not started.

**Goal:** The Mail home tab shows the selected mailbox's messages with smooth scrolling at 100k rows.

**Entry gate:** N-2 complete.

**Work items:**

- [ ] Add `tabulator-tables`. `ui/src/common/table.js` and `ui/src/mail/list.js` with the Lock's columns, text-only formatters, virtual row render, and remote pagination over `message.list`.
- [ ] `message.list` in the core with view kinds `mailbox` and `unified` (`search` in Q-1), offset, limit, and `threaded` (flat first; threaded in N-6).
- [ ] Selection, keyboard focus, and the `list` shortcut scope (`j`/`k`, `Enter`).
- [ ] Live updates from `mail.messages_changed` patch rows by id.
- [ ] `ui/styles/list.css` themes Tabulator on `--pav-*` variables.
- [ ] Record the gzipped size of `ui/dist` in Accomplished. The size is a record. It is not a reason to drop Tabulator.
- [ ] A debug-only fixture generator `debug.seed_messages {count}` to test 100k rows.

**Done means:** Clicking Inbox in the tree lists its messages; 100k seeded rows scroll smoothly.

**Exit gate:** `make test` exits 0. In `make build`: real Inbox lists; with 100k seeded rows, scroll from top to bottom without visible stalls (record a devtools frame rate in Accomplished).

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

## Working log

- 2026-10-16 Opened from plan §6, §7, §10, §13. Decisions: a real HTML5 parser (lexbor) for the sanitizer, locked 2026-10-16 with reasons in the Lock, the element, attribute, URL, and CSS allowlists written down, `pav-link` rewriting, `sanitizer_version`, `pav-blob` for image previews, a Tabulator theme on `--pav-*`, `Ctrl+4` for Files, retention action column, notification grouping in the host.
- 2026-10-16 N-7 extracts attachments with GMime streams from disk to disk, with a flat-memory test. Only capped render bodies are held in memory.
- 2026-10-16 Andrew restored Tabulator for the message list, and for the contacts and files lists that share it. Virtual row render stays, so 100k messages do not become 100k DOM nodes.
- 2026-10-18 Section chords are Ctrl+1 through Ctrl+7: mail, contacts, calendars, tasks, files, notes, argent. Grok has no number. The old Ctrl+4-is-files line in the 2026-10-16 log stays as history.
- 2026-10-18 Those digits are the defaults. Settings → Features assigns Ctrl+1 through Ctrl+9, or none, per button. The earlier log line stays as the default that shipped that morning.
- 2026-10-18 GitHub has no number. The chord row above is unchanged.

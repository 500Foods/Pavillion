# O — COMPOSE

**Status:** Not started (scoped 2026-10-16 from plan §5.8, §6.7–§6.11, §10, §12).
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

Drafts, the composer, identities, quoting, attachments, signatures, the outbox and sending, and `mailto:`. Nothing sends without an explicit Send by the user (or a CLI `send` with the `send` scope, [V-CLI.md](V-CLI.md)).

## Phases

One phase is one session. Do not start the next one in the same conversation.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| O-1 | Draft model: `draft.new`, local autosave, server save to Drafts | Medium | Not started |
| O-2 | Composer frame: SunEditor isolated in a sandboxed page, plain-text mode | Hard | Not started |
| O-3 | Identities, reply/reply-all/forward, quoting, address autocomplete | Medium | Not started |
| O-4 | Attachments, inline images, HTML signatures with `cid:` images | Medium | Not started |
| O-5 | Outbox and sending: MIME build, upload, `EmailSubmission/set`, undo send, retry | Hard | Not started |
| O-6 | `mailto:` handler and single instance | Medium | Not started |

Kind: **Easy** is one module and a test. **Medium** crosses one boundary. **Hard** is a security boundary or the first irreversible network action.

## How a session works

1. Read Resume in [PAVILLION_PLAN.md](../PAVILLION_PLAN.md), the Status column above, and the previous phase's Status, Accomplished, Lessons learned, and Handoff.
2. Re-read only this phase: Goal, Entry gate, Work items, Done means, Exit gate. Open the lock section this phase cites.
3. Ask the qualifying questions. Wait for approval before editing source.
4. Do this phase only. Check a work item `[x]` only after its check has passed. Defer with `[~]`, one line, and the phase it moves to.
5. Before marking complete, write Status, Accomplished, Lessons learned, and Handoff, update the Status column here and in the index, and append the Working log. Then stop.
6. A sitting that ends early sets Status to **in progress**, names the last finished work item, and leaves the rest unchecked.

Leave Accomplished, Lessons learned, and Handoff off a phase that has not started. Send tests go to the Stalwart container or to Andrew's dedicated test mailbox only (`PAVILLION_IT=1`), never to real recipients.

## Lock

### Drafts

- A draft is `{ id, accountId, identityId, mode: "new"|"reply"|"reply_all"|"forward"|"forward_attach", sourceId?, to[], cc[], bcc[], subject, html?, text, attachments[], inReplyTo?, references[], updatedMs }`.
- Autosave locally (`draft_local`) every 5 s while changed and on blur; save to the server Drafts mailbox every 30 s and on close (`Email/set` create with `$draft` and `$seen`, destroying the previous server draft id, which is how JMAP "replaces" a draft). A draft survives an app kill.
- Closing a composer tab with unsaved changes saves first; there is no "discard changes?" prompt unless the save fails. Discard is an explicit button with undo.

### Composer isolation

- The composer edits quoted outside HTML, so it may not run in the main DOM (F Lock). SunEditor runs in its own page, `ui/compose.html` (a second Vite entry), loaded in `<iframe sandbox="allow-scripts">` (no `allow-same-origin`, so it has an opaque origin, no storage, and no IPC). It talks to the parent only by `postMessage` with a fixed message schema (`init`, `content`, `command`, `ready`, `dirty`); the parent checks `event.source` is that frame and validates every message.
- HTML going into the composer (quotes, signatures) passes the core sanitizer first. HTML coming out passes it again before it is saved or sent.
- If O-2 proves SunEditor cannot run under these constraints in WebKitGTK, the fallback is a plain-text composer plus the same frame running a minimal `contenteditable` editor (bold, italic, link, list, quote) as plan §6.7 first described. The choice is recorded in O-2's Lessons.
- Plain text or HTML per identity default (`preferences.mail.composeFormat`, default HTML); text alternative generated from HTML on send.

### Identities and quoting

- Identity choice: the address the original was sent to, else the mailbox's account default, else `preferences.default_identity`. A picker shows all identities of all accounts, grouped by account number.
- Reply quotes with the attribution line `On 2026-10-14 17:04, Name <addr> wrote:` in 24 h local time. Reply-all removes the user's own identities. Forward inline quotes with the header block; forward as attachment attaches the raw `.eml`. `In-Reply-To` and `References` are set; the subject gets one `Re:` or `Fwd:`.
- Autocomplete: contacts plus `recent_address` (filled from sent mail), ranked by frequency × recency, showing the account color ([S-CONTACTS.md](S-CONTACTS.md) S-4 adds contacts; O-3 starts with recents).

### Attachments and signatures

- Drag and drop or picker; files are copied to the draft's blob area. Warn over 20 MB total; refuse over the server's `maxSizeUpload`.
- Inline images get a `Content-ID` and `cid:` references and are sent as `multipart/related`.
- Signatures per identity: HTML and text alternatives, inline images stored locally and attached as `multipart/related` with `cid:` on send. Andrew's bots use cid images, so a signature with cid images must round-trip exactly. A signature is HTML from the user, so it is sanitized like mail and edited in the composer frame. JMAP `Identity` `htmlSignature` and `textSignature` are synced when the server has them; local wins when both exist and is pushed with `Identity/set`.

### Sending

- Outbox worker: build MIME with GMime (`core/src/mime_build.c`) by writing a `GMimeStreamFs` to a temp file, so attachments are streamed in from disk and never held whole. Upload that file to `uploadUrl` with the M streaming rule, `Email/import` into Drafts, then `EmailSubmission/set` with `onSuccessUpdateEmail` moving it to Sent and removing `$draft`. Status through `outbox.update`. A failed send stays in the outbox with a visible error and a Retry; nothing is dropped silently.
- Undo send: `preferences.mail.sendDelaySeconds` (default 10; 0 = off). The outbox item waits that long with an Undo toast before upload.
- Send later: `sendAt`. If the server advertises `FUTURERELEASE` in submission capabilities, use `HOLDUNTIL`; else the outbox holds it locally and the UI says "sends while Pavillion is running".
- Every sent message records `outbox.created_by` (`user` or a bot key name) and sets `local_marks.bot` for bot sends (V Lock).

### `mailto:` and single instance (was plan §12)

- `tauri-plugin-deep-link` registers `mailto:`; the desktop file declares `MimeType=x-scheme-handler/mailto;`; document `xdg-mime default com.500nodes.Pavillion.desktop x-scheme-handler/mailto`. `tauri-plugin-single-instance` forwards a `mailto:` from a second launch to the running app, which emits `app.mailto` and opens a composer. RFC 6068 parsing (to, cc, bcc, subject, body; never attachments).

## O-1: Draft model

**Kind:** Medium
**Status:** Not started.

**Goal:** Drafts exist, autosave locally, and appear in the server's Drafts mailbox.

**Entry gate:** M-7 complete.

**Work items:**

- [ ] `core/src/draft.c`: `draft.new`, `draft.save`, `draft.discard`, `draft.get`; `draft_local` rows; server save on the 30 s timer and on close through the M-6 op queue. This phase registers op kinds `draft.save` (`Email/set` create plus destroy of the previous draft version) and `draft.destroy` with `pav_op_register`.
- [ ] Open a draft from Drafts reopens it in a composer tab (plain `<textarea>` placeholder until O-2).
- [ ] Unity: `test_draft_new_reply_sets_headers`, `test_draft_local_autosave`, `test_draft_server_replace_destroys_old`, `test_draft_survives_restart`.

**Done means:** A draft typed in the placeholder survives `kill -9` and shows in another client's Drafts.

**Exit gate:** `make test` exits 0. In `make dev`, type a draft, `kill -9`, relaunch, it is there; the container's Drafts has it.

## O-2: Composer frame

**Kind:** Hard
**Status:** Not started.

**Goal:** A rich composer that cannot reach the app even if quoted HTML were hostile.

**Entry gate:** O-1 complete.

**Work items:**

- [ ] `ui/compose.html` and `ui/src/compose/frame.js` (SunEditor; add `suneditor` to `ui/package.json`), the message schema, and `ui/src/compose/host.js` in the parent.
- [ ] Verify in `make build`: the frame's origin is opaque, `__TAURI__` and `__TAURI_INTERNALS__` are undefined, `parent.document` throws, and the main CSP still lets SunEditor's own script load (it is a bundled file, not inline). If it cannot, take the Lock's fallback.
- [ ] Toolbar: bold, italic, underline, link, lists, quote, font size, color, remove formatting, image (O-4), source view off.
- [ ] Plain-text mode with a toggle (HTML to text conversion in the core).
- [ ] `Ctrl+S` saves, `Ctrl+Enter` sends (O-5; until then disabled).
- [ ] Vitest: message schema validation rejects unknown types and wrong sources.

**Done means:** A composer tab edits rich text, saves drafts, and passes the isolation checks.

**Exit gate:** `make test` exits 0. In `make build`, quote the hostile corpus message in a reply; nothing executes, the checks hold, the draft saves.

## O-3: Identities, reply, forward, autocomplete

**Kind:** Medium
**Status:** Not started.

**Goal:** Replies and forwards are correct and come from the right identity.

**Entry gate:** O-2 complete.

**Work items:**

- [ ] Identity selection per the Lock, with the picker; Unity `test_identity_choice_order` over fixtures.
- [ ] Reply, reply-all, forward inline, forward as attachment; attribution in 24 h; headers per the Lock. Unity: `test_reply_all_removes_self`, `test_subject_prefix_once`, `test_attribution_24h_vancouver`.
- [ ] `recent_address` filled from sent mail and from M-4 Sent sync; `contact.autocomplete` returns recents (contacts added in S-4).
- [ ] Recipient chips: text only, paste of many addresses, validation.

**Done means:** A reply-all to a message sent to an alias comes from that alias and excludes the user.

**Exit gate:** `make test` exits 0. In `make build`, reply to messages received on three identities; the From is right each time.

## O-4: Attachments, inline images, signatures

**Kind:** Medium
**Status:** Not started.

**Goal:** Files and inline images attach correctly, and cid signatures round-trip.

**Entry gate:** O-3 complete.

**Work items:**

- [ ] `draft.attach {draftId, path, inline}`, drag and drop into the composer (host file drop routed to the composer tab), picker, removal, size warnings.
- [ ] Inline images in the editor as `pav-blob` previews mapped to `cid:` on save.
- [ ] `signature.list`, `signature.set`; Settings → Accounts → Mail → Signature page using the composer frame; insertion on new, reply, and forward per identity; `Identity/set` sync.
- [ ] Unity: `test_build_multipart_related_with_cid`, `test_signature_cid_roundtrip`.

**Done means:** A signature with a cid logo appears intact at the recipient.

**Exit gate:** `make test` exits 0. Inspect the built MIME of a message with the signature: the logo is a `multipart/related` part whose `Content-ID` matches the `cid:` in the HTML. (The real send-to-container check is part of O-5's exit gate.)

## O-5: Outbox and sending

**Kind:** Hard
**Status:** Not started.

**Goal:** Messages send reliably from any identity, with undo, retry, and visible failure.

**Entry gate:** O-4 complete.

**Work items:**

- [ ] `core/src/mime_build.c` (GMime), `core/src/outbox.c`: the Lock's pipeline, `draft.send {draftId, sendAt?}` (enqueues a final `draft.save` op, then hands the draft to the outbox; the outbox is a separate durable pipeline, not an op kind), `outbox.list`, `outbox.retry`, events `outbox.update`, activity `send`.
- [ ] Undo send delay and toast; send later with `FUTURERELEASE` or local hold.
- [ ] Failure states shown in the composer tab and in an Outbox view in the Mail nav.
- [ ] Wire H-3's "Email this log" to create a draft with the log attached.
- [ ] Unity on fixtures: `test_outbox_pipeline_calls_in_order`, `test_outbox_failure_kept_and_retryable`, `test_undo_send_cancels`, `test_send_later_uses_holduntil`, `test_outbox_100mb_attachment_flat_memory` (the MIME build and upload of a 100 MB attachment keep peak RSS growth under 8 MB).

**Done means:** Send from three identities on two tenants; recipients see the correct From, threading headers, and the cid signature; a failed send is visible and retryable (plan P2 acceptance).

**Exit gate:** `make test` exits 0. `PAVILLION_IT=1` send-to-self from three identities succeeds, and the message with the cid signature shows the logo inline (carried over from O-4); with the fixture forcing a 500 on submission, the outbox shows the failure and Retry succeeds after the fixture recovers.

## O-6: `mailto:` and single instance

**Kind:** Medium
**Status:** Not started.

**Goal:** Clicking a `mailto:` link anywhere on the desktop opens a composer in the running app.

**Entry gate:** O-5 complete.

**Work items:**

- [ ] `tauri-plugin-single-instance` and `tauri-plugin-deep-link`; RFC 6068 parser in the core (`mailto.parse`) with Unity tests; `app.mailto` event opens a composer.
- [ ] The desktop file entry and the `xdg-mime` note in [W-SHIP.md](W-SHIP.md).
- [ ] A second plain launch focuses the existing window instead of starting a second core (the CLI never reaches this path; V Lock).

**Done means:** `xdg-open 'mailto:someone@example.com?subject=Hi'` opens a composer with those fields.

**Exit gate:** `make test` exits 0. With the built binary registered, the `xdg-open` call opens the composer in the running app, and a second launch focuses the window.

## Open questions for Andrew

- O-Q1. Undo-send delay: default 10 seconds, or off?
- O-Q2. Default compose format: HTML or plain text?

## Working log

- 2026-10-16 Opened from plan §5.8, §6.7–§6.11, §10, §12. Decisions: the composer runs in a sandboxed `allow-scripts` page (resolving SunEditor against the F Lock), sanitize in and out, undo send and send later, draft replace by destroy-and-create, signatures sanitized and synced with `Identity/set`, `mailto:` parsed in the core.
- 2026-10-16 The outbox builds MIME to a file with GMime streams and uploads it with curl's read callback, with a flat-memory test.

# Y — NOTES

**Status:** Not started (scoped 2026-10-18). The icon and the empty home are J-9. This file is the notes store and the screen.
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

Notes are a notebook of pages, with tags, closer to Evernote than to a second mailbox. There is no Stalwart note object. A note is a file. Search is a local index, because Stalwart's search store covers messages, attachments, contacts, and events, and not file bodies.

## Phases

One phase is one session. Do not start the next one in the same conversation. The index is the order. Y-1 and Y-2 sit after U-2 and before milestone 4. Y-3 sits after V-4.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| Y-1 | Sync `.pavillion/notes` and build the local index | Medium | Not started |
| Y-2 | Notes screen, markdown editor, attachments, loose PDF, Grok context type `note` | Medium | Not started |
| Y-3 | CLI note commands | Medium | Not started |

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

- **Where the bytes live.** One account, not a tree per login. The account is `preferences.ui.notes.accountId`. Y-Q1 proposes the default: the first JMAP account that has file storage, changeable from the Notes screen, confirmed at the start of Y-1. Until an account is chosen, the section explains that in one sentence and stays empty. Notes follow that account's files sync. Y does not add a `sync_notes` flag.
- **Protocol.** The U rule: JMAP `FileNode` when that account advertises it, otherwise WebDAV through `dav.c`. U-Q1 (which store Andrew uses, and on which accounts) is answered in U-1. Y-1 uses that answer. It does not ask it again.
- **Path.** `.pavillion/notes/<notebook>/<note-uuid>/note.md`, plus `attachments/` beside `note.md`. The notebook name is one path segment. The note id is a UUID. YAML front matter holds `title`, `tags`, `created`, and `updated`. The body is Markdown. Exactly one notebook. Many tags.
- **A loose file.** A PDF or other file dropped on a notebook, with no `note.md`, is a row in the list: name, preview, no body. It is not given a fake note. Preview follows the U rules (the F-4 pane for HTML, the system viewer for PDF). PDF text extraction is out. SunEditor HTML is out. The editor is CodeMirror for the Markdown source, and the preview is the F-4 sandbox.
- **Files hides `.pavillion`.** The top-level `.pavillion` folder is not a row in the Files tree. U owns that hide. Notes is the screen for what lives under it. Ceph stays the large document store. Pavillion does not mount Ceph. The server's default max file size is 25 MB. A larger file is the server's refusal, shown as an error, not a second store.
- **Index.** The file is the source of truth. L-3's `note` row stores notebook, title, tags, path, etag, and updated. It does not store the body as a SQL text column. `note_file` stores attachment name, mime, size, and etag. `note_fts` is contentless FTS5 (`content=''`), the same shape as `contact_fts`, over title, body, tags, and attachment names. The body tokens are read from the cached file at index time. Rebuild walks those files.
- **Conflicts.** An etag mismatch uses U-2's prompt: keep both, replace, or skip.
- **Accounts panel.** Y-2 adds the Notes heading for the chosen account. K-3 does not.
- **Grok.** Y-2 registers context type `note` on the open note. No open note, no context. Y-3 adds the CLI. It is not part of V-4.
- **Icon.** Sharp Duotone Solid `fa-notebook`, theme key `--pav-icon-notes`. J-9 places the button.

## Y-1: Sync and index

**Kind:** Medium
**Status:** Not started.

**Goal:** The chosen account's `.pavillion/notes` tree is cached and searchable on this machine.

**Entry gate:** U-2 complete. L-3 has already created the note tables. Confirm Y-Q1 before editing source.

**Work items:**

- [ ] Resolve the notes account (Y-Q1). Store it at `preferences.ui.notes.accountId`. The page clamps a missing id to empty, which means none chosen.
- [ ] List and fetch `.pavillion/notes` through the U files backend. Parse front matter into `note`. Index title, body, tags, and attachment names into `note_fts` from the cached file.
- [ ] A loose file in a notebook becomes `note_file` without a body.
- [ ] Unity: `test_note_front_matter`, `test_note_fts_body_not_in_sql`, `test_loose_pdf_row`.

**Done means:** A note file on the server appears in `note`, and a search hits its body without a body column on `note`.

**Exit gate:** `make test` exits 0.

## Y-2: Screen

**Kind:** Medium
**Status:** Not started.

**Goal:** Andrew can browse notebooks, edit a note, and find one.

**Entry gate:** Y-1 complete.

**Work items:**

- [ ] Nav: notebooks on the chosen account. Home: the notes in the selected notebook, or all notes when none is selected. Empty state until an account is chosen, one sentence.
- [ ] Editor: CodeMirror Markdown, preview in the F-4 pane. Tags. Attachments beside the note. Drop a PDF on a notebook and it lists with a preview and no body.
- [ ] Search uses `note_fts`.
- [ ] The Grok composer from V-8 reports the open note's id (`context.type` `note`). No open note, no context.
- [ ] Settings → Accounts gains a Notes heading for the chosen account.
- [ ] Help: replace the Notes stub with the path, the one notebook, and the search.

**Done means:** A note can be written, tagged, and found, and a dropped PDF shows as a row.

**Exit gate:** `make test` exits 0. In `make build` the editor and the search work against the chosen account.

## Y-3: CLI

**Kind:** Medium
**Status:** Not started.

**Goal:** `pavillion note` can list and change notes through the running app.

**Entry gate:** V-4 complete, and Y-2 complete. V-4 is not extended to cover this.

**Work items:**

- [ ] `note list`, `note get`, `note set` (title, tags, body, notebook). JSON matches the file. No prompt.
- [ ] A bot key needs a scope named in the V Lock before it can mutate. Record the scope name in the V Lock in this phase.
- [ ] Fixture: list, set, get, and a search hit.

**Done means:** `pavillion note list --json` prints the notes in the chosen account.

**Exit gate:** `make test` exits 0.

## Open questions for Andrew

- Y-Q1. Which account is the notes home? Proposed default, confirm at Y-1: the first JMAP account with file storage, stored in `preferences.ui.notes.accountId`, changeable. U-Q1 still decides which file protocol that account uses.

## Working log

- 2026-10-18 Opened. Notes are files under `.pavillion/notes` on one account, plus a local contentless FTS index. Stalwart FTS is not the note search. Markdown, not SunEditor. A loose PDF is a row. The icon is J-9. Y-3 follows V-4.

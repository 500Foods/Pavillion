# U — FILES

**Status:** Not started (scoped 2026-10-16; the old plan named Files in the shell but never specified the protocol).
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

The Files section at an initial level: browse an account's file store, preview, download, upload, make folders, move, and delete. The folder tree is [E-TREE.md](E-TREE.md).

## Phases

One phase is one session. Do not start the next one in the same conversation.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| U-1 | Choose the protocol on Stalwart; sync and browse read-only | Medium | Not started |
| U-2 | Preview, download, upload, new folder, rename, move, delete | Medium | Not started |

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

- **Decision rule (protocol choice stays deferred to U-1):** per account, at runtime, use JMAP `FileNode` if the Session advertises the file storage capability. Otherwise use WebDAV on that account's host, through S-5's shared `dav.c` client. U-1 confirms which one Andrew's Stalwart offers but doesn't change this rule.

- Protocol: JMAP `FileNode` (the JMAP file storage draft) when the server advertises it; otherwise WebDAV on the same Stalwart host (`PROPFIND`, `GET`, `PUT`, `MKCOL`, `MOVE`, `DELETE` through libcurl on the net thread), with the WebDAV URL as an account field `files_url`. U-1 decides which with the real server. Either way it sits behind the backend table as `files_*` functions, and the UI does not know which.
- WebDAV here shares `dav.c` with CardDAV (S-5) and CalDAV (T-7).
- `file_node` (L-3) caches the tree: names, sizes, types, modified time, etag. Contents are fetched on demand into blobs and evicted by the L cache policy.
- Screen (old plan §10): the nav is the folder tree; the home tab is the folder contents (Tabulator: name, size, modified in 24 h, type) beside a large preview. An IMAP account shows Files with an empty tree.
- Preview: images through `pav-blob`; PDF and other types open with the system viewer (copied to the runtime open dir); text files up to 1 MB as text; HTML only in the F-4 pane. Never execute anything.
- Uploads and downloads are activities with progress and cancel ([I-ACTIVITY.md](I-ACTIVITY.md)). They stream to and from disk under the M Lock's streaming rule (curl callbacks, nothing over 1 MB in memory), so a multi-gigabyte file costs no more memory than a small one. Files dropped on the Files tab or the tab strip upload to the current folder.
- Conflicts: an upload over a changed etag asks keep both (rename with ` (1)`), replace, or skip.

## U-1: Protocol and browse

**Kind:** Medium
**Status:** Not started.

**Goal:** Andrew's Stalwart file store is browsable in the Files tab.

**Entry gate:** T-7 complete.

**Work items:**

- [ ] Check Andrew's Stalwart and the container for `FileNode` and for WebDAV; record the choice and version in Accomplished.
- [ ] `core/src/files.c` with the chosen backend: list, stat, changes (FileNode `/changes`, or WebDAV etag comparison on visit); `files.list {accountId, parentId}`, `files.get`; `files.changed`; activity `sync.files`.
- [ ] Files nav tree (E-7 engine) and the contents list.
- [ ] Unity on fixtures: `test_files_list_root`, `test_files_change_detected`.

**Done means:** Folders and files browse with sizes and dates.

**Exit gate:** `make test` exits 0; in `make build` the real file store browses.

## U-2: Transfer and manage

**Kind:** Medium
**Status:** Not started.

**Goal:** Files can be previewed, downloaded, uploaded, and organized.

**Entry gate:** U-1 complete.

**Work items:**

- [ ] `files.download`, `files.upload` (streamed from disk, not through JSON), `files.mkdir`, `files.move`, `files.delete` with confirm; the conflict prompt.
- [ ] Preview pane per the Lock.
- [ ] Drop to upload; the tab-strip drop routing for "anything else" now opens a preview tab (J-4 Handoff).
- [ ] Unity: `test_upload_conflict_keep_both`, `test_download_cancel`.

**Done means:** Upload several files, preview an image, download a PDF, delete with confirm.

**Exit gate:** `make test` exits 0; the four actions work against the real store.

## Open questions for Andrew

- U-Q1. What file storage does Andrew use on Stalwart today (WebDAV file storage, JMAP FileNode, something else), and which accounts have it?

## Working log

- 2026-10-16 Opened. The old plan put Files in the shell, the C module list, and the tree, but never named a protocol. Decisions: FileNode first, WebDAV fallback, behind the backend table; a cached tree; previews never execute.
- 2026-10-16 U-1's entry gate is now T-7, after the CalDAV adapter was added.
- 2026-10-16 Review: the FileNode-or-WebDAV decision rule is now an explicit Lock line, and WebDAV reuses `dav.c`.
- 2026-10-16 Files transfers stream under the M Lock. Removed a stale line that kept CalDAV and CardDAV out of the plan.

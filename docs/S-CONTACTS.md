# S — CONTACTS

**Status:** Not started (scoped 2026-10-16 from old plan §8, §10).
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

Address books and contacts over JMAP for Contacts (RFC 9610, `urn:ietf:params:jmap:contacts`), replacing CardBook. The sync pattern is [M-SYNC.md](M-SYNC.md); the tables are L-3 in [L-STORE.md](L-STORE.md).

## Phases

One phase is one session. Do not start the next one in the same conversation.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| S-1 | Verify Stalwart's JMAP Contacts; sync AddressBook and ContactCard | Medium | Not started |
| S-2 | Contacts screen: rolodex nav, list, contact view | Medium | Not started |
| S-3 | Contact editor: create, edit, delete; address books | Medium | Not started |
| S-4 | vCard and JSContact import and export; compose autocomplete from contacts | Medium | Not started |
| S-5 | CardDAV adapter and the shared DAV client (username/password over TLS) | Hard | Not started |

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

- Requires `urn:ietf:params:jmap:contacts`. S-1 verifies that the running Stalwart advertises it and which JSContact properties round-trip. If it's absent, contacts are local-only (stored with `ra_id` `local`) until S-5's CardDAV adapter can sync them.
- The full JSContact card is stored in `contact.card_json`. The editor edits known fields and preserves unknown ones byte for byte. Known fields: name components, nicknames, emails (typed), phones, addresses, organizations and titles, notes, anniversaries (birthday), photo (blob), online services.
- Address books: list, create, rename, delete, default book per account (`AddressBook/set`). Contacts can be in several books (`addressBookIds`). A book has no trust flag. The user files people into whatever books they want, including ones they treat as trusted or not. Membership does not allow remote images.
- Remote images for a sender who is already a contact (Andrew, 2026-10-30, N Lock): `contact.allow_remote` is a local integer, default 0. It is not a JSContact field, it is not stored in `card_json`, and sync does not send it. `contact.save` leaves it unchanged. The contact view has a switch. The mail banner's Remember, added here, calls `contact.allow_remote` for every contact whose email matches the message's first From address. N-4 builds Load only.
- Search: `contact_fts` over full name, emails, org, notes; `contact.list {bookId?, query?, offset, limit}`.
- Compose autocomplete merges contacts and `recent_address`, ranked by frequency × recency, each showing the account color.
- Import: `.vcf` (vCard 3 and 4, multi-card files) and JSContact JSON; export the same. vCard mapping in `core/src/vcard.c` (libical's vCard support where it helps, else a small parser in that module). Migration: CardBook and Betterbird vCard exports import into a chosen book.
- Screen (old plan §10): the nav is a letter rolodex, a search field, then accounts (books) and recents, flat rows on the tree's renderer. The home tab is the list beside the open contact (split `ui.contacts`). Notes are text; HTML notes go through the F-4 pane.
- Photos are blobs served through `pav-blob`.
- CardDAV (S-5): a `dav` account kind (or a DAV endpoint on an existing account) syncs address books into the same tables. vCards are converted to JSContact with S-4's `vcard.c`, so screens and commands don't know which protocol an account uses.
- The shared DAV client in `core/src/dav.c` serves both CardDAV and CalDAV (T-7). Discovery tries `/.well-known/carddav`, then `current-user-principal`, then `addressbook-home-set`. Delta uses `sync-collection` (RFC 6578), falling back to ctag/ETag comparison. Writes use `If-Match` ETags.
- DAV auth is Basic with the keyring password over verified TLS (K Authentication Lock), and each DAV server gets its own net thread (M Lock).

## S-1: Verify and sync

**Kind:** Medium
**Status:** Not started.

**Goal:** Contacts from the server are in the store and stay current.

**Entry gate:** L-3 complete.

**Work items:**

- [ ] Check the capability on Andrew's Stalwart and the container; record the version and properties in Accomplished. If absent, record that and switch the phase to local-only storage.
- [ ] `AddressBook/get|changes`, `ContactCard/get|changes|query` in `core/src/contacts_sync.c` on the M engine; `addressbook.list`, `contact.list`, `contact.get`; `contacts.changed`; activity `sync.contacts`.
- [ ] Unity on fixtures: `test_contacts_initial`, `test_contacts_delta`, `test_card_unknown_fields_preserved`.

**Done means:** Contacts and books mirror the server.

**Exit gate:** `make test` exits 0; container contacts appear in the store and a change made on the server arrives within one cycle.

## S-2: Contacts screen

**Kind:** Medium
**Status:** Not started.

**Goal:** Browse and read contacts.

**Entry gate:** S-1 complete.

**Work items:**

- [ ] Rolodex letters, search, books and recents in the nav (tree renderer flat rows).
- [ ] List on the shared Tabulator (`ui/src/common/table.js`, N-3) with name, email, org; contact view (text) with photo; open a contact in a sub-tab (J-2). Cells stay text.
- [ ] Click an email to compose; click a phone to copy.
- [ ] The Grok composer from V-8 reports the open contact's id (`context.type` `contact`). No open contact, no context.

**Done means:** Andrew's contacts browse by letter and search.

**Exit gate:** `make test` exits 0; in `make build` letters, search, and the contact view work on real data.

## S-3: Contact editor

**Kind:** Medium
**Status:** Not started.

**Goal:** Create, edit, and delete contacts and books, and the server agrees.

**Entry gate:** S-2 complete.

**Work items:**

- [ ] Editor for the known fields with add and remove of typed rows; photo pick and crop to 512 px.
- [ ] Remote images switch on the contact. `contact.allow_remote {id, allow}` sets the local flag and does not write `card_json`. The mail banner offers Remember when the first From address matches a contact and that flag is off, and choosing it sets the flag on every match. No match, no Remember.
- [ ] `contact.save`, `contact.delete`, `addressbook.create|rename|delete` through the op queue (`ContactCard/set`, `AddressBook/set`).
- [ ] Unity: `test_contact_save_preserves_unknown`, `test_contact_save_conflict_reapplies`.

**Done means:** An edit on the server reflects locally and vice versa (plan P4 acceptance).

**Exit gate:** `make test` exits 0; edit both ways against the container.

## S-4: Import, export, autocomplete

**Kind:** Medium
**Status:** Not started.

**Goal:** CardBook data comes in, and compose suggests contacts.

**Entry gate:** S-3 complete.

**Work items:**

- [ ] `contact.import {bookId, path}`, `contact.export {bookId|ids, path, format}` with `core/src/vcard.c`; activity `import`/`export`.
- [ ] Autocomplete ranking merges contacts with recents.
- [ ] Unity: vCard 3 and 4 fixtures, multi-card, folded lines, encodings, photos; `test_autocomplete_ranks_frequent_first`.

**Done means:** A CardBook/Betterbird export imports, and autocomplete ranks frequent recipients first (plan P4 acceptance).

**Exit gate:** `make test` exits 0; Andrew's real export imports with a count matching the file.

## S-5: CardDAV adapter

**Kind:** Hard
**Status:** Not started.

**Goal:** A CardDAV address book syncs both ways into the same contact tables as JMAP Contacts, through a DAV client that T-7 reuses.

**Entry gate:** S-4 complete.

**Work items:**

- [ ] `core/src/dav.c`:
  - PROPFIND and REPORT over the M net layer
  - discovery as in the Lock
  - `sync-collection` with a ctag/ETag fallback
  - multistatus parsing with libxml2 (added to the dependency list in A and W)
- [ ] `core/src/carddav_sync.c`: address books, then cards. vCard to JSContact through `vcard.c`, with unknown properties preserved. PUT with `If-Match`, DELETE, and a 412 handled as a conflict (refetch, then re-apply).
- [ ] The account `kind` `dav` (B and K) holds the DAV base URL. The K-3 form enables it. Auth is `basic` through `PavAuth`.
- [ ] Fixture: Stalwart's CardDAV in the W-2 container, plus recorded multistatus responses in the fixture server.
- [ ] Unity: `test_dav_discovery_wellknown`, `test_dav_sync_collection_delta`, `test_dav_ctag_fallback`, `test_carddav_put_if_match_conflict`, `test_carddav_unknown_vcard_props_preserved`.

**Done means:** A CardDAV account's contacts show in the Contacts screen and edits round-trip, with no protocol-specific code above the store.

**Exit gate:** `make test` exits 0 with the five tests. Against the Docker Stalwart, add a card on the server and see it arrive within one cycle, then edit it in Pavillion and see it on the server.

## Open questions for Andrew

- S-Q1. Which Stalwart version runs, and does it advertise `urn:ietf:params:jmap:contacts`? (plan Q3)
- S-Q2. Is a CardBook/Betterbird vCard export available to test with? (plan Q6)

## Working log

- 2026-10-16 Opened from old plan §8 and §10. Decisions: local-only fallback, unknown-field preservation, `vcard.c`, the rolodex nav, autocomplete merge.
- 2026-10-16 Andrew: CalDAV/CardDAV come after the JMAP contacts and calendars phases. Added S-5, a CardDAV adapter with the shared `dav.c` client, Basic auth over TLS, and a per-server net thread.
- 2026-10-30 Lock only. Remote images are a local flag on the contact, default off. Remember on the mail banner is this phase, and only when the sender is already in a book. A book does not grant the flag. The flag is not synced. S-1 was not started.

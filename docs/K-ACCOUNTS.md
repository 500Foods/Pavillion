# K — ACCOUNTS

**Status:** K-1 complete (2026-10-18). K-2 and K-3 complete (2026-10-19). K-4 is not started. The index names the next phase.
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

Accounts, credentials, and identities as the user manages them: the keyring, the account commands, account numbers, and the Settings → Accounts panel. The account JSON shape is the [B-CONFIG.md](B-CONFIG.md) Lock; this document adds `number` and OIDC to it. Talking to the server is [M-SYNC.md](M-SYNC.md).

## Phases

One phase is one session. Do not start the next one in the same conversation.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| K-1 | Secrets through the platform: Stronghold, snapshot password in libsecret, fake | Medium | Complete |
| K-2 | Account model and commands: number, add, update, remove, set secret | Medium | Complete |
| K-3 | Settings → Accounts panel: add with test, credentials, edit, remove | Medium | Complete |
| K-4 | OpenID Connect sign-in (authorization code + PKCE) | Hard | Not started |

K-4 (OIDC, Keycloak-style) stays late, after the initial working copy. Every v1 account uses a username and password (K-Q2, resolved).

Kind: **Easy** is one module and a test. **Medium** crosses one boundary. **Hard** adds a browser round trip.

## How a session works

1. Read Resume in [PAVILLION_PLAN.md](../PAVILLION_PLAN.md), the Status column above, and the previous phase's Status, Accomplished, Lessons learned, and Handoff.
2. Re-read only this phase: Goal, Entry gate, Work items, Done means, Exit gate. Open the lock section this phase cites.
3. Ask the qualifying questions. Wait for approval before editing source.
4. Do this phase only. Check a work item `[x]` only after its check has passed. Defer with `[~]`, one line, and the phase it moves to.
5. Before marking complete, write Status, Accomplished, Lessons learned, and Handoff, update the Status column here and in the index, and append the Working log. Before that, update `ui/src/help/pages.js` for what this phase changed: augment, replace, fix, or extend the user page, or add a Developer section when the user page has nothing new. Then stop.
6. A sitting that ends early sets Status to **in progress**, names the last finished work item, and leaves the rest unchecked.

Leave Accomplished, Lessons learned, and Handoff off a phase that has not started.

## Lock

### Secrets

- The keyring abstraction is G-1's secrets function table (`secret_get`, `secret_set`, and the rest of `PavPlatform` in the [G-LAYERS.md](G-LAYERS.md) L1 Lock). K-1 implements that table. Nothing outside `core/platform/` calls libsecret or Stronghold.
- Account secrets live in an IOTA Stronghold snapshot, one file per profile, mode 0600, under that profile's data directory. The core links the Stronghold library. It does not use `tauri-plugin-stronghold`: L1 cannot import Tauri, and the same library later links into Hydrogen. Never in a profile, SQLite, a log, crash output, CLI output, or frontend memory beyond the add-account or credentials form, which clears its field after submit.
- The OS keyring holds only the snapshot password (K-Q4, resolved 2026-10-18). On Linux that is Secret Service through libsecret (gnome-keyring or KeePassXC): service `pavillion`, attribute `user` = `<profile id>/stronghold`, label `Pavillion snapshot`. The first unlock generates 32 random bytes, stores them, and never prompts. Later launches read that value after the session keyring unlocks and open Stronghold with it. No second prompt. The snapshot password is not an account secret and is not deleted when an account is removed.
- Snapshot key and work factor (Andrew, 2026-10-19). The 32 random bytes from the keyring are the Stronghold key as they are: no Argon2, no other KDF in Pavillion. Stronghold seals the snapshot as an age file whose only KDF is scrypt, with log2 N set by the work factor, r 8, p 1, and a fresh 16-byte random salt per write. That stretching is for weak passwords; a 256-bit random key does not need it. So Pavillion writes snapshots with work factor 0 (`try_set_encrypt_work_factor(0)` in `core/platform/stronghold/src/lib.rs`, before any commit). The key does not change, and age reads the work factor from each file's header, so a snapshot written earlier at the default (19) still opens with the same key, and the read is slow only that once. When a read finds a header work factor above 0, the library saves the snapshot again at 0, and the platform logs one `INFO` line (`snapshot re-saved with work factor 0`). There is no typed-passphrase fallback: when the keyring is locked or missing, the call returns `PAV_SECRET_LOCKED` or `PAV_SECRET_FAILED` and never asks. If a passphrase path is ever added, it keeps the default work factor on that path only.
- No caching, zeroize after use (Andrew, 2026-10-19). Every read opens the snapshot, reads the one entry, and closes it; no unlocked snapshot, snapshot key, or decrypted secret is kept between calls in any layer. Every buffer that held a secret is wiped before it is freed: the snapshot key (`explicit_bzero` in `secrets.c`), the value the library hands back (`Zeroizing` in `stronghold/src/lib.rs`, then `pav_secret_free`, which wipes), the `user:password` and Base64 buffers and the `Authorization` header (account test, `auth_basic.c`, the header line and curl list in `net.c`), a typed password in command arguments (`pav_secret_wipe_json` in the dispatcher after the command, and the shell's argument copy with `zeroize`). Memory the code does not own (the page's JavaScript strings, Tauri's IPC buffers, curl's internal request buffer, Stronghold's in-memory store while open) is outside this rule and is kept as short-lived as the call. The page never gets a saved secret back: no command returns one; the form only shows Saved in keyring. A typed password goes page → shell → core once per add, update (`secret`), or Test, and is not kept in the page after submit.
- One seam for secret reads: everything reads a secret through `pav_secret_get` (`core/src/secret.c`), which returns a buffer freed with `pav_secret_free` (wiped). Account code reaches it through `pav_account_saved_secret`; the net layer through `apply_auth_header`. The 1Password source (the "1Password" section of this file, to be built in a later K phase; no plan step yet) plugs in behind that call, not beside it.
- Stronghold client `pavillion`. Account secrets are Store entries, not vault records: a vault record cannot be read back, and Basic auth must put the password on the wire. The key is `<profile id>/<account id>`. The value is the secret. The label `Pavillion <account number> <account name>` is a second Store entry, key `<profile id>/<account id>/label`. One secret per account: the password in v1, or, from K-4, the OIDC refresh token as JSON `{"refresh":"…"}`. The 1Password service token and the cached vault list use the same snapshot, under keys `<profile id>/op-token` and `<profile id>/op-cache`, when that phase writes them.
- `secret_get`, `secret_set`, and `secret_delete` are the account-secret calls. Return 0, or `PAV_SECRET_MISSING`, `PAV_SECRET_LOCKED`, or `PAV_SECRET_FAILED`. `LOCKED` means the OS keyring is locked, so the snapshot password cannot be read. It does not mean a Stronghold record is absent.
- The core reads a secret only on the network thread when building a request, keeps it in memory for that request, and `explicit_bzero`s the buffer after. It is never put in a JSON command result.
- A locked keyring at startup is not an error loop: the account goes to `state = "locked"`, one banner says "Unlock your keyring to sync", and sync retries when the keyring signals unlock (libsecret's collection `locked` property change) or on the next manual sync.
- A profile containing `password`, `token`, `secret`, or `refresh_token` is still rejected (B Lock).

### Accounts

- Account object (B Lock) gains `number` (string, three digits zero-padded, `"001"`). Numbers are assigned at add time as one more than the highest ever used, stored in the index as `next_account_number` so they are never reused, and never renumbered. The number appears in the mail tree, the CLI's `--account`, and the audit log.
- `auth` is the auth-method field on every account (B Lock): `basic` in v1, `oidc` from K-4, `bearer` reserved. `oidc` adds `oidc_issuer` and `oidc_client_id`, and its tokens go in the keyring.
- First accounts (K-Q1, resolved 2026-10-19): the K-3 exit gate and the M-3 entry gate use the W-2 Stalwart container (`tests/stalwart/`, `make it-up` and `make it-seed`), not a live server. Tenant A is `andrew@test-a.local` and `alice@test-a.local`. Tenant B is `bob@test-b.local`. The container serves JMAP on `https://localhost:1443` (compose maps 443 to 1443), so each account uses Override session URL `https://localhost:1443/.well-known/jmap`; `test-a.local` and `test-b.local` do not resolve. The accounts are seeded by `tests/stalwart/seed.sh` with the test password `pavtest-pw`. It is not copied into any other file.

### Authentication (decided 2026-10-16)

- **Protocol order:**
  1. JMAP accounts (K-2, M-1 to M-7)
  2. IMAP/SMTP (M-8, after the rpm and the mail CLI that follow daily-driver mail)
  3. CardDAV (S-5) and CalDAV (T-7), after the JMAP contacts and calendars phases
  4. OIDC (K-4), late
- **v1 auth for every protocol is username and password.** That's HTTP Basic for JMAP and DAV, and SASL PLAIN (LOGIN as a fallback) for IMAP and SMTP. It always runs over verified TLS: HTTPS, implicit TLS, or STARTTLS. Nothing is ever sent in cleartext, and a server with no TLS is refused.
- **"Plaintext passwords" means the auth method, not storage.** When Andrew said "plaintext passwords", he meant plain username/password auth instead of OAuth or OIDC. It does not mean storing passwords in plaintext. A password is never written to disk by Pavillion: it lives only in the platform secret store under the Secrets rules above, and the profile-rejection rule stands.
- **Who does what:** K-2 defines `PavAuth` and `auth_basic.c`, stores credentials (keyring through K-1) and validates them (method known, username present, secret stored). It never touches the network. M-1 calls `PavAuth.apply` on the net thread to put credentials on the wire, and handles 401 through `on_unauthorized`. M-2 is the first real use: `account.test` and session discovery authenticate this way. IMAP, SMTP, and DAV backends (M-8, S-5) call the same interface.
- **Abstraction for OIDC:** `core/include/pav_auth.h` defines one interface per method, `PavAuth { const char *method; int (*apply)(const PavAccount *, PavReq *, PavSecret *); int (*on_unauthorized)(const PavAccount *); }`.
  - `apply` runs on the net thread. It adds `Authorization: Basic …` or fills in the SASL credentials.
  - `on_unauthorized` decides between retrying once (OIDC refresh) and `error:auth` (Basic).
  - Backends (JMAP, IMAP, SMTP, DAV) call only `pav_auth_for(account)`, never a method directly.
  - K-4 adds `auth_oidc.c` and the Credentials page's sign-in button. Nothing in `net.c`, the backends, the keyring layer, or the account schema changes.
- **Discovery (K-Q3, resolved):** the email address's domain gives `https://<domain>/.well-known/jmap`, following redirects. The add form has a manual session-URL override. Andrew will give the real hostnames at K-3.
- **Pinned CA (added 2026-10-19):** an account may carry `caFile` (B Lock), an absolute path to a PEM CA bundle. When set, every request for that account (`account.test` and sync) passes it to curl as `CURLOPT_CAINFO`. Verification stays on; there is still no "accept invalid certificate" toggle. `account.update` with `caFile: ""` clears it.

- One global account list. Each section's nav is a filtered view: an account appears in Mail, Contacts, Calendars, or Files when it has that capability and its `sync_*` flag is on. An IMAP account still shows Files with an empty tree. Tasks lists an account when CalDAV reports VTODO (X), which is not a JMAP capability and is not a row in `account.test`. Notes use one home account (Y), not a row on every login. K-3 builds Mail, Calendar, Contacts, and Files only. X-2 and Y-2 add their Settings → Accounts headings. Argent adds none until Z is scoped. GitHub adds none.
- Order and hidden accounts are tree view state ([E-TREE.md](E-TREE.md)), not account fields. E-7 (Andrew, 2026-10-21) does not store a separate mail-tree order: the mail tree shows the order `mailbox.list` already returns, and the accounts table sorts on its own. Hidden accounts stay in that view state. Drag reorder remains E-6.
- Removing an account deletes the keyring entry, the profile entry, the `account_state` row (cascading to its mail), and its blob directory when `purgeLocal` is true. It needs a confirm, and from the CLI, `--yes`.
- Auth failure (HTTP 401): the account goes to `error:auth`, its loops stop, one banner offers "Re-enter password", and nothing retries until the secret changes. Never loop prompts (the Betterbird/iCloud failure).
- Identity choice for compose: (1) the address the original was sent to, (2) the mailbox's account default, (3) the global default (`preferences.default_identity`). Owned by [O-COMPOSE.md](O-COMPOSE.md); identities are listed by `identity.list`.

### Settings → Accounts panel (was plan §10)

- Accounts is a Settings heading, not another top section. Its panel is a split: the account list on the left, the selected account's form on the right. K-Q8 made that list a Tabulator table. E-7 (Andrew, 2026-10-21) leaves it as that table and does not move it onto the tree engine. Tasks and Notes headings are added by X-2 and Y-2, not by K-3.
- The right pane has a header with the number, name, and section.
- The Credentials page stores and replaces a secret and never renders one. It shows "stored" or "missing" and the date it was set (kept in `account_state`, not the keyring).
- Adding an account: name, email address, kind (JMAP by default; IMAP and DAV stay greyed out until M-8 and S-5 land), username (defaults to the email address), password, and color. The session URL comes from `https://<email domain>/.well-known/jmap`, and an "Override session URL" field replaces it. Auth method shows `basic` (password), and OIDC stays greyed out until K-4. "Test" runs `account.test` and shows the capabilities found (mail, submission, contacts, calendars, sieve, files) before Save is enabled. Do not add a tasks name to that list. VTODO is checked later, in X-1, from the CalDAV component set.
- An "Advanced" disclosure in the account form holds a plain text "CA file" field (`caFile`, an absolute path, optional). It is sent with Test, Add, and Save. Blank means the system store only.

### 1Password (recorded 2026-10-17, build it in K, not before)

Andrew asked for this beside the usual URL, username, and password. It is not part of J-1. The picker uses the J-1 dialog when K-3 builds the Accounts panel.

- A service-account token lets Pavillion list vaults and item secrets. The user picks one in a small dialog. The choice fills that account's username and password.
- Three things are stored in the secure store, not held only in memory: the service token, the vault and item list the picker shows, and the username and password the account actually uses. A restart must not ask 1Password again while that stored copy is still current.
- The cap he named is the 1Password service-account limit of 1000. The published table, checked the same day, puts 1000 on token writes per hour for Business, on token reads per hour for personal accounts, and on the personal and families daily cap. Caching is what keeps ordinary use off that API.
- If a stored username and password are rejected, refresh that one item from 1Password once. If the new value is rejected too, do not prompt in a loop and do not keep calling 1Password. Surface it on the Activity control (a yellow state or similar). I-2 owns that control; K does not paint it early.
- The store he named is IOTA Stronghold, the library inside `tauri-plugin-stronghold` v2, because it is the same library on every platform. K-1 links that library from the platform layer and does not load the Tauri plugin. The platform function table stays. The WebView still drops the token when the form closes. K-Q4 is resolved: libsecret holds the snapshot password, so a session unlock opens Stronghold with no second prompt. Do not implement libsecret as the account-secret store.

## K-1: Secrets through the platform

**Kind:** Medium
**Status:** Complete (2026-10-18).

**Goal:** The core can store, read, and delete an account secret on Linux (Stronghold, snapshot password from libsecret), and tests use the fake.

**Entry gate:** L-1 complete.

**Work items:**

- [x] `core/platform/linux/secrets.c`: read or create the snapshot password with `secret_password_lookup_sync` / `secret_password_store_sync` on the default collection, schema `com.500nodes.pavillion.Snapshot`, attribute `user`. Open the Stronghold snapshot with that password. `secret_get/set/delete` read and write the account record. Link `libsecret-1` and the Stronghold static library. Nothing else calls either.
- [x] The fake platform's in-memory store, with a switch to simulate a locked keyring.
- [x] `core/src/secret.c` wrappers that zero buffers and map errors to `locked`, `missing`, `failed`.
- [x] Unity on the fake: `test_secret_roundtrip`, `test_secret_missing`, `test_secret_locked_reports_locked`, `test_secret_never_logged` (log capture contains no fixture secret).
- [x] A manual check script `tests/manual/secret_smoke.sh` that stores, reads, and deletes a dummy account secret through a debug-only command `debug.secret_smoke`. `secret-tool search service pavillion` shows the snapshot-password entry and never the dummy secret. Deleting the dummy does not delete the snapshot password.

**Done means:** Account secrets work in Stronghold and on the fake, the snapshot password is the only OS-keyring value, and none reaches the log.

**Exit gate:** `make test` exits 0. The smoke script passes on Andrew's Fedora session. `secret-tool search service pavillion` shows the snapshot-password entry and not the dummy secret, before and after the dummy is deleted.

**Accomplished:** Linux `secret_get/set/delete` open a Stronghold snapshot with a snapshot password from libsecret. The fake still covers Unity. `tests/manual/secret_smoke.sh` stored, read, and deleted `smoke/dummy` and left only the `smoke/stronghold` snapshot-password entry. `make test` is 85 Unity tests and 134 Vitest tests, exit 0. `PAVILLION_API_VERSION` stays 3. The phase stamp is `0.47`.

**Lessons learned:** `secret-tool search` prints attributes on stderr, so the smoke check has to read both streams. `make test` and the shell must link `libsecret` once `secrets.c` is in the archive. The Stronghold crate is a shell dependency so the window does not link a second Rust runtime. `#[used]` keeps the three C entry points.

**Handoff:** K-2 is next, step 44, in this file. Do not start it in this sitting. Do not rebuild the writer, the snapshot schema, or `debug.secret_smoke`. Account commands write secrets through `pav_secret_*`, not libsecret. Tests stay on the fake. The smoke profile `smoke/stronghold` is still in the session keyring; it is not a real account. Do not commit the `PAVILLION_BUILD` bump. The phase stamp is `0.47.0`.

## K-2: Account model and commands

**Kind:** Medium
**Status:** Complete (2026-10-19).

**Goal:** Accounts can be added, updated, listed, and removed through commands, with numbers and secrets handled per the Lock.

**Entry gate:** K-1 complete.

**Work items:**

- [x] Add `number` and `next_account_number` to the B Lock and the parser (F-5 helpers). Validation: `id` matches `^[a-z0-9][a-z0-9-]{0,62}$`, generated from the name when absent; `kind` `jmap` (and `imap` accepted but marked unsupported); URLs must be `https://`; `color` `#rgb` or `#rrggbb`; `fetch_bodies` `none|recent|all`.
- [x] Commands `account.list`, `account.add`, `account.update`, `account.remove`, `account.set_secret`. `account.add` writes the keyring first, then the profile, then `account_state`; failure rolls back what was done. Results never include the secret.
- [x] Events `accounts.changed`.
- [x] `core/include/pav_auth.h` and `core/src/auth_basic.c` per the Authentication Lock. `auth` is validated as `basic` (K-4 adds `oidc` to the validator later). Unity (pure functions, no network): `test_auth_basic_header`, `test_auth_unknown_method_rejected`, `test_auth_basic_on_unauthorized_fails`. The wire-level 401 test is M-1's `test_net_401_not_retried`.
- [x] Unity: `test_account_add_assigns_next_number`, `test_account_numbers_never_reused`, `test_account_add_rolls_back_on_profile_write_failure`, `test_account_remove_purges`, `test_account_list_has_no_secret`, `test_account_rejects_http_url`.

**Done means:** An account added by command survives a restart with its number, and its secret is only in the keyring.

**Exit gate:** `make test` exits 0. `rg -a <fixture password> <workdir>` finds nothing after a debug-console add in `make dev`.

**Accomplished:** `account.add` assigns a three-digit zero-padded number from the index `next_account_number`, writes the secret to the keyring first, then the profile, then inserts `account_state`, and rolls back what was done on failure. `account.update` patches allowed fields, refusing `kind` and `auth`. `account.remove` deletes the keyring entry, profile entry, and `account_state` row, with optional `purgeLocal` cascade. `account.set_secret` replaces the keyring value and never writes it to the profile or database. `accounts.changed` is emitted after every mutation as `{accounts:[account]}` with secrets omitted. `core/include/pav_auth.h` defines `PavAccount`, `PavReq`, `PavSecret`, `PavAuth`, and `pav_auth_for`; `core/src/auth_basic.c` implements the `basic` method with a self-contained base64 encoder, sets `Authorization: Basic`, and returns -1 from `on_unauthorized`. `auth` is validated as `basic` only. The index gains `next_account_number` (defaults to 1 on read); the account object gains `number`. Unity: 99 tests 0 failures, including `test_auth_basic_header`, `test_auth_unknown_method_rejected`, `test_auth_missing_method_rejected`, `test_auth_basic_on_unauthorized_fails`, `test_account_list_empty`, `test_account_list_has_no_secret`, `test_account_add_assigns_next_number`, `test_account_rejects_http_url`, `test_account_numbers_never_reused`, `test_account_add_rolls_back_on_profile_write_failure`, `test_account_remove_purges`, `test_account_remove_not_found`, `test_account_update_patch`, `test_account_set_secret`. `PAVILLION_API_VERSION` stays 3. The phase stamp moves to `0.48.0`.

**Lessons learned:** The base64 encoder in `auth_basic.c` had a bug in the 2-remaining-bytes case: it was missing the 3rd data character `k_b64_chars[(b1 & 0x0f) << 2]` and had an extra `=` padding character. Fixed during testing. `pav_db_call` is synchronous, so `account.add`'s `store_account_state` blocks the core thread until the write job completes, simplifying rollback logic. The `account.update` handler copies the patch object's values into the account JSON by reference before serializing the profile, which avoids double-copy but requires careful ordering of validation before mutation. The `accounts.changed` event is emitted as a JSON text of the accounts array, not the full reply envelope.

**Handoff:** K-3 (Settings → Accounts panel) is next, step 48, and needs M-2 (`account.test`) first. K-4 (OIDC) adds `auth_oidc.c` later without touching `auth_basic.c`, the keyring layer, or the account schema. W-2 (fixture server) comes before M-1. The auth interface is stable: `pav_auth_for(account)` is the only call backends will make. Account commands are real and callable now. Do not start K-3 in this sitting.

## K-3: Settings → Accounts panel

**Kind:** Medium
**Status:** Complete (2026-10-19).

**Goal:** Andrew can add his real accounts from the window.

**Entry gate:** M-2 complete.

**Work items:**

- [x] `ui/src/settings/accounts.js`: the split per the Lock, the add form with Test (calls `account.test`), Credentials page, Server settings (session URL, username, auth), Name and colors, Remove with confirm and "also delete local data".
- [x] Every value shown with `textContent`; server error text from `account.test` shown as text.
- [x] The secret field is `type=password`, `autocomplete=off`, cleared after submit and on panel close.
- [x] Vitest: form validation (`https` only, required fields) over the mock transport.

**Done means:** Three real accounts across two tenants are added, tested, and listed with numbers 001–003.

**Exit gate:** `make test` exits 0. In `make build`, add three accounts (plan acceptance for P0: two tenants), each Test shows capabilities, restart, all three listed; a wrong password on Test shows one error, no loop; `rg -a` over the workdir finds none of the passwords.

**Accomplished:**
- `ui/src/settings/accounts.js`: `createAccountsView(api, root)` factory renders the account tree (plain nested list per the E-Lock until E-7), the add form, the Credentials page, and the edit/remove flow. `formState` is the single source of truth. The Test button calls `account.test` and shows capabilities before Save is enabled. The secret field is `type=password`, `autocomplete=off`, and is cleared after test, add, save, and panel close. Every display value uses `textContent`, including server error text. The Credentials page shows "Stored in keyring" or "Not saved yet", never the secret. Remove confirms with a dialog and passes `purgeLocal: true`.
- `ui/src/shell/template.js`: added the "Accounts" heading button between "Tabs" and "Zooms" in the Settings nav, and the `[data-settings="accounts"]` pane with the split layout (`accounts-tree-pane` + `split-resize[data-split="accounts"]` + `accounts-form-pane`), all form fields, Test/Save/Add/Remove buttons, the capabilities and error display, and the Credentials section.
- `ui/src/shell.js`: imports and instantiates `createAccountsView`, calls `refresh()` when the accounts pane is present, destroys it on stop, and calls `bindSplit("accounts")`. `applyHomeContext` auto-detects the `[data-heading="accounts"]` button for session restore.
- `ui/src/prefs.js`: added `accounts` to `DEFAULT_UI` (`split: 0.35, min: 0.15, max: 0.6`), `KNOWN`, and `clampUi`.
- `ui/public/styles/theme.css`: added the `.accounts-*` CSS rules for the split layout, tree list, form fields, test status, error/capabilities, and credentials section.
- `ui/src/help/pages.js`: the "Accounts" help section under Settings documents the Test-then-Save flow, the three-digit number, the Basic auth method, and that OIDC comes later.
- `ui/src/features.test.js`: updated the heading list assertion to include "Accounts".
- `ui/src/settings/accounts.test.js`: 10 Vitest tests covering heading/pane visibility, empty tree placeholder, adding accounts with number assignment, Test button enable/disable logic, valid credentials showing capabilities, wrong password error, Save state after Test, tree selection populating the form, removing accounts, and saving edits.
- `make test` exits 0: 120 Unity tests (0 failures with fixture) and 144 Vitest tests.

**Lessons learned:**
- The accounts tree uses a plain nested list, not the tree engine — E-TREE does not exist yet. The tree buttons use `data-account-id` attributes and `aria-current` for selection state, scoped so the eventual E-7 migration is a rendering swap, not a logic rewrite.
- The Test flow gates Save: `isSaveReady()` requires `testResult.ok` to be true, which only happens after a successful `account.test` call. This prevents saving credentials that haven't been verified.
- The mock transport's `account.test` returns `auth` error when `secret === "wrong"`, simulating a real server rejection without looping. The one-shot error is shown as text, and no retry happens until the user changes the password.
- `applyHomeContext` in `shell.js` already handles arbitrary `[data-heading]` buttons generically, so opening Settings → Accounts from a session restore requires no special case — the accounts heading button and `[data-settings="accounts"]` pane work with the existing mechanism.

**Handoff:** K-3 delivers the full account management UI with mock transport backing. E-1 (step 49) is complete. E-7 will move this tree onto the real tree engine. The account commands (`account.add`, `account.list`, `account.update`, `account.remove`, `account.set_secret`, `account.test`) and the `accounts.changed` event are real from K-2 and M-2. The shell wiring (headings, panes, split binding, session context) is in place and extensible. K-Q1 (which real accounts Andrew adds first) is still open but not blocking for the next phase. `PAVILLION_API_VERSION` stays 3. The phase stamp is `0.50`.

## K-4: OpenID Connect sign-in

**Kind:** Hard
**Status:** Not started.

**Goal:** An account can authenticate with generic OIDC (Keycloak-style; authorization code plus PKCE) and send a bearer token to the server. It plugs in as a second `PavAuth` implementation without rework.

**Entry gate:** The initial working copy is complete (index milestone 4).

**Work items:**

- [ ] Discovery from `oidc_issuer` `/.well-known/openid-configuration`. PKCE S256 with OpenSSL SHA-256. The redirect is a loopback `http://127.0.0.1:<random>/cb` listener in the host for one request, opened in the system browser through opener.
- [ ] Token refresh on the network thread before expiry and on a 401 once; failure goes to `error:auth`.
- [ ] Tokens in the keyring as JSON; never logged.
- [ ] Unity with a fixture token endpoint (W-2 fixture server).

**Done means:** An OIDC account syncs and refreshes its token without prompting.

**Exit gate:** `make test` exits 0; a real OIDC sign-in against Stalwart (if configured) succeeds.

## Open questions for Andrew

- K-Q1. Resolved 2026-10-19: the W-2 Stalwart container, with `andrew@test-a.local` and `alice@test-a.local` (tenant A) and `bob@test-b.local` (tenant B), at `https://localhost:1443/.well-known/jmap` (K Lock, Accounts).
- K-Q2. Resolved 2026-10-16: username and password for every account (Basic or SASL PLAIN over TLS), stored in the keyring. OIDC comes later (K-4).
- K-Q3. Resolved 2026-10-16: discovery uses `https://<email domain>/.well-known/jmap` with a manual override. Andrew gives the hostnames at K-3.
- K-Q4. Resolved 2026-10-18: the OS keyring holds the Stronghold snapshot password. A session unlock opens the snapshot. No second prompt. Account secrets stay in Stronghold.
- K-Q5. Resolved 2026-10-19 (Andrew): Configure. `account.discover {email}` finds the session URL with no credentials: the SRV record `_jmap._tcp.<domain>` first (lowest priority, then highest weight; `https://<target>[:port]/.well-known/jmap`, port omitted when 443), then `https://<domain>/.well-known/jmap`. Redirects are followed the same way as `account.test` (at most 3, `https://` only) and the final response must be JSON with an `apiUrl`. The result is the final resolved URL, which the account form's Configure button writes into Session URL; the form's Email field defaults from the username when it looks like an address. SRV is an OS call, so it is a platform entry, `srv_lookup` (G Lock); the fake answers from a table the tests set. Tests never use the network. Example: 500foods.com has `0 1 443 mail.500foods.com`, and its well-known redirects to `https://mail.500foods.com/jmap/session`. Supersedes the discovery half of K-Q3.
- K-Q6. Resolved 2026-10-19 (Andrew): the account email is stored. Every account object has `email` (`""` or an address). `account.add` and `account.update` take it; `account.list` returns it. Accounts saved before the field get it when the profile attaches: the username when that looks like an address, else `""`, written back with the next profile write. The form's Email field reads and saves it. There is no `account.get`; `account.list` is the read.
- K-Q7. Resolved 2026-10-19 (Andrew): the session URL follows what the server says. After Detect succeeds, the form holds the resolved URL; on a saved account it is written at once with `account.update` and the Server line says `Saved`; on a new account it is saved by Add. `account.test` returns `sessionUrl`, the final URL after redirects; when it differs from the stored one the form takes it and saves it the same way (no prompt; the line says `Session URL updated`). A redirect that dropped `Authorization` (another host) still answers the test, so after updating the URL the form runs Test once more against the final URL, where the password is sent. Settings → Accounts is the first Settings heading. The divider between the account list and the form is the nav divider: one `1px solid var(--pav-border)` line with a 6 px invisible grab handle over it.
- K-Q8. Resolved 2026-10-19 (Andrew): the account list is a Tabulator table (shared component, N Lock): color swatch with the account icon, Name, then one read-only checkbox column per feature with the rail icon in the header: Mail (with J, I, S badges for JMAP, IMAP, SMTP; only J is live, I and S show as unsupported), Calendars, Contacts, Files. A box is checked when the account has that capability (stored from its last verified Test, K-Q10) and its `sync*` flag is on. There is no per-feature enable for Files yet; Files reads capability only. Clicking a row selects the account and loads the form. (2026-10-30) The account that was selected comes back, and the list scroll is restored. Both live on `preferences.ui.tables.accounts`, and a column Reset does not clear them.
- K-Q9. Resolved 2026-10-19 (Andrew, amended the same day): accounts carry `icon`, an HTML snippet of Font Awesome markup, `""` for the default (`<i class="fa-solid fa-envelope"></i>`). The interim chooser writes exactly one element, `<i class="fa-<style> fa-<name>"></i>`, with a class made of `fa-` words only (`[a-z0-9-]`), no other attribute, so it is already a valid Icon Wizard (D-9) snippet. `account.add` and `account.update` take it: `""`, or a string under 4096 bytes that starts with `<`, has no `<script`, `on…=` attribute, or `javascript:`; anything else is `bad_args`. The core only bounds it; the page renders it through `sanitizeIconHtml`, which accepts a Font Awesome `<i>`, a Material `<span>`, or a `pav-asset:` / `pav-system:` image, and draws `envelope` for anything it refuses. `account.list` returns it; accounts saved before it get `""` at attach, like `email`. The account color is applied by the page as the icon color; the interim snippet carries no color. The form's Icon button beside Color opens the Icon Wizard. The account color uses Coloris. The table and every place that shows the account color use it. The Icon Wizard (phase D-9, DRAFT) replaces the chooser and writes larger snippets (FA layers with inline colors and transforms, or `pav-asset:` / `pav-system:` images) in the same key; an image layer refers to an asset in a per-profile asset store (D-9), never to an outside file. Account **color** uses the shared Coloris picker (D-Q7b); account **icon** uses the Icon Wizard — both.
- K-Q10. Resolved 2026-10-19 (Andrew): accounts store what the server offered. Two fields: `capabilities`, an array of the capability words `account.test` returns (`mail`, `submission`, `contacts`, `calendars`, `sieve`, `files`, `websocket`; unknown words are dropped, no repeats), and `capabilitiesChecked`, the UTC time of that check as ISO 8601 with milliseconds and `Z`, or `null`. The keys are camelCase like every other account key (Andrew wrote `capabilities_checked`; the stored key is `capabilitiesChecked`). Accounts saved before them get `[]` and `null` at attach. `account.list` returns both. A successful `account.test` that names a saved account (`id`) and actually sent and passed `Authorization` (`authSent` true; the unauthenticated pass after a cross-host redirect does not count, the page's rerun against the final URL does) stores both; a failed test changes nothing. The core thread writes the profile: the test runs on the worker, which hands the result to the core thread as an internal job, before the reply, so a later `account.list` sees it. One `INFO` line when the stored list changes (`capabilities of <id>: mail, …`); the time alone changing logs nothing. A Test from an unsaved form keeps the result in the form, and `account.add` takes `capabilities` and `capabilitiesChecked` and stores them. `account.update` does not take them. The table ticks from the stored list (sync flags still apply) and shows `Checked <local time>` as the row's feature tooltip, in 24-hour time in the profile time zone; never checked shows `Not checked yet`.
- K-Q11. Resolved 2026-10-19 (Andrew): the account table's columns are, in order, the row indicator (2026-10-20, Andrew: TABULATOR.md "Row indicator column"; a caret on the current row, frozen, not sortable, not chooseable, not exported), the number (`001`), the color swatch with the account icon, Name (the one `widthGrow` column), then (2026-10-20, Andrew, header-row round) JMAP, IMAP, SMTP (text titles), then Mail, Contacts, Calendars, Files (main-nav icons as titles; K-Q8). The swatch column does not sort (`headerSort:false`, no sort icon). Every other column sorts tri-state. The footer shows the account count under `#` (2026-10-20). Name has no calc. `#` and the check columns are centred, and header titles are left-aligned with the sort arrow pinned right. Each protocol and feature column is a read-only checkbox with the checked count in the footer and the last-checked time as its tooltip. JMAP is on when the account has saved capabilities (they come from its JMAP session, K-Q10). IMAP and SMTP are on only when the saved capabilities list `imap` or `smtp`, which nothing produces yet, so they show unchecked. There is no detection for them yet. The old J/I/S badges in the Mail cell are gone. Widths: protocol columns start at 100 px and widen to fit the measured title (2026-10-20 resize round: 80 px clipped to `J...` in the real window), icon columns 68 px. Name is the only column that stretches. Users can resize Number, Name and the check columns (TABULATOR.md "Fitted titles, resizing"). Widths are saved in `preferences.ui.tables.accounts.widths`, Name excepted, and the chooser's Reset restores them. Vertical group dividers sit right of the indicator, left of the swatch, left of JMAP, and left of Mail. Rows are grouped by server with Tabulator `groupBy`: the group label is the stored `sessionUrl`'s host only, for example `mail.500foods.com` (2026-10-20, Andrew: was host plus the URL in parentheses), with the count as a separate badge; an empty or unparsable URL goes to one `(no server)` group. The helper is `serverGroup(sessionUrl)` in `ui/src/settings/accounts-table.js`. Each group header shows its row count. Groups keep the order of their first account. A table footer shows the total, `1 account` or `<n> accounts`. Header and footer are built with `textContent`.
- K-Q12. Resolved 2026-10-20 (Andrew; D-10 moved the navigator and Export to the module footer, with Refresh first, and added Clone and search on the module toolbar): the account table fills its pane. The pane is a flex column; the table has Tabulator `height:"100%"` and the virtual renderer, so the header sits at the top, the footer is pinned to the bottom, and rows scroll vertically only when they do not fit. Tabulator's ResizeTable module (ResizeObserver plus IntersectionObserver) redraws on resize and when a hidden panel is shown; the wrapper's own observer is gone. Data is passed once (constructor, or `replaceData` for later changes). Name is the only growing column; the pane's `min-width` is the sum of the fixed column widths (saved widths included) plus Name's `minWidth` (`tableMinWidth`). Its maximum (2026-10-20, Andrew) is no longer 40%: the table pane may take all but `20rem`, which the form pane keeps as its minimum. The split ratio is clamped to 0.15–0.85, and those bounds are fixed in code, not taken from stored preferences, so every column stays visible with no horizontal scrollbar. Every column sorts tri-state (`headerSortTristate`: asc, desc, none) with Font Awesome icons from `headerSortElement` in the kit style `fa-sharp-duotone fa-solid` (`fa-sort`, `fa-sort-up`, `fa-sort-down`); feature columns sort by checked state, the swatch by color; rows sort within their server group and groups keep the order of their first account. Footer calculations (`columnCalcs:"table"`): Name shows `N accounts`, each feature column the number of checked boxes; this replaces K-Q11's footer text. Group headers keep their count; no per-group calc rows (the count already says it, and a calc row per server doubles the row count). Below the calcs, a navigator row: First, Previous, Next, Last (`backward-step`, `chevron-left`, `chevron-right`, `forward-step`, each with a title and `aria-label`), disabled at the ends and when there are no rows. They move Tabulator single-row selection (`selectableRows:1`, changed only by the page, never toggled by a click) in display order (groups, then sorted rows), load that account into the form, and scroll it into view. With the table focused, Up and Down do the same as Previous and Next. Export on the right writes CSV through Tabulator `download` (`downloadRowRange:"all"`, no group or calc rows): number, name, server, JMAP, IMAP, SMTP, Mail, Contacts, Calendars, Files (`yes`/`no`), email, in that order (2026-10-20 header-row round), and nothing else (no URL parts beyond the server label, no username, no secret; account rows never carry secrets). The export (D-10 revision, 2026-10-20) is built in the page and written by the core: the save dialog picks the path (default `pavillion-accounts.csv`) and `export.write_text` writes it; the blob download did nothing in WebKitGTK. One `INFO` line through `log.client` (`accounts`): `exported <n> accounts to CSV`.
- K-Q13. Resolved 2026-10-20 (Andrew, bug: toolbar Add, fill, Detect, Test, then Save did nothing; the log showed `click account-save` and no `account.add`). Save on a form with no account id (opened by toolbar Add or Clone) adds the account: it calls `account.add` with the form fields, the detected session URL and the typed secret. The form's Add button does the same. A missing name, session URL, username or password is an inline error under Test, never a silent no-op. An add or update failure is shown and logged as one `WARN` line through `log.client` (`accounts`), without the secret. After a successful add the table reloads (`account.list`), the new row is selected, and the indicator and the form are on it. Update reloads and keeps the row selected. Delete reloads and selects the first row.
- K-Q14. Resolved 2026-10-20 (Andrew, real-window round):
  - **Zoom:** two new zoom rows, Accounts table (`accountsTable`, the `#accounts-tree` element) and Accounts form (`accountsForm`, the `.accounts-form-pane` element). They are 5%–500%, multiplied by Shell, and Ctrl+wheel over the element steps it. Before, only the module bars had a zoom; Accounts had none. Accounts form uses CSS zoom, like Mail detail. Accounts table zooms in place (revised later on 2026-10-20): font size plus scaled column widths, never CSS zoom and never a pane-width change; see the TABULATOR.md section on zoom in place.
  - **Split position:** the split ratio is remembered. Dragging always saved it, but `loadPrefs` never copied `accounts` from the stored preferences, so every load used the default 0.35.
  - **Table:** no horizontal scrollbar, a compact header, and full-height dividers (TABULATOR.md, the section on no horizontal scrollbar). The stray `2` under the footer was the wrapped calc row.


## Working log

- 2026-10-16 Opened from plan §3, §10 (Accounts panel), §11 (credentials), and the B Lock. Decisions: `number` with a never-reused counter on the index, keyring label and attribute, locked-keyring state, OIDC as its own Hard phase, account removal order, add-then-test flow.
- 2026-10-16 Andrew: protocol order is JMAP, then IMAP/SMTP, then CalDAV/CardDAV, with OIDC late. v1 auth is username and password (Basic or SASL PLAIN over TLS) with keyring storage, and "plaintext passwords" means the auth method, not storage. Added the `auth` method field and the `PavAuth` interface so K-4 slots in. K-Q2 and K-Q3 resolved, K-Q1 simplified.
- 2026-10-17 Andrew: 1Password service accounts are a first-class source next to URL, username, and password. Persist the token, the picker list, and the chosen username and password in Tauri Stronghold. Refresh once on auth failure, then show it on the Activity control. K-Q4 (the snapshot password) is open. Not built in J-1.
- 2026-10-18 Tasks appear when CalDAV reports VTODO. Notes use one home account. K-3 does not grow Tasks, Notes, or Argent headings. `account.test` does not gain a tasks capability.
- 2026-10-18 GitHub adds no Settings → Accounts heading and no `account.test` row.
- 2026-10-18 K-Q4 resolved: libsecret stores only the snapshot password. Account secrets are Stronghold records. K-1 rewrote the Secrets lock before secret code. Status in progress. Last finished work item: Unity on the fake. `libsecret-devel` is not installed here, so `secrets.c` is not linked yet. The smoke script has not been run.
- 2026-10-18 K-1 complete, step 43. `libsecret-devel` is installed. The smoke script passed. `make test` is 85 Unity tests and 134 Vitest tests. The phase stamp is `0.47`. K-2 is next, step 44. Do not start it in this sitting.
- 2026-10-19 K-2 complete, step 44. Implemented `account.add`, `account.list`, `account.update`, `account.remove`, `account.set_secret` in `core/src/account.c`, registered in `dispatch.c`. Added `core/include/pav_auth.h` and `core/src/auth_basic.c` with the `basic` auth method. `accounts.changed` event emitted after mutations. Index gained `next_account_number`, account object gained `number`. All 99 Unity tests pass (4 auth tests + 9 account tests). Fixed base64 encoder bug in `auth_basic.c` (missing 3rd char, extra padding). `PAVILLION_API_VERSION` stays 3. Phase stamp moves to `0.48.0`.
- 2026-10-19 K-3 complete, step 48. Built the Settings → Accounts split panel in `ui/src/settings/accounts.js` with the account tree (plain nested list, not the tree engine), add form with Test gating Save, Credentials page, edit form, and remove with confirm + purgeLocal. Added the `[data-heading="accounts"]` button and `[data-settings="accounts"]` pane to `ui/src/shell/template.js`, wired `createAccountsView` and `bindSplit("accounts")` in `ui/src/shell.js`, added the `accounts` split config to `ui/src/prefs.js`, and the `.accounts-*` CSS to `ui/public/styles/theme.css`. Updated `ui/src/help/pages.js` with the Accounts help section and `ui/src/features.test.js` for the heading list. 10 Vitest tests pass. `make test` exits 0: 120 Unity (0 failures with fixture) and 144 Vitest. `PAVILLION_API_VERSION` stays 3. The phase stamp moves to `0.49`.
- 2026-10-19 K-Q1 resolved: the first three accounts are the W-2 Stalwart container's `andrew@test-a.local`, `alice@test-a.local`, and `bob@test-b.local`, at `https://localhost:1443/.well-known/jmap`. M-3's entry gate runs the K-3 exit gate with them.
- 2026-10-19 Pre-M-3 fix: per-account `caFile` (pinned CA path) in the account object, `account.add`/`update`/`test`, and the Accounts form's Advanced field; Stalwart test certs generated by `make it-up` into `build/stalwart-tls/` and mounted read-only.
- 2026-10-19 First-accounts note: the Stalwart test password now comes from `tests/stalwart/seed.sh` (`pavtest-pw`), not `config.toml`.
- 2026-10-19 Andrew approved dropping the slow snapshot KDF: the random keyring key is used directly and snapshots are written at age work factor 0; older snapshots open with the same key and are re-saved at 0 on first read (INFO line). See the Secrets lock above.

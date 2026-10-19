# K — ACCOUNTS

**Status:** Not started (scoped 2026-10-16 from plan §3, §10, §11).
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

Accounts, credentials, and identities as the user manages them: the keyring, the account commands, account numbers, and the Settings → Accounts panel. The account JSON shape is the [B-CONFIG.md](B-CONFIG.md) Lock; this document adds `number` and OIDC to it. Talking to the server is [M-SYNC.md](M-SYNC.md).

## Phases

One phase is one session. Do not start the next one in the same conversation.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| K-1 | Secrets through the platform: Stronghold, snapshot password in libsecret, fake | Medium | Done |
| K-2 | Account model and commands: number, add, update, remove, set secret | Medium | Complete |
| K-3 | Settings → Accounts panel: add with test, credentials, edit, remove | Medium | Not started |
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
- Stronghold client `pavillion`. Account secrets are Store entries, not vault records: a vault record cannot be read back, and Basic auth must put the password on the wire. The key is `<profile id>/<account id>`. The value is the secret. The label `Pavillion <account number> <account name>` is a second Store entry, key `<profile id>/<account id>/label`. One secret per account: the password in v1, or, from K-4, the OIDC refresh token as JSON `{"refresh":"…"}`. The 1Password service token and the cached vault list use the same snapshot, under keys `<profile id>/op-token` and `<profile id>/op-cache`, when that phase writes them.
- `secret_get`, `secret_set`, and `secret_delete` are the account-secret calls. Return 0, or `PAV_SECRET_MISSING`, `PAV_SECRET_LOCKED`, or `PAV_SECRET_FAILED`. `LOCKED` means the OS keyring is locked, so the snapshot password cannot be read. It does not mean a Stronghold record is absent.
- The core reads a secret only on the network thread when building a request, keeps it in memory for that request, and `explicit_bzero`s the buffer after. It is never put in a JSON command result.
- A locked keyring at startup is not an error loop: the account goes to `state = "locked"`, one banner says "Unlock your keyring to sync", and sync retries when the keyring signals unlock (libsecret's collection `locked` property change) or on the next manual sync.
- A profile containing `password`, `token`, `secret`, or `refresh_token` is still rejected (B Lock).

### Accounts

- Account object (B Lock) gains `number` (string, three digits zero-padded, `"001"`). Numbers are assigned at add time as one more than the highest ever used, stored in the index as `next_account_number` so they are never reused, and never renumbered. The number appears in the mail tree, the CLI's `--account`, and the audit log.
- `auth` is the auth-method field on every account (B Lock): `basic` in v1, `oidc` from K-4, `bearer` reserved. `oidc` adds `oidc_issuer` and `oidc_client_id`, and its tokens go in the keyring.

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

- One global account list. Each section's nav is a filtered view: an account appears in Mail, Contacts, Calendars, or Files when it has that capability and its `sync_*` flag is on. An IMAP account still shows Files with an empty tree. Tasks lists an account when CalDAV reports VTODO (X), which is not a JMAP capability and is not a row in `account.test`. Notes use one home account (Y), not a row on every login. K-3 builds Mail, Calendar, Contacts, and Files only. X-2 and Y-2 add their Settings → Accounts headings. Argent adds none until Z is scoped. GitHub adds none.
- Order and hidden accounts are tree view state ([E-TREE.md](E-TREE.md)), not account fields.
- Removing an account deletes the keyring entry, the profile entry, the `account_state` row (cascading to its mail), and its blob directory when `purgeLocal` is true. It needs a confirm, and from the CLI, `--yes`.
- Auth failure (HTTP 401): the account goes to `error:auth`, its loops stop, one banner offers "Re-enter password", and nothing retries until the secret changes. Never loop prompts (the Betterbird/iCloud failure).
- Identity choice for compose: (1) the address the original was sent to, (2) the mailbox's account default, (3) the global default (`preferences.default_identity`). Owned by [O-COMPOSE.md](O-COMPOSE.md); identities are listed by `identity.list`.

### Settings → Accounts panel (was plan §10)

- Accounts is a Settings heading, not another top section. Its panel is a split: the account tree on the left, the selected leaf's form on the right. The tree is three levels: account, section (Account, Mail, Calendar, Contacts, Files), page (Name and photo, Credentials, Server settings, Folder settings, Spam settings, Colors). Tasks and Notes headings are added by X-2 and Y-2, not by K-3. Until E-7 the left side is a plain nested list with the same shape; E-7 Handoff moves it onto the tree engine.
- The right pane has a header with the number, name, and section.
- The Credentials page stores and replaces a secret and never renders one. It shows "stored" or "missing" and the date it was set (kept in `account_state`, not the keyring).
- Adding an account: name, email address, kind (JMAP by default; IMAP and DAV stay greyed out until M-8 and S-5 land), username (defaults to the email address), password, and color. The session URL comes from `https://<email domain>/.well-known/jmap`, and an "Override session URL" field replaces it. Auth method shows `basic` (password), and OIDC stays greyed out until K-4. "Test" runs `account.test` and shows the capabilities found (mail, submission, contacts, calendars, sieve, files) before Save is enabled. Do not add a tasks name to that list. VTODO is checked later, in X-1, from the CalDAV component set.

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
**Status:** Not started.

**Goal:** Andrew can add his real accounts from the window.

**Entry gate:** M-2 complete.

**Work items:**

- [ ] `ui/src/settings/accounts.js`: the split per the Lock, the add form with Test (calls `account.test`), Credentials page, Server settings (session URL, username, auth), Name and colors, Remove with confirm and "also delete local data".
- [ ] Every value shown with `textContent`; server error text from `account.test` shown as text.
- [ ] The secret field is `type=password`, `autocomplete=off`, cleared after submit and on panel close.
- [ ] Vitest: form validation (`https` only, required fields) over the mock transport.

**Done means:** Three real accounts across two tenants are added, tested, and listed with numbers 001–003.

**Exit gate:** `make test` exits 0. In `make build`, add three accounts (plan acceptance for P0: two tenants), each Test shows capabilities, restart, all three listed; a wrong password on Test shows one error, no loop; `rg -a` over the workdir finds none of the passwords.

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

- K-Q1. Which accounts to add first at K-3? Name two or three, ideally from two tenants, with their hostnames. (Still open, not blocking until K-3.)
- K-Q2. Resolved 2026-10-16: username and password for every account (Basic or SASL PLAIN over TLS), stored in the keyring. OIDC comes later (K-4).
- K-Q3. Resolved 2026-10-16: discovery uses `https://<email domain>/.well-known/jmap` with a manual override. Andrew gives the hostnames at K-3.
- K-Q4. Resolved 2026-10-18: the OS keyring holds the Stronghold snapshot password. A session unlock opens the snapshot. No second prompt. Account secrets stay in Stronghold.

## Working log

- 2026-10-16 Opened from plan §3, §10 (Accounts panel), §11 (credentials), and the B Lock. Decisions: `number` with a never-reused counter on the index, keyring label and attribute, locked-keyring state, OIDC as its own Hard phase, account removal order, add-then-test flow.
- 2026-10-16 Andrew: protocol order is JMAP, then IMAP/SMTP, then CalDAV/CardDAV, with OIDC late. v1 auth is username and password (Basic or SASL PLAIN over TLS) with keyring storage, and "plaintext passwords" means the auth method, not storage. Added the `auth` method field and the `PavAuth` interface so K-4 slots in. K-Q2 and K-Q3 resolved, K-Q1 simplified.
- 2026-10-17 Andrew: 1Password service accounts are a first-class source next to URL, username, and password. Persist the token, the picker list, and the chosen username and password in Tauri Stronghold. Refresh once on auth failure, then show it on the Activity control. K-Q4 (the snapshot password) is open. Not built in J-1.
- 2026-10-18 Tasks appear when CalDAV reports VTODO. Notes use one home account. K-3 does not grow Tasks, Notes, or Argent headings. `account.test` does not gain a tasks capability.
- 2026-10-18 GitHub adds no Settings → Accounts heading and no `account.test` row.
- 2026-10-18 K-Q4 resolved: libsecret stores only the snapshot password. Account secrets are Stronghold records. K-1 rewrote the Secrets lock before secret code. Status in progress. Last finished work item: Unity on the fake. `libsecret-devel` is not installed here, so `secrets.c` is not linked yet. The smoke script has not been run.
- 2026-10-18 K-1 complete, step 43. `libsecret-devel` is installed. The smoke script passed. `make test` is 85 Unity tests and 134 Vitest tests. The phase stamp is `0.47`. K-2 is next, step 44. Do not start it in this sitting.
- 2026-10-19 K-2 complete, step 44. Implemented `account.add`, `account.list`, `account.update`, `account.remove`, `account.set_secret` in `core/src/account.c`, registered in `dispatch.c`. Added `core/include/pav_auth.h` and `core/src/auth_basic.c` with the `basic` auth method. `accounts.changed` event emitted after mutations. Index gained `next_account_number`, account object gained `number`. All 99 Unity tests pass (4 auth tests + 9 account tests). Fixed base64 encoder bug in `auth_basic.c` (missing 3rd char, extra padding). `PAVILLION_API_VERSION` stays 3. Phase stamp moves to `0.48.0`.

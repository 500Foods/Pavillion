# W — SHIP

**Status:** W-1 complete (2026-10-16). W-2 complete (2026-10-19). W-3 and W-4 are not started. The index names the next phase.
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

Continuous integration, the test fixtures every network phase depends on, the testing rules, and packaging. W-1 and W-2 come early. W-3 and W-4 come right after daily-driver mail (milestone 3), before calendars and files, so the Betterbird replacement is installable on its own.

## Phases

One phase is one session. Do not start the next one in the same conversation.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| W-1 | CI on GitHub Actions: `make test` and `make build` on Fedora | Medium | Complete |
| W-2 | JMAP fixtures: a recorded fixture server and a disposable Stalwart container with seeded accounts | Hard | Complete |
| W-3 | Fedora rpm, the dnf repository, desktop file, autostart | Medium | Not started |
| W-4 | Arch `PKGBUILD` (Omarchy) | Easy | Not started |

Kind: **Easy** is one file and a check. **Medium** crosses one boundary. **Hard** is a new process the tests depend on.

## How a session works

1. Read Resume in [PAVILLION_PLAN.md](../PAVILLION_PLAN.md), the Status column above, and the previous phase's Status, Accomplished, Lessons learned, and Handoff.
2. Re-read only this phase: Goal, Entry gate, Work items, Done means, Exit gate. Open the lock section this phase cites.
3. Ask the qualifying questions. Wait for approval before editing source.
4. Do this phase only. Check a work item `[x]` only after its check has passed. Defer with `[~]`, one line, and the phase it moves to.
5. Before marking complete, write Status, Accomplished, Lessons learned, and Handoff, update the Status column here and in the index, and append the Working log. Before that, update `ui/src/help/pages.js` for what this phase changed: augment, replace, fix, or extend the user page, or add a Developer section when the user page has nothing new. Then stop.
6. A sitting that ends early sets Status to **in progress**, names the last finished work item, and leaves the rest unchecked.

Leave Accomplished, Lessons learned, and Handoff off a phase that has not started.

## Lock

### Tests (was old plan §13)

- C unit tests use Unity, vendored under `tests/unity/framework` without Hydrogen's mock set. `make test` runs Unity, `cargo test` (from F-1), and Vitest (from G-3). It must pass at the end of every phase.
- Unit coverage named across the docs: MIME parse and build round-trip, the sanitizer policy, the query parser, RRULE expansion including DST in America/Vancouver, identity selection, queue coalescing, retention selection, rule compilation, vCard mapping.
- **Fixture server** (`tests/support/jmapfix.sh`, Bash with socat for TLS, the openssl command for the test CA, and jq for JSON, started and stopped by `make test`): serves recorded JMAP responses from `tests/support/fixtures/*.json` keyed by method and arguments, and scripted cases: Session, Mailbox/Email/Thread get, changes, query, set, `cannotCalculateChanges`, `stateMismatch`, 401, 429 with `Retry-After`, 503, a slow response, an EventSource stream with drops, an upload, a download, and a proxy toggle that cuts the network. TLS with a test CA generated into `build/test-ca/` and passed as the account `ca_file`. Pavillion itself never ships an HTTP server.
- **Stalwart container** (`tests/stalwart/`): `docker compose` running Stalwart **v0.16.26**. That's the latest release on GitHub `stalwartlabs/stalwart`, published 2026-10-11 10:33 PT and checked 2026-10-16. The tag is pinned in `tests/stalwart/VERSION` and is the image tag. Bump it on purpose, never with `latest`. Andrew's production is believed to be 0.16.x, so W-2 records the production version and flags a gap of more than one patch line, two domains (`test-a.local`, `test-b.local`) and five accounts (`andrew@` and `alice@test-a.local`; `bob@`, `carol@`, and `dave@test-b.local`) provisioned through the admin API, seeded mailboxes and a message corpus (`tests/corpus/` including the hostile set), contacts, calendars, and files if supported. Configuration: `tests/stalwart/config.json` names only the RocksDB datastore at `/var/lib/stalwart/` (Stalwart v0.16 ignores TOML); `build/stalwart-data` is mounted there, created by make, and the container runs as the invoking user (`PAV_UID`/`PAV_GID`). The environment sets `STALWART_RECOVERY_ADMIN=admin:$PAV_STALWART_ADMIN_PW` (fallback: a fixed test-only value in the Makefile) and `STALWART_PUBLIC_URL=https://localhost:1443`, so Session URLs carry port 1443. `seed.sh` (curl and jq) idempotently creates over JMAP (`urn:stalwart:jmap`) the certificate, both domains, `SystemSettings` (hostname `mail.test-a.local`, default domain and certificate), restarts the container only when the certificate or settings changed, creates missing accounts, and checks the Session with `--cacert`. All five accounts use the test password `pavtest-pw`; Stalwart rejects passwords under 8 characters. `make it-up` creates the CA and data dir, starts the container, waits for `/healthz/live`, and seeds; `make it-seed` reruns the seed alone; `make it-down` stops it; `make it-reset` also wipes `build/stalwart-data`. TLS: `make it-up` first runs `tests/support/testca.sh build/stalwart-tls`, the same generator `jmapfix.sh` uses, which writes one self-signed `test-ca.crt` and `test-ca.key` (SAN `localhost`, `127.0.0.1`, `test-a.local`, `test-b.local`) and regenerates them when missing or expiring within a day. `compose.yaml` mounts `build/stalwart-tls` read-only at `/etc/stalwart/tls`, which the seeded certificate names. No key is in git. `seed.sh` verifies with `--cacert build/stalwart-tls/test-ca.crt`, and an account added against the container sets `caFile` to that path. `build/` is gitignored. Integration tests run with `PAVILLION_IT=1 make test-it` and only against this container or Andrew's dedicated test mailbox.
- Integration tests create and delete their own mailbox `pavillion-it-*` and never touch other folders.
- Corpus `tests/corpus/`: plain, multipart/alternative, HTML newsletters, cid-image signatures (the pattern from Andrew's bot reports), huge attachments, malformed MIME, non-UTF-8 charsets, and the hostile HTML set. Assertion: nothing executes and no network request is made.
- Frontend: Vitest for modules without a window; Playwright is optional, against a mock-transport web build (the G-4 harness), for keyboard flows, compose, and the iframe isolation check.
- Performance budgets: cold start under 1.5 s with 20 accounts cached; list scroll 60 fps at 100k messages; local search under 100 ms on 200k messages. Each has a named test or a recorded manual measurement in the phase that owns it.

### CI

- `.github/workflows/ci.yml` (beside the existing `main.yml` line-count workflow, which stays): job `fedora` in a `fedora:latest` container runs `dnf install` of the A-INIT package list, `make test`, and `make build`, and uploads `dist/pavillion` as an artifact. Job `macos-core` (added by G-1) builds the core and runs Unity on the fake platform. Job `windows-core` (added 2026-10-19) does the same on `windows-latest` under MSYS2 UCRT64 (gcc, make, cmake with `MSYS Makefiles`): `make test` with `LDFLAGS=-lws2_32` after creating `<drive>:\tmp` for the Unity temp paths, no window, `continue-on-error: true` until G-6. MinGW is the Windows toolchain (G-Q4, resolved 2026-10-19, G Lock). `macos-core` adds `-lcurl` to its `LDFLAGS`. Both catch core portability breaks cheaply.
- The CSP-only failure that started F shows up only in `make build`, so CI always runs `make build`, not just `make test`.
- Integration tests are not in CI until the Stalwart container runs on the runner reliably; then a nightly job runs `test-it`.

### Packaging (was old plan §12)

- One executable, `pavillion`. `make build` builds the release binary and copies it to `dist/pavillion`. Cargo writes `src-tauri/target/release/pavillion` on the way. `build/` is the C library and tests. `ui/dist/` is the page Tauri embeds. `make clean` removes `build/` and `dist/`. V-1 does not split that name. See the V Lock.
- WebKit bytes are never inside the ELF. No static link, no vendored WebKit, no AppImage, and no other bundle that would copy the library in. `bundle.active` is false, so `make build` produces no AppImage, deb, or rpm. `.deb` stays out. `pavillion` has a dynamic `NEEDED` entry for system `libwebkit2gtk-4.1` (and the GTK, JavaScriptCore, and libsoup libraries Tauri needs with it). A command-line start maps those libraries. The rpm depends on `webkit2gtk4.1`, so the process maps the system copy. The bytes stay in that system package. The binary contains neither Chromium nor a copy of WebKit.
- Release compile: LTO, opt-level 3, `panic = "abort"`, strip. No UPX. The rpm is already compressed, UPX raises RSS and startup, and a packed ELF that fails `--version` is a second artifact to distrust. The size that matters for the window is the system WebKit, which stays a dependency and is not copied into the package.
- `ui/dist` gzipped size is recorded in Accomplished when H-2 (CodeMirror), N-3 (Tabulator), and O-2 (SunEditor) land. Those three are accepted. A further UI library needs a Lock change first. The recorded size is not a reason to drop one of the three.
- Dynamic link only, for curl, SQLite, OpenSSL (via curl), Jansson, GMime, libical, libsecret, and libuuid. Lua 5.5 and lexbor are the exceptions: they are vendored (R-1, N-2) so the version is pinned and there is no system package to depend on. Do not static-link the system libraries to "make one file".
- `cargo tauri build` embeds `ui/dist`; the WebView loads it through Tauri's custom protocol. Dev uses the Vite URL. The UI is not a Hydrogen payload: Pavillion has no HTTP server for its own window.
- Updates are a dnf repository fed by the rpm. No in-app updater and no telemetry. The rpm depends on `webkit2gtk4.1`, `libsecret`, `gmime30`, `libical`, `jansson`, `libcurl`, `sqlite-libs`, `libuuid`, and a Secret Service provider (gnome-keyring or KeePassXC, as a weak dependency); `libayatana-appindicator-gtk3` only if the tray is enabled. Lua and lexbor are not dependencies; they are inside the core library.
- Desktop file `com.500nodes.Pavillion.desktop` with `MimeType=x-scheme-handler/mailto;text/calendar;text/vcard;` and `Categories=Network;Email;Calendar;ContactManagement;`. Document `xdg-mime default com.500nodes.Pavillion.desktop x-scheme-handler/mailto`.
- Autostart optional via `tauri-plugin-autostart` (XDG autostart), off by default, with a start-minimized option. Reminders and retention only run while the app runs, so Settings recommends autostart.
- Wayland: no X11-only APIs; test under GNOME (Fedora) and Hyprland (Omarchy). Document `WEBKIT_DISABLE_DMABUF_RENDERER=1` as a fallback for rendering glitches. That variable is not the exit workaround. The GUI sets `WEBKIT_DISABLE_COMPOSITING_MODE=1` when it is unset, before the webview exists, so WebKitGTK 2.52 does not abort its web process during EGL teardown (bug 303336). An inherited value wins. Tray optional (Wayland support varies).
- Fonts: Vanadium Sans is the primary UI font and Vanadium Mono the monospace. Whether they ship in the package or rely on a system install is open (W-Q2); the UI falls back to `system-ui` and `ui-monospace`.

## W-1: CI on GitHub Actions

**Kind:** Medium
**Status:** Complete (2026-10-16).

**Goal:** Every push runs `make test` and `make build` on Fedora.

**Entry gate:** F-1 complete.

**Work items:**

- [x] `.github/workflows/ci.yml` per the CI Lock; cache cargo and npm.
- [x] Add the A-INIT Fedora package list as `scripts/fedora-deps.txt` used by CI and by the README.
- [x] The artifact `pavillion-<sha>` is the `dist/pavillion` binary.

**Done means:** A push shows a green Fedora job.

**Exit gate:** A green run linked in Accomplished. A deliberately broken Unity test turns it red (then revert). Passed.

**Accomplished:** `.github/workflows/ci.yml` runs job `fedora` in `fedora:latest` on every push and pull request. It installs `scripts/fedora-deps.txt`, caches the cargo registry, `src-tauri/target`, and `~/.npm`, runs `make test` and `make build`, and uploads `dist/pavillion` as `pavillion-<full sha>`. The README Build section installs from that file. `main.yml` is unchanged.

Green: [run 38961249214](https://github.com/500Foods/Pavillion/actions/runs/38961249214) at `976e16954727ab0f40cb5168d0234b07421d1532`. Fedora 44. Unity 28 tests, 0 failures. Release build 4m 09s. Artifact `pavillion-976e16954727ab0f40cb5168d0234b07421d1532` (1,904,328 bytes).

Red: [run 38962441480](https://github.com/500Foods/Pavillion/actions/runs/38962441480) at `9c486b5`. `test_api_version_matches_header` failed at `tests/unity/src/core_test.c:1378` (`Expected TRUE Was FALSE`). The Test step failed and the Build step did not run.

Revert green: [run 38962706169](https://github.com/500Foods/Pavillion/actions/runs/38962706169) at `4c176869f4ada1cbc44fc6a1a2399c96ccb6020b`. Unity 28 tests, 0 failures. Release build 1m 56s with a cargo cache hit. Artifact `pavillion-4c176869f4ada1cbc44fc6a1a2399c96ccb6020b` (1,904,328 bytes).

**Lessons learned:** `fedora:latest` was Fedora 44, and its `rustc` compiled edition 2024, so Rust and Node stayed Fedora packages. `pkgconf` does not install the `pkg-config` command; `pkgconf-pkg-config` does. `dnf install wget` and `dnf install npm` resolve to `wget2-wget` and `nodejs-npm`. The cargo cache cut the release build from 4m 09s to 1m 56s. `git` is installed in the job before checkout and is not in the package list. `main.yml` commits `README.md` back to `main` on every push, so a following push has to rebase onto that "Apply automatic changes" commit. `make test` is still the Unity binary. The Tests lock's `cargo test` and Vitest are not in the Makefile yet.

**Handoff:** F-5 is next. The assertion in `test_api_version_matches_header` is `version >= 1` again. Do not add the macOS job here; G-1 does. Do not bump `PAVILLION_API_VERSION` for a new helper. A push to `main` also runs the line-count workflow, which will commit `README.md`.

## W-2: JMAP fixtures

**Kind:** Hard
**Status:** Complete (2026-10-19).

**Goal:** Every sync, compose, rules, and PIM phase can test against a fixture server and a disposable Docker Stalwart (v0.16.26) that serves JMAP, IMAP/SMTP, and CalDAV/CardDAV.

**Entry gate:** K-2 complete.

**Work items:**

- [x] `tests/support/jmapfix.sh` with the Lock's cases and the test CA; `make test` starts it on a free port and passes the URL and CA to the Unity runner through environment variables.
- [x] `tests/stalwart/compose.yaml`, `config.json`, `VERSION`, and `seed.sh` (pins `stalwartlabs/stalwart:v0.16.26`). `seed.sh` provisions the two domains and five accounts through the admin API (Lock) and checks `andrew@test-a.local`'s Session.
- [x] `make it-up`, `make it-seed`, `make it-down`, `make test-it`.
- [~] Record the first real Session and responses from the container into `tests/support/fixtures/` with a `record` mode, scrubbed of secrets. Deferred to M-3, which first runs the container (M-3 work items).
- [~] Seed mailboxes and the `tests/corpus/` messages into the container. `seed.sh` does not do this yet. Deferred to M-3 (M-3 work items).
- [x] A smoke Unity test `test_fixture_server_session`; integration smoke `it_session_lists_mail_capability` is skipped until M-2 (`account.test`) exists.

**Done means:** `make test` runs against the fixture server, and `make it-up && make it-seed && PAVILLION_IT=1 make test-it` passes against Stalwart.

**Exit gate:** Both commands pass on Andrew's machine; `docker ps` shows nothing from the fixture project after `make it-down`.

**Accomplished:** `tests/support/jmapfix.sh` is a Bash HTTPS fixture server (socat, openssl, jq) serving a JMAP Session at `/jmap/session`, a `/.well-known/jmap` redirect, a JMAP API at `/jmap/api`, and an EventSource stream at `/jmap/events`. Scripted cases: `error=401`, `error=429` (with Retry-After), `error=503`, `error=cannotCalculateChanges`, `error=stateMismatch`, `slow=1`, `proxy=cut`/`proxy=restore`, plus `/jmap/upload` and `/jmap/download/<id>`. It generates a test CA into `build/test-ca/` and writes `build/fixture-state.txt` (port, CA path) for `make test`. `tests/stalwart/` has `compose.yaml`, `config.toml` (two domains, five seeded accounts), `VERSION` (v0.16.26), and `seed.sh`. `make test` starts the fixture server via `scripts/run-fixture.sh`, runs Unity with `PAV_FIXTURE_URL` and `PAV_FIXTURE_CA`, then runs Vitest, and stops the fixture on exit. `make it-up`, `make it-seed`, `make it-down`, and `make test-it` targets are defined. Unity test `test_fixture_server_session` verifies the fixture serves a Session with `apiUrl` and `mail` capability. `make test`: 100 Unity tests, 0 failures; 134 Vitest tests, 0 failures.

**Lessons learned:** The fixture state file has two lines (port, CA path); read them with `sed -n '1p'` and `sed -n '2p'`, not `awk '{print $2}'` (the CA line has one field). The `trap` in `run-test.sh` reliably stops the fixture even when Unity fails. Python 3.14 removed `ssl.wrap_socket`; use `SSLContext.wrap_socket` instead.

**Handoff:** M-1 is next (step 46), and needs W-2 complete. The fixture server's port and CA are passed to the Unity runner through `PAV_FIXTURE_URL` and `PAV_FIXTURE_CA`. The `record` mode for capturing real Stalwart responses is not yet implemented; it needs the container running. The integration smoke test is skipped until M-2 implements `account.test`.

## W-3: rpm, dnf repository, desktop integration

**Kind:** Medium
**Status:** Not started.

**Goal:** Pavillion installs and updates on Fedora with `dnf`.

**Entry gate:** N-9 complete. Milestone 3 includes the composer, so `mailto:` has somewhere to go. Packaging does not wait on files or on the CLI.

**Work items:**

- [ ] `packaging/rpm/pavillion.spec` building from a source tarball with `make build`, installing `/usr/bin/pavillion`, the desktop file, icons, and the metainfo file.
- [ ] No UPX step (Packaging Lock).
- [ ] A dnf repository layout (`createrepo_c`), signing key, and a `.repo` file; where it is hosted is open question W-Q1.
- [ ] Autostart setting through `tauri-plugin-autostart`.
- [ ] The `mailto:` registration verified after install.

**Done means:** `dnf install pavillion` from the repo works, and `dnf upgrade` picks up a new build.

**Exit gate:** On a clean Fedora VM: install, launch, mailto opens a composer, upgrade to a bumped version.

## W-4: Arch `PKGBUILD`

**Kind:** Easy
**Status:** Not started.

**Goal:** Omarchy builds Pavillion from source against system libraries.

**Entry gate:** W-3 complete.

**Work items:**

- [ ] `packaging/arch/PKGBUILD` with `depends` on `webkit2gtk-4.1`, `libsecret`, `gmime3`, `libical`, `jansson`, `curl`, `sqlite`.
- [ ] Test under Hyprland.

**Done means:** `makepkg -si` installs a working Pavillion on Omarchy.

**Exit gate:** Andrew runs it on the Omarchy machine.

## Open questions for Andrew

- W-Q1. Fedora rpm only to start, or the Omarchy build from day one? Where is the dnf repo hosted? (old plan Q12)
- W-Q2. Vanadium Sans and Mono: package the font files with the app (license permitting) or rely on a system install? (old plan Q13)
- W-Q3. Resolved 2026-10-16: Docker, with Stalwart pinned to v0.16.26 (the latest GitHub release, checked 2026-10-16). Record the production version at W-2.

## Working log

- 2026-10-16 Opened from old plan §12 and §13 and the 2026-10-15 suggestions (CI with `make build`, a disposable Stalwart with seeded accounts). Decisions: a Python fixture server with a test CA, a pinned Stalwart version, `make it-*` targets, rpm dependency list, UPX fallback by `--version`.
- 2026-10-16 Andrew: the fixtures use Docker. Stalwart pinned to v0.16.26, the latest GitHub release (published 2026-10-11 10:33 PT, checked 2026-10-16). W-Q3 resolved.
- 2026-10-16 W-1 in progress. Added `.github/workflows/ci.yml` (job `fedora` in `fedora:latest`, cargo registry and `src-tauri/target` cached, npm cache is `~/.npm`), `scripts/fedora-deps.txt` (24 names; `dnf install --assumeno` resolved every name on Fedora 43), and the README Build section. Rust and Node are the Fedora packages. `pkgconf-pkg-config` is the package that installs the `pkg-config` command. `git` is installed in the workflow and is not in the package list. No commit and no push. `make test` is 28 Unity tests. The exit gate waits on Andrew's push.
- 2026-10-16 W-1 green run: https://github.com/500Foods/Pavillion/actions/runs/38961249214 at `976e169`. Fedora 44. Unity 28/0. Release build 4m 09s. Artifact `pavillion-976e16954727ab0f40cb5168d0234b07421d1532`. The deliberate red test is still open.
- 2026-10-16 W-1 complete. Red run https://github.com/500Foods/Pavillion/actions/runs/38962441480 at `9c486b5` (`test_api_version_matches_header` failed, Build did not run). Revert green https://github.com/500Foods/Pavillion/actions/runs/38962706169 at `4c17686`, cargo cache hit, release build 1m 56s. F-5 is next.
- 2026-10-18 The packaging lock names `WEBKIT_DISABLE_COMPOSITING_MODE` as the WebKitGTK 2.52 exit workaround. `WEBKIT_DISABLE_DMABUF_RENDERER` stays the Wayland rendering fallback. No phase was started.
- 2026-10-18 Packaging stays one executable. V-1 does not add `pavillion-gui`. A command-line start maps system WebKit. No phase was started.
- 2026-10-19 W-2 complete. Added `tests/support/jmapfix.py` (HTTPS fixture server with test CA, JMAP Session at `/jmap/session`, `/.well-known/jmap` redirect, JMAP API at `/jmap/api`), `tests/stalwart/` (compose.yaml, config.toml with test-a.local and test-b.local and five accounts, VERSION v0.16.26, seed.py), `scripts/run-fixture.sh`, `scripts/run-test.sh`, Makefile `test`, `it-up`, `it-seed`, `it-down`, and `test-it` targets, and Unity test `test_fixture_server_session`. `make test`: 100 Unity tests + 134 Vitest tests, 0 failures.
- 2026-10-19 Fixtures moved to the AGENTS.md Bash toolset. `tests/support/jmapfix.py` is now `tests/support/jmapfix.sh` (socat `OPENSSL-LISTEN` forks one Bash handler per connection; openssl makes the test CA; jq builds the JSON; cut, event count, and blobs live in `build/fixture-run/`), and `tests/stalwart/seed.py` is now `tests/stalwart/seed.sh` (curl and jq). `scripts/run-fixture.sh`, `make it-seed`, the macOS CI brew line, and `scripts/fedora-deps.txt` (socat, openssl, jq) follow. Same routes, status codes, headers, and bodies. `POST /jmap/proxy?restore=1` now works while cut, and `proxy=cut`/`proxy=restore` and `error=cannotCalculateChanges`/`error=stateMismatch` on the URL now do what this Lock says.
- 2026-10-19 W-2's seed line corrected: `seed.sh` only sends `Capability/get`. Seeding mailboxes and the `tests/corpus/` messages is deferred to M-3.
- 2026-10-19 Pre-M-3 fix: per-account `caFile` (pinned CA path) in the account object, `account.add`/`update`/`test`, and the Accounts form's Advanced field; Stalwart test certs generated by `make it-up` into `build/stalwart-tls/` and mounted read-only.
- 2026-10-19 Added the `windows-core` CI job (MSYS2 UCRT64, `make test`, no window, `continue-on-error` until G-6), beside `macos-core`. G-Q4 stays open.
- 2026-10-19 CI portability: `windows-core` links `ws2_32` and creates `<drive>:\tmp`; `macos-core` links `-lcurl`. G-Q4 resolved as MinGW.
- 2026-10-19 Stalwart test server moved to v0.16 config: `config.toml` replaced by `config.json` (datastore only) and API seeding in `seed.sh`; `make it-up` auto-seeds; data in `build/stalwart-data`; test password `pavtest-pw`.

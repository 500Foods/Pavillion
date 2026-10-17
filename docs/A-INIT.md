# A — INIT

**Status:** A-5 complete (2026-10-14). A-6 was already complete. No further phase in this document.
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

A running Pavillion: one executable, one window, the C core started. JSON fields are the lock in [B-CONFIG.md](B-CONFIG.md). Threads and logging are the lock in [C-CORE.md](C-CORE.md). The frame is the lock in [D-UI.md](D-UI.md).

## Phases

One phase is one session. Do not start the next one in the same conversation.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| A-1 | C library skeleton and Makefile | Easy | Complete |
| A-2 | Unity harness | Easy | Complete |
| A-3 | Tauri and Vite window | Medium | Complete |
| A-4 | Link the C library into the shell | Hard | Complete |
| A-5 | Working directory and a real start | Medium | Complete |
| A-6 | `--help` and `--version` | Easy | Complete |

Kind: **Easy** is one module and a Unity test, or a static page. **Medium** crosses one boundary (the window, the filesystem, a thread). **Hard** is the first join of two build systems. A phase that does not finish is split, not carried into the next session as a second phase.

## How a session works

1. Read Resume in `PAVILLION_PLAN.md`, the Status column above, and the previous phase's Status, Accomplished, Lessons learned, and Handoff.
2. Re-read only this phase: Goal, Work items, Done means, Exit gate. Open the lock section this phase cites.
3. Ask the qualifying questions. Wait for approval before editing source.
4. Do this phase only. Check a work item `[x]` only after its check has passed. Defer with `[~]`, one line, and the phase it moves to.
5. Before marking complete, write Status, Accomplished, Lessons learned, and Handoff, and append the Working log. Before that, update `ui/src/help/pages.js` for what this phase changed: augment, replace, fix, or extend the user page, or add a Developer section when the user page has nothing new. Then stop.
6. A sitting that ends early sets Status to **in progress**, names the last finished work item, and leaves the rest unchecked.

Leave Accomplished, Lessons learned, and Handoff off a phase that has not started.

## Lock

### Rust is minimal (2026-10-16)

- Pavillion will later be built into Hydrogen, which is C. `src-tauri` does only Tauri work: the window, the webview, dialogs, the plugins, and one generic forwarding command and event. All logic is in the C core. This is the [G-LAYERS.md](G-LAYERS.md) L3 Lock. The A-4/A-5 Rust code that does more (`lib.rs` window state, the prefs struct, workdir creation) is historical, and G-8 moves it into C. Do not add to it.

### Done when A-5 is complete

- `make dev` builds the C library, starts Vite, and opens a Pavillion window.
- No argument uses `~/.local/share/pavillion`. One argument is the working directory. A second argument is an error.
- A first run creates `Pavillion.json` and `profiles/default.json`, logs `core ready`, and the window shows the profile name `Default` and the state `ready`.
- `make test` runs the Unity suite and it passes.
- Quitting the window calls `pavillion_stop` and the process exits.

### Layout

```
Pavillion/
  Makefile
  PAVILLION_PLAN.md
  docs/
  core/
    CMakeLists.txt
    include/pavillion.h
    src/
  src-tauri/
    Cargo.toml
    tauri.conf.json
    build.rs
    src/main.rs
  ui/
    package.json
    vite.config.js
    index.html
    src/main.js
    src/api.js
    styles/theme.css
  tests/unity/
    framework/          ThrowTheSwitch Unity, vendored
    src/
```

The repo does not build against the Philement tree. Unity is a copy of the framework Hydrogen uses.

### Tools and make

Fedora packages: `gcc`, `cmake`, `make`, `pkgconf`, the Tauri 2 Linux prerequisites (WebKitGTK 4.1 and whatever its current guide lists), `jansson-devel`, `libcurl-devel`, `sqlite-devel`, `openssl-devel`, `libsecret-devel`, `libuuid-devel`, `gmime-devel`, `libical-devel`, plus Rust and Node. Do not pin versions here. Lua 5.5 (R-1) and lexbor (N-2) are vendored under `third_party/`; they are not Fedora packages. The system libraries stay dynamic (W Lock). `libuuid` stays until a phase shows nothing calls it.

The core is C11. The shell identifier is `com.500nodes.pavillion`.

| Target | What it does |
| --- | --- |
| `make dev` | Build `libpavillion_core.a`, then `cargo tauri dev` |
| `make test` | Build the Unity runner and run it |
| `make build` | Release core, then `cargo tauri build` |

The Makefile is the entry point. `build.rs` also runs CMake, so a direct cargo invocation still links.

### Process

The shell owns the process and the window. The core owns the working directory. `~` is expanded by the invoking shell, not by Pavillion. A path that is not a writable directory becomes state `error` and the window shows that string.

## A-1: C library skeleton and Makefile

**Kind:** Easy
**Status:** Complete (2026-10-14). No variances. `pavillion_status` returns `-1` when passed NULL. That return was not in the work items. The filled struct matches them.

**Goal:** A static library with the three public functions, built by `make test`'s CMake step, before Unity exists.

**Entry gate:** None.

**Work items:**

- [x] Add `core/CMakeLists.txt`, `core/include/pavillion.h`, and `core/src/pavillion.c`.
- [x] `pavillion_start` ignores the path, writes nothing, and returns `0`. Status state is `ready`, profile name and error are empty strings. `pavillion_stop` is safe to call once.
- [x] A Makefile target configures and builds `libpavillion_core.a`. `make dev` and `make build` may print that the shell is not here yet. They must not fail the library build.

**Done means:** `make` produces `libpavillion_core.a` and the three symbols are in the archive.

**Exit gate:** `make` exits 0, and `nm` on the archive lists `pavillion_start`, `pavillion_stop`, and `pavillion_status`. Passed. `nm build/libpavillion_core.a` lists `pavillion_start`, `pavillion_stop`, and `pavillion_status` as text symbols. `make`, `make dev`, `make build`, and `make test` all exited 0.

**Accomplished:** `core/CMakeLists.txt`, `core/include/pavillion.h`, `core/src/pavillion.c`, and `Makefile`. CMake writes `build/libpavillion_core.a`. `make dev` and `make build` build that archive and print that the Tauri shell is not in A-1. `make test` builds it and prints that Unity is not in A-1. `.gitignore` ignores `build/`.

**Lessons learned:** The Make target `build` and the directory `build/` share a name. `build` is `.PHONY`, so Make does not treat the directory as an up-to-date target. A-2 should keep that declaration if it adds recipes. The compiler on this machine is GCC 15.3.1 via ccache.

**Handoff:** A-2 vendors Unity and links `build/libpavillion_core.a`. Do not change the three signatures. `pavillion_stop` is already safe to call twice. `pavillion_status` zeroes the struct, sets `state` to `ready`, and returns `-1` on a NULL pointer. Start still ignores the path and writes no files.

## A-2: Unity harness

**Kind:** Easy
**Status:** Complete (2026-10-14). No variances. The test also asserts that start and status return `0`. Those returns were already the A-1 contract.

**Goal:** The same Unity framework Hydrogen uses runs one test against the stub library.

**Entry gate:** A-1 complete.

**Work items:**

- [x] Vendor ThrowTheSwitch Unity under `tests/unity/framework`. Do not point the build at the Philement tree.
- [x] `tests/unity/src/core_test.c` calls start, asserts state `ready` and an empty profile name, calls stop, and calls stop a second time.
- [x] `make test` builds the runner and runs it.

**Done means:** `make test` prints a passing Unity result for that one test.

**Exit gate:** `make test` exits 0. Passed. A clean `make test` configured CMake, built `build/libpavillion_core.a`, compiled `build/core_test`, and printed `1 Tests 0 Failures 0 Ignored` and `OK`. `make`, `make dev`, and `make build` still exit 0.

**Accomplished:** Unity 2.6.2 sources `unity.c`, `unity.h`, and `unity_internals.h` are copied under `tests/unity/framework/src/`, with `tests/unity/framework/LICENSE.txt`. `tests/unity/src/core_test.c` is the runner. `make test` compiles that runner against `build/libpavillion_core.a` and runs it. The three public signatures are unchanged.

**Lessons learned:** The compile set is enough. The Ruby generators, examples, and extras were not copied, and the build includes only `tests/unity/framework/src`. A hand-written `main` calls `UNITY_BEGIN`, `RUN_TEST`, and `UNITY_END`. `setUp` and `tearDown` are empty functions in the test file. GCC 15.3.1 accepts those Unity sources with `-Wall -Wextra -Wpedantic`. The runner binary stays in `build/`, which `make clean` removes and `.gitignore` already ignores. `build` is still `.PHONY`.

**Handoff:** A-3 does not link this library and does not run the runner. `make test` must keep passing. Do not change the three signatures. The one test is `test_start_reports_ready_and_stop_is_repeatable`. A-5 replaces its expectations with a temp directory, profile name `Default`, and the log line `core ready`. Includes are `-Icore/include` and `-Itests/unity/framework/src`. Do not point them at the Philement tree.

## A-3: Tauri and Vite window

**Kind:** Medium
**Status:** Complete (2026-10-14). No variances in the window. `make dev` runs Tauri CLI 2.12.3 from `ui/node_modules` (`npm exec tauri`), which runs `cargo run`. There is no global `cargo-tauri` install.

**Goal:** `make dev` opens a window. The page is static. The C library is not linked yet.

**Entry gate:** A-1 complete. A-2 may still be open. The window does not depend on the test runner.

**Work items:**

- [x] Add `ui/` with Vite, `index.html`, and a page that contains the word Pavillion.
- [x] Add `src-tauri/` for Tauri 2, identifier `com.500nodes.pavillion`, dev URL pointed at Vite.
- [x] `make dev` starts Vite and `cargo tauri dev`.

**Done means:** A window opens and the word Pavillion is visible.

**Exit gate:** `make dev` opens that window. Close it and the process exits. Passed. Vite served `http://localhost:1420/` with the word Pavillion in the body. The window was 1200 by 800, titled Pavillion, and the word was visible. Closing it exited `make dev` with status 0, and port 1420 closed. The C library is not linked.

**Accomplished:** `ui/` is a Vite app (`index.html`, `src/main.js`, `vite.config.js`). `src-tauri/` is Tauri 2, identifier `com.500nodes.pavillion`, dev URL `http://localhost:1420`, window title Pavillion. `make dev` installs the UI packages, starts Vite, and runs the Tauri CLI. `make build` is wired to `tauri build`. `make test` still passes.

**Lessons learned:** The WebKit runtime packages are not enough to compile. The first missing file was `libsoup-3.0.pc`. Tauri's Fedora set (`webkit2gtk4.1-devel`, `librsvg2-devel`, and the packages listed with them) provides the headers. npm 11 warned that esbuild's install script is outside `allowScripts`; Vite still served the page. The repo-local Tauri CLI is enough. A global `cargo tauri` subcommand is not required.

**Handoff:** A-6 adds `--help` and `--version` in the shell, before the window opens and before any core call. The core is still not linked. The dev binary is `src-tauri/target/debug/pavillion`. Do not change the page. After the flags work, `make dev` must still open this window. D-1 is the phase that adds the dark frame.

## A-4: Link the C library into the shell

**Kind:** Hard
**Status:** Complete (2026-10-14). No variances. The page calls the command from `main.js`. D-2 moves that import into `api.js`.

**Goal:** The Tauri process calls `pavillion_status` and the window shows the stub state.

**Entry gate:** A-2 and A-3 complete.

**Work items:**

- [x] `src-tauri/build.rs` builds the CMake library and links it.
- [x] A Tauri command `status` calls `pavillion_status` and returns `profileName`, `profileId`, `state`, and `error`.
- [x] The page shows the `state` string from that command. With the A-1 stub, that string is `ready`.

**Done means:** The window shows `ready`, and that word came from the C function, not from a literal in the page.

**Exit gate:** `make dev` shows `ready`. `make test` still exits 0. Passed. The body showed `ready`. `ui/` has no literal `ready`. `nm` lists `pavillion_status` in `src-tauri/target/debug/pavillion`. `make test` printed `OK`. `--version` still prints `pavillion 0.1.0 (2026-10-14)` and does not open a window. Closing the window made `make dev` exit 0.

**Accomplished:** `build.rs` runs CMake, links `build/libpavillion_core.a`, and registers the `status` command so Tauri generates `allow-status`. `src-tauri/src/lib.rs` calls `pavillion_status` and returns the four camelCase fields. The core is not started. `CMAKE_POSITION_INDEPENDENT_CODE` is on so the static library links into the PIE binary.

**Lessons learned:** Rust's link is PIE. Without position-independent C objects the link fails with `R_X86_64_32` against `.rodata`. Tauri does not create `allow-status` unless `build.rs` lists the command on `AppManifest`. The generated file is `src-tauri/permissions/autogenerated/status.toml`, and it is gitignored. The stub fills `ready` without `pavillion_start`.

**Handoff:** D-2 moves the `@tauri-apps/api` import from `ui/src/main.js` into `ui/src/api.js`, calls `status()` once, and writes `profileName` and `state`. The name is blank with this stub. Do not call `pavillion_start`. The flag check stays in front of the window. A-5 is the phase that starts the core.

## A-5: Working directory and a real start

**Kind:** Medium
**Status:** Complete (2026-10-14). No variances.

**Goal:** The stub is gone. Start loads a profile, and the window shows its name.

**Entry gate:** B-4, C-6, and D-2 complete.

**Work items:**

- [x] The shell passes no argument as `~/.local/share/pavillion`, one argument as the working directory, and rejects a second argument before calling the core.
- [x] Replace the A-1 stub with `pavillion_start` from C-6. The Unity test from A-2 is updated to the C-6 behavior: a temp directory, profile name `Default`, log line `core ready`.
- [x] Quitting the window calls `pavillion_stop`.

**Done means:** A first run on a fresh directory shows `Default` and `ready`, and `make test` passes the updated test.

**Exit gate:** `make test` exits 0. Passed: 21 tests, 0 failures. `make dev DEV_DIR=/tmp/pav-dev-W5Ep5Q` showed Pavillion, Default, and ready in a 1200×800 window. `logs/default.log` contained `INFO  core  core ready`. Closing the window made `make dev` exit 0. Two positional arguments and an unknown flag print help to stderr and exit 1. `--help` and `--version` still exit 0 before a window.

**Accomplished:** `main.rs` resolves the working directory and `lib.rs` calls `pavillion_start` before the window exists. `RunEvent::Exit` calls `pavillion_stop`. A missing directory is created mode `0700`. The profile log is opened for append when no test output is set.

**Lessons learned:** Tauri forwards application arguments only after a second `--`. `make dev DEV_DIR=<path>` runs `tauri dev -- -- <path>`. One `--` put the path in front of cargo's own flags, the binary treated those flags as unknown, and it exited before the window. The default directory is `$HOME/.local/share/pavillion`. A path on the command line is not tilde-expanded. The Unity start test was updated in C-6 and still passes.

**Handoff:** D-3 clears the profile name when state is `error`. The page still calls `status()` once and does not call `pavillion_start`.

## A-6: `--help` and `--version`

**Kind:** Easy
**Status:** Complete (2026-10-14). No variances. `-h` is an unknown flag. The phase names the long forms only.

**Goal:** The one executable answers `--help` and `--version` without opening a window.

**Entry gate:** A-3 complete. The binary exists. A-4 and A-5 may still be open.

**Work items:**

- [x] `pavillion --help` prints the usage (`pavillion [WORKDIR]`), the version, and the release date. A git commit is included when the build knows one.
- [x] `pavillion --version` prints the version line and nothing else.
- [x] Neither flag opens a window or starts the core. An unknown flag prints the same help to stderr and exits non-zero.

**Done means:** Both flags print the version string baked into that build, and a normal launch still opens the window.

**Exit gate:** Run `pavillion --help` and `pavillion --version` from the build output. Then `make dev` still opens the window. Passed. From `src-tauri/target/debug/pavillion`, `--help` printed `pavillion 0.1.0 (2026-10-14)`, `commit fb0230f48bc0`, and `Usage: pavillion [WORKDIR]`, and exited 0. `--version` printed only `pavillion 0.1.0 (2026-10-14)`. `--nope` printed the same help on stderr, wrote nothing to stdout, and exited 1. No window appeared. `make dev` then opened the same 1200 by 800 window, the word Pavillion was visible, and closing it exited 0. `make test` still passes.

**Accomplished:** `src-tauri/src/main.rs` handles the flags before `pavillion_lib::run`. `src-tauri/build.rs` bakes the release date `2026-10-14` and, when git succeeds, a 12-character commit from `git rev-parse --short=12 HEAD`. The core is not linked and is not called.

**Lessons learned:** The version line is one string, `pavillion <version> (<release date>)`. `--version` prints that line alone. `--help` adds the commit line and the usage line. The release date is a constant in `build.rs`, not the clock at compile time. A flag mixed with any other argument is an unknown flag. A positional path is not a flag, so it still opens the window. A-5 owns that argument.

**Handoff:** D-1 adds the dark frame and does not move this flag code. A-4 links the C library and must leave the flag check in front of the window and in front of any core call. The dev binary remains `src-tauri/target/debug/pavillion`.

## Working log

- 2026-10-14 A-1: C11 static library and Makefile. Archive is `build/libpavillion_core.a`. Status returns `-1` on NULL. Unity and Tauri are not in this phase.
- 2026-10-14 A-2: Unity 2.6.2 vendored under `tests/unity/framework`. `make test` runs `build/core_test` and prints `OK`.
- 2026-10-14 A-3: Vite page and Tauri 2 shell. `make dev` opened a 1200 by 800 window titled Pavillion, the word was visible, and closing it exited the process. WebKit devel headers were required to compile.
- 2026-10-14 A-6: `--help` and `--version` exit before the window. Version line is `pavillion 0.1.0 (2026-10-14)`. Unknown flags print that help on stderr and exit 1.
- 2026-10-14 A-4: The shell links `libpavillion_core.a`. The window body shows `ready` from `pavillion_status`. The C objects are built position-independent.
- 2026-10-14 A-5: The shell calls `pavillion_start` with the working directory and `pavillion_stop` on quit. A fresh directory showed Default and ready. The profile log contains `core ready`.

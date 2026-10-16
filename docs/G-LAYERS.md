# G — Layers, platforms, Lithium, and Lua

**Status:** Design, not started (opened 2026-10-15). No phase is open.
**Resume:** Read the Lock. G-1 is the first phase and follows F-5. Where this document and §2 or §17 in [PAVILLION_PLAN.md](../PAVILLION_PLAN.md) disagree, this document wins.

Three goals:

1. Pavillion runs on Linux first, then macOS and Windows, and maybe iPad one day.
2. The UI can be lifted into Lithium.
3. Lua is embedded, as in Hydrogen.

All three come down to the same thing: clear layers with one written contract between each pair.

## Layers

```
+------------------------------------------------------------+
| L4  UI (ui/)        vanilla JS, HTML, CSS. Host-agnostic.   |
|       talks only to the Pavillion API object (api.js)       |
+------------------------------------------------------------+
| L3  Host adapter    Tauri today (src-tauri). Lithium later. |
|       maps the API to the core C ABI; window, dialogs       |
+------------------------------------------------------------+
| L2  Core (core/)    portable C11: config, store, sync,      |
|       JMAP, MIME, sanitize, log, Lua. No OS calls.          |
+------------------------------------------------------------+
| L1  Platform (core/platform/<os>/)  one function table:     |
|       dirs, secrets, threads/time, fs modes, notify hook,   |
|       single-instance, network-change, power              |
+------------------------------------------------------------+
```

## Lock

### L1 Platform

- The core includes `pav_platform.h` and nothing OS-specific. `#ifdef __linux__` appears only under `core/platform/`.
- `core/platform/posix/` holds what Linux and macOS share (pthreads, file modes). `core/platform/linux/` holds libsecret and XDG dirs. `macos/`, `windows/`, and `ios/` come later, using Keychain, Credential Manager, `%APPDATA%`, and Win32 threads or a pthreads shim.
- The platform layer is a struct of function pointers installed at `pavillion_start`, so the Unity tests can install a fake platform (an in-memory secret store and temporary directories). The tests never touch the real keyring.
- Paths are built only through `pav_platform->dir(PAV_DIR_CONFIG | DATA | CACHE | LOG | RUNTIME)` and a join helper. Code never contains `/` literals or `~` expansion.
- The B-4 file-mode code (0600, 0700) moves behind the platform layer. On Windows it becomes an ACL or a no-op.

### L2 Core and its ABI

- The core's public surface is one header, `core/include/pavillion.h`, plus the structs it declares. Everything the UI can do is a **command**: a name and a JSON argument in, and a JSON result or a JSON event out. That is how icons and the kit work today. Fixed structs are kept only where speed matters, and F-1 guards them.
- Events (sync progress, new mail, log lines) go out through one registered callback, `pav_on_event(cb, user)`, as JSON. The host decides how to deliver them (Tauri `emit`, Lithium's own channel).
- The command and event list is a written contract, `docs/API.md`, that the UI and every host share. A command is not real until it is listed there.
- The core has no Tauri or Rust types, no WebView assumptions, and no UI strings beyond error text.

### L3 Host adapter

- `src-tauri` becomes thin. It has one generic `invoke("core", {cmd, args})` that forwards to the core, plus the window, dialog, deep-link, and tray work the core can't do. Per-command Rust handlers are replaced as commands move to the JSON dispatcher.
- Lithium is a second host. It loads the same `ui/` and supplies its own `api.js` transport. Nothing in `ui/` may import `@tauri-apps/*` except the Tauri transport file.

### L4 UI

- `ui/src/api.js` is the API object. It is transport-agnostic and is created with a transport: `createApi(tauriTransport)` or `createApi(lithiumTransport)`. Screens never see the transport.
- The UI is one ES module root with no global singletons, can be mounted into a given element (`mount(el, api)`), and leaves `document` and `window` alone outside that root. That is what lets Lithium embed it.
- CSS variables are namespaced (`--pav-*`) and the styles are scoped under the mount root, so they don't fight Lithium's own CSS.
- Font Awesome loading belongs to the host page, not the UI module. Lithium may already load its own kit, so the UI only expects the `fa-*` classes to resolve. See the [F-HARDEN.md](F-HARDEN.md) Lock.

### Lua

This replaces "Lua left behind" in §2 and §17.4.

- Lua 5.5, embedded in the core (L2) and linked statically from source in the tree so every platform builds the same version. It is the same generation Hydrogen uses, and the `H`-style host table becomes `P` here.
- Uses: message rules and filters (sorting on arrival), computed views and saved searches, templates and signatures, small automations and the hooks behind the §16 CLI, and user-defined actions in the UI. Lua is not a plugin system. §1's "no add-ons" stands: scripts run against a fixed, documented `P` API, not against internals.
- Sandbox: no `os`, `io`, `package.loadlib`, `require` of C modules, or `debug`. There is an instruction-count hook and a memory cap per run. Each run gets a fresh state or a pooled state reset between runs. Scripts only reach the network through `P.*` calls the core mediates and logs.
- Threading: Lua never runs on the UI thread or inside a sync worker's critical path. It runs on a script worker fed by the command queue, with an async-handle plus `P.wait` pattern borrowed from Hydrogen's `H.wait`.
- Sending: `P.mail.send` creates a **draft** only, consistent with §16. Nothing a script does sends mail without a user action or a per-script permission the user grants explicitly.
- Scripts live in the profile (`scripts/` with a manifest in `Pavillion.json`), get edited in CodeMirror 6 (already planned), and are logged by name and run id.
- Reference docs follow Hydrogen's split: `docs/LUA_API.md` for the `P` table, linking to Hydrogen's [LUA_FEATURES.md](/mnt/extra/Projects/Philement/docs/H/LUA_FEATURES.md) for the language itself, so it isn't duplicated.

### Mail rules

Mail rules are Lua's first job.

- **Two tiers, one engine.** Most rules are declarative JSON in the profile, built in a rules editor: conditions on from, to, cc, list-id, subject, header, size, account, folder, or has-attachment (matching by equals, contains, glob, or regex), and actions to move, copy, tag/keyword, flag, mark read, set priority, or stop. The core compiles each declarative rule to a Lua function. Advanced rules are raw Lua against the same `P.rule` API. Only one engine runs, and both tiers behave the same way.
- **When rules run:** on arrival, after sync stores a message and before it is announced to the UI, so a moved message never flashes in the Inbox. They also run on demand ("apply rules to this folder"), and as a dry run that lists what would happen without changing anything.
- **Effects are batched.** A rule returns intents (move X to Y, set keyword Z). The core applies them as JMAP `Email/set` batches through sync, so scripts never call the network directly and a run can be undone from the log.
- **Order and stop:** rules run in a fixed order per account, with an optional global set first. `stop` ends evaluation for that message.
- **Safety:** this uses the same sandbox and caps as all Lua. A rule that errors or hits the time cap is disabled with a banner, and the message is left untouched. No rule can send mail. Reply and forward actions only create drafts.
- **Server side:** a declarative rule that maps cleanly to Sieve (`fileinto`, `addflag`, header and address tests) can be exported to Stalwart through JMAP Sieve (`urn:ietf:params:jmap:sieve`), so it applies even when Pavillion is closed. Raw-Lua rules stay client-side. Each rule shows where it runs, and the same rule is never active in both places.
- **Testing:** Unity tests feed fixture messages through compiled rules and assert the intents. The rules editor shows the generated Lua read-only, so advanced users can start from it.

## Phases

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| G-1 | `pav_platform.h`, the Linux backend, and a fake for tests. Move dirs and file modes behind it | Medium | Not started |
| G-2 | JSON command dispatcher, `pav_on_event`, and `docs/API.md`. Move icons, kit, and UI prefs onto it | Medium | Not started |
| G-3 | Generic Tauri `core` command. `api.js` takes a transport. No `@tauri-apps` import outside the transport | Medium | Not started |
| G-4 | UI mountable: `mount(el, api)`, `--pav-*` variables, scoped styles. A bare HTML harness mounts it without Tauri | Medium | Not started |
| G-5 | Lua 5.5 vendored into the core build with the sandboxed state, the `P.log`/`P.system` table, a script worker, and Unity tests | Medium | Not started |
| G-7 | Mail rules: declarative JSON compiled to Lua, on-arrival run, dry run, batched intents | Medium | Not started |
| G-6 | Second platform smoke build (macOS or Windows) of the core and `make test` only. No packaging | Hard | Not started |

G-1 through G-4 go before mail, because mail would otherwise grow on the current Tauri-shaped boundary. G-5 can follow mail P0. G-7 (mail rules) follows G-5 and needs real sync. G-6 is whenever a second machine is available.

## Open questions for Andrew

1. Lithium host: does Lithium run the C core itself (WASM, native module, or a server-side Hydrogen-like process), or does Pavillion's UI talk to a running Pavillion over a socket or HTTP? This decides what `lithiumTransport` is.
2. Lua: one fresh state per run (simple and safe) or pooled states (faster rules on bulk sync)?
3. Sieve export: does the Stalwart version in use advertise `urn:ietf:params:jmap:sieve`, and should simple rules default to the server or to the client?
4. Windows: MSVC or MinGW for the core? It affects the pthreads choice in L1.

## Working log

- 2026-10-15 Opened from a design conversation: platform split, Lithium embedding, Lua. Not a phase.

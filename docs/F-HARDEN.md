# F — Hardening

**Status:** Not started (opened 2026-10-15 from a project review). No phase is open.
**Resume:** F-1 is the next phase in this document. Read the Lock, then F-1 only.

These are structural fixes the review on 2026-10-15 found after D-3. They go in before mail is real, because each gets more expensive once JMAP, accounts, and credentials sit on top of it. Where this document and §2, §7, or §11 in [PAVILLION_PLAN.md](../PAVILLION_PLAN.md) disagree, this document wins.

## Phases

One phase is one session. Do not start the next one in the same conversation.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| F-1 | One definition of the FFI structs | Medium | Not started |
| F-2 | UI preferences as an opaque JSON blob | Medium | Not started |
| F-3 | The kit setting is an id, not HTML | Easy | Not started |
| F-4 | Message pane isolation, proven before mail | Medium | Not started |
| F-5 | Trim `config.c` | Medium | Not started |
| F-6 | Decide where JMAP and sync live | Easy (decision) | Not started |

F-6 is a written decision, not code. It must be closed before the first mail letter is opened.

## How a session works

Same as [D-UI.md](D-UI.md): read Resume, re-read only this phase, ask the qualifying questions, wait for approval, do this phase only, then write Status, Accomplished, Lessons learned, Handoff, and the Working log line, and stop.

## Lock

### Font Awesome stays a kit

- Font Awesome Pro is loaded **only** through the official kit (`45801522ef`, Pro Plus, SVG/JS method). It is an open-source project, and the kit is the only distribution the license allows. Do not copy Pro SVGs, CSS, or font files into the tree, and do not vendor the kit loader. Do not "fix" CSP problems by self-hosting.
- The kit loader fetches `pro-plus.min.js` from `ka-p.fontawesome.com` with `fetch()` and runs it as an **inline** `<script>`. That is why `script-src` carries `'unsafe-inline'` in the release build. `make dev` never shows the problem, because the Vite page is not served with the Tauri CSP.
- `'unsafe-inline'` is only effective because `dangerousDisableAssetCspModification` is `true`. If Tauri adds a hash or a nonce to `script-src`, browsers ignore `'unsafe-inline'` and the icons vanish again in `make build` only. Keep the flag, and leave this note beside it.
- `font-src 'self' data: https://ka-p.fontawesome.com` is set because the kit has the v4 and v5 font-face shims on.
- Consequence: the main window can run inline script. So nothing untrusted may ever reach the main document's DOM as HTML. Mail, contact notes, calendar descriptions, file previews, server error text, and settings values are inserted with `textContent` or rendered in the sandboxed pane. `innerHTML` with outside data is a review failure.

### Message pane

- All outside HTML renders in `<iframe sandbox srcdoc=…>` with no `allow-scripts`, no `allow-same-origin`, no `allow-forms`, no `allow-popups`, and its own CSP meta as in §7. The main window's CSP, and its Font Awesome hosts, never apply inside it.
- The frame has no Tauri IPC. Capabilities grant IPC to the main window origin only.
- Icons inside a message, if ever needed, are drawn by the parent around the frame, never inside it.

### FFI

- A struct crossing C and Rust has one definition, in a C header. Rust gets it from `bindgen` in `src-tauri/build.rs`, or, if bindgen is refused, from hand mirrors guarded by a size and offset check at startup.

## F-1: One definition of the FFI structs

**Kind:** Medium
**Status:** Not started.

**Goal:** `PavillionStatus`, `PavShellPrefs`, and every future shared struct are defined once, in C. A field added on one side only fails the build or the start, never corrupts memory.

**Work items:**

- [ ] Generate the Rust declarations with `bindgen` from `core/include/pavillion.h` in `build.rs`, and delete the hand mirrors in `src-tauri/src/lib.rs`.
- [ ] If bindgen is not wanted: export `pav_abi_sizes()` from C (size of each shared struct plus a version number) and have the shell refuse to start on mismatch, with a clear error.
- [ ] Add a Unity test for the C side of the check.

**Done means:** Adding a field to a C struct without touching Rust either builds correctly (bindgen) or stops at launch with a named error.

## F-2: UI preferences as an opaque JSON blob

**Kind:** Medium
**Status:** Not started.

**Goal:** A new UI knob (a slider, a split, a font) touches only `ui/` and does not change the C header, the Rust struct, or the `PAV_PREFS_*` mask.

**Work items:**

- [ ] The core stores `preferences.ui` as a JSON object it validates for shape (an object, a size limit) and otherwise does not interpret, like icons and the kit already are.
- [ ] Move zoom, splits, nav width, chrome, and fonts into that object. The core keeps only what it acts on itself (window geometry, if the shell restores it before the page loads).
- [ ] Migrate an existing profile on load. Keep a test that reads an old profile.

**Done means:** The Fonts setting is persisted with no C or Rust change.

## F-3: The kit setting is an id, not HTML

**Kind:** Easy
**Status:** Not started.

**Goal:** The Settings field can no longer inject script into the main window, which runs inline script because of the kit.

**Work items:**

- [ ] Accept only a kit id matching `^[0-9a-f]{10}$`, in the UI and again in `pavillion_kit_apply`. Reject anything else with an error, not a silent fallback.
- [ ] Remove the raw-HTML branch from `loadFontAwesomeKit` in `ui/src/main.js`, and the help text that offers a `<script>` tag.
- [ ] The kit URL is always built as `https://kit.fontawesome.com/<id>.js`.
- [ ] Icon definitions stay class strings. Raw SVG in an icon field is parsed with `DOMParser` as `image/svg+xml`, and only `svg`, `path`, `g`, `circle`, `rect`, and `title` elements with presentational attributes are kept. Otherwise, drop the raw-HTML option.

**Done means:** Pasting `<script>alert(1)</script>` into the kit or an icon field does nothing but show an error.

## F-4: Message pane isolation, proven before mail

**Kind:** Medium
**Status:** Not started.

**Goal:** The sandboxed pane exists and is tested with hostile HTML before any real message is shown.

**Work items:**

- [ ] Add the iframe to the mail detail pane with the attributes and CSP in the Lock.
- [ ] A fixture page with `<script>`, `onerror`, `javascript:` links, `<meta refresh>`, a remote `img`, and a `<form>` renders with none of them active.
- [ ] Check in the release build (not `make dev`) that `window.__TAURI__` and `window.parent.__TAURI__` are unreachable from the frame, and that the main window's `'unsafe-inline'` does not apply inside it.
- [ ] Capabilities file: IPC for the main window origin only.

**Done means:** The fixture renders as inert text and layout in `make build`.

## F-5: Trim `config.c`

**Kind:** Medium
**Status:** Not started.

**Goal:** `core/src/config.c` (about 1,500 lines after B-5) gets a small helper layer before mail settings, accounts, and JMAP endpoints are added to it.

**Work items:**

- [ ] Measure how much is repeated jansson get, type check, and default code.
- [ ] Introduce field-table or helper functions (`cfg_get_str`, `cfg_get_num_range`, and so on) and move existing parsing onto them, with no behavior change. The Unity tests are the guard.

**Done means:** `make test` passes unchanged, and adding a profile field is one line plus a test.

## F-6: Decide where JMAP and sync live

**Kind:** Easy (a written decision)
**Status:** Not started.

**Goal:** §2 already says the C core owns JMAP with libcurl (one worker per login, a shared multi handle, a cap of 6 in flight) and Jansson. Confirm that against the layering in [G-LAYERS.md](G-LAYERS.md) (sync is L2, sockets and network-change come from L1), and fill in the gaps below in §2 and §5.

**Questions to settle:**

- C core with libcurl (the multi interface on one sync thread, or one thread per account) and jansson, or Rust in the shell with the C core as store and logic?
- How push (JMAP EventSource or WebSocket) shares that loop.
- How a slow or failing account is kept from stalling the others, and how backoff is logged.
- What crosses the FFI: commands and IDs, never message bodies in bulk.

**Done means:** §2 and §5 name the owner and the threading model, and the mail letter's first phase can cite them.

## Working log

- 2026-10-15 Opened from a project review. Release-build icons were fixed by adding `'unsafe-inline'` to `script-src` and `font-src` for the Font Awesome hosts. Not a phase.

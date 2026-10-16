# Tabs — Design Exploration

**Status:** Exploration (not a phase). Written to scope the tab features described in the
design conversation. Maps each topic to the current code, the tier it touches, the open
questions, and how topics interact. Does not draft phases.

**Phase 1 scope (confirmed features):** Pinned tabs, keyboard shortcuts, drag-and-drop files,
right-click context menu (pin, duplicate, mute, close variants), tab muting, session restore,
vertical tabs (icon + label, title on hover), improved tooltips (floating UI), tab widths.
Browsing is in scope (sandboxed external browsing via `+` tab).

## 0a. Confirmed Phase 1 Features

These are confirmed for Phase 1 implementation and are noted here as additions/modifications to
the topics below:

| Feature | Description | Notes |
|---|---|---|
| Pinned tabs | Pin frequently-used tabs to fixed left position, reduced width | Icon-only in vertical mode |
| Keyboard shortcuts | Ctrl+Tab (prev), Ctrl+Shift+Tab (next), Ctrl+W/Ctrl+F4 (close), Ctrl+T (new tab), Ctrl+R (reload) | |
| Drag-and-drop files | Drop files onto tab strip to open new tabs | |
| Right-click context menu | Pin/Unpin, Duplicate, Mute, Close, Close Others, Close to Right | |
| Tab muting | Toggle mute state with visual indicator; muted tabs suppress audio | |
| Session restore | Automatically reopen tabs from previous session on app launch | Beyond F-2 prefs blob |
| Vertical tabs | Icon + label, full title on hover; easy toggle between horizontal/vertical | Initial feature |
| Improved tooltips | Floating UI like Lithium project for tab titles, icons, etc. | Separate consideration |
| Tab widths | Already discussed; include min/max width settings | |
| General browsing | Sandboxed external browsing via `+` tab | Confirmed in scope |

## 0. Current architecture

The tab system lives in three places:

- `ui/index.html:79-82` — the `.tabs` strip containing `#tablist` (flex row, `display: contents`)
  and a `tabs-spacer` with `data-tauri-drag-region` that fills remaining width.
- `ui/src/shell.js:162-281` — the `sections` map (mail, contacts, calendars, files, settings,
  activity, help), a `tabs = new Map()` keyed by section id, and `openSection(id)` which either
  creates a `<button role="tab">` (text-only, click → `openSection`) or switches to an existing one.
  Home tabs are permanent; there is no close button, no icon, no drag.
- `ui/styles/theme.css:109-135` — `.tabs` is `flex: none`, `[role="tab"]` has `border-right`,
  `padding: 0.4rem 0.9rem`, transparent background; selected takes `--surface-button-active`.

Nav buttons (top and bottom icon rows at `index.html:13-75`) already call `openSection`. Nav body
buttons (tree items at `shell.js:531-538`) only set `aria-current` locally.

Preferences flow through: `api.js` → Tauri `invoke` → `src-tauri/src/lib.rs` →
`PavShellPrefs` (FFI struct, `pavillion.h:16`) → `pavillion_prefs_apply` → C core writes
`preferences.ui` in the profile JSON. Zoom, nav width, and splits are fixed struct fields.
Icons and the FA kit are already an opaque JSON sub-object (`preferences.ui.icons` /
`preferences.ui.fontawesome_kit`), retrieved via separate C functions.

The F-2 phase ([F-HARDEN.md](F-HARDEN.md)) will make UI prefs an opaque JSON blob so that new
UI knobs do not touch the FFI struct. That is the preferred path for tab-specific preferences.

---

## Topic 1 — Tab appearance: icon before title, close button after, smaller text/icons

### What exists
Tabs are `textContent`-only buttons (`shell.js:267-273`). No icon, no close affordance.

### Design options

**Icon source.** Home tabs reuse the section's nav icon. That icon already lives in
`iconOverrides` / `data-icon-definition` on the nav button (`index.html:15`, `shell.js:46-61`).
A tab can read the same source so the override applies there too. Sub-tabs need a separate icon
mapping (message = `fa-envelope`, contact = `fa-address-card`, URL = `fa-globe`, etc.). The
`sections` object could gain an `icon` default for home tabs; sub-tab icons would be chosen by
the caller in `shell.js`.

**Close button.** A separate `<button>` inside each tab (not a `<button>` inside a `<button>`,
which is invalid HTML). The tab itself becomes a container with a click handler for activation,
and the close button calls `stopPropagation()` so its click does not bubble to switch tabs.
Home tabs are not closable. `aria-label="Close"` with the section/title name for accessibility.

**Smaller text/icons.** A CSS rule under `.tabs [role="tab"]` reducing `font-size` and icon
`width`/`height` below the current nav-level sizing. The nav icon is `1.15rem`; tab icons could be
`0.9rem` or smaller, with tab text at `0.85rem` or `0.8rem`.

**Closable indicator.** Visually distinguish closable tabs (have an X) from permanent ones
(no X). Hover state on the close button only.

### Tier impact
- **UI only**: `theme.css` rules, `shell.js` tab creation (container element + close handler).
- No Rust or C changes.

### Questions
1. Should the close button also have a middle-click or `Ctrl+W` keyboard shortcut? Yes (keyboard shortcuts are Phase 1).
2. Icon set for sub-tabs: which specific FA classes? Mail message, contacts, URL browser, etc.
3. Pinned tab behavior: interaction with right-click menu and drag-to-reorder.
4. Tab muting: visual indicator design (speaker icon with mute/unmute states).
5. Session restore: should tab position/order be restored, or just the set of open tabs?

---

## Topic 2 — Resource cleanup on tab close (C core lifecycle)

### What exists
No tab-specific resource tracking. The app has one SQLite database (`core/src/store.c`) shared
across all views. Message bodies are cached in a `body_cache` table (`PAVILLION_PLAN.md:295`);
blobs live in the filesystem (`PAVILLION_PLAN.md:141` blobs dir).

### Design options

**Option A — Opaque tab handle (recommended).** The C core issues an opaque handle when a
view opens (e.g., a message tab loads a body). The shell stores it per tab. On close, the shell
calls a new command `pavillion_tab_close(tab_handle)` that drops body-cache entries, blob
references, or any in-memory state tied to that handle. The handle is opaque (an integer or a
pointer cast to `u64`); the shell never interprets it.

**Option B — View-specific commands.** The shell calls `pavillion_message_view_closed(message_id)`
for message tabs, `pavillion_contact_view_closed(contact_id)` for contacts, etc. Each feature
gets its own C function. Simpler than handles but proliferates entry points and doesn't cover
generic tabs (URLs).

**Option C — Reference counting in core.** The core tracks how many views reference each body or
blob. When the count drops to zero, the data is eligible for eviction. No explicit close call
needed. But eviction is lazy and "freeing" may be delayed, not immediate as the user wants.

### Integration with the command queue
`core/src/pavillion.c:16-19` defines `CORE_CMD_*` enums and a ring buffer queue
(`CORE_QUEUE_CAP 8`). A new `CORE_CMD_TAB_CLOSE` would follow the exact same pattern as
`CORE_CMD_PREFS` / `CORE_CMD_ICONS` / `CORE_CMD_KIT`:
1. Shell pushes the handle through FFI.
2. Core thread pops, runs cleanup, signals done.
3. The existing `g_prefs_*` / `g_icons_*` state machine pattern would be mirrored for
   `g_tab_close_*`.

### What "resources" to free
- **Mail message tabs**: `body_cache` entries for the loaded message (text/plain, html_sanitized).
  The `local_path` for raw `.eml` if opened. Blobs for inline images if not referenced elsewhere.
- **Contact tabs**: loaded contact's full JSON.
- **URL/browser tabs**: any cached page content or history.
- **Shared resources** (SQLite itself, nav mailboxes list) are never freed — only view-specific
  data.

### The "if possible" caveat
Some state may be cached for performance (prefetched message bodies, §5 item 5). The "if
possible" means: free what is safe to free, keep what is shared. The core's cache eviction
policy (when it lands in P3) would interact here.

### Tier impact
- **C core**: new FFI function + command enum + cleanup logic. Follows the established
  `pavillion_icons_apply` / `pavillion_kit_apply` pattern.
- **Rust bridge**: new `#[tauri::command]` calling the FFI (like `save_icons`).
- **UI**: call the new `api.js` function from the tab close handler.
- **FFI struct**: F-1 (one definition of FFI structs) should include any tab handle type.

### Questions
1. Which specific C-layer resources are the priority? Body cache first, then blobs, then
   in-memory state?
2. Should there be a tab-open command too (to register the handle), or is close-only sufficient?
3. Does the core need to track which handles exist (for stop/join safety)?

---

## Topic 3 — Drag-to-reorder tabs

### What exists
No drag support. The nav resize handle (`shell.js:287-302`) uses pointer events — a reusable
pattern.

### Design options

**Pointer events (consistent with nav resize).** `pointerdown` on a tab captures the pointer.
`pointermove` computes the tab's new position; a placeholder element or opacity indicates where
it would drop. `pointerup` commits the reorder. This avoids the HTML5 DnD accessibility gaps.

**Persisted order.** The new order writes through `saveLayout` into `preferences.ui.tab_order`
(a JSON array of tab ids or section ids). This is the opaque-blob pattern from F-2.

**Keyboard accessibility.** Arrow keys (or `Home`/`End`) to move the active tab within the strip.
Announced via `aria-live` or `aria-roledescription="tab, reorderable with arrow keys"`. The
`tablist` gets `aria-dropeffect` removed (deprecated); instead, live-region announcements.

### Tier impact
- **UI only**: `shell.js` pointer handlers + CSS for drag feedback.
- **Rust/C**: extend `prefs()` / `save_layout` to carry `tab_order` through the existing
  `saveLayout` → FFI → `preferences.ui` pipeline. No new FFI calls needed if `tab_order` rides
  on the existing `LayoutSave` JSON path.

### Questions
1. Should reordering apply to home tabs only, or also to sub-tabs (messages, URLs)?
2. Is tab order per-section or global? If a user has mail-home, mail-message, and a URL tab,
   can they all be freely reordered?

---

## Topic 4 — Nav click opens/switches to the tab

### What exists
Top nav buttons and bottom nav buttons call `openSection(id)` which creates-or-switches a home
tab (`shell.js:255-281`). Nav body tree buttons only set `aria-current` locally
(`shell.js:531-538`).

### Design
The existing `openSection` already implements the "open if absent, switch if present" behavior
the user wants. Extending it to nav body buttons:

- Mail tree: clicking Inbox → `openSection("mail")`, with Inbox selected in the tree.
- Contacts letters: clicking a letter → `openSection("contacts")`.
- Calendars: clicking a calendar → `openSection("calendars")`.
- Files tree: clicking a folder → `openSection("files")`.

The home tab is the "main tab for that feature." Nav body clicks that represent a deeper
context (e.g., a specific message in the list) would open a sub-tab instead, which is
Topic 5.

### Tier impact
- **UI only**: extend the nav body click handlers to call `openSection`.

---

## Topic 5 — Additional tabs per feature (mail messages, contacts, etc.)

### What exists
Only home tabs exist. §10 of [PAVILLION_PLAN.md](../PAVILLION_PLAN.md) already specifies this:
"A message may open as its own tab, titled by subject. That tab closes."

### Design — tab model refactor

The current `tabs = new Map()` maps section id → `<button>`. This must become a proper tab
registry. Suggested model:

```
type Tab = {
  id: string,            // unique, e.g. "mail:home" or "mail:msg:abc123"
  section: string,       // "mail" | "contacts" | "url" | ...
  title: string,         // "Mail" | "Subject: ..." | "example.com"
  icon: string,          // FA class or section default
  closable: boolean,     // home tabs: false; sub-tabs: true
  context?: object,      // { messageId, contactId, url } — view-specific data
  order: number,         // for persisted reorder
}
```

`tabs` becomes `Map<tabId, Tab>` with the DOM element stored on the Tab object (or via a
`WeakMap`). `openSection(id)` creates a home tab; a new `openTab({ section, title, context })`
creates a sub-tab. The `+` URL tab is just `openTab({ section: "url", ... })`.

### Home tab semantics
- Home tabs are always present for each section that has been visited.
- They are not closable (no close button).
- Their icon comes from the nav button icon override.
- Closing the last non-home tab returns focus to the home tab.

### Sub-tab contexts
| Feature | Trigger | Tab title | Tab icon | Closable |
|---|---|---|---|---|
| Mail message | Open message in new tab | Subject | `fa-envelope` (or section color icon) | Yes |
| Contact | Open contact in new tab | Name | `fa-address-card` | Yes |
| URL browser | + tab | Domain or page title | `fa-globe` | Yes |

### Tier impact
- **UI**: major refactor of `shell.js` tab management. This is a prerequisite for Topics 1, 2, 6, 8, 9.
- **C core**: only if sub-tabs need core-side resource tracking (Topic 2). The `context` field
  is primarily a UI concern for now.

### Questions
1. Should multiple message tabs for the same message be allowed, or should opening a message
   that already has a tab just switch to it?
2. What is the maximum number of open tabs? Should there be a cap or lazy unload?

---

## Topic 6 — Tab overflow: scroll slider vs. wrap

### What exists
`#tablist` is `display: contents` inside `.tabs` (a flex row). No overflow handling. Tabs can
shrink indefinitely.

### Design options

**Option A — Scroll with slider (preferred for desktop).** Wrap `#tablist` in a scroll
container. When total tab width exceeds container width, show:
- Left/right scroll buttons (chevron icons) at the edges.
- A slider control below the tabs (a `range` input or a scrollbar) that maps to
  `scrollLeft` of the tab strip.

The slider appears only when overflow is detected (via `ResizeObserver` + width measurement).
The `tabs-spacer` drag region would need to remain on the right of the slider, or the slider
pushes it aside.

**Option B — Wrap to multiple lines.** `flex-wrap: wrap` on the tab container. Each line is a
row of tabs. "Not wrapping an individual tab" means `min-width: fit-content` or a fixed
min-width per tab so no tab splits across lines. The active tab's line gets the background;
inactive lines are flush.

**Option C — Both, as a setting.** A preferences toggle: "Scroll" vs "Wrap". Default to scroll
for consistency with desktop browser conventions.

### Interaction with max width (Topic 9)
If browser tabs are capped at 200px max width (Topic 9), and the user opens many, scroll/wrap
becomes mandatory. The scroll container must accommodate the cumulative width.

### Tier impact
- **UI only**: `theme.css` (`overflow-x: auto`, `flex-wrap`) + `shell.js` (width measurement,
  scroll button logic, slider sync).
- **Rust/C**: a preference for the mode (`preferences.ui.tab_overflow: "scroll" | "wrap"`).

### Questions
1. Default mode: scroll or wrap?
2. Should the slider be a native scrollbar (simpler) or a custom range input (more control)?
3. Does the drag region (`tabs-spacer`) coexist with a scrollbar, or does the scrollbar push it?

---

## Topic 7 — Colors settings tab: section color per main app

### What exists
- Theme variables live in `ui/styles/dark.css` and `theme.css`. Surfaces are gradients.
- Accounts have a `color` field in the profile (`B-CONFIG.md:130`), not yet wired to the UI.
- Settings is a screen with headings (Zoom, Theme, Fonts, Icons at `index.html:54-59`).

### Design

**New "Colors" heading.** A fifth settings panel listing each main section (mail, contacts,
calendars, files) with a color picker. Stored as `preferences.ui.colors`:

```json
"ui": {
  "colors": {
    "mail": "#2a7",
    "contacts": "#5a8",
    "calendars": "#a75",
    "files": "#7a2"
  }
}
```

**CSS variable scheme.** Each section's color becomes a custom property:
`--pav-color-mail`, `--pav-color-contacts`, etc. (namespaced per the G-LAYERS.md L4 lock:
"`--pav-*` variables"). Tab styling applies the section's color as:
- A tinted background on the active tab: `background: var(--pav-color-mail)` blended with
  the surface gradient, or a `::before` overlay with `opacity: 0.15`.
- Icon color inheritance: `.tab-icon { color: var(--pav-color-mail) }` so the icon takes
  the section hue rather than the default `--fg`.

**Sub-tab icons.** The plan says "separate icons for additional pages for each section
(like mail messages) ... having potentially a different icon." The sub-tab keeps its own icon
(envelope for messages) but inherits the section color. This is a CSS-only distinction:
`.tab-icon.mail-sub { color: var(--pav-color-mail); }` with the icon class set per tab type.

**Gradient interaction.** The existing dark theme uses gradients (`--surface-nav: linear-gradient(...)`).
A section color tint could be a CSS `color-mix(in srgb, var(--pav-color-mail) 25%, transparent)`
overlay, or a separate `--surface-tab-mail` gradient variable. The L4 lock says themes override
semantic properties; section colors are a user preference that overrides a specific semantic.

### Storage path
Follow the icons/kit pattern (F-2, G-2): the C core stores `preferences.ui.colors` as an opaque
JSON object. New C functions `pavillion_colors_get` / `pavillion_colors_apply`, or — cleaner —
store colors inside the existing `preferences.ui` blob that F-2 will expose as opaque JSON.
This avoids touching `PavShellPrefs` at all.

### Tier impact
- **UI**: new Settings panel (HTML in `index.html`, handler in `shell.js`), CSS variables + rules.
- **Rust**: add `colors` to `PrefsReply` and `LayoutSave` (or carry under the opaque `ui` blob).
- **C core**: `pav_profile_read_colors` / `pav_profile_write_colors` mirroring the icons pattern.

### Questions
1. Should the color apply to the tab background, the icon, or both?
2. Should there be per-account color overrides (the account `color` field), or is section-level
   sufficient?
3. How does the section color interact with the existing gradient surfaces?

---

## Topic 8 — `+` tab for URL browsing, Browser settings section

### What exists
No browser/URL tab. The app renders mail in a sandboxed iframe (`§7`, F-4). Settings has
Zoom, Theme, Fonts, Icons headings.

### Design — CONFIRMED: External web browsing (sandboxed)

The `+` button opens a tab with a URL input and an embedded browser (the sandboxed iframe).
This is confirmed as in scope for Phase 1.

**Security model** (`F-HARDEN.md` Lock):
- The CSP (`tauri.conf.json:25`) allows `frame-src 'self'`. A general URL browser needs
  `frame-src *` or per-origin allowlisting — a security review item.
- External pages need the full §7 sandbox (no `allow-scripts`, no `allow-same-origin`,
  no `allow-forms`) or a separate webview with isolation.
- The plan's `open_external_link` command (`PAVILLION_PLAN.md:199`) confirms external links
  opened from page content should use the OS opener, but the `+` tab itself is embedded browsing.

### Browser settings section

A "Browser" heading under Settings:
- **Default pages**: a list of URLs loaded when the app starts alongside mail home tabs.
   Stored as an array in `preferences.ui.browser.home_pages: string[]`.
- **New tab default**: the URL loaded when the `+` tab is clicked and no URL is typed,
   or `about:blank`. Stored as `preferences.ui.browser.new_tab_url: string`.
- **Tab orientation**: a toggle for vertical/horizontal tab strip
   (`preferences.ui.tab_orientation: "vertical" | "horizontal"`).

**Security constraints** (must hold):
- External content renders in a sandboxed iframe (`sandbox`, no `allow-scripts`, no
   `allow-same-origin`, no `allow-forms`), per §7 and F-4.
- A URL bar at the top of the browser tab, with back/forward/refresh buttons.
- Navigation confirmation (F-2/F-3 hardening): the user confirms before a link leaves the
   site.
- The Font Awesome CSP exception hosts never apply inside the frame.

### Tier impact
- **UI**: `shell.js` (URL input rendering, iframe sandboxing) + `theme.css`.
- **Rust**: new `invoke` commands if the browser needs core-side bookmark/history storage.
- **C core**: only if browser history/bookmarks need SQLite persistence (new tables).

### Questions
1. **Is external web browsing really in scope?** The project is a mail/contacts/calendar client.
   A browser tab is a product expansion. If not, what URLs does the `+` tab navigate to?
2. If browsing is in scope, should it be a separate Tauri webview (with its own isolation) or
   reuse the sandboxed iframe pattern?
3. Are "default pages" app-internal views or external URLs? The wording "default pages to load
   initially" suggests they appear on startup — does that replace the mail home tab?

---

## Topic 9 — Browser tab max width (setting, ~200px default)

### What exists
No max-width on tabs. The flex layout shrinks tabs to fit.

### Design
- CSS: `.tab.browser { max-width: 200px; }` — applies to URL tabs specifically, or to all tabs.
- Setting: `preferences.ui.tab_max_width: 200` (pixels), adjustable in a settings panel.
- Interaction: a setting of `0` means "no max" (tabs shrink to fit). At 200px, many browser
  tabs trigger the overflow slider (Topic 6).

### Tier impact
- **UI only**: CSS + a slider in settings.
- **Rust/C**: carry `tab_max_width` through the existing prefs pipeline.

---

## Cross-cutting concerns

### 1. Tab model refactor is the foundation
Topics 1, 2, 3, 5, 6, 8, and 9 all depend on a proper tab registry (not just `Map<sectionId,
button>`). Topic 5's refactor should happen first, or at minimum be designed alongside the
others. The current `openSection` function must be generalized.

### 2. Home tab vs. sub-tab distinction
Home tabs are permanent and not closable. Sub-tabs (messages, contacts, URLs) are ephemeral.
This affects:
- Close button rendering (Topic 1)
- Resource cleanup (Topic 2) — only sub-tabs clean up C resources
- Persistence (Topic 3) — only home tab order is persisted; sub-tab order may be session-
  scoped or persisted per-session
- Nav clicks (Topic 4) — always opens home tab

### 3. Preference persistence strategy
Tab-related preferences that belong in `preferences.ui` (per F-2):
- `tab_order` (Topic 3)
- `colors` (Topic 7)
- `tab_overflow` mode (Topic 6)
- `tab_max_width` (Topic 9)
- `browser.home_pages` / `browser.new_tab_url` (Topic 8)

These should all follow the icons/kit pattern (opaque JSON sub-objects in the C core), not be
added as fields to `PavShellPrefs`. F-2 is the gate for this.

### 4. Icon inheritance
Tab icons for home tabs come from the nav button icon override. Sub-tab icons are chosen by the
feature. Section colors (Topic 7) tint both. The icon-loading path already centralizes in
`main.js:loadFontAwesomeKit` / `shell.js:applyIconOverride`. Tab icons should reuse
`applyIconOverride` for home tabs and a simpler class-based render for sub-tabs.

### 5. The `+` tab placement
The `+` button should be at the right end of the tab strip, after all tabs but before (or
replacing) the `tabs-spacer` drag region. When overflow scrolling is active, the `+` should be
pinned to the right edge so it's always reachable. Alternatively, the `+` is always the last
tab in the list and scrolls with the others.

### 6. Accessibility
- `role="tab"` is already used. The close button needs `aria-label` with the tab title.
- Reordering via keyboard: `ArrowLeft`/`ArrowRight` moves the active tab.
- `aria-orientation="horizontal"` on the tablist (already implicit via `role="tablist"`).
- Focus management: after closing a tab, focus moves to the next tab or the home tab.
- Keyboard shortcuts (Topic 11) complement standard tab navigation.

### 7. Session restore
Session restore needs a persistence mechanism beyond the F-2 prefs blob:
- Store list of open tabs (ids, section, title, context, pinned status, muted status) on quit.
- On launch, restore the tab set, then load content for the active tab first (lazy load others).
- The existing `saveLayout` / `LayoutSave` JSON path can carry the session snapshot.
- Rust side needs a new `save_session` / `restore_session` command pair, following the
  `saveLayout` pattern in `lib.rs`.
- C core side: if session restore needs SQLite persistence, a `sessions` table in `store.c`.

### 8. Vertical tabs
Vertical tabs require:
- CSS: a `.tabs-vertical` class that transforms the tab strip from horizontal flex row to a
  vertical column. Icons stack above labels (or side-by-side with `flex-direction: row`).
- JS: a toggle in the toolbar or settings that adds/removes the class. The toggle state persists
  in `preferences.ui.tab_orientation: "horizontal" | "vertical"`.
- Pinned tabs: icon-only in vertical mode (label hidden); full icons + labels for regular tabs.
- Overflow: vertical tabs scroll vertically when they exceed viewport height.

### 9. Tooltips (floating UI)
Following the Lithium project pattern:
- Use `data-tooltip` attributes on tab elements.
- A lightweight JS module positions the tooltip (floating UI library or custom absolute
  positioning with `position: fixed` and arrow calculation).
- CSS: tooltip styled with `--pav-*` variables (background, border, arrow).
- Applies to: tab titles (on hover or focus), tab icons, and potentially other UI elements.

---

## Tier change summary

| Topic | C core | Rust bridge | UI (JS/CSS) | Depends on |
|---|---|---|---|---|
| 1. Tab appearance | — | — | Major | Topic 5 |
| 2. Resource cleanup | Major (new FFI cmd) | New command | Minor | Topic 5 |
| 3. Drag reorder | Minor (prefs) | Minor (prefs) | Major | Topic 5 |
| 4. Nav click | — | — | Minor | — |
| 5. Sub-tabs | — (or F-2 for context) | — | Major refactor | — |
| 6. Overflow | Minor (prefs) | Minor (prefs) | Major | Topic 5 |
| 7. Colors | Major (read/write) | Major (prefs) | Major | F-2 |
| 8. URL/browser tab | Major (if bookmarks) | Major (if commands) | Major | Topic 5, F-4 |
| 9. Tab max width | Minor (prefs) | Minor (prefs) | Minor | Topic 6 |
| 10. Pinned tabs | — | — | Moderate | Topic 5 |
| 11. Keyboard shortcuts | — | — | Moderate | Topic 5 |
| 12. Drag-and-drop files | — | — | Moderate | Topic 5 |
| 13. Right-click context menu | — | — | Moderate | Topics 1, 10 |
| 14. Tab muting | — | — | Moderate | Topic 5 |
| 15. Session restore | — (or prefs) | Minor (prefs) | Moderate | Topic 5 |
| 16. Vertical tabs | — | — | Moderate | Topic 5 |
| 17. Improved tooltips | — | — | Moderate | — |

---

## Open questions for Andrew — Resolved

The following were discussed and resolved:

1. **Browser/URL tabs** — RESOLVED: External web browsing is in scope, sandboxed from the app.
   The `+` tab opens an internal URL-input/browser with navigation controls.

2. **Vertical tabs** — CONFIRMED: Initial feature with easy toggle between horizontal/vertical.
   Vertical shows icons + labels, full titles on hover, pinned tabs icon-only.

3. **Tooltips** — CONFIRMED: Improve to use floating UI like the Lithium project.

4. **Pinned tabs** — CONFIRMED: Pin frequently-used tabs to fixed left position, reduced width.

5. **Keyboard shortcuts** — CONFIRMED: Ctrl+Tab, Ctrl+W, Ctrl+T, Ctrl+R.

6. **Drag-and-drop files** — CONFIRMED: Drop files onto tab strip to open.

7. **Right-click context menu** — CONFIRMED: Pin/Unpin, Duplicate, Mute, Close variants.

8. **Tab muting** — CONFIRMED: Toggle mute state with visual indicator.

9. **Session restore** — CONFIRMED: Automatically reopen tabs from previous session.

10. **Tab widths** — CONFIRMED: Min/max width settings.

## Open questions for Andrew

The following remain open for brainstorming:

1. **Resource cleanup scope**: Which C-layer resources should be freed on tab close — cached
   message bodies first, then blobs, then in-memory state? The "if possible" caveat needs
   specifics per resource type.

2. **Tab persistence**: Are sub-tabs (messages, contacts, URLs) session-only, or should their
   existence (not their full content) be restored across restarts? The plan says "Home tabs
   stay put," implying sub-tabs are ephemeral.

3. **Color settings storage**: Store section colors in the opaque `preferences.ui.colors` JSON
   blob (following the icons/kit pattern, requires F-2), or as fields on `PavShellPrefs`? The
   former is preferred per F-2 but means colors are unavailable until F-2 lands.

4. **Tab overflow default**: Scroll-with-slider or wrap? Or a user-selectable setting with one
   as default?

5. **Tab reorder scope**: Can all tab types (home, message, contact, URL) be freely reordered,
   or only within their section?

6. **Closing a tab with unsaved work**: Should a tab close prompt if it contains an unsaved
   draft composer or in-progress edit? Or do sub-tabs auto-save?

7. **`+` tab behavior on click**: Does clicking `+` immediately open a URL input, or does it
   show a menu of default pages from the Browser settings?

8. **Multiple tabs for the same context**: If a user opens the same message in a new tab twice,
   should it switch to the existing tab or create a duplicate?

9. **Vertical tab toggle**: Should the toggle be a toolbar button, a setting, or a context menu
   option? Where does it live in the UI?

10. **Tooltip floating UI**: Should tooltips follow cursor movement, or anchor to the element?
    How does this interact with the existing `--pav-*` variable scheme?

---

## Related docs

- [H-LOGGING.md](H-LOGGING.md) — App-wide log viewer: C-level logging API, CodeMirror-based viewer,
  search/filter UI, print/email forwarding, right-click menu splitting Activity vs Log views.
- [I-ACTIVITY.md](I-ACTIVITY.md) — Activity feed: in-progress operations (mailbox checks, file
  copies, downloads) with status, progress, ETA, and cancel buttons.

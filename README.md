[![CI](https://github.com/500Foods/Pavillion/actions/workflows/ci.yml/badge.svg)](https://github.com/500Foods/Pavillion/actions/workflows/ci.yml)
[![Release](https://github.com/500Foods/Pavillion/actions/workflows/release.yml/badge.svg)](https://github.com/500Foods/Pavillion/actions/workflows/release.yml)
[![Latest release](https://img.shields.io/github/v/release/500Foods/Pavillion?label=latest)](https://github.com/500Foods/Pavillion/releases/latest)
[![Count Lines of Code](https://github.com/500Foods/Pavillion/actions/workflows/main.yml/badge.svg)](https://github.com/500Foods/Pavillion/actions/workflows/main.yml)

# <img src="ui/public/mark.png" alt="" width="40" height="40" align="absmiddle"> Pavillion

[Site](https://500foods.github.io/Pavillion/) · downloads and release notes

A Linux desktop for the mail, contacts, calendars, and files on a self-hosted Stalwart server, and the private record of the people and the work that mail belongs to.

[Stalwart](https://stalw.art/) already presents those four in one [JMAP](https://jmap.io/) session. Pavillion is the client for that session. The same window is the CRM: correspondence, people, calendar, and files in one place, on a server the operator runs, with a local store so the record remains readable when the server is quiet. Tasks, notes, and ledgers follow on that store. One person, one process, one active profile. A small team shares the server. Each person runs Pavillion.

The spelling is deliberate. Pavillion, not a misspelling, and not an older British or French form.

## This build

The shell opens. The client does not.

The rail, the tabs, themes, Help, the activity list, and the log are in the window. The rail is two bars. A drag reorders a button, or moves it to the other bar. Message HTML is isolated in a release build. Mail, Contacts, Calendars, and Files open empty frames. Tasks, Notes, Argent, and GitHub report that the section is not available. Grok Bots shows an empty list and a disabled Add button. No account can be added.

The sections below are the product under construction. They are not a description of this binary.

## The client

JMAP is the native path, not a compatibility layer on an IMAP client. Mail includes submission and Sieve. Contacts are JSContact, with address books. Calendars include recurrence and time zones. Files sit beside the message they belong to. One session covers the accounts on that server.

A mailbox on another server is IMAP and SMTP, username and password over TLS. That account is mail only.

CardDAV and CalDAV come later, for a server that is not Stalwart, or for a collection Stalwart exposes that way. They write the same tables. They are not a second address book and not a second calendar. A contact or an event is a row in the local store. The screen does not depend on which protocol filled it.

OpenID Connect, for a server that already uses it, is in scope and is not built. Gmail sign-in, Microsoft sign-in, Exchange, and POP3 are outside the product.

## The record

A hosted CRM keeps a copy of the people and sells it back as leads and stages. Pavillion does not. The record is the mail, the address book, the calendar, and the files, held on the operator's server and in a local SQLite store. Search is local, on what has already been fetched, including bodies. Server search covers what the local index does not yet hold. Each server has its own network thread, so a slow server stalls only itself.

The desk holds about twenty identities. A unified inbox when one list is wanted, and the separate accounts when it is not. A reply uses the address the message was sent to, then the mailbox default, then a global default.

Times are 24-hour. The default zone is `America/Vancouver`. A calendar event may name its own. Fedora is the first platform, and the one continuous integration builds. macOS, Windows, and iPad are deferred, not dropped.

Task lists carry a title, a plain description, a status, a priority, and an optional due date. They do not carry assignees. An IMAP-only account has an empty task list. A collection that holds only to-dos is hidden from Calendars. A collection that holds only events is hidden from Tasks. A collection that holds both is listed in both. Tasks are CalDAV to-dos, after calendars. They are not a JMAP task list, and they are not a Stalwart server job.

Notes are a notebook of Markdown pages, with tags, on one file account, under a `.pavillion` folder that Files hides. There is no Stalwart note object. Search for notes is local. Until an account is chosen, the section stays empty.

Argent is the books. A row there is a ledger, not an account. Account remains the word for a mail login. The icon is on the rail. The ledgers are not scoped, and they are not in the build sequence.

GitHub is a footer icon. A later association may tie repository events to projects on the task lists, and those projects to contacts. That work is recorded and not scheduled. There is no event list, no Projects field, and no contact link.

## Surfaces

**Mail.** Threads, flags, move, archive, attachments, a composer, an outbox, and folder retention. HTML mail is drawn in a sandbox: no scripts, no forms, no popups, and no navigation of the top window. Remote images stay off until allowed for that message. The isolation is already enforced in a release build. The mailbox it wraps is not in this tree. Sieve on Stalwart is the server-side filter. Local rules, when they arrive, are sandboxed Lua with a fixed API. A rule returns an intent. The core applies it. The same rules can be exported as Sieve, so the server can run them when the desktop is closed. That work follows daily-driver mail.

**Contacts.** Address books, a letter index, an editor that preserves unknown card fields, and vCard import and export. Compose completion comes from the people already written to. If the server does not advertise JMAP Contacts, contacts stay local until CardDAV can sync them. That check has not been run.

**Calendars.** Week, day, month, and agenda. Recurrence and time zones are handled in the core. Invitations inside mail follow the first working copy. They do not arrive with the first calendar screen. A JMAP Calendars check on the running server is the entry to that work. It is a go or a no-go, and it has not been made.

**Files.** Browse, preview, download, upload, folders, rename, move, and delete, on the Stalwart file store. The protocol, JMAP files or WebDAV, is chosen when that work starts, against the server in use. An IMAP-only account shows an empty tree.

## Boundaries

- No telemetry, no plugin gallery, and no in-app updater. Packages, when they exist, are ordinary system packages.
- No multi-user mode, and no shared Pavillion database. Settings do not sync between machines.
- No HTTP server in the application. An agent receives the command line, and only while Pavillion is running. That command line is not built. There is no MCP server.
- No PGP or S/MIME unless that decision is made on purpose. No RSS, chat, or newsgroups.
- A password, a token, and a refresh token do not belong in the profile. The secure store that holds them arrives with accounts, and is not in this build.
- Profile, log, and database files are mode `0600`. The directories that hold them are `0700`. A write is a temporary file, then a rename.
- Outside text stays text. Subjects, addresses, names, notes, file names, server errors, and log lines are not inserted as HTML into the main document.

## Construction

Sync, storage, mail, and auth belong to a portable C11 core. The core does not know it is inside a window. A thin [Tauri](https://tauri.app/) 2 shell owns the window, the webview, and the dialogs, and forwards one command and one event stream. The page is HTML, CSS, and JavaScript, with no framework and no CSS library.

Dark, light, and follow-the-system ship with the application. A theme is a CSS file. A missing file falls back and does not stop the window.

The release build is one executable, `dist/pavillion`. WebKit is the system library. It is not packed into that binary. The same file is the window and the later command line. A command-line call does not start a second copy of the program.

Linux is the first platform. Fedora continuous integration, on every push and every pull request, runs the tests and then the release build.

The order of the work is [PAVILLION_PLAN.md](PAVILLION_PLAN.md). The map of the layers, the threads, and the data flow is [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

Store, accounts, and sync precede a real mailbox tree. Daily-driver mail follows. Packages, the command line, IMAP for other servers, and mail rules follow that mail. Contacts, calendars, files, tasks, and notes are the working copy after it. OpenID Connect, invitations, and a second platform are polish after that copy. Argent and the GitHub association are outside the sequence.

## Credentials

A typed URL, username, and password is the ordinary source. A 1Password service account is the other. The token is set once. The chosen vault items fill the accounts in use. Pavillion keeps three things in the secure store: the token, the list the picker shows, and the username and password that account is actually using. A restart does not call 1Password again while that copy is still good. A service account has a hard request cap. Caching is what stays under it. A rejected login refreshes that one item a single time. A second rejection is shown on the activity list. Pavillion stops calling 1Password, and it stops asking.

This arrives with accounts. It is not in this build.

## Agents

Grok Bots is an optional footer button, hidden until Settings turns it on. A saved webhook and a key hand a short job to a bot already in use, against the item that is open. Pavillion posts the request. The bot acts through the command line and reports back: accepted, working, done, or failed. That status is visible on the bot, on the open item, and in Activity.

The webhook carries the request and the id of the open item. The message body and the file stay on this machine. The bot fetches what it needs with a key issued for it, and can send mail only when that key is allowed to send. With the application closed, no bot acts and no mail goes out. Saved phrases, of the kind that fire when new mail arrives, follow rules. They are not mail rules, and they do not export to Sieve.

The section shell is on the rail. The webhook, the keys, and the command line are not built.

## Build

On Fedora, install the packages named in [scripts/fedora-deps.txt](scripts/fedora-deps.txt). The file is names only, with no versions:

```sh
dnf install $(grep -vE '^[[:space:]]*(#|$)' scripts/fedora-deps.txt)
```

| Command | Result |
| --- | --- |
| `make test` | C tests, then the UI tests. Does not bump the build count. |
| `make dev` | Library, page, and window. The mail screens are still empty. |
| `make build` | Release binary at `dist/pavillion`, and the content-security check. No rpm, deb, or AppImage. |
| `make mini` | Size build at `dist/pavillion-mini`, packed with UPX. Needs `upx`, `strip`, and ImageMagick. Not a package. |

[`.github/workflows/ci.yml`](.github/workflows/ci.yml) installs that same package list, then runs `make test` and `make build`.

No argument uses the default data directory. One argument is a working directory. A second argument is an error. The invoking shell expands `~`. Pavillion does not.

The license is [MIT](LICENSE.md). Third-party notices (including LGPL runtimes in the Windows build) are in [THIRD_PARTY.md](THIRD_PARTY.md).

## Repository Information

<!--CLOC-START -->
```cloc
Last updated at 2026-10-28 00:46:53 UTC
-------------------------------------------------------------------------------
Language                     files          blank        comment           code
-------------------------------------------------------------------------------
C                              264          21728           4471         420051
C/C++ Header                   282           6288           6834         241214
JavaScript                     116           1931            672          26665
Markdown                        42           3093              2           6918
CSS                              7            490             50           3437
JSON                            14              0              0           2640
Rust                             6            123            115           1684
Bourne Shell                    12            105            195           1579
CMake                           22            384            184           1398
YAML                             6             40             46            331
HTML                            25              8              0            208
make                             1             22             21            155
SQL                              4              0              0            130
TOML                             2             11              8             63
Text                             2              8              0             57
-------------------------------------------------------------------------------
SUM:                           805          34231          12598         706530
-------------------------------------------------------------------------------
49 Files were skipped (duplicate, binary, or without source code):
  png: 18
  eml: 9
  conf: 2
  gitignore: 2
  lock: 2
  theme: 2
  version: 2
  c: 1
  desktop: 1
  drawio: 1
  gitattributes: 1
  icns: 1
  ico: 1
  in: 1
  license: 1
  md: 1
  notice: 1
  pavillion_pin: 1
  spec: 1
```
<!--CLOC-END-->

## Sponsor / Donate / Support

If you find this work interesting, helpful, or valuable, or that it has saved you time, money, or both, please consider directly supporting these efforts financially via [GitHub Sponsors](https://github.com/sponsors/500Foods) or donating via [Buy Me a Pizza](https://www.buymeacoffee.com/andrewsimard500). Also, check out these other [GitHub Repositories](https://github.com/500Foods?tab=repositories&q=&sort=stargazers) that may interest you.

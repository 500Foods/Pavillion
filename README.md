# Pavillion

A Linux desktop for the mail, contacts, calendars, and files on a server you run yourself.

If you keep a lot of addresses, you already know the pile: one program for mail, another for contacts, a third for the calendar, and the files off in a fourth place. Each program has its own account list, and none of them treat [JMAP](https://jmap.io/) as the way the server already works. [Stalwart](https://stalw.art/) speaks JMAP for all four of those in one session. Pavillion is the one window for that session, for someone who self-hosts Stalwart and also keeps a few ordinary mailboxes elsewhere.

The desk it is built for holds about twenty identities. A unified inbox when you want one list. The address a message was sent to, when you reply. Fast switching between accounts. One person, one process, one active profile. A local SQLite store, so search and reading keep working when the server is quiet.

## Four sections, one store

Mail, Contacts, Calendars, and Files sit on the same rail and the same accounts.

- **Mail** is the rich path on JMAP, including Sieve on Stalwart. IMAP and SMTP cover mailboxes that live on some other server, and those accounts are mail only.
- **Contacts and calendars** share that local store. CardDAV and CalDAV come later, and they write the same tables.
- **Files** is the Stalwart file surface, next to the message you were just reading. An IMAP-only account shows an empty tree there.

HTML mail is drawn in a sandbox. Scripts stay off. Remote images stay off until you allow them for that message. Passwords stay in the system keyring.

The window is a left rail and a tab strip, with dark, light, and follow-the-system themes, plus your own theme file if you want one. The work of sync, storage, mail, and auth lives in a portable C core, small enough to read and test. A thin [Tauri](https://tauri.app/) 2 shell owns the window and the dialogs. The page is HTML, CSS, and JavaScript, so a theme is a CSS file you can copy and edit.

Telemetry, a plugin gallery, and an in-app updater are out of the product. When packages exist, they are ordinary system packages. Linux is the first platform, and Fedora is what continuous integration builds. macOS, Windows, and iPad wait. v1 also leaves out Gmail and Microsoft sign-in, Exchange, and POP3. Ordinary OpenID Connect, for a server that already uses it, is in scope.

## Two things a typical mail program skips

Both are part of the design. 1Password arrives with accounts. Grok Bots arrive with the command line. A build from this tree has the shell, and neither of these yet.

### 1Password

A 1Password service-account token sits beside a typed URL, username, and password. Set the token once, pick the vault items for the accounts you use, and Pavillion keeps three things: the token, the list the picker shows, and the username and password that account is actually using. A restart leaves 1Password alone while that copy is still good. A service account has a hard request cap, and caching is what stays under it. If a login is rejected, Pavillion refreshes that one item a single time. A second rejection shows on the Activity list. Pavillion stops calling 1Password, and it stops asking.

### Grok Bots

An optional fifth section, hidden until Settings turns it on. Save a webhook URL and a key for a Grok bot you already use, then hand it a short job against whatever you have open:

- "File this over there."
- "Add the third timeslot for this event to my work calendar and tell Fred about it."
- "Convert this Word document to a PDF and email it to Alice."

Pavillion posts the request. The bot does the work through Pavillion's own command line and reports back: accepted, working, done, or failed. You see that status on the bot, on the item you had open, and in Activity. The webhook carries the request and the id of the open item. The message body and the file stay here. The bot fetches what it needs with the command line, using a key you issued for it, and it can send mail only when that key is allowed to send. The app has to be running. The command line is the whole surface a bot gets.

## Where the work stands

Early, and the window is already a program you can open. The rail, the tabs, themes, the activity list, and the log viewer are in the tree. Accounts, sync, and the working screens for mail, contacts, calendars, and files are the stretch in front of us.

The order of that work is [PAVILLION_PLAN.md](PAVILLION_PLAN.md). The map of the program, with the layer and thread diagrams, is [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

Clone it and look around. Open an issue for anything that breaks. Start a discussion if you want a say in where it goes. This is a good time for that. Packages, a longer guide, and demos follow once there is mail to show.

The license is [MIT](LICENSE.md).

## Build

On Fedora, install the packages named in [scripts/fedora-deps.txt](scripts/fedora-deps.txt). The file is names only, with no versions:

```sh
dnf install $(grep -vE '^[[:space:]]*(#|$)' scripts/fedora-deps.txt)
```

`make test` runs the C tests and the UI tests. `make build` writes `dist/pavillion`. `make dev` serves the page and opens the window, which is how to look at the shell while the mail screens are still empty. [`.github/workflows/ci.yml`](.github/workflows/ci.yml) installs that same package list, then runs `make test` and `make build`.

## Repository Information

[![Count Lines of Code](https://github.com/500Foods/Pavillion/actions/workflows/main.yml/badge.svg)](https://github.com/500Foods/Pavillion/actions/workflows/main.yml)
<!--CLOC-START -->
```cloc
Last updated at 2026-10-17 23:00:49 UTC
-------------------------------------------------------------------------------
Language                     files          blank        comment           code
-------------------------------------------------------------------------------
C                               19           1425            235          13270
JavaScript                      55            667            115           9636
Markdown                        27           2207              2           4437
JSON                             7              0              0           2648
CSS                              6            300              2           1854
C/C++ Header                    14            345            406           1826
Rust                             3             19              7            402
YAML                             3             21             17             92
HTML                             3              4              0             82
Text                             2              7              0             51
make                             1             11              3             51
CMake                            1              3              1             30
TOML                             1              4              0             21
-------------------------------------------------------------------------------
SUM:                           142           5013            788          34400
-------------------------------------------------------------------------------
25 Files were skipped (duplicate, binary, or without source code):
  png: 17
  gitignore: 2
  theme: 2
  gitattributes: 1
  icns: 1
  ico: 1
  lock: 1
```
<!--CLOC-END-->

## Sponsor / Donate / Support

If you find this work interesting, helpful, or valuable, or that it has saved you time, money, or both, please consider directly supporting these efforts financially via [GitHub Sponsors](https://github.com/sponsors/500Foods) or donating via [Buy Me a Pizza](https://www.buymeacoffee.com/andrewsimard500). Also, check out these other [GitHub Repositories](https://github.com/500Foods?tab=repositories&q=&sort=stargazers) that may interest you.

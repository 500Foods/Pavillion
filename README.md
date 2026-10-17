# Pavillion

A desktop mail, contacts, calendars, and files client for self-hosted Stalwart (JMAP-first), built with Tauri 2, a portable C core, and a vanilla JS UI.

The plan and the implementation order are in [PAVILLION_PLAN.md](PAVILLION_PLAN.md); the phase documents are in [docs/](docs/).

## Preview Features

Some things that set this apart from your average email program.

- Stalwart-first thinking. JMAP is the first protocol implemented out of the gate, and Mail, Contacts, Calendars, and Files are all first-class citizens.
- 1Password integration. Set up a service worker token and link it to the accounts you want to use and never think about passwords again.
- Grok Bot integration. Add a webhook url+key and then send your favorite Grok Bot your requests. "File this over there". "Add the third timeslot for this event to my work calendar and tell Fred about it". "Convert this Word document to a PDF and email it to Alice". So fun. Believe me.

## Build

On Fedora, install the packages named in [scripts/fedora-deps.txt](scripts/fedora-deps.txt). The file has names only, with no versions:

```sh
dnf install $(grep -vE '^[[:space:]]*(#|$)' scripts/fedora-deps.txt)
```

`make test` runs the Unity suite. `make build` writes `dist/pavillion`. `.github/workflows/ci.yml` installs that same list, then runs those two targets.

## Additional Notes

While this project is currently under active development, feel free to give it a try and post any issues you encounter.  Or start a discussion if you would like to help steer the project in a particular direction.  Early days yet, so a good time to have your voice heard.  As the project unfolds, additional resources will be made available, including platform binaries, more documentation, demos, and so on.

## Repository Information

[![Count Lines of Code](https://github.com/500Foods/Pavillion/actions/workflows/main.yml/badge.svg)](https://github.com/500Foods/Pavillion/actions/workflows/main.yml)
<!--CLOC-START -->
```cloc
Last updated at 2026-10-17 20:28:54 UTC
-------------------------------------------------------------------------------
Language                     files          blank        comment           code
-------------------------------------------------------------------------------
C                               19           1425            235          13270
JavaScript                      50            510             95           7340
Markdown                        27           2116              2           4263
JSON                             7              0              0           2648
C/C++ Header                    14            345            406           1826
CSS                              6            284              2           1740
Rust                             3             19              7            402
YAML                             3             21             17             92
HTML                             3              4              0             82
Text                             2              7              0             51
make                             1             11              3             51
CMake                            1              3              1             30
TOML                             1              4              0             21
-------------------------------------------------------------------------------
SUM:                           137           4749            768          31816
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

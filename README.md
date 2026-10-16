# Pavillion

A desktop mail, contacts, calendars, and files client for self-hosted Stalwart (JMAP-first), built with Tauri 2, a portable C core, and a vanilla JS UI.

The plan and the implementation order are in [PAVILLION_PLAN.md](PAVILLION_PLAN.md); the phase documents are in [docs/](docs/).

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
Last updated at 2026-10-16 21:30:55 UTC
-------------------------------------------------------------------------------
Language                     files          blank        comment           code
-------------------------------------------------------------------------------
C                                8            690            199           6284
Markdown                        26           1647              2           3645
C/C++ Header                     7            278            328           1701
JSON                             4              0              0           1478
Rust                             3             53              4            824
CSS                              4            138              0            788
JavaScript                       4             52              3            630
HTML                             1              0              0            220
YAML                             3             18             15             81
Text                             2              7              0             51
make                             1             11              3             32
TOML                             1              4              0             20
CMake                            1              2              0             16
-------------------------------------------------------------------------------
SUM:                            65           2900            554          15770
-------------------------------------------------------------------------------
23 Files were skipped (duplicate, binary, or without source code):
  png: 17
  gitignore: 2
  gitattributes: 1
  icns: 1
  ico: 1
  lock: 1
```
<!--CLOC-END-->

## Sponsor / Donate / Support
If you find this work interesting, helpful, or valuable, or that it has saved you time, money, or both, please consider directly supporting these efforts financially via [GitHub Sponsors](https://github.com/sponsors/500Foods) or donating via [Buy Me a Pizza](https://www.buymeacoffee.com/andrewsimard500). Also, check out these other [GitHub Repositories](https://github.com/500Foods?tab=repositories&q=&sort=stargazers) that may interest you.

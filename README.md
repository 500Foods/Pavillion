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
Last updated at 2026-10-16 23:23:31 UTC
-------------------------------------------------------------------------------
Language                     files          blank        comment           code
-------------------------------------------------------------------------------
C                               13            919            209           8130
Markdown                        27           1990              2           4046
JSON                             5              0              0           2030
C/C++ Header                    10            315            371           1771
JavaScript                      10             88              7           1072
CSS                              4            138              0            790
Rust                             3             41              6            575
HTML                             1              0              0            227
YAML                             3             21             17             92
Text                             2              7              0             51
make                             1             11              3             47
CMake                            1              3              1             24
TOML                             1              4              0             20
-------------------------------------------------------------------------------
SUM:                            81           3537            616          18875
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

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
Last updated at 2026-10-17 21:39:12 UTC
-------------------------------------------------------------------------------
Language                     files          blank        comment           code
-------------------------------------------------------------------------------
C                               19           1425            235          13270
JavaScript                      52            559            100           8281
Markdown                        27           2183              2           4408
JSON                             7              0              0           2648
C/C++ Header                    14            345            406           1826
CSS                              6            292              2           1796
Rust                             3             19              7            402
YAML                             3             21             17             92
HTML                             3              4              0             82
Text                             2              7              0             51
make                             1             11              3             51
CMake                            1              3              1             30
TOML                             1              4              0             21
-------------------------------------------------------------------------------
SUM:                           139           4873            773          32958
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

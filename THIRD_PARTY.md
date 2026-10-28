# Third-party notices

Pavillion itself is MIT licensed — see [LICENSE.md](LICENSE.md).

## Vendored in the source tree

| Component | License | Where |
|---|---|---|
| [lexbor](https://github.com/lexbor/lexbor) | Apache-2.0 | `third_party/lexbor/` (see its `LICENSE` and `NOTICE`) |

## Linux packages (Fedora rpm)

The rpm depends on system libraries (WebKitGTK, GTK, libsecret, GMime, libical, jansson, libcurl, sqlite, libuuid). Those binaries are not shipped inside the rpm; the distribution packages them and their licenses.

## Windows MSI (redistributed DLLs)

The Windows MSI ships unmodified MSYS2 UCRT64 DLLs next to `pavillion.exe` (dynamic linking; compatible replacements are allowed), plus Microsoft’s `WebView2Loader.dll` from the WebView2 SDK (via `webview2-com-sys` / tauri-build). Exact MinGW set: whatever `packaging/windows/collect-dlls.sh` staged for that build. Source packages: [MSYS2](https://packages.msys2.org/) / [MINGW-packages](https://github.com/msys2/MINGW-packages). WebView2 Runtime itself is expected on the machine (Edge / Evergreen).

| Library | License |
|---|---|
| GLib / GObject / GIO / GModule (glib2) | LGPL-2.1-or-later |
| GMime | LGPL-2.1-or-later |
| libintl (gettext) | LGPL-2.1-or-later |
| libiconv | LGPL-2.1-or-later |
| libunistring | LGPL-3.0-or-later |
| libffi | MIT |
| PCRE2 | BSD-3-Clause |
| libcurl | curl (MIT-style) |
| OpenSSL | Apache-2.0 |
| nghttp2 / nghttp3 / ngtcp2 / libssh2 / libpsl / brotli / zstd | MIT / BSD-style (see each package) |
| libidn2 | (LGPL / GPL dual — LGPL used as a library) |
| Jansson | MIT |
| SQLite | Public domain |
| zlib | zlib |
| GCC runtime (libgcc, libstdc++, winpthreads) | GPL-3.0 with GCC Runtime Library Exception / MIT+BSD |

A copy of this file and of `LICENSE.md` is installed beside `pavillion.exe` by the MSI.

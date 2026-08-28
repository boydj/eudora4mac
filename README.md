# Eudora for Apple Silicon / macOS

A native rebuild of the classic **Eudora 6.2.4** Mac email client for modern
Apple Silicon macOS. The original Carbon/PowerPC/CodeWarrior source (released
by the Computer History Museum in 2017) is re-homed onto a portable C++20
core with a SwiftUI front end, while the classic on-disk formats are kept
byte-compatible so an existing Eudora mail folder works unchanged.

[![CI](https://github.com/boydj/eudora4mac/actions/workflows/ci.yml/badge.svg)](https://github.com/boydj/eudora4mac/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/boydj/eudora4mac?include_prereleases&sort=semver)](https://github.com/boydj/eudora4mac/releases)

> **Heritage:** based on the open-source **Eudora 6.2.4** source. "Eudora" is
> a registered trademark; this is an unofficial, community rebuild.

---

## Download

Grab the latest build from the [**Releases**](https://github.com/boydj/eudora4mac/releases)
page — e.g. `Eudora-6.2.4-macos.1-macos-arm64.zip` (Apple Silicon, macOS 13+).

The app is **ad-hoc signed, not notarized**, so on first launch Gatekeeper
will block it. Either **right-click the app → Open** (then confirm), or allow
it under **System Settings → Privacy & Security → Open Anyway**.

## Repository layout

This repo holds **both** the original source we ported from and the modern
port built on top of it:

| Path | What it is |
| --- | --- |
| `core/` | **EudoraCore** — the portable C++20 engine (new) |
| `swift/EudoraKit/` | Swift wrapper over the core's C API (new) |
| `swift/EudoraApp/` | the SwiftUI app (new) |
| `packaging/` | `Eudora.app` bundling + signing/notarization script (new) |
| `.github/workflows/` | CI (core + app) and the tag-driven Release (new) |
| `PORTING.md` | the legacy→modern mapping and deliberate deviations |
| *(repo root)* | the original Eudora 6.2.4 Carbon source (`*.c`, `*.r`, `*.proj`, …) |

## Architecture

- **EudoraCore** (`core/`, C++20) — mail store (mbox + big-endian TOC
  sidecars), MIME/RFC 822 parsing, POP3 / IMAP / SMTP over a portable
  transport (OpenSSL on Linux; a Security-framework/SecureTransport decorator
  on macOS, no external dependency), the filters engine, address book, and
  the outgoing composer. It exposes a stable **C bridge**
  (`core/api/` + `core/include/eudora/eudora_core.h`).
- **EudoraKit** (`swift/EudoraKit/`) — idiomatic Swift types over that C API.
- **EudoraApp** (`swift/EudoraApp/`) — the classic UI in SwiftUI: mailbox
  browser, composer, filters, address book, and settings. macOS only.

The Classic-Mac memory manager, File Manager, and Carbon UI are gone; the
engine is plain C++/POSIX and the UI is SwiftUI. Legacy data is read and
written in its original big-endian, CodeWarrior-aligned layout.

## Building from source

### Core engine + tests (Linux or macOS)

```sh
cmake -B build -S core
cmake --build build -j
ctest --test-dir build --output-on-failure   # 12 suites
```

### The macOS app (macOS 13+, Swift 5.9+)

```sh
swift build            # debug
swift build -c release
```

### Package `Eudora.app`

```sh
swift build -c release
bash packaging/make_app_bundle.sh .build/release dist 6.2.4-macos.1
```

The bundle is ad-hoc signed by default. Set `MACOS_SIGN_IDENTITY` (and the
notary variables) to produce a Developer-ID-signed, notarized build — see the
header of `packaging/make_app_bundle.sh`. Pushing a `v*` tag runs the Release
workflow, which builds and attaches the zip automatically.

## Features

- Mailbox viewer: HTML rendering (remote resources stripped), sorting,
  search, multi-select actions, printing.
- Message actions: reply / reply-all / forward (re-attaching parts) /
  redirect / send-again, junk, priority, labels.
- Filters: full editor plus executed actions — transfer, copy, junk, label,
  status, priority, subject, sound, notify, forward, redirect, reply, open,
  print, speak.
- Composer: NSTextView editor with the system spell/grammar checker, optional
  styled text sent as `multipart/alternative`, stationery, recipient
  autocomplete.
- Accounts: POP3 and IMAP (folder listing, flag write-back, UIDVALIDITY/UID
  sync), auto-check, junk sweep, Getting Attention.
- Address book with groups (recursive expansion) and contact import.
- Classic attachment decoders (uuencode, BinHex 4.0, AppleSingle/Double) and
  full RFC 2047 charset coverage.
- Account passwords stored in the macOS **Keychain**.

## Status

The C++ core is fully test-covered (12 `ctest` suites, run in CI on macOS and
Linux) and the app compiles cleanly in CI (`swift build` debug + release).
The app has **not yet been runtime-tested on a Mac** — on-device validation
(fetch/send, the styled-text composer) is the natural next step. Notarization
is wired but cert-gated (needs an Apple Developer ID).

See [`PORTING.md`](PORTING.md) for the detailed legacy→modern mapping, the TOC
format spec, and the behavioral deviations that were made deliberately.

## License

Distributed under the **BSD 3-Clause Clear License** — the license under
which the Computer History Museum released the Eudora source in 2017
(© 2017, Computer History Museum). It governs both the original source and
this port. See [`LICENSE`](LICENSE). Some legacy files carry additional
historical copyright notices, which are preserved in place.

"Eudora" is a registered trademark, used here only to identify the software
this project derives from. This is an unofficial rebuild, not affiliated with
or endorsed by the trademark holder or the Computer History Museum.

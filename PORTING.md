# Porting Eudora 6.2.4 to Apple Silicon

[![CI](https://github.com/boydj/eudora4mac/actions/workflows/ci.yml/badge.svg)](https://github.com/boydj/eudora4mac/actions/workflows/ci.yml)

This repository contains the original QUALCOMM / Computer History Museum
source of **Eudora 6.2.4 for Mac** (Carbon, PowerPC, CodeWarrior) plus a
new, modern extraction of its core business logic in [`core/`](core/):
**EudoraCore**, a C++20 static library that builds for
`arm64-apple-macos` (and any POSIX host) with CMake.

The legacy tree is left byte-for-byte untouched as reference. Nothing in
`core/` includes a legacy header; every module is a translation, with the
original file/line provenance noted in its header comments.

## Building

```sh
cmake -B build -S core        # on macOS: configures arm64 / macOS 26 by default
cmake --build build
ctest --test-dir build        # 9 suites, all platform-independent
```

- **macOS**: `CMAKE_OSX_ARCHITECTURES=arm64` and
  `CMAKE_OSX_DEPLOYMENT_TARGET=26.0` are set automatically (override with
  the usual cache variables). Product: `libeudora_core.a` +
  `core/include/eudora/`.
- **TLS**: two interchangeable decorators over the same Transport seam.
  On Apple platforms the Security-framework one
  (`net/apple_tls_transport.*`, SecureTransport with custom I/O — the only
  Apple API that can wrap an existing connection, which STARTTLS needs) is
  always built, so TLS works with no external dependency. When
  `find_package(OpenSSL)` succeeds the OpenSSL decorator is preferred; on
  non-Apple platforms it is the only TLS option.
- **Linux**: the same tree builds and the full test suite runs; that is how
  the port is verified in CI-like environments.

### CI/CD (GitHub Actions)

- **CI** (`.github/workflows/ci.yml`), on pushes and pull requests:
  the core builds and its full ctest suite runs on both `ubuntu-latest`
  and `macos-latest`, and the SwiftPM app builds (debug + release) on
  macOS.  Each run packages a ready-to-run **Eudora.app** and uploads it
  as the `Eudora-app` artifact.
- **Release** (`.github/workflows/release.yml`): pushing a `v*` tag
  builds and packages the app and attaches the zip to a GitHub Release.
- **Packaging** (`packaging/make_app_bundle.sh`) also works locally:

  ```sh
  swift build -c release
  bash packaging/make_app_bundle.sh          # -> dist/Eudora.app + zip
  ```

  The bundle is **ad-hoc signed** (no Developer ID in CI): on another
  machine, right-click → Open the first launch, or clear quarantine with
  `xattr -d com.apple.quarantine Eudora.app`.  Real distribution needs a
  Developer ID certificate + notarization (the script marks where they
  slot in).  Bundled builds also enable the real notification-center
  path that bare `swift run` builds cannot use.

## The app — the classic UI, rebuilt

`swift/EudoraApp` is a native SwiftUI recreation of the classic Eudora
experience, running entirely on EudoraCore. On a Mac:

```sh
swift run EudoraApp          # development run
```

or open the package in Xcode and run the `EudoraApp` scheme (make an app
target from it for a bundled release build; set `CFBundleIconFile` to the
classic `Eudora.icns`, which the app also carries as a package resource
and applies at runtime — the Dock shows the original icon either way).

The bundled `swift/EudoraApp/Resources/Eudora.icns` is regenerated from
the original 1990s art by [`tools/make_icon.py`](tools/make_icon.py): the
legacy icon *source* directories (`Icons/`, `Eudora60Icons/`,
`new_icons/`) lost their resource forks in the archive import, so the
compiled icns inside the legacy app bundle is the only surviving art.
The tool decodes its classic elements (`it32`/`t8mk` 128 px on down),
bicubic-upscales the 128 px master for the Retina slots (256/512/1024),
and emits a modern PNG-element icns.  The legacy file itself is left
untouched.

What it recreates, and from where:

- **The mailbox window** (`MailboxPane`) — the classic summary table with
  the Status / Priority / Attachments / Label / Who / Date / K / Subject
  columns (ColumnHead, STR# 26100), the original status glyphs
  (• R D F Q S T), priority chevrons, and the 15-label palette
  (System 7 labels 1-7 plus PrivColors 27900 for 8-15 — names and colors
  editable in Settings), over a preview pane. Double-click opens the
  message in its own window; the Delete key files it in Trash.
- **The Mailboxes sidebar** — In / Out / Trash / Junk first with unread
  counts, then user mailboxes; New Mailbox creation.
- **Message actions** — Reply ⌘R / Reply to All ⇧⌘R (quoting under the
  attribution line, honoring the Replying settings and PREF_NOT_ME),
  Forward, Redirect (the classic "orig (by way of me)" From), Send
  Again, Change Priority, Junk ⇧⌘J / Not Junk (junk transfer score),
  Make Address Book Entry ⌘K — in the Message menu and the context menu.
  Answered messages are marked replied/forwarded/redistributed.
- **The composition window** (`ComposeView`) — Eudora's header block
  (From / To / Subject / Cc / Bcc / Attached) over the body, the icon
  bar's priority popup, signature appending after the classic `-- `
  separator, and the two classic verbs: **Queue** (into Out as QUEUED)
  and **Send** (SMTP now, copy kept in Out as SENT).  The body is an
  NSTextView-backed editor with the system **spell/grammar checker**;
  a **Styled** toggle turns on rich text (Bold ⌘B / Italic ⌘I /
  Underline ⌘U / Fonts), sent as a `multipart/alternative` with the
  plain body first (the classic styled-text composer).  Recipient fields
  **autocomplete** from the Address Book, and **Stationery** (message
  templates in the "Stationery Folder") can be applied or saved from the
  icon bar.
- **Attachments** — the preview pane lists a message's MIME attachment
  parts (via the new `mail/mime_walker`) with click-to-save decoding;
  multipart bodies display their first text part instead of raw
  boundaries.
- **The Filters window** (`FiltersView`) — the two-pane editor:
  filter list with up/down ordering, Match pane (incoming / outgoing /
  manual, header combo, the verb menu with the classic display names,
  conjunction, second term) and the Action pane (up to five actions),
  reading and writing the real "Eudora Filters" file.  Filter events run
  as the original did: incoming after Check Mail, outgoing on Queue,
  manual on ⌘J.  Executed actions: transfer, copy, junk score, label,
  status, priority (incl. raise/lower), subject, sound, notify, **speak**
  (AVSpeechSynthesizer, the action's phrase or the subject), **open**
  (shows each matched message in its own window), **print**, and the
  message-generating **forward** / **redirect** / **reply** — each builds
  the outgoing message with the default personality (Forward re-attaches
  the original's parts; Redirect keeps the "by way of" From; Reply's
  parameter names an optional stationery whose text becomes the body) and
  queues it to Out, marking the original.  Only **personality**
  (re-homing a stored message to a personality) stays inert — it has no
  modern analogue.
- **The Address Book window** — nicknames, addresses, notes, expansion
  preview, saved to the real "Eudora Nicknames" file.  Groups (multi-
  address nicknames) are flagged in the list and expand recursively;
  **Import…** merges contacts from a text, CSV, or tab-delimited file.
- **Settings** — the classic panel-list dialog: Getting Started,
  Personalities (POP3 **and IMAP** accounts, include-in-checks,
  signature choice), Checking Mail (auto-check every N minutes,
  send-on-check, leave-on-server days, skip-big-messages), Sending
  Mail, Replying (quote prefix, attribution, reply-all-includes-self),
  Signatures (files in the classic "Signature Folder"), Getting
  Attention (alert / sound / Dock badge / open mailbox), Junk Mail
  (threshold, transfer score, empty-after-days), Fonts & Display,
  Labels, and the mail folder location. Stored as `EudoraSettings.json`
  in the mail folder; **account passwords live in the macOS Keychain**
  (never in the JSON), and a legacy settings file's inline password is
  migrated to the Keychain on the next save.  Older settings files load
  unchanged — decoding is tolerant of missing keys.
- **Mail automation** — auto-check on the configured interval; after
  each check the classic pipeline runs: incoming filters, the junk sweep
  (score ≥ threshold moves to Junk), junk aging (old junk to Trash),
  then Getting Attention (sound, notification — or a Dock bounce in a
  bare `swift run` build, where the notification center is unavailable —
  and the Dock unread badge).
- **Menus** — Message (Check Mail ⌘M, Send Queued Messages ⌘T, the
  message actions above, Filter Messages ⌘J, Delete ⌘⌫), Special
  (Filters…, Address Book… ⌘L, Empty Trash, Compact Mailbox).

Checking covers **every personality** marked include-in-checks — POP3
accounts via `eudora_pop3_fetch_opts` (UIDL dedup, leave-on-server days,
big-message skip) and IMAP accounts via `eudora_imap_fetch_ex`
(UIDVALIDITY/UID dedup, flags mapped to states, optional
delete-and-expunge) — all delivering into In.

Everything operates on the classic on-disk formats, so a mail folder
migrated from a real Mac (mailboxes + `.toc` files, "Eudora Filters",
"Eudora Nicknames") works in place.

## Attaching a SwiftUI frontend

The repository root carries a **Swift package** (`Package.swift`): add this
repo as a package dependency in Xcode and `import EudoraKit` — SwiftPM
compiles the C++ core itself (TLS included, via the Security framework)
and `swift/EudoraKit` provides idiomatic types (`Mailbox`,
`ParsedMessage` with `MessagePart` attachments, `AddressBook`,
`FilterSet`, `Composer`, `pop3Fetch`/`imapFetch`/`smtpSend`).

Underneath, `core/include/eudora/eudora_core.h` is a flat `extern "C"`
interface usable from any language:

- mailboxes: `eudora_mailbox_open` (reads or rebuilds the `.toc`),
  summaries, raw message reads, state changes, delete + compact, save
- message parsing: RFC 822 headers (RFC 2047-decoded), MIME facts,
  base64/quoted-printable body decoding, address lists
- `eudora_pop3_fetch`: one call: connect (TLS optional), authenticate
  (SASL/APOP/USER-PASS), download into an mbox with proper envelopes,
  update the `.toc`
- `eudora_smtp_send`: one call: EHLO, STARTTLS, AUTH, envelope, DATA with
  dot stuffing
- filters: load/parse/save the classic "Eudora Filters" file and evaluate
  messages, returning fired actions in the engine's execution order
- address book: the "Eudora Nicknames" format, nickname expansion, and
  membership queries backing the filters' intersects-file verb
- composition: an RFC 822 builder (RFC 2047 headers, QP bodies,
  multipart/mixed base64 attachments) feeding `eudora_smtp_send`

C++ consumers use `#include <eudora/core.hpp>`; the IMAP engine
(`protocols/imap.hpp`) is currently C++-only.

## What was ported (and from where)

| Modern module | Legacy source | Notes |
|---|---|---|
| `compat/endian.hpp` | — | explicit big-endian codecs; the legacy tree had **zero** byte-swapping (never ran little-endian) |
| `compat/pstring`, `compat/macroman` | Pascal-string idioms, MacRoman | text is UTF-8 internally; Pascal/MacRoman only at the serialization boundary |
| `compat/macdate` | `DateToSeconds`, `ZoneSecs`, `TZName2Offset`, `CStr2Zone` | timestamps stay Mac-epoch (1904) in the file format; Unix epoch at the API |
| `compat/hashes` | `HashWithSeedLo`/`MIDHash` (message.c:4551) | bit-identical, so stored `msgIdHash`/`uidHash` stay valid |
| `mailstore/toc_format` | `TOCType`/`MSumType` (Include/mailbox.h), `toc.c` | see **TOC binary format** below |
| `mailstore/toc_io` | `ReadDForkTOC`/`WriteTOC` (toc.c) | data-fork sidecar only; resource-fork TOCs can't exist on modern filesystems |
| `mailstore/line_reader` | `lineio.c` | accepts CR/LF/CRLF with byte-accurate offsets (legacy was CR-only) |
| `mailstore/mbox_parser` | `buildtoc.c` (`BuildTOC`/`ReadSum`/`IsFromLine`/`BeautifyDate`/`BeautifyFrom`/`BeautifySubj`/`GleanFrom`/`UnixDate2Secs`) | includes the composition-order out-message heuristic, bulk detection, body-format sniffing, RFC 2047 decoding of From/Subject |
| `mailstore/table_of_contents` | `SaveMessageSum`/`DeleteSum`/`FindSumByHash` (mailbox.c), `GetTOCK`/`FindTOCSpot` | |
| `mailstore/compaction` | `squish.c` (`NeedsCompaction`/`CompactMailbox`) | note: legacy `compact.c` was the *composition window*, not compaction |
| `mail/mime_codec` | `Encode64`/`Decode64`/`EncodeQP`/`DecodeQP` (mime.c:106-478) | streaming state machines incl. text-mode newline fixes and SMTP transparency |
| `mail/rfc2047` | `Fix1342`/`Translate1342`/`PseudoQP` (lex822.c) | decodes to UTF-8 (legacy went to MacRoman); unknown charsets left intact |
| `mail/lex822` | `lex822.c` token classes | tokenizes in-memory field bodies (legacy read from TransStream) |
| `mail/header_parser` | `header.c` (`ReadHeader`/`HeaderDesc`) | unfolding, MIME type/params/boundary/filename, transfer encoding |
| `mail/address_parser` | `SuckPtrAddresses` (address.c:84) | verbatim state-table port; group syntax yields `name:`/`;` marker tokens exactly as the original did |
| `net/transport` | `TransVector` (mydefs.h:369) | abstract class; `flush_input` fixes the pop.c:532 `OTFlushInput` vtable leak; cancellation is a per-connection atomic instead of the thread-local `CommandPeriod` |
| `net/posix_transport` | `tcp.c` (2,638 lines of Open Transport) | getaddrinfo + poll-sliced blocking sockets; no event loop inside reads |
| `net/line_receiver` | `NetRecvLine` (ph.c:3055) | incl. the `TREAT_BODY_CR_AS_CRLF` Exchange workaround |
| `net/tls_transport` | `ssl.c` + `OpenSSL.cp` | same decorator/custom-BIO architecture, direct OpenSSL link; per-stream state (legacy had one global `ESSLSubTrans` slot) |
| `protocols/pop3` | `pop.c` protocol half | greeting/CAPA/STLS, auth ladder, STAT/LIST/UIDL/LAST/TOP/RETR/DELE, `ReadPOPLine` dot-unstuffing + `>From ` escaping |
| `protocols/smtp` | `sendmail.c` protocol half | EHLO parsing (incl. `AUTH=` quirk), HELO fallback, `GetReplyLo` multiline reader, 452→552 and rcpt-5xx→550 fixups, DATA dot stuffing |
| `protocols/sasl` | `sasl.c` | CRAM-MD5 / PLAIN / LOGIN + APOP digest |
| `protocols/md5` | `md5.c` | RFC 1321/2104 |
| `filters/filter_file` | `filtmng.c` + `FiltDefs` string tables | text format round-trips; `copyInstead` and `raise`/`lower` migrations preserved |
| `filters/match_engine` | `filtrun.c` match cascade | all 15 verbs, LWSP-insensitive matching, junk meta-term, `FAPass` execution ordering, stop actions |
| `filters/regexp` | `regexp.c` (Spencer V8 adaptation) | POSIX extended regex (same Spencer lineage) behind the `SearchRegExpPtr` interface |
| `addressbook/nicknames` | `nickmng.c`/`nickexp.c` | "Eudora Nicknames" alias/note format, quoted names, continuations, recursive expansion with cycle protection |
| `mail/composer` | `sendmail.c` MIME-generation half | RFC 822 builder: 2047-encoded headers, dated/Message-Id'd, QP or 7bit bodies, multipart/mixed base64 attachments; BinHex/AppleDouble/PICT conversions dropped |
| `protocols/imap` | `CrispinIMAP/` + `imapnetlib.c` role | direct IMAP4rev1 engine: tagged commands, full literal handling both directions, AUTHENTICATE/LOGIN, SELECT/LIST/FETCH/STORE/SEARCH/APPEND/EXPUNGE, STARTTLS hook |

## The `.toc` binary format (decoded in `mailstore/toc_format.*`)

A `.toc` file is a raw memory image of the legacy in-memory Handle:
a **278-byte header** (`TOCType` through `sums`) followed by one
**220-byte record per message** (`MSumType`), all **big-endian**, laid out
with **mac68k (2-byte) struct alignment**, **CodeWarrior MSB-first
bitfields**, and smallest-fit enums (`StateEnum` is one byte). There is no
magic number; validity is the size invariant
`file == 278 + max(1, count) × 220` plus per-record range checks
(`InsaneTOC`). Live pointers the original serialized as garbage (`refN`,
`messH`, `cache`, `win`, `next`, …) are skipped on read and zeroed on
write, exactly as `ReadTOC` reset them on load. `boxSize` stores the
mailbox size **plus one** ("add 1 to signal that we know it's ok",
toc.c:426). The embedded `FSSpec` is machine-local; like the original,
the loader re-stamps the mailbox location from the file's actual path.

Note: the shipping **Carbon (CFM)** build's layout is implemented. The
unfinished Mach-O target in `Eudora.proj.xml` used PowerPC natural
alignment and would have produced *different* file offsets; it never
shipped, so `.toc` files in the wild are the mac68k layout.

## Deliberately not ported

- **All Carbon UI** — windows, dialogs, menus, PETE editor, toolbar,
  drawers (`boxact.c`, `compact.c`, `filtwin.c`, `mywindow.c`, …). The
  Swift package / C API is the attachment point for a new SwiftUI
  frontend.
- **The IMAP sync engine** (`imapdownload.c`'s local-mirror logic): the
  protocol layer is fully ported (`protocols/imap`); a frontend drives
  synchronization policy through it.
- **Dead code**: the abandoned "MIME Store" rewrite
  (`mstore.c`/`mstoc.c`/`msmaildb.c`/`msiddb.c`/`msinfo.c` — never called
  from live code), `ctb.c`/`dial.c` (compiled out since before 6.2.4),
  `sslCerts.cp` (behind `#if 0`), and every CFM/Mach-O bridge
  (`MachOWrapper.*`, `wrappers.cp`, the `CreateSSLBundle` symlink hack).
- Address book / nicknames, preferences, junk scoring (the Bayes plugin
  API), attachment en/decoding beyond the transfer codecs
  (BinHex/AppleDouble/AppleSingle, PICT/QuickTime), printing, speech,
  LDAP/Ph, Palm conduits, the ad/registration subsystems.

## Known behavioral deviations

- Text at the API is UTF-8; legacy TOC records written by real Eudora
  store MacRoman unless `FLAG_UTF8` is set — both are decoded correctly,
  and records are written back with the same convention.
- Nickname ("intersects") terms match against literal address lists;
  nickname *expansion* requires the address book, exposed as a hook.
- Filter "date" terms match a modern short date string rather than the
  resource-driven localized date formats.
- RFC 2047 decoding covers utf-8, iso-8859-1, windows-1252, us-ascii and
  macintosh; other charsets are left encoded (the original punted to its
  resource translation tables in the same situation).
- POP3 command pipelining (the `POPCmds` stack) is not implemented;
  commands run sequentially.
- Filter *terms* that match on the junk score (`less`/`greater` against
  the summary) never match through `eudora_filters_run`: the C entry
  point evaluates messages without their TOC summaries.  The junk
  *action* (assigning a score) and the threshold sweep work fully.
- IMAP accounts fetch INBOX into the local In mailbox (the classic
  minimal-download flavor); folder browsing (eudora_imap_list_folders) and
  read/replied flag write-back (eudora_imap_sync_flags, via an
  X-Eudora-Imap-Uid stamp) are implemented, but full two-way folder sync
  and the big-message/leave-on-server-days options remain POP3-only.

### Deferred UI pieces

- **Drag-and-drop** of messages onto sidebar mailboxes is not implemented;
  the Transfer context menu / menu command moves messages instead.  (Per-row
  drag off a SwiftUI `Table` is fragile enough that it was left out rather
  than shipped without runtime verification.)  Sorting, column search,
  multi-message selection actions, printing, and HTML rendering are all in.

### Consciously not reproduced (from the fidelity audit)

These legacy behaviors were reviewed and deliberately left un-ported, with
the reasoning recorded so the divergence is a choice, not an oversight:

- **Pre-6.2 `CleanseTOC` version-gated upgrades** — the minor-version
  fixups a real 6.2.4 would run when loading an *older* TOC (minor&lt;7
  subject rewriting, minor&lt;9 UTF-8 stripping, mood/spam resets). A 6.2.4
  TOC is already minor 9, so these only fire on ancient imports; they carry
  real regression risk for no real-world input. The always-run parts
  (arrival-time fallback, serial renumbering) *are* implemented.
- **Byte-for-byte preservation of inert TOC fields** — pluginKey,
  pluginValue, profile[], ezOpenSerialNum and the spares are decoded and
  re-encoded as zero rather than passed through (see toc_format.hpp); no
  feature the port implements reads them.
- **Timezone offset truncation bug** — the classic `'zon#'` handling
  truncated a few far-east offsets through a `short index`; the port
  recognizes those zones with their *correct* offsets instead of
  reproducing the bug.
- **Address auto-qualification** — legacy appended a default domain
  (`PREF_AUTOQUAL`) to bare local parts; that pref is unset by default, so
  the port matches the common case and simply omits the feature.
- **MacRoman script case-folding** in `same_address` — the port folds case
  in ASCII, correct for the UTF-8 addresses modern mail uses.

### Hardening beyond the original

- IMAP literal sizes are capped (128 MiB) and the response tokenizer is
  hardened so a malicious or broken server cannot hang the client or drive
  an unbounded allocation.
- Line breaks in a sender address, credentials, or an IMAP mailbox name are
  rejected to prevent command injection.
- The OpenSSL TLS path enforces a TLS 1.2 minimum (the Apple SecureTransport
  path already did).
- `eudora_mailbox_summary`'s returned `from`/`subject` pointers are stable
  for the mailbox handle's whole life; message appends `fsync` the data and
  roll back a partial write.

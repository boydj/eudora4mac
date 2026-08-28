#!/usr/bin/env bash
# Populate a demo "Eudora Folder" with sample mail so the app has something
# to show in screenshots.  The mailboxes are plain classic-Eudora mbox files
# (a "From <addr> <date>" envelope line per message); the app builds the .toc
# sidecars itself on first open.
#
# Usage: packaging/screenshots/seed_demo_folder.sh <destination-folder>
# The destination is created if needed and populated with In / Out / Trash /
# Junk / a user mailbox, plus "Eudora Nicknames" and "EudoraSettings.json".

set -euo pipefail

DEST="${1:?usage: seed_demo_folder.sh <destination-folder>}"
mkdir -p "$DEST" "$DEST/Signature Folder"

# --- In: a handful of varied messages ---------------------------------------
cat > "$DEST/In" <<'MBOX'
From welcome@eudora.example Mon Jan 06 09:14:22 2025
From: The Eudora Team <welcome@eudora.example>
To: You <you@example.com>
Subject: Welcome back to Eudora
Date: Mon, 6 Jan 2025 09:14:22 -0800
X-Priority: 2

Eudora rides again — now native on Apple Silicon.

Your mailboxes, filters, and nicknames carry over from the classic
Folder untouched. Check Mail, compose, and file away just like before.

-- The Eudora Team

From digest@swmews.example Mon Jan 06 07:02:10 2025
From: Software Weekly <digest@swmews.example>
To: You <you@example.com>
Subject: This week: a mail client returns from 2006
Date: Mon, 6 Jan 2025 07:02:10 -0800
Content-Type: text/html; charset=us-ascii

<html><body>
<h2>Software Weekly</h2>
<p>The <b>classic Eudora</b> source, released by the Computer History
Museum, has been rebuilt to run natively on modern macOS.</p>
<ul><li>POP3, IMAP and SMTP over TLS</li>
<li>Filters, labels, and the mood-watch junk score</li>
<li>The original mbox + TOC on-disk formats, unchanged</li></ul>
<p>Remote images are stripped by default — the classic privacy setting.</p>
</body></html>

From maria@example.org Sun Jan 05 18:40:55 2025
From: Maria Alvarez <maria@example.org>
To: You <you@example.com>
Subject: Re: dinner Saturday?
Date: Sun, 5 Jan 2025 18:40:55 -0800

Saturday works great. 7pm at the usual place?

I'll bring the photos from the trip — remind me to send them
after we talk.

Maria

From reports@build.example Sat Jan 04 11:05:00 2025
From: Build Bot <reports@build.example>
To: You <you@example.com>
Subject: Nightly build succeeded (log attached)
Date: Sat, 4 Jan 2025 11:05:00 -0800
Content-Type: multipart/mixed; boundary="=_demo_boundary_="

--=_demo_boundary_=
Content-Type: text/plain; charset=us-ascii

All 12 test suites passed. Full log attached.

--=_demo_boundary_=
Content-Type: text/plain; name="build.log"
Content-Disposition: attachment; filename="build.log"

[12/12] test_imap_fetch .... Passed
100% tests passed, 0 tests failed out of 12
--=_demo_boundary_=--

From newsletter@trailclub.example Fri Jan 03 08:30:00 2025
From: Trail Club <newsletter@trailclub.example>
To: You <you@example.com>
Subject: January hikes and a new members' map
Date: Fri, 3 Jan 2025 08:30:00 -0800

Happy new year! Three group hikes this month, plus the interactive
trail map members asked for. Details on the site.

Reply STOP to unsubscribe.
MBOX

# --- Out: one message kept as sent ------------------------------------------
cat > "$DEST/Out" <<'MBOX'
From you@example.com Sun Jan 05 19:02:11 2025
From: You <you@example.com>
To: Maria Alvarez <maria@example.org>
Subject: Re: dinner Saturday?
Date: Sun, 5 Jan 2025 19:02:11 -0800

7pm is perfect. See you Saturday!
MBOX

# --- A user mailbox ---------------------------------------------------------
cat > "$DEST/Projects" <<'MBOX'
From lee@example.net Thu Jan 02 14:20:00 2025
From: Lee Nakamura <lee@example.net>
To: You <you@example.com>
Subject: Porting notes for the mail store
Date: Thu, 2 Jan 2025 14:20:00 -0800

The big-endian TOC serializer is the tricky part — mind the
CodeWarrior mac68k alignment on MSumType. Everything else fell out
cleanly once the Pascal strings were handled.

Lee
MBOX

# Empty special mailboxes so the sidebar shows them.
: > "$DEST/Trash"
: > "$DEST/Junk"

# --- Address book -----------------------------------------------------------
cat > "$DEST/Eudora Nicknames" <<'NICK'
alias Maria maria@example.org
note Maria <name:Maria Alvarez>photographer, hiking friend
alias Lee lee@example.net
note Lee <name:Lee Nakamura>on the porting project
alias "Trail Club" newsletter@trailclub.example
NICK

# --- Settings: one personality, no password (Keychain holds those) ----------
cat > "$DEST/EudoraSettings.json" <<'JSON'
{
  "personalities" : [
    {
      "id" : "11111111-1111-1111-1111-111111111111",
      "name" : "Dominant",
      "realName" : "Alex Demo",
      "emailAddress" : "you@example.com",
      "username" : "you@example.com",
      "popHost" : "mail.example.com",
      "popPort" : 995,
      "popSecurity" : "immediateTLS",
      "smtpHost" : "smtp.example.com",
      "smtpPort" : 587,
      "smtpSecurity" : "startTLS",
      "accountType" : "imap",
      "includeInChecks" : true
    }
  ],
  "dominantIndex" : 0,
  "autoCheck" : false
}
JSON

echo "seeded demo mail folder at: $DEST"
ls -1 "$DEST"

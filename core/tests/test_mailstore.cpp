#include <cstdio>
#include <filesystem>
#include <fstream>

#include "compat/hashes.hpp"
#include "compat/macdate.hpp"
#include "mailstore/compaction.hpp"
#include "mailstore/mbox_parser.hpp"
#include "mailstore/toc_format.hpp"
#include "mailstore/toc_io.hpp"
#include "test_framework.hpp"

using namespace eudora;
namespace fs = std::filesystem;

namespace {

fs::path temp_dir() {
    const fs::path d = fs::temp_directory_path() / "eudora_core_tests";
    fs::create_directories(d);
    return d;
}

void write_file(const fs::path &p, const std::string &content) {
    std::ofstream f(p, std::ios::binary);
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
}

// A small CR-terminated mailbox, as a real Mac Eudora file would look.
const std::string kMailbox =
    "From alice@example.com Wed Jun 14 12:36:18 1989\r"
    "Date: Wed, 14 Jun 1989 12:36:18 -0500\r"
    "From: Alice Wonder <alice@example.com>\r"
    "To: bob@example.org\r"
    "Subject: RE: lunch plans\r"
    "Message-Id: <123.abc@example.com>\r"
    "Precedence: bulk\r"
    "\r"
    "Shall we?\r"
    "<x-html><b>hi</b></x-html>\r"
    "From bob@example.org Thu Jun 15 09:00:00 1989\r"
    "Date: Thu, 15 Jun 1989 09:00:00 GMT\r"
    "From: bob@example.org (Bob Builder)\r"
    "To: alice@example.com\r"
    "Subject: found it\r"
    " continued subject\r"
    "\r"
    "body line\r";

} // namespace

TEST_CASE("is_from_line recognizes sendmail envelopes") {
    CHECK(is_from_line("From paul@uxc.cso.uiuc.edu Wed Jun 14 12:36:18 1989"));
    CHECK(is_from_line("From alice@example.com Wed Jun 14 12:36:18 1989\r"));
    CHECK(!is_from_line("From: someone <a@b.c>"));
    CHECK(!is_from_line("FromX y z"));
    CHECK(!is_from_line("From only-an-address"));
    // "remote from host" style (UUCP) is accepted.
    CHECK(is_from_line("From uunet!alice Wed Jun 14 12:36:18 1989 remote from uunet"));
}

TEST_CASE("beautify_from") {
    CHECK_EQ(beautify_from("Alice Wonder <alice@example.com>"), "Alice Wonder");
    CHECK_EQ(beautify_from("bob@example.org (Bob Builder)"), "Bob Builder");
    CHECK_EQ(beautify_from("<plain@addr.example>"), "<plain@addr.example>");
    CHECK_EQ(beautify_from("\"Quoted Name\" <q@example.com>"), "Quoted Name");
    // A whitespace-only From was non-empty before trimming, so it becomes
    // SOME_BOZO "Unspecified" (buildtoc.c:1214).
    CHECK_EQ(beautify_from("   "), "Unspecified");
    // The parenthesized-name branch runs even when the value starts with '<'.
    CHECK_EQ(beautify_from("<orig@x.com> (Joe)"), "Joe");
}

TEST_CASE("beautify_subject fixes Outlookisms") {
    CHECK_EQ(beautify_subject("RE: hello"), "Re: hello");
    CHECK_EQ(beautify_subject("FW: hello"), "Fwd: hello");
    // The legacy match is case-INSENSITIVE (striscmp).
    CHECK_EQ(beautify_subject("re: hello"), "Re: hello");
    CHECK_EQ(beautify_subject("Fw: hello"), "Fwd: hello");
    CHECK_EQ(beautify_subject("Re: hello"), "Re: hello"); // already fine
    CHECK_EQ(beautify_subject("Fwd: hello"), "Fwd: hello"); // not double-fixed
    CHECK_EQ(beautify_subject("RE: x", false), "RE: x");  // pref off
}

TEST_CASE("beautify_date honors named far-east zones") {
    // JST is in the classic 'zon#' table (+9h); it must not be treated as
    // local time.  The zone is reported in seconds east of UTC.
    long jz = 0;
    beautify_date("Mon, 1 Jan 2001 09:00:00 JST", jz);
    CHECK_EQ(jz, 9 * 3600);
    long nz = 0;
    beautify_date("Mon, 1 Jan 2001 00:00:00 NZD", nz);
    CHECK_EQ(nz, 13 * 3600);
    long hz = 0;
    beautify_date("Mon, 1 Jan 2001 00:00:00 HST", hz);
    CHECK_EQ(hz, -10 * 3600);
}

TEST_CASE("beautify_date parses RFC822 dates") {
    long zone = 0;
    const std::uint32_t secs = beautify_date("Wed, 14 Jun 1989 12:36:18 -0500", zone);
    CHECK_EQ(zone, -5 * 3600);
    const DateTimeParts p = mac_seconds_to_date(secs);
    // 12:36:18 -0500 == 17:36:18 UTC
    CHECK_EQ(p.year, 1989);
    CHECK_EQ(p.month, 6);
    CHECK_EQ(p.day, 14);
    CHECK_EQ(p.hour, 17);
    CHECK_EQ(p.minute, 36);
    CHECK_EQ(p.second, 18);

    long zone2 = 0;
    const std::uint32_t secs2 = beautify_date("Thu, 15 Jun 1989 09:00:00 GMT", zone2);
    CHECK_EQ(zone2, 0);
    const DateTimeParts p2 = mac_seconds_to_date(secs2);
    CHECK_EQ(p2.hour, 9);

    // Two-digit year gets windowed.
    long zone3 = 0;
    const std::uint32_t secs3 = beautify_date("1 Jan 99 00:00:00 +0000", zone3);
    CHECK_EQ(mac_seconds_to_date(secs3).year, 1999);
}

TEST_CASE("kr_hash matches legacy algorithm shape") {
    // Deterministic + sensitive to every bit; sentinel values never returned.
    const auto h1 = kr_hash("123.abc@example.com");
    const auto h2 = kr_hash("123.abc@example.con");
    CHECK(h1 != 0);
    CHECK(h1 != h2);
    CHECK_EQ(mid_hash("<123.abc@example.com>"), h1);
    CHECK_EQ(mid_hash("(comment) <123.abc@example.com>"), h1);
    // Legacy MIDHash hashed an empty first token as Hash("") == 1 (a valid
    // hash), so empty/comment-only Message-Ids dedup against each other.
    CHECK_EQ(mid_hash(""), 1u);
    CHECK_EQ(mid_hash("(only a comment)"), 1u);
    CHECK_EQ(kr_hash(""), 1u);
}

TEST_CASE("mbox parsing builds correct summaries") {
    const fs::path box = temp_dir() / "In";
    write_file(box, kMailbox);

    auto toc = build_toc(box);
    CHECK(toc.has_value());
    if (!toc)
        return;
    CHECK_EQ(toc->count(), 2);
    CHECK(toc->which == MailboxKind::In);

    const auto &s1 = toc->sums[0];
    CHECK_EQ(s1.offset, 0);
    CHECK_EQ(s1.from, "Alice Wonder");
    CHECK_EQ(s1.subject, "Re: lunch plans");
    CHECK(valid_hash(s1.msg_id_hash));
    CHECK_EQ(s1.msg_id_hash, kr_hash("123.abc@example.com"));
    CHECK((s1.opts & sumopt::kBulk) != 0);   // Precedence: bulk
    CHECK((s1.opts & sumopt::kHtml) != 0);   // <x-html> body tag
    CHECK_EQ(s1.orig_zone, -300);            // -0500 in minutes
    CHECK(s1.serial_num == 1);

    const auto &s2 = toc->sums[1];
    CHECK_EQ(s2.from, "Bob Builder");
    // A folded Subject keeps a separating space (buildtoc.c:528).
    CHECK_EQ(s2.subject, "found it continued subject");
    CHECK(s2.serial_num == 2);

    // Offsets tile the file exactly.
    CHECK_EQ(s2.offset, s1.offset + s1.length);
    CHECK_EQ(static_cast<std::size_t>(s2.offset + s2.length), kMailbox.size());
    // Body offset points at the blank separator line.
    const auto blank = kMailbox.find("\r\r");
    CHECK_EQ(static_cast<std::size_t>(s1.body_offset), blank + 1);
}

TEST_CASE("mbox parsing handles LF line endings identically for offsets") {
    std::string lf = kMailbox;
    for (auto &c : lf)
        if (c == '\r')
            c = '\n';
    const fs::path box = temp_dir() / "InLF";
    write_file(box, lf);
    auto toc = build_toc(box);
    CHECK(toc.has_value());
    if (!toc)
        return;
    CHECK_EQ(toc->count(), 2);
    CHECK_EQ(static_cast<std::size_t>(toc->sums[1].offset + toc->sums[1].length),
             lf.size());
}

TEST_CASE("TOC image encode/decode round trip with invariants") {
    TableOfContents toc;
    toc.which = MailboxKind::In;
    toc.imap = false;
    toc.preview_hi = 42;
    toc.last_sort = 3;
    toc.sorts[0] = 5;
    toc.does_expire = true;
    toc.expire_units = ExpireUnits::Months;
    toc.expire_interval = 7;

    MessageSummary s;
    s.offset = 0;
    s.length = 100;
    s.body_offset = 40;
    s.state = MessageState::Replied;
    s.spam_score = -5;
    s.spam_because = 3;
    s.seconds = 0xA1B2C3D4u;
    s.orig_zone = -300;
    s.flags = sumflag::kHasAttachments | sumflag::kUtf8;
    s.opts = sumopt::kBulk | sumopt::kFlowed;
    s.from = "Alice Wonder";
    s.subject = "Re: lunch plans";
    s.priority = 80;
    s.score = -2;
    s.out_type = 2;
    s.msg_id_hash = 0x12345678u;
    s.uid_hash = 0x9ABCDEF0u;
    s.saved_pos = {10, 20, 300, 400};
    toc.append(s);

    MessageSummary s2 = s;
    s2.offset = 100;
    s2.length = 50;
    s2.body_offset = 10;
    s2.state = MessageState::Unread;
    s2.from = "Bob";
    s2.subject = "hi";
    toc.append(s2);

    const auto image = tocfmt::encode(toc, "In", 150, 0xDEADBEEFu);
    CHECK_EQ(image.size(), tocfmt::image_size(2));
    CHECK_EQ(image.size(), 278u + 2u * 220u);

    tocfmt::TocError err;
    auto back = tocfmt::decode(image, &err, 150);
    CHECK(err == tocfmt::TocError::None);
    CHECK(back.has_value());
    if (!back)
        return;
    CHECK_EQ(back->count(), 2);
    CHECK(back->which == MailboxKind::In);
    CHECK_EQ(back->preview_hi, 42);
    CHECK_EQ(back->last_sort, 3);
    CHECK_EQ(back->sorts[0], 5);
    CHECK(back->does_expire);
    CHECK(back->expire_units == ExpireUnits::Months);
    CHECK_EQ(back->expire_interval, 7u);
    CHECK_EQ(back->box_size, 151); // size + 1
    CHECK_EQ(back->write_date, 0xDEADBEEFu);

    const auto &r = back->sums[0];
    CHECK_EQ(r.length, 100);
    CHECK_EQ(r.body_offset, 40);
    CHECK(r.state == MessageState::Replied);
    CHECK_EQ(r.spam_score, -5);
    CHECK_EQ(r.spam_because, 3);
    CHECK_EQ(r.seconds, 0xA1B2C3D4u);
    CHECK_EQ(r.orig_zone, -300);
    CHECK_EQ(r.flags, s.flags);
    CHECK_EQ(r.opts, s.opts);
    CHECK_EQ(r.from, "Alice Wonder");
    CHECK_EQ(r.subject, "Re: lunch plans");
    CHECK_EQ(r.priority, 80);
    CHECK_EQ(r.score, -2);
    CHECK_EQ(r.out_type, 2);
    CHECK_EQ(r.msg_id_hash, 0x12345678u);
    CHECK_EQ(r.uid_hash, 0x9ABCDEF0u);
    CHECK_EQ(r.saved_pos.top, 10);
    CHECK_EQ(r.saved_pos.right, 400);
    CHECK_EQ(r.serial_num, 1);
    CHECK_EQ(back->next_serial_num, 3);

    // Bit-level spot checks against the big-endian layout: state byte at
    // offset 12 of the first summary; count at header offset 262.
    CHECK_EQ(image[278 + 12], static_cast<std::uint8_t>(MessageState::Replied));
    CHECK_EQ(image[262], 0);
    CHECK_EQ(image[263], 2);
    // spam word: spamScore -5 => 0xFB in the top byte (MSB-first bitfield).
    CHECK_EQ(image[278 + 14], 0xFB);
}

TEST_CASE("TOC decode rejects corrupt images") {
    TableOfContents toc;
    MessageSummary s;
    s.offset = 0;
    s.length = 100;
    toc.append(s);
    auto image = tocfmt::encode(toc, "Box", 100, 0);

    tocfmt::TocError err;
    // Truncated image fails the size invariant.
    auto bad = tocfmt::decode(
        std::span<const std::uint8_t>(image.data(), image.size() - 1), &err);
    CHECK(!bad.has_value());

    // Summary range beyond the mailbox fails InsaneTOC's check.
    auto bad2 = tocfmt::decode(image, &err, 50);
    CHECK(!bad2.has_value());
    CHECK(err == tocfmt::TocError::BadSummaryRange);

    // Future version is refused.
    image[0] = 0;
    image[1] = 9;
    auto bad3 = tocfmt::decode(image, &err);
    CHECK(!bad3.has_value());
    CHECK(err == tocfmt::TocError::UnsupportedVersion);
}

TEST_CASE("TOC file round trip via toc_io") {
    const fs::path box = temp_dir() / "RoundTrip";
    write_file(box, kMailbox);
    auto toc = build_toc(box);
    CHECK(toc.has_value());
    if (!toc)
        return;

    const fs::path tocFile = toc_path_for_mailbox(box);
    CHECK_EQ(tocFile.filename().string(), "RoundTrip.toc");
    CHECK(write_toc(*toc, tocFile));

    tocfmt::TocError err;
    auto back = read_toc(tocFile, box, &err);
    CHECK(back.has_value());
    CHECK(err == tocfmt::TocError::None);
    if (back)
        CHECK_EQ(back->count(), toc->count());
}

TEST_CASE("compaction removes deleted messages") {
    const fs::path box = temp_dir() / "Squish";
    write_file(box, kMailbox);
    auto toc = build_toc(box);
    CHECK(toc.has_value());
    if (!toc)
        return;

    CHECK(!needs_compaction(*toc, static_cast<std::int64_t>(kMailbox.size())));

    // Delete the first message; the file now has a hole at the front.
    const std::int32_t second_len = toc->sums[1].length;
    CHECK(toc->remove(0));
    CHECK(needs_compaction(*toc, static_cast<std::int64_t>(kMailbox.size())));
    CHECK_EQ(wasted_bytes(*toc, static_cast<std::int64_t>(kMailbox.size())),
             static_cast<std::int64_t>(kMailbox.size()) - second_len);

    CHECK(compact_mailbox(*toc));
    CHECK_EQ(toc->sums[0].offset, 0);
    CHECK_EQ(static_cast<std::int64_t>(fs::file_size(box)),
             static_cast<std::int64_t>(second_len));

    // The surviving message still parses from its new offset.
    auto again = build_toc(box);
    CHECK(again.has_value());
    if (again) {
        CHECK_EQ(again->count(), 1);
        CHECK_EQ(again->sums[0].from, "Bob Builder");
    }
}

EUTEST_MAIN

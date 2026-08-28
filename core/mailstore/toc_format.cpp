#include "mailstore/toc_format.hpp"

#include "compat/endian.hpp"
#include "compat/macroman.hpp"
#include "compat/pstring.hpp"

namespace eudora::tocfmt {

namespace {

// The `from`/`subj` Pascal strings are MacRoman unless FLAG_UTF8 is set.
std::string decode_sum_string(const BigEndianReader &r, std::size_t base,
                              std::size_t off, std::size_t cap, bool utf8) {
    const std::string raw = pascal_to_string(r.bytes(base + off, cap));
    return utf8 ? raw : macroman_to_utf8(raw);
}

void encode_sum_string(BigEndianWriter &w, std::size_t base, std::size_t off,
                       std::size_t cap, const std::string &text, bool utf8) {
    std::string raw;
    if (utf8) {
        raw = text;
    } else {
        utf8_to_macroman(text, raw);
    }
    std::vector<std::uint8_t> buf(cap, 0);
    string_to_pascal(raw, buf);
    w.bytes(base + off, buf);
}

MessageSummary decode_summary(const BigEndianReader &r, std::size_t base) {
    MessageSummary s;
    s.offset = r.i32(base + sum::offset);
    s.length = r.i32(base + sum::length);
    s.body_offset = r.i32(base + sum::bodyOffset);
    s.state = static_cast<MessageState>(r.u8(base + sum::state));

    const std::uint32_t spam = r.u32(base + sum::spamBits);
    s.spam_score = static_cast<std::int8_t>(sign_extend(
        cw_bits(spam, spambits::spamScore_first, spambits::spamScore_width), 8));
    s.spam_because = static_cast<std::uint8_t>(
        cw_bits(spam, spambits::spamBecause_first, spambits::spamBecause_width));

    s.arrival_seconds = r.u32(base + sum::arrivalSeconds);
    s.from_hash = r.u32(base + sum::fromHash);
    s.serial_num = r.i32(base + sum::serialNum);
    s.seconds = r.u32(base + sum::seconds);
    s.flags = r.u32(base + sum::flags);
    s.saved_pos.top = r.i16(base + sum::savedPos);
    s.saved_pos.left = r.i16(base + sum::savedPos + 2);
    s.saved_pos.bottom = r.i16(base + sum::savedPos + 4);
    s.saved_pos.right = r.i16(base + sum::savedPos + 6);
    s.priority = r.u8(base + sum::priority);
    s.orig_priority = r.u8(base + sum::origPriority);
    s.table_id = r.i16(base + sum::tableId);

    const std::uint16_t scoreWord = r.u16(base + sum::scoreBits);
    // short score:4 (MSB-first: bits 15..12, signed), outType:4 (11..8).
    s.score = static_cast<std::int8_t>(
        sign_extend((scoreWord >> 12) & 0xF, 4));
    s.out_type = static_cast<std::uint8_t>((scoreWord >> 8) & 0xF);

    s.orig_zone = r.i16(base + sum::origZone);
    s.sig_id = r.u32(base + sum::sigId);

    const bool utf8 = (s.flags & sumflag::kUtf8) != 0;
    s.from = decode_sum_string(r, base, sum::from, sum::fromCap, utf8);
    s.pop_pers_id = r.u32(base + sum::popPersId);
    s.pers_id = r.u32(base + sum::persId);
    s.msg_id_hash = r.u32(base + sum::msgIdHash);
    s.subj_id = r.i16(base + sum::subjId);
    s.subject = decode_sum_string(r, base, sum::subj, sum::subjCap, utf8);
    s.opts = r.u32(base + sum::opts);
    s.uid_hash = r.u32(base + sum::uidHash);
    return s;
}

void encode_summary(BigEndianWriter &w, std::size_t base, const MessageSummary &s) {
    w.i32(base + sum::offset, s.offset);
    w.i32(base + sum::length, s.length);
    w.i32(base + sum::bodyOffset, s.body_offset);
    w.u8(base + sum::state, static_cast<std::uint8_t>(s.state));

    std::uint32_t spam = 0;
    spam = cw_set_bits(spam, spambits::spamScore_first, spambits::spamScore_width,
                       static_cast<std::uint32_t>(static_cast<std::uint8_t>(s.spam_score)));
    spam = cw_set_bits(spam, spambits::spamBecause_first, spambits::spamBecause_width,
                       s.spam_because);
    w.u32(base + sum::spamBits, spam);

    w.u32(base + sum::arrivalSeconds, s.arrival_seconds);
    w.u32(base + sum::fromHash, s.from_hash);
    w.i32(base + sum::serialNum, s.serial_num);
    w.u32(base + sum::seconds, s.seconds);
    w.u32(base + sum::flags, s.flags);
    w.i16(base + sum::savedPos, s.saved_pos.top);
    w.i16(base + sum::savedPos + 2, s.saved_pos.left);
    w.i16(base + sum::savedPos + 4, s.saved_pos.bottom);
    w.i16(base + sum::savedPos + 6, s.saved_pos.right);
    w.u8(base + sum::priority, s.priority);
    w.u8(base + sum::origPriority, s.orig_priority);
    w.i16(base + sum::tableId, s.table_id);

    std::uint16_t scoreWord = 0;
    scoreWord |= static_cast<std::uint16_t>(
        (static_cast<std::uint8_t>(s.score) & 0xF) << 12);
    scoreWord |= static_cast<std::uint16_t>((s.out_type & 0xF) << 8);
    w.u16(base + sum::scoreBits, scoreWord);

    w.i16(base + sum::origZone, s.orig_zone);
    w.u32(base + sum::sigId, s.sig_id);

    const bool utf8 = (s.flags & sumflag::kUtf8) != 0;
    encode_sum_string(w, base, sum::from, sum::fromCap, s.from, utf8);
    w.u32(base + sum::popPersId, s.pop_pers_id);
    w.u32(base + sum::persId, s.pers_id);
    w.u32(base + sum::msgIdHash, s.msg_id_hash);
    w.i16(base + sum::subjId, s.subj_id);
    encode_sum_string(w, base, sum::subj, sum::subjCap, s.subject, utf8);
    w.u32(base + sum::opts, s.opts);
    w.u32(base + sum::uidHash, s.uid_hash);
    // cache / selected / messH stay zero: they were live pointers.
}

} // namespace

std::optional<TableOfContents> decode(std::span<const std::uint8_t> image,
                                      TocError *error, std::int64_t mailbox_size) {
    const auto fail = [&](TocError e) -> std::optional<TableOfContents> {
        if (error)
            *error = e;
        return std::nullopt;
    };

    if (image.size() < kTocTypeSize)
        return fail(TocError::TooSmall);

    BigEndianReader r(image);
    TableOfContents toc;
    toc.version = r.i16(hdr::version);
    if (toc.version > kCurrentTocVersion)
        return fail(TocError::UnsupportedVersion);

    const int count = r.i16(hdr::count);
    // TOCSizeShouldBe / InsaneTOC size invariant.
    if (count < 0 || image.size() != image_size(count))
        return fail(TocError::SizeMismatch);

    const std::uint32_t flagWord = r.u32(hdr::flagBits);
    toc.imap = cw_bits(flagWord, hdrbits::imapTOC, 1) != 0;
    toc.virtual_box = cw_bits(flagWord, hdrbits::virtualTOC, 1) != 0;

    toc.minor_version = r.i16(hdr::minorVersion);
    toc.which = static_cast<MailboxKind>(r.i16(hdr::which));
    toc.last_sort = r.i32(hdr::lastSort);
    for (int i = 0; i < 12; ++i)
        toc.sorts[i] = r.u8(hdr::sorts + static_cast<std::size_t>(i));
    toc.preview_hi = r.i16(hdr::previewHi);
    toc.next_serial_num = r.i32(hdr::nextSerialNum);
    toc.unread_count = r.i32(hdr::unreadCount);
    toc.used_k = r.u32(hdr::usedK);
    toc.total_k = r.u32(hdr::totalK);

    const std::uint32_t expire = r.u32(hdr::expireBits);
    toc.does_expire = cw_bits(expire, 0, 1) != 0;
    toc.expire_units = static_cast<ExpireUnits>(cw_bits(expire, 1, 3));
    toc.expire_interval = cw_bits(expire, 4, 28);

    toc.write_date = r.u32(hdr::writeDate);
    toc.box_size = r.i32(hdr::boxSize);

    toc.sums.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const std::size_t base = kHeaderSize + static_cast<std::size_t>(i) * kSummarySize;
        MessageSummary s = decode_summary(r, base);

        // InsaneTOC per-summary checks (toc.c:969-979).
        const bool imap_placeholder = toc.imap && s.offset < 0;
        if ((s.offset < 0 && !toc.imap) || s.length < 0 || s.body_offset < 0 ||
            s.body_offset > s.length ||
            (mailbox_size >= 0 && !imap_placeholder &&
             static_cast<std::int64_t>(s.offset) + s.length > mailbox_size))
            return fail(TocError::BadSummaryRange);

        toc.sums.push_back(std::move(s));
    }

    // InsaneTOC boxSize check (toc.c:953): stored boxSize is size+1.
    if (mailbox_size >= 0 && toc.box_size &&
        static_cast<std::int64_t>(toc.box_size) - 1 != mailbox_size)
        return fail(TocError::BadSummaryRange);

    if (toc.next_serial_num < 1)
        toc.next_serial_num = 1;
    for (const auto &s : toc.sums)
        if (s.serial_num >= toc.next_serial_num)
            toc.next_serial_num = s.serial_num + 1;

    if (error)
        *error = TocError::None;
    return toc;
}

std::vector<std::uint8_t> encode(const TableOfContents &toc,
                                 const std::string &mailbox_name,
                                 std::int64_t mailbox_size,
                                 std::uint32_t write_date) {
    const int count = toc.count();
    BigEndianWriter w(image_size(count));

    w.i16(hdr::version, kCurrentTocVersion);

    std::uint32_t flagWord = 0;
    if (toc.imap)
        flagWord = cw_set_bits(flagWord, hdrbits::imapTOC, 1, 1);
    if (toc.virtual_box)
        flagWord = cw_set_bits(flagWord, hdrbits::virtualTOC, 1, 1);
    w.u32(hdr::flagBits, flagWord);

    // Legacy FSSpec: vRefNum/parID are machine-local and meaningless off the
    // original Mac; the original re-stamped them on every load (toc.c:228).
    std::string mac_name;
    utf8_to_macroman(mailbox_name, mac_name);
    std::vector<std::uint8_t> nameBuf(64, 0);
    string_to_pascal(mac_name.size() > 63 ? mac_name.substr(0, 63) : mac_name, nameBuf);
    w.bytes(hdr::specName, nameBuf);

    w.i32(hdr::lastSort, toc.last_sort);
    for (int i = 0; i < 12; ++i)
        w.u8(hdr::sorts + static_cast<std::size_t>(i), toc.sorts[i]);
    w.i16(hdr::previewHi, toc.preview_hi);
    w.i32(hdr::nextSerialNum, toc.next_serial_num);
    w.i32(hdr::unreadCount, toc.unread_count);
    w.u32(hdr::usedK, toc.used_k);
    w.u32(hdr::totalK, toc.total_k);
    w.i16(hdr::unreadBase, 0);
    w.i16(hdr::minorVersion, kCurrentTocMinorVersion);

    std::uint32_t expire = 0;
    if (toc.does_expire)
        expire = cw_set_bits(expire, 0, 1, 1);
    expire = cw_set_bits(expire, 1, 3, static_cast<std::uint32_t>(toc.expire_units));
    expire = cw_set_bits(expire, 4, 28, toc.expire_interval);
    w.u32(hdr::expireBits, expire);

    w.u32(hdr::writeDate, write_date ? write_date : toc.write_date);
    // "add 1 to signal that we know it's ok" (toc.c:426)
    w.i32(hdr::boxSize, mailbox_size >= 0
                            ? static_cast<std::int32_t>(mailbox_size + 1)
                            : toc.box_size);
    w.i16(hdr::count, static_cast<std::int16_t>(count));
    w.i16(hdr::which, static_cast<std::int16_t>(toc.which));

    for (int i = 0; i < count; ++i) {
        const std::size_t base = kHeaderSize + static_cast<std::size_t>(i) * kSummarySize;
        encode_summary(w, base, toc.sums[static_cast<std::size_t>(i)]);
    }
    return std::move(w.data());
}

} // namespace eudora::tocfmt

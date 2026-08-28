#include "mailstore/compaction.hpp"

#include <cstdio>
#include <filesystem>
#include <system_error>
#include <vector>

namespace eudora {

namespace fs = std::filesystem;

std::int64_t wasted_bytes(const TableOfContents &toc, std::int64_t mailbox_size) {
    std::int64_t live = 0;
    for (const auto &s : toc.sums)
        if (s.offset >= 0)
            live += s.length;
    return mailbox_size - live;
}

bool needs_compaction(const TableOfContents &toc, std::int64_t mailbox_size) {
    std::int64_t spot = 0;
    for (const auto &s : toc.sums) {
        if (s.offset < 0)
            continue; // IMAP placeholder (squish.c:166)
        if (s.offset != spot)
            return true;
        spot += s.length;
    }
    return spot != mailbox_size;
}

bool compact_mailbox(TableOfContents &toc) {
    if (toc.mailbox_path.empty())
        return false;

    std::FILE *in = std::fopen(toc.mailbox_path.string().c_str(), "rb");
    if (!in)
        return false;

    fs::path tmp = toc.mailbox_path;
    tmp += ".compact.tmp";
    std::FILE *out = std::fopen(tmp.string().c_str(), "wb");
    if (!out) {
        std::fclose(in);
        return false;
    }

    // Copy live ranges in TOC order, rewriting offsets (squish.c:271-291).
    std::vector<std::int32_t> new_offsets(toc.sums.size(), -1);
    std::int64_t size = 0;
    bool ok = true;
    std::vector<char> buf(64 * 1024);
    for (std::size_t i = 0; ok && i < toc.sums.size(); ++i) {
        const auto &s = toc.sums[i];
        if (s.offset < 0)
            continue; // not downloaded (IMAP)
        if (std::fseek(in, s.offset, SEEK_SET) != 0) {
            ok = false;
            break;
        }
        std::int64_t remaining = s.length;
        new_offsets[i] = static_cast<std::int32_t>(size);
        while (ok && remaining > 0) {
            const std::size_t chunk = static_cast<std::size_t>(
                remaining < static_cast<std::int64_t>(buf.size())
                    ? remaining
                    : static_cast<std::int64_t>(buf.size()));
            const std::size_t got = std::fread(buf.data(), 1, chunk, in);
            if (got == 0) {
                ok = false;
                break;
            }
            if (std::fwrite(buf.data(), 1, got, out) != got) {
                ok = false;
                break;
            }
            remaining -= static_cast<std::int64_t>(got);
            size += static_cast<std::int64_t>(got);
        }
    }

    std::fclose(in);
    if (std::fclose(out) != 0)
        ok = false;

    std::error_code ec;
    if (!ok) {
        fs::remove(tmp, ec);
        return false;
    }

    // ExchangeFiles equivalent: atomic replace (squish.c:317).
    fs::rename(tmp, toc.mailbox_path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }

    for (std::size_t i = 0; i < toc.sums.size(); ++i)
        if (toc.sums[i].offset >= 0)
            toc.sums[i].offset = new_offsets[i];

    toc.recalc_kbytes(size);
    toc.box_size = static_cast<std::int32_t>(size + 1);
    return true;
}

} // namespace eudora

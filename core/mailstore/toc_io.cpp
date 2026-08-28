#include "mailstore/toc_io.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <system_error>
#include <vector>

#include "compat/macdate.hpp"

namespace eudora {

namespace fs = std::filesystem;

std::filesystem::path toc_path_for_mailbox(const std::filesystem::path &mailbox) {
    fs::path p = mailbox;
    p += ".toc"; // TOC_SUFFIX (StringDefs id 7408)
    return p;
}

std::optional<TableOfContents> read_toc(const fs::path &toc_file,
                                        const fs::path &mailbox,
                                        tocfmt::TocError *error) {
    std::error_code ec;
    std::int64_t mailbox_size = -1;
    if (!mailbox.empty()) {
        const auto sz = fs::file_size(mailbox, ec);
        if (!ec)
            mailbox_size = static_cast<std::int64_t>(sz);
    }

    std::FILE *f = std::fopen(toc_file.string().c_str(), "rb");
    if (!f) {
        if (error)
            *error = tocfmt::TocError::TooSmall;
        return std::nullopt;
    }
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<std::uint8_t> image(size > 0 ? static_cast<std::size_t>(size) : 0);
    const std::size_t got = image.empty() ? 0 : std::fread(image.data(), 1, image.size(), f);
    std::fclose(f);
    image.resize(got);

    auto toc = tocfmt::decode(image, error, mailbox_size);
    if (toc)
        toc->mailbox_path = mailbox;
    return toc;
}

bool write_toc(const TableOfContents &toc, const fs::path &toc_file) {
    std::error_code ec;
    std::int64_t mailbox_size = -1;
    std::uint32_t write_date = 0;
    if (!toc.mailbox_path.empty()) {
        const auto sz = fs::file_size(toc.mailbox_path, ec);
        if (!ec)
            mailbox_size = static_cast<std::int64_t>(sz);
        const auto mtime = fs::last_write_time(toc.mailbox_path, ec);
        if (!ec) {
            // Apple's libc++ lacks std::chrono::clock_cast; convert the
            // file-clock time via the two clocks' current readings.
            const auto file_now = fs::file_time_type::clock::now();
            const auto sys_now = std::chrono::system_clock::now();
            const auto sys =
                sys_now + std::chrono::duration_cast<
                              std::chrono::system_clock::duration>(mtime - file_now);
            const auto unix_secs =
                std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch())
                    .count();
            write_date = unix_to_mac(unix_secs);
        }
    }

    const std::string name = toc.mailbox_path.empty()
                                 ? toc_file.stem().string()
                                 : toc.mailbox_path.filename().string();
    const auto image = tocfmt::encode(toc, name, mailbox_size, write_date);

    fs::path tmp = toc_file;
    tmp += ".tmp"; // TEMP_SUFFIX pattern, as SaveFiltersLo used
    std::FILE *f = std::fopen(tmp.string().c_str(), "wb");
    if (!f)
        return false;
    const bool ok = std::fwrite(image.data(), 1, image.size(), f) == image.size();
    std::fclose(f);
    if (!ok) {
        fs::remove(tmp, ec);
        return false;
    }
    fs::rename(tmp, toc_file, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

} // namespace eudora

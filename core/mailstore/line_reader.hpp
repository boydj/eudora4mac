// Buffered mailbox line reader — the modern lineio.c.
//
// The legacy reader was CR-only (classic Mac mailboxes are CR-terminated,
// lineio.c:70).  Mailboxes copied to a modern system may have been converted
// to LF or CRLF, so this reader accepts all three; a returned line always
// ends in a single canonical '\r' (matching what the parser expects) but
// tell()/line lengths are computed from real file offsets, so TOC offsets
// stay byte-accurate for the file as it exists on disk.

#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace eudora {

class LineReader {
public:
    enum Result {
        Error = -1,  // I/O error
        Eof = 0,     // no more characters
        LineStart = 1,  // line returned starts at a line boundary (LINE_START)
        LineMiddle = 2, // continuation of an over-long line (LINE_MIDDLE)
    };

    LineReader() = default;
    ~LineReader();
    LineReader(const LineReader &) = delete;
    LineReader &operator=(const LineReader &) = delete;

    bool open(const std::filesystem::path &file);
    void close();
    bool is_open() const { return file_ != nullptr; }

    // Reposition to an absolute byte offset (SeekLine).
    bool seek(std::int64_t offset);

    // Read the next line into `line`, at most max_len bytes (GetLine).
    // The terminator is included as a single '\r' when it fits.  Returns
    // LineStart/LineMiddle/Eof/Error.
    Result get_line(std::string &line, std::size_t max_len = 255);

    // Byte offset where the last returned line began (TellLine).
    std::int64_t tell() const { return last_spot_; }

    // Current read position within the file.
    std::int64_t position() const { return file_spot_ + buffer_spot_; }

private:
    bool fill_buffer();

    std::FILE *file_ = nullptr;
    std::vector<char> buffer_;
    std::size_t buffer_filled_ = 0;
    std::size_t buffer_spot_ = 0;
    std::int64_t file_spot_ = 0; // file offset of buffer_[0]
    std::int64_t last_spot_ = 0;
    bool at_line_start_ = true;
};

} // namespace eudora

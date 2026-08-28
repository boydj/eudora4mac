#include "mailstore/line_reader.hpp"

namespace eudora {

namespace {
constexpr std::size_t kBufferSize = 8192; // LIKE_BUFFER
}

LineReader::~LineReader() { close(); }

bool LineReader::open(const std::filesystem::path &file) {
    close();
    file_ = std::fopen(file.string().c_str(), "rb");
    if (!file_)
        return false;
    buffer_.resize(kBufferSize);
    buffer_filled_ = 0;
    buffer_spot_ = 0;
    file_spot_ = 0;
    last_spot_ = 0;
    at_line_start_ = true;
    return fill_buffer() || true; // empty file is fine
}

void LineReader::close() {
    if (file_) {
        std::fclose(file_);
        file_ = nullptr;
    }
    buffer_.clear();
    buffer_filled_ = buffer_spot_ = 0;
    file_spot_ = last_spot_ = 0;
    at_line_start_ = true;
}

bool LineReader::seek(std::int64_t offset) {
    if (!file_ || std::fseek(file_, static_cast<long>(offset), SEEK_SET) != 0)
        return false;
    file_spot_ = offset;
    buffer_spot_ = 0;
    buffer_filled_ = 0;
    last_spot_ = offset;
    at_line_start_ = true;
    fill_buffer();
    return true;
}

bool LineReader::fill_buffer() {
    file_spot_ += static_cast<std::int64_t>(buffer_filled_);
    buffer_spot_ = 0;
    buffer_filled_ = std::fread(buffer_.data(), 1, buffer_.size(), file_);
    return buffer_filled_ > 0;
}

LineReader::Result LineReader::get_line(std::string &line, std::size_t max_len) {
    line.clear();
    if (!file_)
        return Error;
    if (buffer_spot_ >= buffer_filled_ && !fill_buffer())
        return Eof;

    const Result where = at_line_start_ ? LineStart : LineMiddle;
    last_spot_ = file_spot_ + static_cast<std::int64_t>(buffer_spot_);

    // Leave room for the canonical terminator, as the original reserved one
    // byte (`size--` in GetLine).
    if (max_len > 0)
        --max_len;

    bool saw_terminator = false;
    while (!saw_terminator) {
        while (buffer_spot_ < buffer_filled_) {
            const char c = buffer_[buffer_spot_];
            if (c == '\r' || c == '\n') {
                ++buffer_spot_;
                if (c == '\r') {
                    // Consume a following LF (CRLF) as part of the terminator.
                    if (buffer_spot_ >= buffer_filled_)
                        fill_buffer();
                    if (buffer_spot_ < buffer_filled_ && buffer_[buffer_spot_] == '\n')
                        ++buffer_spot_;
                }
                saw_terminator = true;
                break;
            }
            if (line.size() >= max_len) {
                // Over-long line: return what we have; next call continues it.
                at_line_start_ = false;
                return where;
            }
            // The original replaced NULs with spaces (lineio.c:142).
            line += (c == '\0') ? ' ' : c;
            ++buffer_spot_;
        }
        if (!saw_terminator && !fill_buffer()) {
            // EOF with no terminator: return the fragment as-is.
            at_line_start_ = true;
            return where;
        }
    }

    line += '\r';
    at_line_start_ = true;
    return where;
}

} // namespace eudora

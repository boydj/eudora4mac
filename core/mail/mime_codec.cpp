#include "mail/mime_codec.hpp"

#include <cstdlib>

namespace eudora {

namespace {

constexpr char kEncode[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

constexpr short kSkip = -1;
constexpr short kFail = -2;
constexpr short kPad = -3;

// gDecode (mime.c:36-55): base64 digit values with whitespace/pad classes.
short decode_digit(std::uint8_t c) {
    if (c == '\t' || c == '\n' || c == '\r' || c == ' ')
        return kSkip;
    if (c == '=')
        return kPad;
    if (c >= 'A' && c <= 'Z')
        return static_cast<short>(c - 'A');
    if (c >= 'a' && c <= 'z')
        return static_cast<short>(c - 'a' + 26);
    if (c >= '0' && c <= '9')
        return static_cast<short>(c - '0' + 52);
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return kFail;
}

} // namespace

// ---- Base64Encoder ---------------------------------------------------------

void Base64Encoder::encode_three(const std::uint8_t *b, std::string &out) {
    if (!newline_.empty() && bytes_on_line_ == 68) {
        out += newline_;
        bytes_on_line_ = 0;
    }
    bytes_on_line_ += 4;
    out += kEncode[b[0] >> 2];
    out += kEncode[((b[0] & 0x3) << 4) | (b[1] >> 4)];
    out += kEncode[((b[1] & 0xF) << 2) | (b[2] >> 6)];
    out += kEncode[b[2] & 0x3F];
}

void Base64Encoder::update(std::string_view data, std::string &out) {
    std::size_t i = 0;
    if (partial_count_) {
        while (partial_count_ < 3 && i < data.size())
            partial_[partial_count_++] = static_cast<std::uint8_t>(data[i++]);
        if (partial_count_ == 3) {
            encode_three(partial_, out);
            partial_count_ = 0;
        }
    }
    while (data.size() - i >= 3) {
        std::uint8_t b[3] = {static_cast<std::uint8_t>(data[i]),
                             static_cast<std::uint8_t>(data[i + 1]),
                             static_cast<std::uint8_t>(data[i + 2])};
        encode_three(b, out);
        i += 3;
    }
    while (i < data.size())
        partial_[partial_count_++] = static_cast<std::uint8_t>(data[i++]);
}

void Base64Encoder::finish(std::string &out) {
    if (partial_count_) {
        if (partial_count_ < 2)
            partial_[1] = 0;
        partial_[2] = 0;
        encode_three(partial_, out);
        out[out.size() - 1] = '=';
        if (partial_count_ == 1)
            out[out.size() - 2] = '=';
        partial_count_ = 0;
    }
}

// ---- Base64Decoder ---------------------------------------------------------

long Base64Decoder::update(std::string_view data, std::string &out) {
    long inval = 0;

    // FIX_NL (mime.c:190): for text parts, CRLF -> CR and LF -> CR.
    const auto fix_nl = [&]() {
        if (!text_ || out.empty())
            return;
        if (out.back() == '\n') {
            if (was_cr_) {
                was_cr_ = false;
                out.pop_back();
            } else {
                out.back() = '\r';
            }
        } else {
            was_cr_ = out.back() == '\r';
        }
    };

    for (unsigned char c : data) {
        const short decode = decode_digit(c);
        switch (decode) {
        case kSkip:
            break;
        case kFail:
            ++inval;
            break;
        case kPad:
            ++pad_count_;
            break;
        default:
            // A non-pad after a pad means the pad was an error.
            if (pad_count_) {
                inval += pad_count_;
                pad_count_ = 0;
            }
            switch (state_) {
            case 0:
                partial_ = static_cast<std::uint8_t>(decode << 2);
                ++state_;
                break;
            case 1:
                out += static_cast<char>(partial_ | (decode >> 4));
                partial_ = static_cast<std::uint8_t>((decode & 0xF) << 4);
                ++state_;
                fix_nl();
                break;
            case 2:
                out += static_cast<char>(partial_ | (decode >> 2));
                partial_ = static_cast<std::uint8_t>((decode & 0x3) << 6);
                ++state_;
                fix_nl();
                break;
            case 3:
                out += static_cast<char>(partial_ | decode);
                state_ = 0;
                fix_nl();
                break;
            }
        }
    }
    inval_count_ += inval;
    return inval;
}

long Base64Decoder::finish() {
    switch (state_) {
    case 0:
        inval_count_ += pad_count_; // came out even: no pads expected
        break;
    case 1:
        inval_count_ += 1 + pad_count_; // data missing
        break;
    case 2:
        inval_count_ += std::labs(pad_count_ - 2); // need exactly 2 pads
        break;
    case 3:
        inval_count_ += std::labs(pad_count_ - 1); // need exactly 1 pad
        break;
    }
    return inval_count_;
}

std::string base64_encode(std::string_view data, const std::string &newline) {
    Base64Encoder enc(newline);
    std::string out;
    enc.update(data, out);
    enc.finish(out);
    return out;
}

bool base64_decode(std::string_view data, std::string &out, bool text) {
    Base64Decoder dec(text);
    out.clear();
    dec.update(data, out);
    return dec.finish() == 0;
}

// ---- QpEncoder -------------------------------------------------------------

void QpEncoder::update(std::string_view data, std::string &out) {
    static const char *hex = "0123456789ABCDEF";
    int bpl = bytes_on_line_;

    const auto qpnl = [&]() {
        out += newline_;
        bpl = 0;
    };
    const auto room_for = [&](long x) {
        if (bpl + x > 76) {
            out += '=';
            qpnl();
        }
    };

    for (std::size_t i = 0; i < data.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(data[i]);
        if (c == '\r') {
            qpnl();
            continue;
        }
        if (c == '\n')
            continue; // skip other newline characters

        bool encode;
        if (c == ' ' || c == '\t') {
            // Encode trailing whitespace before a newline; otherwise keep
            // words together when the next break is near (mime.c:361-372).
            encode = i + 1 < data.size() && data[i + 1] == '\r';
            if (!encode) {
                for (std::size_t next = i + 1; next < data.size(); ++next)
                    if (data[next] == ' ' || data[next] == '\r') {
                        if (next - i < 20)
                            room_for(static_cast<long>(next - i));
                        break;
                    }
            }
        } else {
            encode = c == '=' || c < 33 || c > 126;
        }
        room_for(encode ? 3 : 1);
        // Protect "From " lines and leading dots (SMTP transparency).
        encode = encode || (bpl == 0 && (c == 'F' || c == '.'));
        if (encode) {
            out += '=';
            out += hex[(c >> 4) & 0xF];
            out += hex[c & 0xF];
            bpl += 3;
        } else {
            out += static_cast<char>(c);
            ++bpl;
        }
    }
    bytes_on_line_ = bpl;
}

// ---- QpDecoder -------------------------------------------------------------

namespace {
int hex_digit(unsigned char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}
} // namespace

long QpDecoder::update(std::string_view data, std::string &out) {
    // Trim spaces before a trailing CR (mime.c:420-431).
    std::string trimmed;
    if (!data.empty() && data.back() == '\r') {
        std::size_t e = data.size() - 1;
        while (e > 0 && data[e - 1] == ' ')
            --e;
        trimmed.assign(data.substr(0, e));
        trimmed += '\r';
        data = trimmed;
    }

    long errs = 0;
    for (unsigned char c : data) {
        switch (state_) {
        case State::SkipLf:
            state_ = State::Normal;
            if (c == '\n')
                break; // second half of a soft-break CRLF
            [[fallthrough]];
        case State::Normal:
            if (c == '=')
                state_ = State::Equal;
            else
                out += static_cast<char>(c);
            break;
        case State::Equal:
            if (c == '\r') {
                state_ = State::SkipLf; // soft line break (tolerates CRLF)
            } else if (c == '\n') {
                state_ = State::Normal; // LF-only soft break
            } else {
                state_ = State::Byte1;
            }
            break;
        case State::Byte1: {
            const int hi = hex_digit(last_char_);
            const int lo = hex_digit(c);
            if (hi < 0 || lo < 0)
                ++errs;
            else
                out += static_cast<char>((hi << 4) | lo);
            state_ = State::Normal;
            break;
        }
        }
        last_char_ = c;
    }
    return errs;
}

long QpDecoder::finish() {
    return (state_ == State::Equal || state_ == State::Byte1) ? 1 : 0;
}

std::string qp_encode(std::string_view text, const std::string &newline) {
    QpEncoder enc(newline);
    std::string out;
    enc.update(text, out);
    return out;
}

bool qp_decode(std::string_view data, std::string &out) {
    QpDecoder dec;
    out.clear();
    long errs = dec.update(data, out);
    errs += dec.finish();
    return errs == 0;
}

} // namespace eudora

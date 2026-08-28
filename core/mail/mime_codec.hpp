// MIME transfer codecs — the modern mime.c encoder/decoder layer.
//
// Streaming Base64 and Quoted-Printable codecs with explicit state, ported
// from Encode64/Decode64/EncodeQP/DecodeQP (mime.c:106-478).  The streaming
// forms preserve the original semantics (call with data repeatedly, then
// finish() to flush); whole-buffer helpers cover the common case.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace eudora {

// ---- Base64 ----------------------------------------------------------------

class Base64Encoder {
public:
    // newline: inserted every 68 output characters ("\r" for mail bodies);
    // empty disables wrapping (used by SASL).
    explicit Base64Encoder(std::string newline = "\r") : newline_(std::move(newline)) {}

    void update(std::string_view data, std::string &out);
    void finish(std::string &out); // emits final group with '=' padding

private:
    void encode_three(const std::uint8_t *b, std::string &out);

    std::string newline_;
    std::uint8_t partial_[3] = {};
    int partial_count_ = 0;
    int bytes_on_line_ = 0;
};

class Base64Decoder {
public:
    // text=true converts CRLF/LF line ends in the decoded output to CR,
    // matching the original's FIX_NL behavior for text parts.
    explicit Base64Decoder(bool text = false) : text_(text) {}

    // Returns the number of invalid characters seen in this chunk.
    long update(std::string_view data, std::string &out);
    // Returns the total error count including padding mismatches.
    long finish();

private:
    bool text_;
    int state_ = 0;
    long inval_count_ = 0;
    long pad_count_ = 0;
    std::uint8_t partial_ = 0;
    bool was_cr_ = false;
};

std::string base64_encode(std::string_view data, const std::string &newline = "");
// Returns false if the input had decoding errors.
bool base64_decode(std::string_view data, std::string &out, bool text = false);

// ---- Quoted-Printable ------------------------------------------------------

class QpEncoder {
public:
    explicit QpEncoder(std::string newline = "\r") : newline_(std::move(newline)) {}
    // `data` is CR-terminated text (the mailbox convention).
    void update(std::string_view data, std::string &out);

private:
    std::string newline_;
    int bytes_on_line_ = 0;
};

class QpDecoder {
public:
    // Returns the number of errors seen in this chunk.
    long update(std::string_view data, std::string &out);
    long finish();

private:
    enum class State { Normal, Equal, Byte1, SkipLf };
    State state_ = State::Normal;
    std::uint8_t last_char_ = 0;
};

std::string qp_encode(std::string_view text, const std::string &newline = "\r");
bool qp_decode(std::string_view data, std::string &out);

} // namespace eudora

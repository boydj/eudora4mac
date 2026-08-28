#include "mail/header_parser.hpp"

#include <cctype>

#include "compat/hashes.hpp"
#include "mail/lex822.hpp"
#include "mail/rfc2047.hpp"

namespace eudora {

namespace {

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

std::string lower(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s)
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Iterate raw lines regardless of terminator convention.
struct LineCursor {
    std::string_view text;
    std::size_t pos = 0;

    bool next(std::string_view &line) {
        if (pos >= text.size())
            return false;
        std::size_t end = pos;
        while (end < text.size() && text[end] != '\r' && text[end] != '\n')
            ++end;
        line = text.substr(pos, end - pos);
        if (end < text.size()) {
            if (text[end] == '\r' && end + 1 < text.size() && text[end + 1] == '\n')
                ++end;
            ++end;
        }
        pos = end;
        return true;
    }
};

} // namespace

MessageParts split_message(std::string_view raw) {
    LineCursor cur{raw};
    std::string_view line;
    std::size_t header_end = 0;
    std::size_t body_start = raw.size();
    while (true) {
        const std::size_t line_start = cur.pos;
        if (!cur.next(line)) {
            header_end = raw.size();
            break;
        }
        if (line.empty()) {
            header_end = line_start;
            body_start = cur.pos;
            break;
        }
    }
    return {raw.substr(0, header_end), raw.substr(body_start)};
}

HeaderSet HeaderSet::parse(std::string_view raw) {
    HeaderSet hs;
    LineCursor cur{raw};
    std::string_view line;
    HeaderField current;
    bool have_current = false;

    const auto flush = [&]() {
        if (have_current) {
            hs.fields_.push_back(current);
            current = {};
            have_current = false;
        }
    };

    while (cur.next(line)) {
        if (line.empty())
            break; // end of headers
        if ((line.front() == ' ' || line.front() == '\t') && have_current) {
            // Folded continuation: unfold with a single space.
            std::string_view cont = line;
            while (!cont.empty() && (cont.front() == ' ' || cont.front() == '\t'))
                cont.remove_prefix(1);
            current.value += ' ';
            current.value += cont;
            continue;
        }
        flush();
        const auto colon = line.find(':');
        if (colon == std::string_view::npos)
            continue; // not a header line; ignore (ReadHeaderLo skipped junk)
        current.name = std::string(line.substr(0, colon));
        std::string_view v = line.substr(colon + 1);
        while (!v.empty() && (v.front() == ' ' || v.front() == '\t'))
            v.remove_prefix(1);
        current.value = std::string(v);
        have_current = true;
    }
    flush();

    // Derive MIME facts.
    if (auto ct = hs.get("Content-Type")) {
        const auto tokens = lex822_tokenize(*ct);
        // type "/" subtype *(";" attribute "=" value)
        std::size_t i = 0;
        if (i < tokens.size() && tokens[i].kind == Token822::Atom)
            hs.content_type_ = lower(tokens[i++].text);
        if (i + 1 < tokens.size() && tokens[i].kind == Token822::Special &&
            tokens[i].text == "/" &&
            (tokens[i + 1].kind == Token822::Atom ||
             tokens[i + 1].kind == Token822::QText)) {
            hs.content_subtype_ = lower(tokens[i + 1].text);
            i += 2;
        }
        while (i < tokens.size()) {
            // Find "; name = value".
            if (tokens[i].kind == Token822::Special && tokens[i].text == ";") {
                if (i + 3 < tokens.size() && tokens[i + 1].kind == Token822::Atom &&
                    tokens[i + 2].kind == Token822::Special &&
                    tokens[i + 2].text == "=" &&
                    (tokens[i + 3].kind == Token822::Atom ||
                     tokens[i + 3].kind == Token822::QText)) {
                    // Value may continue across '.'/atom runs; take the token.
                    hs.content_attributes_.push_back(
                        {lower(tokens[i + 1].text), tokens[i + 3].text});
                    i += 4;
                    // Glue "a.b.c"-style unquoted values back together.
                    while (i + 1 < tokens.size() &&
                           tokens[i].kind == Token822::Special &&
                           tokens[i].text != ";" &&
                           (tokens[i + 1].kind == Token822::Atom ||
                            tokens[i + 1].kind == Token822::QText)) {
                        hs.content_attributes_.back().value +=
                            tokens[i].text + tokens[i + 1].text;
                        i += 2;
                    }
                    continue;
                }
            }
            ++i;
        }
    } else {
        // RFC 822 default (header.c defaulted to text/plain).
        hs.content_type_ = "text";
        hs.content_subtype_ = "plain";
    }

    if (auto cte = hs.get("Content-Transfer-Encoding")) {
        const auto tokens = lex822_tokenize(*cte);
        std::string enc;
        if (!tokens.empty() && tokens[0].kind == Token822::Atom)
            enc = lower(tokens[0].text);
        // "7bit"/"8bit" tokenize as atom "7bit" (digits are atom chars).
        if (enc == "7bit")
            hs.transfer_encoding_ = TransferEncoding::SevenBit;
        else if (enc == "8bit")
            hs.transfer_encoding_ = TransferEncoding::EightBit;
        else if (enc == "binary")
            hs.transfer_encoding_ = TransferEncoding::Binary;
        else if (enc == "quoted-printable")
            hs.transfer_encoding_ = TransferEncoding::QuotedPrintable;
        else if (enc == "base64")
            hs.transfer_encoding_ = TransferEncoding::Base64;
        else if (!enc.empty())
            hs.transfer_encoding_ = TransferEncoding::Other;
    }

    return hs;
}

std::optional<std::string_view> HeaderSet::get(std::string_view name) const {
    for (const auto &f : fields_)
        if (iequals(f.name, name))
            return std::string_view(f.value);
    return std::nullopt;
}

std::vector<std::string_view> HeaderSet::get_all(std::string_view name) const {
    std::vector<std::string_view> out;
    for (const auto &f : fields_)
        if (iequals(f.name, name))
            out.emplace_back(f.value);
    return out;
}

std::string HeaderSet::get_decoded(std::string_view name) const {
    if (auto v = get(name))
        return decode_rfc2047(*v);
    return {};
}

std::optional<std::string_view>
HeaderSet::content_attribute(std::string_view name) const {
    for (const auto &a : content_attributes_)
        if (iequals(a.name, name))
            return std::string_view(a.value);
    return std::nullopt;
}

std::string HeaderSet::boundary() const {
    if (auto b = content_attribute("boundary"))
        return std::string(*b);
    return {};
}

std::string HeaderSet::filename() const {
    // Content-Disposition filename wins; Content-Type name is the fallback
    // (ExtractHDHFilename precedence).
    if (auto cd = get("Content-Disposition")) {
        const auto tokens = lex822_tokenize(*cd);
        for (std::size_t i = 0; i + 2 < tokens.size(); ++i)
            if (tokens[i].kind == Token822::Atom &&
                iequals(tokens[i].text, "filename") &&
                tokens[i + 1].kind == Token822::Special &&
                tokens[i + 1].text == "=")
                return decode_rfc2047(tokens[i + 2].text);
    }
    if (auto n = content_attribute("name"))
        return decode_rfc2047(*n);
    return {};
}

std::uint32_t HeaderSet::message_id_hash() const {
    if (auto mid = get("Message-Id"))
        return mid_hash(*mid);
    return 0; // kNeverHashed
}

} // namespace eudora

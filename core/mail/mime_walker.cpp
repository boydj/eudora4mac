#include "mail/mime_walker.hpp"

#include "mail/mime_codec.hpp"

namespace eudora {

namespace {

constexpr std::size_t npos = std::string_view::npos;

// Is `line` (already stripped of its terminator) a boundary delimiter for
// `boundary`?  Sets *terminal for the closing "--boundary--" form.
// Trailing whitespace is ignored (RFC 2046 permits padding).
bool is_boundary_line(std::string_view line, std::string_view boundary,
                      bool *terminal) {
    if (line.size() < boundary.size() + 2 || line[0] != '-' || line[1] != '-')
        return false;
    if (line.compare(2, boundary.size(), boundary) != 0)
        return false;
    std::string_view rest = line.substr(2 + boundary.size());
    while (!rest.empty() && (rest.back() == ' ' || rest.back() == '\t'))
        rest.remove_suffix(1);
    if (rest == "--") {
        *terminal = true;
        return true;
    }
    if (rest.empty()) {
        *terminal = false;
        return true;
    }
    return false;
}

struct Segment {
    std::size_t begin, end;
};

// The [begin,end) spans between the boundary delimiter lines of `boundary`
// within raw[body_off, body_off+body_len).  Empty when no delimiter was
// found at all (caller then degrades to one opaque part).
std::vector<Segment> split_by_boundary(std::string_view raw,
                                       std::size_t body_off,
                                       std::size_t body_len,
                                       const std::string &boundary) {
    std::vector<Segment> segs;
    const std::size_t end = body_off + body_len;
    std::size_t pos = body_off;
    std::size_t seg_start = npos;
    while (pos < end) {
        std::size_t eol = pos;
        while (eol < end && raw[eol] != '\r' && raw[eol] != '\n')
            ++eol;
        const std::string_view line = raw.substr(pos, eol - pos);
        bool terminal = false;
        const bool is_delim = is_boundary_line(line, boundary, &terminal);
        const std::size_t line_start = pos;
        // Advance past the CR, LF, or CRLF terminator.
        pos = eol;
        if (pos < end && raw[pos] == '\r')
            ++pos;
        if (pos < end && raw[pos] == '\n')
            ++pos;
        if (!is_delim)
            continue;
        if (seg_start != npos)
            segs.push_back({seg_start, line_start});
        if (terminal)
            return segs;
        seg_start = pos;
    }
    // Unterminated final segment (missing closing delimiter).
    if (seg_start != npos)
        segs.push_back({seg_start, end});
    return segs;
}

void walk_block(std::string_view raw, std::size_t off, std::size_t len,
                int depth, int max_depth, bool entered_rfc822,
                std::vector<MimePart> &out) {
    const std::string_view block = raw.substr(off, len);
    const MessageParts mp = split_message(block);
    const HeaderSet hs = HeaderSet::parse(mp.header_block);
    // All views point into the one raw buffer; offsets are pointer math.
    const std::size_t body_off =
        off + static_cast<std::size_t>(mp.body.data() - block.data());
    const std::size_t body_len = mp.body.size();

    const std::string &type = hs.content_type();
    if (type == "multipart" && depth < max_depth) {
        const std::string boundary = hs.boundary();
        if (!boundary.empty()) {
            const auto segs =
                split_by_boundary(raw, body_off, body_len, boundary);
            if (!segs.empty()) {
                for (const auto &s : segs)
                    walk_block(raw, s.begin, s.end - s.begin, depth + 1,
                               max_depth, entered_rfc822, out);
                return;
            }
        }
        // No usable boundary: fall through to one opaque part.
    } else if (type == "message" && hs.content_subtype() == "rfc822" &&
               !entered_rfc822 && depth < max_depth) {
        walk_block(raw, body_off, body_len, depth + 1, max_depth, true, out);
        return;
    }

    MimePart part;
    part.type = type.empty() ? "text" : type;
    part.subtype = hs.content_subtype().empty() && part.type == "text"
                       ? "plain"
                       : hs.content_subtype();
    part.filename = hs.filename();
    part.encoding = hs.transfer_encoding();
    part.body_offset = body_off;
    part.body_length = body_len;
    part.depth = depth;
    // An attachment is anything named, or a non-text leaf inside a
    // multipart (an unnamed application/octet-stream still saves).
    part.is_attachment =
        !part.filename.empty() ||
        (depth > 0 && part.type != "text" && part.type != "multipart" &&
         part.type != "message");
    out.push_back(std::move(part));
}

} // namespace

std::vector<MimePart> walk_mime(std::string_view raw_message, int max_depth) {
    std::vector<MimePart> parts;
    if (!raw_message.empty())
        walk_block(raw_message, 0, raw_message.size(), 0, max_depth, false,
                   parts);
    return parts;
}

std::string decode_part(std::string_view raw_message, const MimePart &part) {
    const std::string_view body = part.body(raw_message);
    std::string out;
    switch (part.encoding) {
    case TransferEncoding::Base64:
        base64_decode(body, out, part.type == "text");
        return out;
    case TransferEncoding::QuotedPrintable:
        qp_decode(body, out);
        return out;
    default:
        return std::string(body);
    }
}

} // namespace eudora

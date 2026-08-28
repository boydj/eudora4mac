#include "mail/address_parser.hpp"

#include <array>
#include <cctype>

namespace eudora {

namespace {

// AddressCharEnum / AddressStateEnum (address.c:29-54).
enum CharClass {
    Regular = 0,
    Comma,
    LParen,
    RParen,
    LAngle,
    RAngle,
    LBrak,
    RBrak,
    DQuote,
    Colon,
    Semicolon,
    ADone,
};

enum State {
    sNoChange = -1,
    sPlain = 0,
    sParen,
    sAngle,
    sBrak,
    sQuot,
    sTrail,
    sTDone,
    // decaying states
    sError,
    sToken,
    sBrakE,
    sQuotE,
    incPar,
    decPar,
    sColon,
    sSem,
    sTrailB,
};

// AddrStateTable (address.c:55-64), verbatim.
constexpr signed char kTable[sTDone + 1][ADone + 1] = {
    /*            .          ,       (       )       <       >       [       ]       "       :       ;      done */
    /* sPlain */ {sNoChange, sToken, incPar, sError, sAngle, sError, sBrak, sError, sQuot, sColon, sSem, sToken},
    /* sParen */ {sNoChange, sNoChange, incPar, decPar, sNoChange, sNoChange, sNoChange, sNoChange, sNoChange, sNoChange, sNoChange, sError},
    /* sAngle */ {sNoChange, sNoChange, incPar, decPar, sError, sTrailB, sBrak, sError, sQuot, sNoChange, sNoChange, sError},
    /* sBrak */  {sNoChange, sNoChange, sNoChange, sNoChange, sNoChange, sNoChange, sError, sBrakE, sNoChange, sNoChange, sNoChange, sError},
    /* sQuot */  {sNoChange, sNoChange, sNoChange, sNoChange, sNoChange, sNoChange, sNoChange, sNoChange, sQuotE, sNoChange, sNoChange, sError},
    /* sTrail */ {sNoChange, sToken, incPar, decPar, sError, sError, sNoChange, sNoChange, sQuot, sNoChange, sSem, sToken},
    /* sTDone */ {sNoChange, sToken, incPar, sError, sAngle, sError, sBrak, sError, sQuot, sColon, sSem, sToken},
};

bool addr_white(unsigned char c) { return c <= ' '; }

} // namespace

std::optional<std::vector<std::string>> parse_addresses(std::string_view text,
                                                        bool want_comments) {
    std::vector<std::string> addresses;
    std::string buffer; // the address being accumulated
    std::size_t addr_end = 0; // position after last non-white addr char (unused
                              // without auto-qualification, kept for fidelity)
    int paren = 0;
    int state = sPlain, old_state = sPlain;
    unsigned char last_c = 0;

    const auto restart = [&]() {
        buffer.clear();
        addr_end = 0;
        last_c = 0;
    };

    // AddrChar / CmmntChar / AddrAny (address.c:104-106).
    const auto addr_char = [&](unsigned char c) {
        if ((want_comments ? c != '\r' : !addr_white(c)) && old_state != sTDone) {
            buffer += static_cast<char>(c);
            if (!addr_white(c))
                addr_end = buffer.size();
        }
    };
    const auto cmmnt_char = [&](unsigned char c) {
        if (want_comments && old_state < sTDone && c != '\r')
            buffer += static_cast<char>(c);
    };
    const auto addr_any = [&](unsigned char c) {
        if (old_state < sTDone)
            buffer += static_cast<char>(c);
    };

    std::size_t spot = 0;
    while (spot < text.size() && addr_white(static_cast<unsigned char>(text[spot])))
        ++spot;
    restart();

    constexpr std::size_t kAddrMax = 253; // Str255 minus bookkeeping
    bool done = false;
    while (!done && buffer.size() < kAddrMax) {
        int cclass;
        unsigned char c = 0;
        if (spot >= text.size()) {
            cclass = ADone;
        } else {
            c = static_cast<unsigned char>(text[spot++]);
            switch (c) {
            case ',': cclass = Comma; break;
            case '(': cclass = LParen; break;
            case ')': cclass = RParen; break;
            case '[': cclass = LBrak; break;
            case ']': cclass = RBrak; break;
            case '<': cclass = LAngle; break;
            case '>': cclass = RAngle; break;
            case '"': cclass = DQuote; break;
            case ':':
                // "::" is regular text (address.c:141-145).
                if ((spot >= 2 && text[spot - 2] == ':') ||
                    (spot < text.size() && text[spot] == ':'))
                    cclass = Regular;
                else
                    cclass = Colon;
                break;
            case ';': cclass = Semicolon; break;
            default: cclass = Regular; break;
            }
            // Hacky way of dealing with backslash (address.c:150).
            if (last_c == '\\')
                cclass = Regular;
            last_c = c;
        }

        int next = kTable[state][cclass];
        bool rescan = true;
        while (rescan) {
            rescan = false;
            if (next == sNoChange)
                next = state;
            switch (next) {
            case sPlain:
                addr_char(c);
                break;
            case sAngle:
                if (state == sAngle || want_comments)
                    addr_char(c);
                else
                    restart();
                break;
            case sColon:
                addr_char(c);
                next = sToken;
                rescan = true;
                break;
            case sBrak:
                if (state != sBrak)
                    old_state = state;
                addr_any(c);
                break;
            case sQuot:
                if (state != sQuot)
                    old_state = state;
                if (old_state == sTrail)
                    cmmnt_char(c);
                else
                    addr_any(c);
                break;
            case sTDone:
                break;
            case sError:
                return std::nullopt;
            case sSem:
            case sToken: {
                while (!buffer.empty() &&
                       addr_white(static_cast<unsigned char>(buffer.back())))
                    buffer.pop_back();
                if (!buffer.empty()) {
                    // TrimWhite/TrimInitialWhite happen in the macros above;
                    // trim leading white for safety.
                    std::size_t b = 0;
                    while (b < buffer.size() &&
                           std::isspace(static_cast<unsigned char>(buffer[b])))
                        ++b;
                    addresses.push_back(buffer.substr(b));
                }
                const bool was_sem = next == sSem;
                restart();
                old_state = sPlain;
                if (was_sem)
                    addr_char(c);
                next = sPlain;
                while (spot < text.size() &&
                       addr_white(static_cast<unsigned char>(text[spot])))
                    ++spot;
                break;
            }
            case sTrailB:
                next = sTrail;
                cmmnt_char('>');
                break;
            case sBrakE:
                next = old_state;
                addr_char(c);
                break;
            case sQuotE:
                next = old_state;
                if (old_state == sTrail)
                    cmmnt_char(c);
                else
                    addr_any(c);
                break;
            case incPar:
                if (!paren++)
                    old_state = state;
                next = sParen;
                cmmnt_char('(');
                break;
            case decPar:
                if (!--paren)
                    next = old_state;
                else
                    next = sParen;
                cmmnt_char(')');
                break;
            default:
                cmmnt_char(c);
                break;
            }
        }
        state = next;
        if (cclass == ADone)
            done = true;
    }
    if (!done)
        return std::nullopt; // address too long (AddrFull punt)
    return addresses;
}

std::string short_address(std::string_view address) {
    auto parsed = parse_addresses(address, false);
    if (parsed && !parsed->empty())
        return parsed->front();
    return std::string(address);
}

bool same_address(std::string_view a, std::string_view b) {
    const std::string sa = short_address(a);
    const std::string sb = short_address(b);
    if (sa.size() != sb.size())
        return false;
    for (std::size_t i = 0; i < sa.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(sa[i])) !=
            std::tolower(static_cast<unsigned char>(sb[i])))
            return false;
    return true;
}

} // namespace eudora

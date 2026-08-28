#include "protocols/sasl.hpp"

#include <cctype>

#include "mail/mime_codec.hpp"
#include "protocols/md5.h"

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

std::string hex_digest(const unsigned char digest[16]) {
    static const char *hex = "0123456789abcdef";
    std::string out;
    out.reserve(32);
    for (int i = 0; i < 16; ++i) {
        out += hex[(digest[i] >> 4) & 0xF];
        out += hex[digest[i] & 0xF];
    }
    return out;
}

// ExtractStamp: the "<...>" timestamp from the POP banner.
std::string extract_stamp(std::string_view banner) {
    const auto lt = banner.find('<');
    if (lt == std::string_view::npos)
        return {};
    const auto gt = banner.find('>', lt + 1);
    if (gt == std::string_view::npos)
        return {};
    return std::string(banner.substr(lt, gt - lt + 1));
}

} // namespace

SaslMechanism sasl_consider(SaslMechanism current, std::string_view offered) {
    SaslMechanism found = SaslMechanism::None;
    if (iequals(offered, "CRAM-MD5"))
        found = SaslMechanism::CramMD5;
    else if (iequals(offered, "PLAIN"))
        found = SaslMechanism::Plain;
    else if (iequals(offered, "LOGIN"))
        found = SaslMechanism::Login;

    if (found == SaslMechanism::None)
        return current;
    if (current == SaslMechanism::None)
        return found;
    // Smaller indices are considered more secure (sasl.c:169).
    return static_cast<int>(found) < static_cast<int>(current) ? found : current;
}

std::string_view sasl_name(SaslMechanism mech) {
    switch (mech) {
    case SaslMechanism::CramMD5: return "CRAM-MD5";
    case SaslMechanism::Plain: return "PLAIN";
    case SaslMechanism::Login: return "LOGIN";
    default: return "";
    }
}

std::string sasl_cram_md5_response(std::string_view user, std::string_view password,
                                   std::string_view base64_challenge) {
    std::string challenge;
    if (!base64_decode(base64_challenge, challenge) || challenge.empty())
        return {};
    unsigned char digest[16];
    eudora_hmac_md5(challenge.data(), challenge.size(), password.data(),
                    password.size(), digest);
    std::string raw(user);
    raw += ' ';
    raw += hex_digest(digest);
    return base64_encode(raw);
}

std::string sasl_plain_response(std::string_view authorize, std::string_view user,
                                std::string_view password) {
    std::string raw(authorize);
    raw += '\0';
    raw += user;
    raw += '\0';
    raw += password;
    return base64_encode(raw);
}

std::string sasl_login_user(std::string_view user) {
    return base64_encode(user);
}

std::string sasl_login_password(std::string_view password) {
    return base64_encode(password);
}

std::string apop_digest(std::string_view banner, std::string_view password) {
    const std::string stamp = extract_stamp(banner);
    if (stamp.empty())
        return {};
    EudoraMD5Context ctx;
    unsigned char digest[16];
    eudora_md5_init(&ctx);
    eudora_md5_update(&ctx, stamp.data(), stamp.size());
    eudora_md5_update(&ctx, password.data(), password.size());
    eudora_md5_final(&ctx, digest);
    return hex_digest(digest);
}

} // namespace eudora

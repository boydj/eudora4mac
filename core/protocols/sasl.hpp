// SASL mechanisms — the modern sasl.c (CRAM-MD5, PLAIN, LOGIN).
//
// The transport-agnostic response builders; POP3 and SMTP drive the rounds
// exactly as SASLDo did.  GSSAPI/Kerberos are not ported (they depended on
// the classic KClient/MIT GSS frameworks).

#pragma once

#include <string>
#include <string_view>

namespace eudora {

enum class SaslMechanism { None = 0, CramMD5, Plain, Login };

// Pick the strongest mechanism from an advertised list token
// (SASLFind semantics: smaller enum = stronger; unknown names ignored).
SaslMechanism sasl_consider(SaslMechanism current, std::string_view offered_name);
std::string_view sasl_name(SaslMechanism mech);

// CRAM-MD5 (SASLCramMD5 + GenKeyedDigest): base64 challenge in, base64
// "user hexdigest" out.  Returns empty on a bad challenge.
std::string sasl_cram_md5_response(std::string_view user, std::string_view password,
                                   std::string_view base64_challenge);

// PLAIN (SASLPlain / AUTHPLAIN_FMT "authorize NUL user NUL pass"), base64.
std::string sasl_plain_response(std::string_view authorize, std::string_view user,
                                std::string_view password);

// LOGIN rounds: base64(user), then base64(password).
std::string sasl_login_user(std::string_view user);
std::string sasl_login_password(std::string_view password);

// APOP digest (GenDigest, pop.c:2419): MD5(timestamp+password) in hex; the
// timestamp is the "<...>" portion of the POP greeting banner.  Returns
// empty when the banner has no timestamp.
std::string apop_digest(std::string_view banner, std::string_view password);

} // namespace eudora

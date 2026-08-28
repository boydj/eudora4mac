// EudoraCore — C++ umbrella header.
//
// The modernized business logic of Eudora 6.2.4: mail store (mbox + .toc),
// RFC 822/MIME parsing, POP3/SMTP protocol engines, and the filter engine.
// See PORTING.md at the repository root for the mapping to the legacy
// sources.  Swift/Objective-C frontends use eudora/eudora_core.h instead.

#pragma once

#include "compat/hashes.hpp"
#include "compat/macdate.hpp"
#include "compat/macroman.hpp"

#include "mailstore/compaction.hpp"
#include "mailstore/line_reader.hpp"
#include "mailstore/mbox_parser.hpp"
#include "mailstore/summary.hpp"
#include "mailstore/toc_format.hpp"
#include "mailstore/toc_io.hpp"

#include "addressbook/nicknames.hpp"

#include "mail/address_parser.hpp"
#include "mail/composer.hpp"
#include "mail/header_parser.hpp"
#include "mail/lex822.hpp"
#include "mail/mime_codec.hpp"
#include "mail/rfc2047.hpp"

#include "net/line_receiver.hpp"
#include "net/posix_transport.hpp"
#include "net/transport.hpp"
#if defined(EUDORA_HAVE_TLS)
#include "net/tls_transport.hpp"
#endif

#include "protocols/imap.hpp"
#include "protocols/pop3.hpp"
#include "protocols/sasl.hpp"
#include "protocols/smtp.hpp"

#include "filters/filter_file.hpp"
#include "filters/filter_types.hpp"
#include "filters/match_engine.hpp"

namespace eudora {
inline constexpr const char *kCoreVersion = "0.1.0";
// The legacy lineage this core was extracted from.
inline constexpr const char *kLegacyVersion = "6.2.4";
} // namespace eudora

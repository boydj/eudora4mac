// RFC 822 address-list parser — the modern address.c.
//
// A faithful port of the SuckPtrAddresses table-driven state machine
// (address.c:84-293).  Given a header value like
//     "Alice <alice@example.com> (note), bob@example.org"
// it yields the address tokens ["alice@example.com", "bob@example.org"], or
// the full texts with comments when want_comments is set.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eudora {

// Parse an address list.  Returns std::nullopt on a malformed list (the
// original's sError punt); an empty vector just means no addresses.
std::optional<std::vector<std::string>>
parse_addresses(std::string_view text, bool want_comments = false);

// ShortAddr (address.c:341): reduce one address to its user@host form.
std::string short_address(std::string_view address);

// SameAddressStr (address.c:313): case-insensitive equality of short forms.
bool same_address(std::string_view a, std::string_view b);

} // namespace eudora

// The "Eudora Nicknames" address book — the modern nickmng.c/nickexp.c.
//
// File format (nickmng.c:35-42):
//     alias name-of-alias expansion of alias<newline>
//     note name-of-alias <tag:value>... free notes<newline>
// Nicknames containing spaces are double-quoted; newlines may be escaped
// with a trailing backslash; unknown lines are ignored.  The notes field
// carries <tag:value> pairs (address book fields) plus free text.

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eudora {

struct Nickname {
    std::string name;
    std::string addresses; // raw expansion text (comma-separated list)
    std::string notes;     // raw notes incl. <tag:value> pairs
    bool group = false;    // more than one address
};

class AddressBook {
public:
    static AddressBook parse(std::string_view text);
    static std::optional<AddressBook> load(const std::filesystem::path &file);

    std::string serialize() const;
    bool save(const std::filesystem::path &file) const;

    const std::vector<Nickname> &nicknames() const { return nicks_; }
    std::vector<Nickname> &nicknames() { return nicks_; }

    // Case-insensitive lookup (NickMatchFound semantics).
    const Nickname *find(std::string_view name) const;

    // Add or replace (ReplaceNickname).
    void set(Nickname nick);
    bool remove(std::string_view name);

    // Import contacts from a delimited text (the classic Import Contacts):
    // one entry per line as "Display Name <addr>", "name<TAB>addr[<TAB>notes]",
    // "name,addr[,notes]", or a bare "addr".  A nickname is derived from the
    // display name (else the address' local part); blank/comment (#) lines are
    // skipped.  Returns the parsed nicknames without merging.
    static std::vector<Nickname> import_contacts(std::string_view text);

    // Merge another book's nicknames in.  When overwrite is false, an
    // existing nickname of the same name is kept; otherwise it is replaced.
    // Returns the number of nicknames added or replaced.
    int merge(const std::vector<Nickname> &incoming, bool overwrite);

    // ExpandAliases (nickexp.c): resolve an address list, replacing tokens
    // that name nicknames with their addresses, recursively, with cycle
    // protection.  Unknown tokens pass through.
    std::vector<std::string> expand(std::string_view address_list) const;

    // Does this exact address appear in any nickname's expansion?
    // (HashAppearsInAliasFile — backs the filters' intersectsFile verb.)
    bool contains_address(std::string_view address) const;

    // <tag:value> field from a nickname's notes (GetTaggedFieldValueInNotes).
    static std::optional<std::string> note_field(const Nickname &nick,
                                                 std::string_view tag);
    static void set_note_field(Nickname &nick, std::string_view tag,
                               std::string_view value);

private:
    std::vector<Nickname> nicks_;
};

} // namespace eudora

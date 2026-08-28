#include "addressbook/nicknames.hpp"

#include <cctype>
#include <fstream>
#include <functional>
#include <set>

#include "mail/address_parser.hpp"

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

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

// Split logical lines: physical lines end at CR/LF/CRLF, but a trailing
// backslash continues onto the next line (nickmng.c "newlines may be
// escaped with \").
std::vector<std::string> logical_lines(std::string_view text) {
    std::vector<std::string> lines;
    std::string current;
    std::size_t i = 0;
    while (i <= text.size()) {
        std::size_t end = i;
        while (end < text.size() && text[end] != '\r' && text[end] != '\n')
            ++end;
        std::string_view phys = text.substr(i, end - i);
        const bool at_end = end >= text.size();
        if (end < text.size() && text[end] == '\r' && end + 1 < text.size() &&
            text[end + 1] == '\n')
            ++end;
        i = end + 1;

        if (!phys.empty() && phys.back() == '\\') {
            phys.remove_suffix(1);
            current += phys;
            if (!at_end)
                continue;
        } else {
            current += phys;
        }
        if (!current.empty())
            lines.push_back(current);
        current.clear();
        if (at_end)
            break;
    }
    return lines;
}

// Parse `alias "name" rest` / `note name rest` (nickmng.c:200-231).
bool parse_command_line(std::string_view line, std::string_view &cmd,
                        std::string &name, std::string_view &rest) {
    std::size_t i = 0;
    while (i < line.size() && line[i] != ' ')
        ++i;
    cmd = line.substr(0, i);
    while (i < line.size() && line[i] == ' ')
        ++i;
    if (i >= line.size())
        return false;
    if (line[i] == '"') {
        const std::size_t close = line.find('"', i + 1);
        if (close == std::string_view::npos)
            return false;
        name = std::string(line.substr(i + 1, close - i - 1));
        i = close + 1;
    } else {
        std::size_t j = i;
        while (j < line.size() && line[j] != ' ')
            ++j;
        name = std::string(line.substr(i, j - i));
        i = j;
    }
    while (i < line.size() && line[i] == ' ')
        ++i;
    rest = line.substr(i);
    return !name.empty();
}

bool needs_quotes(std::string_view name) {
    return name.find(' ') != std::string_view::npos;
}

} // namespace

AddressBook AddressBook::parse(std::string_view text) {
    AddressBook book;
    for (const auto &line : logical_lines(text)) {
        std::string_view cmd, rest;
        std::string name;
        if (!parse_command_line(line, cmd, name, rest))
            continue;
        const bool is_alias = iequals(cmd, "alias");
        const bool is_note = !is_alias && iequals(cmd, "note");
        if (!is_alias && !is_note)
            continue; // unknown lines ignored

        Nickname *nick = nullptr;
        for (auto &n : book.nicks_)
            if (iequals(n.name, name)) {
                nick = &n;
                break;
            }
        if (!nick) {
            book.nicks_.push_back(Nickname{name, "", "", false});
            nick = &book.nicks_.back();
        }
        if (is_alias && nick->addresses.empty()) {
            nick->addresses = std::string(trim(rest));
            auto parsed = parse_addresses(nick->addresses, false);
            nick->group = parsed && parsed->size() > 1;
        } else if (is_note && nick->notes.empty()) {
            nick->notes = std::string(trim(rest));
        }
    }
    return book;
}

std::optional<AddressBook> AddressBook::load(const std::filesystem::path &file) {
    std::ifstream f(file, std::ios::binary);
    if (!f)
        return std::nullopt;
    std::string text((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
    return parse(text);
}

std::string AddressBook::serialize() const {
    std::string out;
    for (const auto &n : nicks_) {
        const std::string quoted =
            needs_quotes(n.name) ? "\"" + n.name + "\"" : n.name;
        if (!n.addresses.empty()) {
            out += "alias " + quoted + " " + n.addresses + "\r";
        }
        if (!n.notes.empty()) {
            out += "note " + quoted + " " + n.notes + "\r";
        }
    }
    return out;
}

bool AddressBook::save(const std::filesystem::path &file) const {
    const std::string text = serialize();
    std::filesystem::path tmp = file;
    tmp += ".temp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f)
            return false;
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!f)
            return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, file, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

const Nickname *AddressBook::find(std::string_view name) const {
    for (const auto &n : nicks_)
        if (iequals(n.name, name))
            return &n;
    return nullptr;
}

void AddressBook::set(Nickname nick) {
    for (auto &n : nicks_)
        if (iequals(n.name, nick.name)) {
            n = std::move(nick);
            return;
        }
    nicks_.push_back(std::move(nick));
}

bool AddressBook::remove(std::string_view name) {
    for (auto it = nicks_.begin(); it != nicks_.end(); ++it)
        if (iequals(it->name, name)) {
            nicks_.erase(it);
            return true;
        }
    return false;
}

std::vector<std::string> AddressBook::expand(std::string_view list) const {
    std::vector<std::string> out;
    // Case-normalized names currently being expanded (cycle protection).
    std::set<std::string> in_progress;

    const auto lower = [](std::string_view s) {
        std::string l;
        for (char c : s)
            l += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return l;
    };

    const std::function<void(std::string_view)> walk =
        [&](std::string_view addresses) {
            auto parsed = parse_addresses(addresses, false);
            if (!parsed)
                return;
            for (const auto &token : *parsed) {
                if (token.empty() || token == ";" || token.back() == ':')
                    continue; // group-syntax markers
                // Quoted multi-word nicknames keep their quotes through the
                // address parser; strip them for the lookup.
                std::string_view name = token;
                if (name.size() >= 2 && name.front() == '"' && name.back() == '"')
                    name = name.substr(1, name.size() - 2);
                const Nickname *nick = find(name);
                if (nick && !nick->addresses.empty()) {
                    const std::string key = lower(name);
                    if (in_progress.insert(key).second) {
                        walk(nick->addresses);
                        in_progress.erase(key);
                        continue;
                    }
                }
                out.push_back(token);
            }
        };
    walk(list);
    return out;
}

bool AddressBook::contains_address(std::string_view address) const {
    const std::string want = short_address(address);
    for (const auto &n : nicks_) {
        auto parsed = parse_addresses(n.addresses, false);
        if (!parsed)
            continue;
        for (const auto &a : *parsed)
            if (iequals(a, want))
                return true;
    }
    return false;
}

std::optional<std::string> AddressBook::note_field(const Nickname &nick,
                                                   std::string_view tag) {
    // <tag:value> pairs; value runs to the closing '>'.
    const std::string open = "<" + std::string(tag) + ":";
    std::size_t pos = 0;
    while ((pos = nick.notes.find('<', pos)) != std::string::npos) {
        if (nick.notes.compare(pos, open.size(), open) == 0 ||
            (nick.notes.size() > pos + open.size() &&
             iequals(std::string_view(nick.notes).substr(pos, open.size()), open))) {
            const std::size_t vstart = pos + open.size();
            const std::size_t close = nick.notes.find('>', vstart);
            if (close == std::string::npos)
                return std::nullopt;
            return nick.notes.substr(vstart, close - vstart);
        }
        ++pos;
    }
    return std::nullopt;
}

void AddressBook::set_note_field(Nickname &nick, std::string_view tag,
                                 std::string_view value) {
    const std::string open = "<" + std::string(tag) + ":";
    const std::size_t pos = nick.notes.find(open);
    const std::string pair = open + std::string(value) + ">";
    if (pos != std::string::npos) {
        const std::size_t close = nick.notes.find('>', pos);
        if (close != std::string::npos) {
            nick.notes.replace(pos, close - pos + 1, pair);
            return;
        }
    }
    nick.notes.insert(0, pair);
}

} // namespace eudora

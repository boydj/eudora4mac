#include "mail/lex822.hpp"

namespace eudora {

namespace {

bool is_special(char c) {
    switch (c) {
    case '(': case ')': case '<': case '>': case '@': case ',': case ';':
    case ':': case '\\': case '"': case '.': case '[': case ']': case '=':
    case '/': case '?':
        return true;
    default:
        return false;
    }
}

bool is_lwsp(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

} // namespace

std::vector<Lex822Token> lex822_tokenize(std::string_view field) {
    std::vector<Lex822Token> tokens;
    std::size_t i = 0;
    const std::size_t n = field.size();

    while (i < n) {
        const char c = field[i];
        if (is_lwsp(c)) {
            ++i;
            continue;
        }
        if (c == '"') {
            std::string text;
            ++i;
            while (i < n && field[i] != '"') {
                if (field[i] == '\\' && i + 1 < n)
                    ++i;
                text += field[i++];
            }
            if (i < n)
                ++i; // closing quote
            tokens.push_back({Token822::QText, std::move(text)});
        } else if (c == '(') {
            std::string text;
            int depth = 1;
            ++i;
            while (i < n && depth > 0) {
                if (field[i] == '\\' && i + 1 < n) {
                    ++i;
                    text += field[i++];
                    continue;
                }
                if (field[i] == '(')
                    ++depth;
                else if (field[i] == ')' && --depth == 0) {
                    ++i;
                    break;
                }
                text += field[i++];
            }
            tokens.push_back({Token822::Comment, std::move(text)});
        } else if (c == '[') {
            std::string text;
            ++i;
            while (i < n && field[i] != ']') {
                if (field[i] == '\\' && i + 1 < n)
                    ++i;
                text += field[i++];
            }
            if (i < n)
                ++i;
            tokens.push_back({Token822::DomainLit, std::move(text)});
        } else if (is_special(c)) {
            tokens.push_back({Token822::Special, std::string(1, c)});
            ++i;
        } else {
            std::string text;
            while (i < n && !is_lwsp(field[i]) && !is_special(field[i]))
                text += field[i++];
            tokens.push_back({Token822::Atom, std::move(text)});
        }
    }
    tokens.push_back({Token822::End, {}});
    return tokens;
}

std::string quote822(std::string_view text, bool quote_spaces) {
    bool need = false;
    for (char c : text) {
        if (is_special(c) || (quote_spaces && (c == ' ' || c == '\t'))) {
            need = true;
            break;
        }
    }
    if (!need)
        return std::string(text);
    std::string out = "\"";
    for (char c : text) {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

} // namespace eudora

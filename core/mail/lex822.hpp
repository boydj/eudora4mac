// RFC 822 structured-field tokenizer — the modern lex822.c.
//
// The legacy tokenizer pulled bytes from a TransStream; this port tokenizes
// an in-memory field body (the header parser hands it unfolded values).
// Token classes follow Token822Enum (Include/lex822.h:22-35).

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace eudora {

enum class Token822 {
    Atom,       // run of non-special, non-space characters
    QText,      // "quoted string" (content, quotes stripped)
    Comment,    // (comment) (content, parens stripped)
    DomainLit,  // [domain.literal] (content, brackets stripped)
    Special,    // one special character: ()<>@,;:\".[]=/?
    End,
};

struct Lex822Token {
    Token822 kind;
    std::string text;
};

// Tokenize a structured field body.  Linear whitespace separates tokens and
// is not returned.  Backslash escapes are honored inside quoted strings and
// comments.
std::vector<Lex822Token> lex822_tokenize(std::string_view field);

// Quote822: quote a token if it contains specials/spaces.
std::string quote822(std::string_view text, bool quote_spaces);

} // namespace eudora

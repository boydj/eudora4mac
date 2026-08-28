// Regular expressions for filter terms.
//
// The legacy regexp.c was Henry Spencer's V8 matcher adapted to relocatable
// Handles (with raw pointers into moveable memory — a latent bug even
// then).  The modern build uses the platform's POSIX extended regex — the
// same Spencer lineage and syntax family — behind an RAII wrapper with the
// SearchRegExpPtr interface (compile once, find the first match offset).

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace eudora {

class Regexp {
public:
    // Compile; returns nullptr on a bad pattern (regcomp returning nil).
    static std::shared_ptr<Regexp> compile(const std::string &pattern);
    ~Regexp();

    Regexp(const Regexp &) = delete;
    Regexp &operator=(const Regexp &) = delete;

    // SearchRegExpPtr: offset of the first match at/after `offset`, or
    // std::nullopt.  Embedded NULs in `text` are treated as spaces (the
    // line readers upstream already do the same).
    std::optional<std::size_t> search(std::string_view text,
                                      std::size_t offset = 0) const;

private:
    Regexp();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace eudora

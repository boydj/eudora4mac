// Minimal dependency-free unit test harness for EudoraCore.

#pragma once

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace eutest {

struct Case {
    const char *name;
    std::function<void()> fn;
};

inline std::vector<Case> &registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int failures = 0;
inline int checks = 0;

struct Registrar {
    Registrar(const char *name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

#define EUTEST_CONCAT2(a, b) a##b
#define EUTEST_CONCAT(a, b) EUTEST_CONCAT2(a, b)

#define TEST_CASE(name)                                                        \
    static void EUTEST_CONCAT(eutest_fn_, __LINE__)();                         \
    static ::eutest::Registrar EUTEST_CONCAT(eutest_reg_, __LINE__)(           \
        name, EUTEST_CONCAT(eutest_fn_, __LINE__));                            \
    static void EUTEST_CONCAT(eutest_fn_, __LINE__)()

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++::eutest::checks;                                                    \
        if (!(cond)) {                                                         \
            ++::eutest::failures;                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,       \
                         #cond);                                               \
        }                                                                      \
    } while (0)

#define CHECK_EQ(a, b)                                                         \
    do {                                                                       \
        ++::eutest::checks;                                                    \
        if (!((a) == (b))) {                                                   \
            ++::eutest::failures;                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, \
                         #a, #b);                                              \
        }                                                                      \
    } while (0)

inline int run_all() {
    for (const auto &c : registry()) {
        const int before = failures;
        c.fn();
        std::printf("%s %s\n", failures == before ? "ok  " : "FAIL", c.name);
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

} // namespace eutest

#define EUTEST_MAIN                                                            \
    int main() { return ::eutest::run_all(); }

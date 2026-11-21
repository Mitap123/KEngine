// K Engine — Tests/Unit/TestFramework.h
//
// Tiny self-contained test harness (no third-party dependency; see
// DEPENDENCIES.md). Usage:
//
//   KTEST(my_case) { KCHECK(1 + 1 == 2); KCHECK_EQ(x, 3); }
//
// A failed KCHECK records the failure and continues the test case, so one
// run reports every broken assertion. Test cases self-register.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace ktest {

struct Case {
    const char* name;
    void (*fn)();
};

std::vector<Case>& registry();
void fail(const char* file, int line, const std::string& message);
int current_failures();
void reset_current_failures();

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

// Test sizes: the default ("fast") mode keeps every test meaningful but small enough that
// the whole suite is quick even on an old CPU with an unoptimized, iterator-checked STL
// (the first Windows Debug run took 311 s). KENGINE_TEST_FULL=1 selects the full sizes.
inline bool full_mode() {
    static const bool v = [] {
        const char* e = std::getenv("KENGINE_TEST_FULL");
        return e != nullptr && e[0] == '1';
    }();
    return v;
}
inline std::size_t scaled(std::size_t fast, std::size_t full) { return full_mode() ? full : fast; }

template <class T>
std::string show(const T& v) {
    if constexpr (std::is_same_v<T, std::string>) {
        return "\"" + v + "\"";
    } else if constexpr (std::is_same_v<T, bool>) {
        return v ? "true" : "false";
    } else if constexpr (std::is_enum_v<T>) {
        return std::to_string(static_cast<std::underlying_type_t<T>>(v));
    } else if constexpr (std::is_arithmetic_v<T>) {
        std::ostringstream os;
        os.precision(17);
        os << +v;  // unary + promotes char-sized ints so they print as numbers
        return os.str();
    } else {
        return "<value>";
    }
}

}  // namespace ktest

#define KTEST(name)                                                \
    static void name();                                            \
    static ::ktest::Registrar ktest_registrar_##name(#name, &name); \
    static void name()

#define KCHECK(cond)                                                            \
    do {                                                                        \
        if (!(cond)) ::ktest::fail(__FILE__, __LINE__, "CHECK failed: " #cond); \
    } while (0)

#define KCHECK_EQ(a, b)                                                                     \
    do {                                                                                    \
        const auto ktest_a = (a); /* by value: (a)/(b) may be rvalue-derived refs */       \
        const auto ktest_b = (b);                                                           \
        if (!(ktest_a == ktest_b)) {                                                        \
            ::ktest::fail(__FILE__, __LINE__,                                               \
                          std::string("CHECK_EQ failed: " #a " == " #b "  (") +             \
                              ::ktest::show(ktest_a) + " vs " + ::ktest::show(ktest_b) + ")"); \
        }                                                                                   \
    } while (0)

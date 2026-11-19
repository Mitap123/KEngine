// Test runner: runs every registered KTEST (optionally filtered by substring)
// and exits nonzero if any assertion failed.
//
//   k_unit_tests                 run all
//   k_unit_tests <substring>     run cases whose name contains <substring>
//   k_unit_tests --list          list case names
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

#include "Tests/Unit/TestFramework.h"

namespace ktest {

std::vector<Case>& registry() {
    static std::vector<Case> r;  // function-local: safe against static-init order
    return r;
}

namespace {
int g_current_failures = 0;
}

void fail(const char* file, int line, const std::string& message) {
    ++g_current_failures;
    std::printf("    %s:%d: %s\n", file, line, message.c_str());
}

int current_failures() { return g_current_failures; }
void reset_current_failures() { g_current_failures = 0; }

}  // namespace ktest

int main(int argc, char** argv) {
    const std::string filter = (argc > 1) ? argv[1] : "";
    if (filter == "--list") {
        for (const auto& c : ktest::registry()) std::printf("%s\n", c.name);
        return 0;
    }

    int passed = 0, failed = 0, not_run = 0;
    for (const auto& c : ktest::registry()) {
        if (!filter.empty() && std::string(c.name).find(filter) == std::string::npos) {
            ++not_run;
            continue;
        }
        ktest::reset_current_failures();
        std::printf("[ RUN  ] %s\n", c.name);
        try {
            c.fn();
        } catch (const std::exception& e) {
            ktest::fail(__FILE__, __LINE__, std::string("uncaught exception: ") + e.what());
        } catch (...) {
            ktest::fail(__FILE__, __LINE__, "uncaught non-standard exception");
        }
        if (ktest::current_failures() == 0) {
            ++passed;
            std::printf("[  OK  ] %s\n", c.name);
        } else {
            ++failed;
            std::printf("[ FAIL ] %s (%d failed checks)\n", c.name, ktest::current_failures());
        }
    }
    std::printf("\nsummary: passed=%d failed=%d not_run=%d\n", passed, failed, not_run);
    if (passed + failed == 0) {
        std::printf("error: no test cases were run (bad filter?)\n");
        return 2;
    }
    return failed == 0 ? 0 : 1;
}

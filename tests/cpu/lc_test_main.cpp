#include "lc_test.h"

#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

namespace lc::test {

namespace {
int g_failuresInCurrentCase = 0;
}

std::vector<Case>& Registry() {
    static std::vector<Case> cases;
    return cases;
}

Registrar::Registrar(const char* name, std::function<void()> body) {
    Registry().push_back(Case{name, std::move(body)});
}

void Fail(const char* file, int line, const std::string& message) {
    ++g_failuresInCurrentCase;
    std::printf("    %s(%d): %s\n", file, line, message.c_str());
}

}  // namespace lc::test

int main(int argc, char** argv) {
    std::string filter;
    bool list = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--list") == 0) {
            list = true;
        } else if (std::strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
            filter = argv[++i];
        } else {
            std::printf("usage: lc_cpu_tests [--list] [--filter <substring>]\n");
            return 2;
        }
    }

    int passed = 0;
    int failed = 0;
    for (const auto& c : lc::test::Registry()) {
        if (!filter.empty() && c.name.find(filter) == std::string::npos) {
            continue;
        }
        if (list) {
            std::printf("%s\n", c.name.c_str());
            continue;
        }
        lc::test::g_failuresInCurrentCase = 0;
        try {
            c.body();
        } catch (const lc::test::RequireFailed&) {
            // Already recorded.
        } catch (const std::exception& e) {
            lc::test::Fail("<exception>", 0, std::string("unhandled exception: ") + e.what());
        } catch (...) {
            lc::test::Fail("<exception>", 0, "unhandled non-standard exception");
        }
        if (lc::test::g_failuresInCurrentCase == 0) {
            ++passed;
            std::printf("[PASS] %s\n", c.name.c_str());
        } else {
            ++failed;
            std::printf("[FAIL] %s\n", c.name.c_str());
        }
    }
    if (list) {
        return 0;
    }
    std::printf("%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}

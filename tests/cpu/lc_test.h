// Minimal test harness: LC_TEST registers a case; LC_CHECK* record failures; LC_REQUIRE aborts the case.
#pragma once

#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace lc::test {

struct Case {
    std::string name;
    std::function<void()> body;
};

std::vector<Case>& Registry();

struct Registrar {
    Registrar(const char* name, std::function<void()> body);
};

// Records a failure for the running case. Does not throw.
void Fail(const char* file, int line, const std::string& message);

// Thrown by LC_REQUIRE to abandon the current case after recording the failure.
struct RequireFailed {};

}  // namespace lc::test

#define LC_TEST(name)                                                                     \
    static void lc_test_##name();                                                         \
    static ::lc::test::Registrar lc_registrar_##name(#name, &lc_test_##name);             \
    static void lc_test_##name()

#define LC_CHECK(cond)                                                                    \
    do {                                                                                  \
        if (!(cond)) ::lc::test::Fail(__FILE__, __LINE__, "LC_CHECK failed: " #cond);     \
    } while (0)

#define LC_CHECK_EQ(a, b)                                                                 \
    do {                                                                                  \
        if (!((a) == (b))) ::lc::test::Fail(__FILE__, __LINE__, "LC_CHECK_EQ failed: " #a " == " #b); \
    } while (0)

#define LC_CHECK_NEAR(a, b, tol)                                                          \
    do {                                                                                  \
        if (!(std::fabs(static_cast<double>(a) - static_cast<double>(b)) <= static_cast<double>(tol))) \
            ::lc::test::Fail(__FILE__, __LINE__,                                          \
                             "LC_CHECK_NEAR failed: " #a " ~ " #b " (got " + std::to_string(static_cast<double>(a)) + \
                                 " vs " + std::to_string(static_cast<double>(b)) + ")");  \
    } while (0)

#define LC_REQUIRE(cond)                                                                  \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            ::lc::test::Fail(__FILE__, __LINE__, "LC_REQUIRE failed: " #cond);            \
            throw ::lc::test::RequireFailed{};                                            \
        }                                                                                 \
    } while (0)

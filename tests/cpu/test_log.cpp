#include "lc_test.h"

#include "core/clock.h"
#include "core/error.h"
#include "core/ids.h"
#include "core/log.h"

#include <string>

LC_TEST(log_counts_errors_even_when_filtered) {
    lc::log::Init(nullptr);
    lc::log::SetMinLevel(lc::log::Level::Error);
    const std::size_t before = lc::log::ErrorCount();
    lc::log::Warn("this warning is filtered and not counted");
    LC_CHECK_EQ(lc::log::ErrorCount(), before);
    lc::log::Error("deliberate test error {} (expected in the test output)", 42);
    LC_CHECK_EQ(lc::log::ErrorCount(), before + 1);
    lc::log::SetMinLevel(lc::log::Level::Info);
}

LC_TEST(clock_is_monotonic_and_timestamps_have_expected_shape) {
    const double a = lc::Clock::SecondsSinceStart();
    const double b = lc::Clock::SecondsSinceStart();
    LC_CHECK(b >= a);
    const std::string iso = lc::Clock::TimestampIso8601();
    LC_CHECK_EQ(iso.size(), std::size_t{19});
    LC_CHECK_EQ(iso[4], '-');
    LC_CHECK_EQ(iso[10], 'T');
    LC_CHECK_EQ(lc::Clock::TimestampCompact().size(), std::size_t{15});
}

LC_TEST(error_message_includes_file_and_line) {
    bool caught = false;
    try {
        LC_THROW("boom");
    } catch (const lc::Error& e) {
        caught = true;
        const std::string what = e.what();
        LC_CHECK(what.find("boom") != std::string::npos);
        LC_CHECK(what.find("test_log.cpp:") != std::string::npos);
    }
    LC_CHECK(caught);
}

LC_TEST(strong_ids_compare_and_validate) {
    lc::MeshId a{1};
    lc::MeshId b{1};
    lc::MeshId c;
    LC_CHECK(a == b);
    LC_CHECK(a.IsValid());
    LC_CHECK(!c.IsValid());
    LC_CHECK(c.value == lc::kInvalidId);
}

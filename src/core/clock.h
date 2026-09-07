// Monotonic process clock and wall-clock timestamps.
#pragma once

#include <string>

namespace lc {

struct Clock {
    // Seconds since the first call in this process (the log initializes it early).
    static double SecondsSinceStart();

    // Local wall-clock time as "YYYY-MM-DDTHH:MM:SS".
    static std::string TimestampIso8601();

    // Wall-clock time usable in file names: "YYYYMMDD-HHMMSS".
    static std::string TimestampCompact();
};

}  // namespace lc

// Startup conveniences for people who did not start the program from a terminal (spec §20 M7:
// clean startup errors; §21: no developer terminal required).
#pragma once

#include <string_view>

namespace lc {

// A modal error box (used for startup failures when no terminal will show the log).
void ShowErrorDialog(std::string_view title, std::string_view text);

// Closes the console when this process is its only user (Windows created it for a double-click);
// a console shared with a shell or a test runner stays attached. Returns true when it was closed.
bool ReleaseOwnConsole();

}  // namespace lc

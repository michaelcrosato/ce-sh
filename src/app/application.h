// Application modes: adapter listing, windowed diagnostics, headless validation and capture, and
// the resize test. Owns the window, device, queue, swap chain, and renderer for one run.
#pragma once

#include "app/options.h"

namespace lc {

class Application {
public:
    explicit Application(AppOptions options);

    // Returns the process exit code: 0 success, 1 failure, 2 usage, 3 unsupported hardware.
    int Run();

private:
    int RunListAdapters();
    int RunRender();

    AppOptions options_;
};

}  // namespace lc

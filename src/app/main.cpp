#include "app/application.h"
#include "app/options.h"
#include "core/error.h"
#include "core/log.h"
#include "platform/win_error.h"

#include <cstdio>
#include <exception>
#include <string>
#include <vector>

int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.push_back(lc::WideToUtf8(argv[i]));
    }

    const lc::ParsedOptions parsed = lc::ParseAppOptions(args);
    if (!parsed.options) {
        std::fprintf(stderr, "error: %s\n\n%s", parsed.error.c_str(), parsed.usage.c_str());
        return 2;
    }
    if (parsed.options->help) {
        std::printf("%s", parsed.usage.c_str());
        return 0;
    }

    int code = 0;
    try {
        lc::Application app(*parsed.options);
        code = app.Run();
    } catch (const lc::UnsupportedHardware& e) {
        lc::log::Error("Unsupported hardware: {}", e.what());
        code = 3;
    } catch (const lc::UsageError& e) {
        lc::log::Error("Usage error: {}", e.what());
        code = 2;
    } catch (const std::exception& e) {
        lc::log::Error("Fatal error: {}", e.what());
        code = 1;
    }
    lc::log::Shutdown();
    return code;
}

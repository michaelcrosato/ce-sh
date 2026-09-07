#include "app/application.h"
#include "app/options.h"
#include "core/build_info.h"
#include "core/clock.h"
#include "core/error.h"
#include "core/log.h"
#include "platform/dialogs.h"
#include "platform/files.h"
#include "platform/win_error.h"

#include <cstdio>
#include <exception>
#include <format>
#include <string>
#include <vector>

namespace {

// Startup failures reach a person through a message box when nothing else would show them: a
// windowed run started by a double-click or a shortcut. Automated runs and --no-dialog never see one.
bool WantsDialog(const lc::AppOptions& o) {
    return !(o.noDialog || o.headless || o.validate || o.listAdapters || o.simulateOnly || o.help || o.version);
}

bool ArgumentsAskForQuiet(const std::vector<std::string>& args) {
    for (const std::string& a : args) {
        if (a == "--no-dialog" || a == "--headless" || a == "--validate" || a == "--list-adapters" || a == "--simulate-only" || a == "--help" || a == "--version") {
            return true;
        }
    }
    return false;
}

std::string LogHint(const lc::AppOptions& o) { return o.logFile ? "\n\nThe log file: " + o.logFile->string() : std::string(); }

}  // namespace

int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.push_back(lc::WideToUtf8(argv[i]));
    }
    if (args.empty()) args = lc::GameLaunchArguments();  // A launch without arguments (a double-click) is the game.

    const lc::ParsedOptions parsed = lc::ParseAppOptions(args);
    if (!parsed.options) {
        std::fprintf(stderr, "error: %s\n\n%s", parsed.error.c_str(), parsed.usage.c_str());
        if (!ArgumentsAskForQuiet(args)) {
            lc::ShowErrorDialog("Last Circuit", "The command line is not valid:\n\n" + parsed.error + "\n\nRun LastCircuit.exe --help from a terminal for the options.");
        }
        return 2;
    }
    if (parsed.options->help) {
        std::printf("%s", parsed.usage.c_str());
        return 0;
    }
    if (parsed.options->version) {
        std::printf("Last Circuit %s build %s%s (%s)\n", lc::build::kVersion, lc::build::kGitCommit, lc::build::kGitDirty ? "-dirty" : "", lc::build::kConfig);
        return 0;
    }

    lc::AppOptions options = *parsed.options;
    if (options.play && !options.logFile) {
        options.logFile = lc::files::UserDataDirectory() / "logs" / std::format("LastCircuit-{}.log", lc::Clock::TimestampCompact());
    }
    const bool dialogs = WantsDialog(options);
    if (dialogs) lc::ReleaseOwnConsole();  // The console of a double-click launch closes; the log file keeps the record.

    int code = 0;
    std::string failure;
    try {
        lc::Application app(options);
        code = app.Run();
    } catch (const lc::UnsupportedHardware& e) {
        lc::log::Error("Unsupported hardware: {}", e.what());
        code = 3;
        failure = std::string("Last Circuit needs a graphics adapter with DirectX Raytracing Tier 1.1 and Shader Model 6.5 (an NVIDIA GeForce RTX card "
                              "or an equivalent) with a current graphics driver.\n\nWhat was found: ") + e.what();
    } catch (const lc::UsageError& e) {
        lc::log::Error("Usage error: {}", e.what());
        code = 2;
        failure = std::string("Last Circuit was started with something it cannot use:\n\n") + e.what();
    } catch (const std::exception& e) {
        lc::log::Error("Fatal error: {}", e.what());
        code = 1;
        failure = std::string("Last Circuit could not continue:\n\n") + e.what();
    }
    if (code != 0 && dialogs) {
        // Failures reported through the exit code alone (a missing or invalid level file, a device
        // lost during the run) still get a box that points at the log.
        if (failure.empty()) {
            const std::string reason = lc::log::LastError();
            failure = reason.empty() ? std::format("Last Circuit stopped with exit code {}. The log file names the problem.", code)
                                     : std::format("Last Circuit could not continue (exit code {}):\n\n{}", code, reason);
        }
        lc::ShowErrorDialog("Last Circuit could not start", failure + LogHint(options));
    }
    lc::log::Shutdown();
    return code;
}

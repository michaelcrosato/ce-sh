// Strict command-line parser. Every option must be declared; unknown options, repeated options,
// missing values, and malformed numbers are reported as errors instead of being ignored.
#pragma once

#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lc {

class ArgParser {
public:
    void AddFlag(std::string name, std::string help);
    void AddStringOption(std::string name, std::string help, std::string defaultValue);
    void AddIntOption(std::string name, std::string help, int defaultValue);
    void AddFloatOption(std::string name, std::string help, float defaultValue);

    // Accepts "--name value" and "--name=value". Returns an error message on failure.
    std::optional<std::string> Parse(std::span<const std::string> args);

    // True when the flag or option appeared on the command line.
    bool Has(std::string_view name) const;

    // Value given on the command line, or the declared default. Numeric getters are safe after a
    // successful Parse because values were validated then.
    std::string GetString(std::string_view name) const;
    int GetInt(std::string_view name) const;
    float GetFloat(std::string_view name) const;

    std::string Usage(std::string_view programName, std::string_view description) const;

private:
    enum class Kind { Flag, String, Int, Float };
    struct Decl {
        std::string name;
        std::string help;
        Kind kind;
        std::string defaultValue;
    };

    const Decl* Find(std::string_view name) const;

    std::vector<Decl> decls_;
    std::map<std::string, std::string, std::less<>> values_;
    std::set<std::string, std::less<>> present_;
};

}  // namespace lc

#include "core/cli.h"

#include <charconv>
#include <format>

namespace lc {

namespace {

bool ParseInt(std::string_view text, int& out) {
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, out);
    return result.ec == std::errc{} && result.ptr == end;
}

bool ParseFloat(std::string_view text, float& out) {
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, out);
    return result.ec == std::errc{} && result.ptr == end;
}

}  // namespace

void ArgParser::AddFlag(std::string name, std::string help) {
    decls_.push_back(Decl{std::move(name), std::move(help), Kind::Flag, {}});
}

void ArgParser::AddStringOption(std::string name, std::string help, std::string defaultValue) {
    decls_.push_back(Decl{std::move(name), std::move(help), Kind::String, std::move(defaultValue)});
}

void ArgParser::AddIntOption(std::string name, std::string help, int defaultValue) {
    decls_.push_back(Decl{std::move(name), std::move(help), Kind::Int, std::to_string(defaultValue)});
}

void ArgParser::AddFloatOption(std::string name, std::string help, float defaultValue) {
    decls_.push_back(Decl{std::move(name), std::move(help), Kind::Float, std::format("{}", defaultValue)});
}

const ArgParser::Decl* ArgParser::Find(std::string_view name) const {
    for (const Decl& d : decls_) {
        if (d.name == name) {
            return &d;
        }
    }
    return nullptr;
}

std::optional<std::string> ArgParser::Parse(std::span<const std::string> args) {
    values_.clear();
    present_.clear();

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg.size() < 3 || arg[0] != '-' || arg[1] != '-') {
            return std::format("unexpected argument '{}' (options start with --; see --help)", arg);
        }

        std::string name = arg.substr(2);
        std::optional<std::string> value;
        if (const auto eq = name.find('='); eq != std::string::npos) {
            value = name.substr(eq + 1);
            name = name.substr(0, eq);
        }

        const Decl* decl = Find(name);
        if (decl == nullptr) {
            return std::format("unknown option '--{}' (see --help for the list of options)", name);
        }
        if (present_.contains(name)) {
            return std::format("option '--{}' was given more than once", name);
        }
        present_.insert(name);

        if (decl->kind == Kind::Flag) {
            if (value.has_value()) {
                return std::format("flag '--{}' does not take a value", name);
            }
            continue;
        }

        if (!value.has_value()) {
            if (i + 1 >= args.size()) {
                return std::format("option '--{}' requires a value", name);
            }
            const std::string& next = args[i + 1];
            if (next.size() >= 2 && next[0] == '-' && next[1] == '-') {
                return std::format("option '--{}' requires a value, but found '{}'", name, next);
            }
            value = next;
            ++i;
        }

        if (decl->kind == Kind::Int) {
            int parsed = 0;
            if (!ParseInt(*value, parsed)) {
                return std::format("option '--{}' expects an integer, got '{}'", name, *value);
            }
        } else if (decl->kind == Kind::Float) {
            float parsed = 0.0f;
            if (!ParseFloat(*value, parsed)) {
                return std::format("option '--{}' expects a number, got '{}'", name, *value);
            }
        }
        values_[name] = *value;
    }
    return std::nullopt;
}

bool ArgParser::Has(std::string_view name) const { return present_.find(name) != present_.end(); }

std::string ArgParser::GetString(std::string_view name) const {
    if (const auto it = values_.find(name); it != values_.end()) {
        return it->second;
    }
    const Decl* decl = Find(name);
    return decl != nullptr ? decl->defaultValue : std::string{};
}

int ArgParser::GetInt(std::string_view name) const {
    int value = 0;
    ParseInt(GetString(name), value);
    return value;
}

float ArgParser::GetFloat(std::string_view name) const {
    float value = 0.0f;
    ParseFloat(GetString(name), value);
    return value;
}

std::string ArgParser::Usage(std::string_view programName, std::string_view description) const {
    std::string text = std::format("{}\n\nUsage: {} [options]\n\nOptions:\n", description, programName);
    std::size_t width = 0;
    for (const Decl& d : decls_) {
        width = std::max(width, d.name.size() + (d.kind == Kind::Flag ? 0 : 8));
    }
    for (const Decl& d : decls_) {
        std::string left = "--" + d.name;
        switch (d.kind) {
            case Kind::Flag: break;
            case Kind::String: left += " <text>"; break;
            case Kind::Int: left += " <int>"; break;
            case Kind::Float: left += " <num>"; break;
        }
        text += std::format("  {:<{}}  {}", left, width + 2, d.help);
        if (d.kind != Kind::Flag && !d.defaultValue.empty()) {
            text += std::format(" [default: {}]", d.defaultValue);
        }
        text += "\n";
    }
    return text;
}

}  // namespace lc

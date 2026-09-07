// Minimal JSON emitter for reports and capture metadata (pretty-printed, UTF-8, escapes control
// characters). Not a parser; scene files get a real parser in M2.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lc {

class JsonWriter {
public:
    void BeginObject();
    void EndObject();
    void BeginArray();
    void EndArray();

    void Key(std::string_view key);
    void Value(std::string_view text);
    void Value(const char* text) { Value(std::string_view(text)); }
    void Value(const std::string& text) { Value(std::string_view(text)); }
    void Value(bool value);
    void Value(double value);
    void Value(float value) { Value(static_cast<double>(value)); }
    void Value(std::int64_t value);
    void Value(std::uint64_t value);
    void Value(std::int32_t value) { Value(static_cast<std::int64_t>(value)); }
    void Value(std::uint32_t value) { Value(static_cast<std::uint64_t>(value)); }
    void Null();

    template <class T>
    void Field(std::string_view key, const T& value) {
        Key(key);
        Value(value);
    }

    const std::string& Text() const { return out_; }

private:
    void BeforeValue();
    void Newline();
    void Raw(std::string_view text) { out_.append(text); }

    std::string out_;
    std::vector<bool> firstInScope_;  // One entry per open object/array.
    bool afterKey_ = false;
};

std::string JsonEscape(std::string_view text);

}  // namespace lc

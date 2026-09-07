#include "core/json_writer.h"

#include <cmath>
#include <format>

namespace lc {

std::string JsonEscape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 2);
    for (const char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out += std::format("\\u{:04x}", static_cast<unsigned>(static_cast<unsigned char>(c)));
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

void JsonWriter::Newline() {
    out_ += '\n';
    out_.append(firstInScope_.size() * 2, ' ');
}

void JsonWriter::BeforeValue() {
    if (afterKey_) {
        afterKey_ = false;
        return;
    }
    if (firstInScope_.empty()) {
        return;
    }
    if (!firstInScope_.back()) {
        out_ += ',';
    }
    firstInScope_.back() = false;
    Newline();
}

void JsonWriter::BeginObject() {
    BeforeValue();
    Raw("{");
    firstInScope_.push_back(true);
}

void JsonWriter::EndObject() {
    const bool empty = firstInScope_.back();
    firstInScope_.pop_back();
    if (!empty) {
        Newline();
    }
    Raw("}");
}

void JsonWriter::BeginArray() {
    BeforeValue();
    Raw("[");
    firstInScope_.push_back(true);
}

void JsonWriter::EndArray() {
    const bool empty = firstInScope_.back();
    firstInScope_.pop_back();
    if (!empty) {
        Newline();
    }
    Raw("]");
}

void JsonWriter::Key(std::string_view key) {
    BeforeValue();
    Raw("\"" + JsonEscape(key) + "\": ");
    afterKey_ = true;
}

void JsonWriter::Value(std::string_view text) {
    BeforeValue();
    Raw("\"" + JsonEscape(text) + "\"");
}

void JsonWriter::Value(bool value) {
    BeforeValue();
    Raw(value ? "true" : "false");
}

void JsonWriter::Value(double value) {
    BeforeValue();
    if (!std::isfinite(value)) {
        Raw("null");
    } else {
        Raw(std::format("{}", value));
    }
}

void JsonWriter::Value(std::int64_t value) {
    BeforeValue();
    Raw(std::format("{}", value));
}

void JsonWriter::Value(std::uint64_t value) {
    BeforeValue();
    Raw(std::format("{}", value));
}

void JsonWriter::Null() {
    BeforeValue();
    Raw("null");
}

}  // namespace lc

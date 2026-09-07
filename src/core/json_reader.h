// Strict JSON parser (RFC 8259) into a small DOM. No comments, no trailing commas, no NaN/Infinity,
// depth limited to 64, errors report line and column. Used for replay files and scene files.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lc::json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;
    static Value MakeBool(bool b);
    static Value MakeNumber(double n);
    static Value MakeString(std::string s);
    static Value MakeArray(std::vector<Value> items);
    static Value MakeObject(std::vector<std::string> keys, std::vector<Value> values);

    Type GetType() const { return type_; }
    bool IsNull() const { return type_ == Type::Null; }
    bool IsBool() const { return type_ == Type::Bool; }
    bool IsNumber() const { return type_ == Type::Number; }
    bool IsString() const { return type_ == Type::String; }
    bool IsArray() const { return type_ == Type::Array; }
    bool IsObject() const { return type_ == Type::Object; }

    bool AsBool() const { return bool_; }
    double AsNumber() const { return number_; }
    const std::string& AsString() const { return string_; }

    // Arrays and objects.
    std::size_t Size() const { return type_ == Type::Object ? keys_.size() : items_.size(); }
    const Value* At(std::size_t index) const;                 // Array element or nullptr.
    const Value* Get(std::string_view key) const;             // Object member or nullptr.
    const std::vector<Value>& Items() const { return items_; }  // Array items, or object values in key order.
    const std::vector<std::string>& Keys() const { return keys_; }

    // Convenience accessors with defaults (return the default when the member is absent or mistyped).
    double NumberOr(std::string_view key, double fallback) const;
    bool BoolOr(std::string_view key, bool fallback) const;
    std::string StringOr(std::string_view key, std::string_view fallback) const;

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Value> items_;
    std::vector<std::string> keys_;
};

struct ParseResult {
    std::optional<Value> value;  // Empty on error.
    std::string error;           // "line L, column C: message" on error.
};

ParseResult Parse(std::string_view text);

}  // namespace lc::json

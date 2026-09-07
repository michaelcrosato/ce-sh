#include "core/json_reader.h"

#include <charconv>
#include <cstdint>
#include <format>

namespace lc::json {

Value Value::MakeBool(bool b) {
    Value v;
    v.type_ = Type::Bool;
    v.bool_ = b;
    return v;
}

Value Value::MakeNumber(double n) {
    Value v;
    v.type_ = Type::Number;
    v.number_ = n;
    return v;
}

Value Value::MakeString(std::string s) {
    Value v;
    v.type_ = Type::String;
    v.string_ = std::move(s);
    return v;
}

Value Value::MakeArray(std::vector<Value> items) {
    Value v;
    v.type_ = Type::Array;
    v.items_ = std::move(items);
    return v;
}

Value Value::MakeObject(std::vector<std::string> keys, std::vector<Value> values) {
    Value v;
    v.type_ = Type::Object;
    v.keys_ = std::move(keys);
    v.items_ = std::move(values);
    return v;
}

const Value* Value::At(std::size_t index) const {
    if (type_ != Type::Array || index >= items_.size()) {
        return nullptr;
    }
    return &items_[index];
}

const Value* Value::Get(std::string_view key) const {
    if (type_ != Type::Object) {
        return nullptr;
    }
    for (std::size_t i = 0; i < keys_.size(); ++i) {
        if (keys_[i] == key) {
            return &items_[i];
        }
    }
    return nullptr;
}

double Value::NumberOr(std::string_view key, double fallback) const {
    const Value* v = Get(key);
    return v != nullptr && v->IsNumber() ? v->AsNumber() : fallback;
}

bool Value::BoolOr(std::string_view key, bool fallback) const {
    const Value* v = Get(key);
    return v != nullptr && v->IsBool() ? v->AsBool() : fallback;
}

std::string Value::StringOr(std::string_view key, std::string_view fallback) const {
    const Value* v = Get(key);
    return v != nullptr && v->IsString() ? v->AsString() : std::string(fallback);
}

namespace {

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    ParseResult Run() {
        ParseResult result;
        Value value;
        if (!ParseValue(value, 0)) {
            result.error = error_;
            return result;
        }
        SkipWhitespace();
        if (pos_ != text_.size()) {
            Fail("unexpected trailing characters");
            result.error = error_;
            return result;
        }
        result.value = std::move(value);
        return result;
    }

private:
    static constexpr int kMaxDepth = 64;

    bool Fail(const std::string& message) {
        if (error_.empty()) {
            error_ = std::format("line {}, column {}: {}", line_, column_, message);
        }
        return false;
    }

    bool AtEnd() const { return pos_ >= text_.size(); }
    char Peek() const { return AtEnd() ? '\0' : text_[pos_]; }

    char Take() {
        const char c = text_[pos_++];
        if (c == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
        return c;
    }

    void SkipWhitespace() {
        while (!AtEnd()) {
            const char c = Peek();
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                Take();
            } else {
                break;
            }
        }
    }

    bool Expect(char c) {
        if (Peek() != c) {
            return Fail(std::format("expected '{}'", c));
        }
        Take();
        return true;
    }

    bool ParseValue(Value& out, int depth) {
        if (depth > kMaxDepth) {
            return Fail("nesting deeper than 64 levels");
        }
        SkipWhitespace();
        if (AtEnd()) {
            return Fail("unexpected end of input");
        }
        const char c = Peek();
        if (c == '{') return ParseObject(out, depth);
        if (c == '[') return ParseArray(out, depth);
        if (c == '"') {
            std::string s;
            if (!ParseString(s)) return false;
            out = Value::MakeString(std::move(s));
            return true;
        }
        if (c == 't') return ParseLiteral("true", Value::MakeBool(true), out);
        if (c == 'f') return ParseLiteral("false", Value::MakeBool(false), out);
        if (c == 'n') return ParseLiteral("null", Value{}, out);
        if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber(out);
        return Fail(std::format("unexpected character '{}'", c));
    }

    bool ParseLiteral(std::string_view literal, Value value, Value& out) {
        if (text_.substr(pos_, literal.size()) != literal) {
            return Fail(std::format("expected '{}'", literal));
        }
        for (std::size_t i = 0; i < literal.size(); ++i) Take();
        out = std::move(value);
        return true;
    }

    bool ParseNumber(Value& out) {
        const std::size_t start = pos_;
        if (Peek() == '-') Take();
        if (Peek() == '0') {
            Take();
        } else if (Peek() >= '1' && Peek() <= '9') {
            while (Peek() >= '0' && Peek() <= '9') Take();
        } else {
            return Fail("invalid number");
        }
        if (Peek() == '.') {
            Take();
            if (!(Peek() >= '0' && Peek() <= '9')) return Fail("digit expected after decimal point");
            while (Peek() >= '0' && Peek() <= '9') Take();
        }
        if (Peek() == 'e' || Peek() == 'E') {
            Take();
            if (Peek() == '+' || Peek() == '-') Take();
            if (!(Peek() >= '0' && Peek() <= '9')) return Fail("digit expected in exponent");
            while (Peek() >= '0' && Peek() <= '9') Take();
        }
        const std::string_view token = text_.substr(start, pos_ - start);
        double value = 0.0;
        const auto r = std::from_chars(token.data(), token.data() + token.size(), value);
        if (r.ec != std::errc{} || r.ptr != token.data() + token.size()) {
            return Fail("number out of range");
        }
        out = Value::MakeNumber(value);
        return true;
    }

    static void AppendUtf8(std::string& s, std::uint32_t cp) {
        if (cp < 0x80) {
            s += static_cast<char>(cp);
        } else if (cp < 0x800) {
            s += static_cast<char>(0xC0 | (cp >> 6));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            s += static_cast<char>(0xE0 | (cp >> 12));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            s += static_cast<char>(0xF0 | (cp >> 18));
            s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool ParseHex4(std::uint32_t& out) {
        if (pos_ + 4 > text_.size()) return Fail("truncated \\u escape");
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = Take();
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<std::uint32_t>(c - 'A' + 10);
            else return Fail("invalid hex digit in \\u escape");
        }
        out = value;
        return true;
    }

    bool ParseString(std::string& out) {
        if (!Expect('"')) return false;
        while (true) {
            if (AtEnd()) return Fail("unterminated string");
            const char c = Take();
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return Fail("control character in string");
            if (c != '\\') {
                out += c;
                continue;
            }
            if (AtEnd()) return Fail("unterminated escape");
            const char e = Take();
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    std::uint32_t cp = 0;
                    if (!ParseHex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (text_.substr(pos_, 2) != "\\u") return Fail("high surrogate without low surrogate");
                        Take();
                        Take();
                        std::uint32_t low = 0;
                        if (!ParseHex4(low)) return false;
                        if (low < 0xDC00 || low > 0xDFFF) return Fail("invalid low surrogate");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return Fail("unexpected low surrogate");
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default:
                    return Fail(std::format("invalid escape '\\{}'", e));
            }
        }
    }

    bool ParseArray(Value& out, int depth) {
        if (!Expect('[')) return false;
        std::vector<Value> items;
        SkipWhitespace();
        if (Peek() == ']') {
            Take();
            out = Value::MakeArray(std::move(items));
            return true;
        }
        while (true) {
            Value item;
            if (!ParseValue(item, depth + 1)) return false;
            items.push_back(std::move(item));
            SkipWhitespace();
            if (Peek() == ',') {
                Take();
                SkipWhitespace();
                if (Peek() == ']') return Fail("trailing comma in array");
                continue;
            }
            if (Peek() == ']') {
                Take();
                out = Value::MakeArray(std::move(items));
                return true;
            }
            return Fail("expected ',' or ']' in array");
        }
    }

    bool ParseObject(Value& out, int depth) {
        if (!Expect('{')) return false;
        std::vector<std::string> keys;
        std::vector<Value> values;
        SkipWhitespace();
        if (Peek() == '}') {
            Take();
            out = Value::MakeObject(std::move(keys), std::move(values));
            return true;
        }
        while (true) {
            SkipWhitespace();
            if (Peek() != '"') return Fail("expected a string key");
            std::string key;
            if (!ParseString(key)) return false;
            SkipWhitespace();
            if (!Expect(':')) return false;
            Value value;
            if (!ParseValue(value, depth + 1)) return false;
            for (const std::string& existing : keys) {
                if (existing == key) return Fail(std::format("duplicate key '{}'", key));
            }
            keys.push_back(std::move(key));
            values.push_back(std::move(value));
            SkipWhitespace();
            if (Peek() == ',') {
                Take();
                SkipWhitespace();
                if (Peek() == '}') return Fail("trailing comma in object");
                continue;
            }
            if (Peek() == '}') {
                Take();
                out = Value::MakeObject(std::move(keys), std::move(values));
                return true;
            }
            return Fail("expected ',' or '}' in object");
        }
    }

    std::string_view text_;
    std::size_t pos_ = 0;
    int line_ = 1;
    int column_ = 1;
    std::string error_;
};

}  // namespace

ParseResult Parse(std::string_view text) {
    Parser parser(text);
    return parser.Run();
}

}  // namespace lc::json

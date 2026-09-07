#include "lc_test.h"

#include "core/json_reader.h"
#include "core/json_writer.h"

#include <string>

LC_TEST(json_reader_parses_nested_documents) {
    const std::string text = R"({"name": "lamp\u00e9 \"q\"", "on": true, "count": 3, "ratio": -1.5e2, "list": [1, 2.5, "x", null, [true]], "empty": {}, "nested": {"a": {"b": [0]}}})";
    const lc::json::ParseResult r = lc::json::Parse(text);
    LC_REQUIRE(r.value.has_value());
    const lc::json::Value& v = *r.value;
    LC_CHECK(v.IsObject());
    LC_CHECK_EQ(v.Size(), std::size_t{7});
    LC_CHECK_EQ(v.StringOr("name", ""), std::string("lamp\xC3\xA9 \"q\""));
    LC_CHECK(v.BoolOr("on", false));
    LC_CHECK_NEAR(v.NumberOr("count", 0), 3.0, 0.0);
    LC_CHECK_NEAR(v.NumberOr("ratio", 0), -150.0, 0.0);
    const lc::json::Value* list = v.Get("list");
    LC_REQUIRE(list != nullptr && list->IsArray());
    LC_CHECK_EQ(list->Size(), std::size_t{5});
    LC_CHECK_NEAR(list->At(1)->AsNumber(), 2.5, 0.0);
    LC_CHECK(list->At(3)->IsNull());
    LC_CHECK(list->At(4)->At(0)->AsBool());
    LC_CHECK(list->At(9) == nullptr);
    LC_CHECK(v.Get("empty")->IsObject() && v.Get("empty")->Size() == 0);
    LC_CHECK_NEAR(v.Get("nested")->Get("a")->Get("b")->At(0)->AsNumber(), 0.0, 0.0);
    LC_CHECK(v.Get("missing") == nullptr);
    LC_CHECK_NEAR(v.NumberOr("name", 7.0), 7.0, 0.0);  // Mistyped member falls back.
}

LC_TEST(json_reader_rejects_invalid_documents) {
    const char* bad[] = {
        "{\"a\": 1,}", "[1, 2,]", "{a: 1}", "\"unterminated", "{\"a\": tru}", "[1] extra", "01", "1.", "-", "{\"a\": 1, \"a\": 2}",
        "\"bad \\x escape\"", "[\"\\ud800\"]", "", "{\"a\":\n[1, 2}",
    };
    for (const char* text : bad) {
        const lc::json::ParseResult r = lc::json::Parse(text);
        if (r.value.has_value()) {
            lc::test::Fail(__FILE__, __LINE__, std::string("accepted invalid JSON: ") + text);
        } else {
            LC_CHECK(r.error.find("line") != std::string::npos && r.error.find("column") != std::string::npos);
        }
    }
    // Line and column point at the problem.
    const lc::json::ParseResult r = lc::json::Parse("{\n  \"a\": 1,\n  \"b\": ]\n}");
    LC_REQUIRE(!r.value.has_value());
    LC_CHECK(r.error.rfind("line 3, column 8", 0) == 0);
}

LC_TEST(json_reader_depth_limit_and_surrogates) {
    std::string deep;
    for (int i = 0; i < 70; ++i) deep += '[';
    for (int i = 0; i < 70; ++i) deep += ']';
    LC_CHECK(!lc::json::Parse(deep).value.has_value());
    std::string ok;
    for (int i = 0; i < 60; ++i) ok += '[';
    for (int i = 0; i < 60; ++i) ok += ']';
    LC_CHECK(lc::json::Parse(ok).value.has_value());

    const lc::json::ParseResult r = lc::json::Parse("\"\\ud83d\\ude00\"");
    LC_REQUIRE(r.value.has_value());
    LC_CHECK_EQ(r.value->AsString(), std::string("\xF0\x9F\x98\x80"));
}

LC_TEST(json_reader_round_trips_writer_output) {
    lc::JsonWriter w;
    w.BeginObject();
    w.Field("scene", "two_room");
    w.Field("tick", 42u);
    w.Field("alpha", 0.25);
    w.Key("moves");
    w.BeginArray();
    w.Value(-1.0);
    w.Value(true);
    w.Null();
    w.EndArray();
    w.EndObject();
    const lc::json::ParseResult r = lc::json::Parse(w.Text());
    LC_REQUIRE(r.value.has_value());
    LC_CHECK_EQ(r.value->StringOr("scene", ""), std::string("two_room"));
    LC_CHECK_NEAR(r.value->NumberOr("tick", 0), 42.0, 0.0);
    LC_CHECK_NEAR(r.value->NumberOr("alpha", 0), 0.25, 0.0);
    LC_CHECK_EQ(r.value->Get("moves")->Size(), std::size_t{3});
}

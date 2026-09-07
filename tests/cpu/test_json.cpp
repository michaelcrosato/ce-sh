#include "lc_test.h"

#include "core/json_writer.h"

#include <string>

LC_TEST(json_writer_nests_and_escapes) {
    lc::JsonWriter j;
    j.BeginObject();
    j.Field("name", "tab\there \"quoted\"");
    j.Field("count", 3u);
    j.Field("ratio", 0.5);
    j.Field("flag", false);
    j.Key("list");
    j.BeginArray();
    j.Value(1);
    j.Value("two");
    j.BeginObject();
    j.EndObject();
    j.EndArray();
    j.Key("empty");
    j.BeginArray();
    j.EndArray();
    j.Key("nothing");
    j.Null();
    j.EndObject();

    const std::string expected =
        "{\n"
        "  \"name\": \"tab\\there \\\"quoted\\\"\",\n"
        "  \"count\": 3,\n"
        "  \"ratio\": 0.5,\n"
        "  \"flag\": false,\n"
        "  \"list\": [\n"
        "    1,\n"
        "    \"two\",\n"
        "    {}\n"
        "  ],\n"
        "  \"empty\": [],\n"
        "  \"nothing\": null\n"
        "}";
    LC_CHECK_EQ(j.Text(), expected);
    LC_CHECK_EQ(lc::JsonEscape(std::string("a\x01" "b")), std::string("a\\u0001b"));
}

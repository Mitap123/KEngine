#include <cmath>
#include <limits>
#include <string>

#include "Engine/Serialization/Json.h"
#include "Tests/Unit/TestFramework.h"

using k::Json;

namespace {
bool parses(const std::string& s) { return Json::parse(s).ok(); }
std::string parse_error_message(const std::string& s) {
    auto r = Json::parse(s);
    return r.ok() ? std::string() : r.error().message;
}
}  // namespace

KTEST(json_parses_scalars_and_containers) {
    auto r = Json::parse(R"({"a":[1,2.5,-3e2,true,false,null],"b":{"c":"d"}, "e": ""})");
    KCHECK(r.ok());
    if (!r.ok()) return;
    const Json& j = r.value();
    KCHECK(j.is_object());
    const Json* a = j.find("a");
    KCHECK(a && a->is_array() && a->items().size() == 6);
    if (a && a->items().size() == 6) {
        KCHECK_EQ(a->items()[0].as_number(), 1.0);
        KCHECK_EQ(a->items()[1].as_number(), 2.5);
        KCHECK_EQ(a->items()[2].as_number(), -300.0);
        KCHECK(a->items()[3].is_bool() && a->items()[3].as_bool());
        KCHECK(a->items()[4].is_bool() && !a->items()[4].as_bool());
        KCHECK(a->items()[5].is_null());
    }
    const Json* c = j.find("b") ? j.find("b")->find("c") : nullptr;
    KCHECK(c && c->is_string() && c->as_string() == "d");
    KCHECK(j.find("missing") == nullptr);
}

KTEST(json_accepts_surrounding_whitespace_and_primitives_at_root) {
    KCHECK(parses("  \n\t 42 \r\n"));
    KCHECK(parses("null"));
    KCHECK(parses("\"x\""));
    KCHECK(parses("[]"));
    KCHECK(parses("{}"));
}

KTEST(json_rejects_syntax_errors) {
    const char* bad[] = {
        "", "   ", "{", "}", "[", "]", "[1,]", "[,1]", "{\"a\":1,}", "{\"a\"}", "{\"a\":}", "{a:1}",
        "{'a':1}", "[1 2]", "[1,,2]", "nul", "tru", "True", "NaN", "Infinity", "-Infinity", "+1",
        "01", "-01", "1.", ".5", "1e", "1e+", "--1", "0x10", "\"unterminated", "\"bad\\q\"",
        "// c\n1", "/* c */ 1", "1 2", "{} {}", "[1]x", "\"tab\there\"",  // raw TAB in string
    };
    for (const char* s : bad) {
        const bool ok = parses(s);
        if (ok) ktest::fail(__FILE__, __LINE__, std::string("accepted invalid JSON: ") + s);
    }
}

KTEST(json_error_message_reports_line_and_column) {
    const std::string msg = parse_error_message("{\n  \"a\": 1,\n  \"b\": ]\n}");
    KCHECK(msg.find("line 3") != std::string::npos);
    auto r = Json::parse("[1,");
    KCHECK(!r.ok());
    if (!r.ok()) KCHECK_EQ(r.error().code, std::string("json.syntax"));
}

KTEST(json_rejects_duplicate_keys) {
    const std::string msg = parse_error_message(R"({"a":1,"a":2})");
    KCHECK(!msg.empty());
    KCHECK(msg.find("duplicate") != std::string::npos);
}

KTEST(json_rejects_utf8_bom) {
    KCHECK(!parses("\xEF\xBB\xBF{}"));
}

KTEST(json_string_escapes_and_unicode) {
    auto r = Json::parse("\"a\\\"b\\\\c\\/d\\b\\f\\n\\r\\t \\u00e9 \\u20ac \\ud83d\\ude00\"");
    KCHECK(r.ok());
    if (!r.ok()) return;
    // é = C3 A9, € = E2 82 AC, U+1F600 = F0 9F 98 80
    KCHECK_EQ(r.value().as_string(),
              std::string("a\"b\\c/d\b\f\n\r\t \xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80"));
    // Raw (unescaped) valid UTF-8 passes through.
    auto raw = Json::parse("\"h\xC3\xA9llo\"");
    KCHECK(raw.ok() && raw.value().as_string() == "h\xC3\xA9llo");
}

KTEST(json_rejects_invalid_strings) {
    KCHECK(!parses("\"\\ud83d\""));          // lone high surrogate
    KCHECK(!parses("\"\\ude00\""));          // lone low surrogate
    KCHECK(!parses("\"\\ud83d\\u0041\""));    // high surrogate + non-low-surrogate
    KCHECK(!parses("\"\\u12\""));            // truncated escape
    KCHECK(!parses("\"\\u12zz\""));          // bad hex digit
    KCHECK(!parses("\"\xFF\""));             // invalid UTF-8 byte
    KCHECK(!parses("\"\xC0\x80\""));         // overlong NUL
    KCHECK(!parses("\"\xC3\""));             // truncated sequence
    KCHECK(!parses("\"\xC3\x28\""));         // bad continuation byte
    KCHECK(!parses("\"\xED\xA0\x80\""));     // UTF-8-encoded surrogate
    KCHECK(!parses("\"\xF4\x90\x80\x80\"")); // > U+10FFFF
    KCHECK(!parses(std::string("\"a\0b\"", 5)));  // raw NUL
}

KTEST(json_nesting_depth_is_limited) {
    auto nested = [](int depth) {
        return std::string(static_cast<std::size_t>(depth), '[') + std::string(static_cast<std::size_t>(depth), ']');
    };
    KCHECK(parses(nested(100)));
    KCHECK(!parses(nested(Json::kMaxDepth + 2)));
    KCHECK(!parses(nested(100000)));  // would overflow the stack if unchecked
}

KTEST(json_number_range_and_as_int) {
    // Policy (see Json.h): never silently turn a non-zero number into inf or 0.
    KCHECK(!parses("1e999"));    // overflow
    KCHECK(!parses("-1e999"));
    KCHECK(!parses("1e-999"));   // non-zero value that underflows to zero
    KCHECK(!parses("2e-324"));   // just below the smallest denormal
    KCHECK(parses("5e-324"));    // smallest denormal is representable
    KCHECK(parses("1e-320"));    // other denormals are accepted
    KCHECK(parses("0e-999"));    // a literal zero is zero regardless of exponent
    auto r = Json::parse("[3, 3.0, 3.5, -7, 9007199254740992, 1e300]");
    KCHECK(r.ok());
    if (!r.ok()) return;
    const auto& it = r.value().items();
    KCHECK(it[0].as_int() == 3);
    KCHECK(it[1].as_int() == 3);
    KCHECK(!it[2].as_int().has_value());
    KCHECK(it[3].as_int() == -7);
    KCHECK(it[4].as_int() == 9007199254740992ll);
    KCHECK(!it[5].as_int().has_value());  // beyond int64
    KCHECK(!Json::string("3").as_int().has_value());
}

KTEST(json_dump_compact_and_pretty_are_deterministic) {
    Json o = Json::object();
    o.set("z", Json::number(1));
    o.set("a", Json::array());
    Json arr = Json::array();
    arr.push_back(Json::boolean(true));
    arr.push_back(Json());
    arr.push_back(Json::string("x"));
    o.set("m", std::move(arr));

    auto compact = o.dump();
    KCHECK(compact.ok());
    if (compact.ok()) KCHECK_EQ(compact.value(), std::string(R"({"z":1,"a":[],"m":[true,null,"x"]})"));

    auto pretty = o.dump(2);
    KCHECK(pretty.ok());
    if (pretty.ok()) {
        KCHECK_EQ(pretty.value(), std::string("{\n  \"z\": 1,\n  \"a\": [],\n  \"m\": [\n    true,\n    null,\n    \"x\"\n  ]\n}\n"));
    }
}

KTEST(json_set_replaces_existing_member_keeping_position) {
    Json o = Json::object();
    o.set("a", Json::number(1));
    o.set("b", Json::number(2));
    o.set("a", Json::number(3));
    KCHECK_EQ(o.members().size(), static_cast<std::size_t>(2));
    KCHECK_EQ(o.members()[0].first, std::string("a"));
    KCHECK_EQ(o.find("a")->as_number(), 3.0);
}

KTEST(json_numbers_round_trip_exactly) {
    const double values[] = {0.0, -0.0, 1.0, -1.0, 0.1, 1.0 / 3.0, 123456.789e3, 1e-300, 1e300,
                             5e-324, 1.7976931348623157e308, 9007199254740993.0, 0.30000000000000004};
    for (double v : values) {
        auto text = Json::number(v).dump();
        KCHECK(text.ok());
        if (!text.ok()) continue;
        auto back = Json::parse(text.value());
        KCHECK(back.ok());
        if (!back.ok()) continue;
        const double got = back.value().as_number();
        // Exact bit equality (distinguishes -0.0 from 0.0 too).
        KCHECK(std::signbit(got) == std::signbit(v));
        KCHECK_EQ(got, v);
    }
}

KTEST(json_dump_escapes_strings_and_round_trips) {
    const std::string tricky = std::string("q\"b\\n\n\t\x01\x1f \xC3\xA9 \xF0\x9F\x98\x80");
    auto text = Json::string(tricky).dump();
    KCHECK(text.ok());
    if (!text.ok()) return;
    KCHECK(text.value().find('\n') == std::string::npos);  // newline must be escaped
    auto back = Json::parse(text.value());
    KCHECK(back.ok() && back.value().as_string() == tricky);
}

KTEST(json_dump_rejects_non_finite_numbers) {
    for (double bad : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                       -std::numeric_limits<double>::infinity()}) {
        Json a = Json::array();
        a.push_back(Json::number(bad));
        auto r = a.dump();
        KCHECK(!r.ok());
        if (!r.ok()) KCHECK_EQ(r.error().code, std::string("json.non_finite_number"));
    }
}

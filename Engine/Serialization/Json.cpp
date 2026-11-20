#include "Engine/Serialization/Json.h"

#include <charconv>
#include <cmath>
#include <system_error>
#include <unordered_set>

namespace k {

// ---------------------------------------------------------------- value ---

Json Json::boolean(bool b) { Json j; j.type_ = Type::Bool; j.b_ = b; return j; }
Json Json::number(double d) { Json j; j.type_ = Type::Number; j.n_ = d; return j; }
Json Json::string(std::string s) { Json j; j.type_ = Type::String; j.s_ = std::move(s); return j; }
Json Json::array() { Json j; j.type_ = Type::Array; return j; }
Json Json::object() { Json j; j.type_ = Type::Object; return j; }

std::optional<std::int64_t> Json::as_int() const {
    if (type_ != Type::Number) return std::nullopt;
    if (!std::isfinite(n_) || std::floor(n_) != n_) return std::nullopt;
    // 2^63 is exactly representable as a double; anything >= it overflows int64.
    if (n_ >= 9223372036854775808.0 || n_ < -9223372036854775808.0) return std::nullopt;
    return static_cast<std::int64_t>(n_);
}

Json Json::object_from_unique_members(std::vector<std::pair<std::string, Json>> members) {
    Json j;
    j.type_ = Type::Object;
    j.obj_ = std::move(members);
    return j;
}

void Json::push_back(Json v) {
    if (type_ == Type::Array) arr_.push_back(std::move(v));
}

void Json::set(std::string key, Json v) {
    if (type_ != Type::Object) return;
    for (auto& m : obj_) {
        if (m.first == key) { m.second = std::move(v); return; }
    }
    obj_.emplace_back(std::move(key), std::move(v));
}

const Json* Json::find(std::string_view key) const {
    if (type_ != Type::Object) return nullptr;
    for (const auto& m : obj_) {
        if (m.first == key) return &m.second;
    }
    return nullptr;
}

// --------------------------------------------------------------- parser ---

namespace {

class Parser {
public:
    explicit Parser(std::string_view t) : text_(t) {}

    Result<Json> run() {
        skip_ws();
        Json root;
        if (!parse_value(root, 0)) return fail_result();
        skip_ws();
        if (pos_ != text_.size()) {
            fail("unexpected trailing content after the root value");
            return fail_result();
        }
        return root;
    }

private:
    std::string_view text_;
    std::size_t pos_ = 0;
    std::string err_;
    std::size_t err_pos_ = 0;

    Result<Json> fail_result() const {
        // line/column are 1-based; column counts bytes.
        std::size_t line = 1, col = 1;
        for (std::size_t i = 0; i < err_pos_ && i < text_.size(); ++i) {
            if (text_[i] == '\n') { ++line; col = 1; } else { ++col; }
        }
        return make_error("json.syntax",
                          "JSON syntax error at line " + std::to_string(line) + ", column " +
                              std::to_string(col) + ": " + err_);
    }

    bool fail(std::string msg) {
        if (err_.empty()) { err_ = std::move(msg); err_pos_ = pos_; }
        return false;
    }

    bool at_end() const { return pos_ >= text_.size(); }
    char peek() const { return text_[pos_]; }

    void skip_ws() {
        while (!at_end()) {
            const char c = peek();
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos_; else break;
        }
    }

    bool consume_literal(std::string_view lit) {
        if (text_.substr(pos_, lit.size()) == lit) { pos_ += lit.size(); return true; }
        return fail("invalid literal");
    }

    bool parse_value(Json& out, int depth) {
        if (depth > Json::kMaxDepth) return fail("nesting too deep");
        if (at_end()) return fail("unexpected end of input");
        switch (peek()) {
            case 'n': if (!consume_literal("null")) return false; out = Json(); return true;
            case 't': if (!consume_literal("true")) return false; out = Json::boolean(true); return true;
            case 'f': if (!consume_literal("false")) return false; out = Json::boolean(false); return true;
            case '"': {
                std::string s;
                if (!parse_string(s)) return false;
                out = Json::string(std::move(s));
                return true;
            }
            case '[': return parse_array(out, depth);
            case '{': return parse_object(out, depth);
            default:
                if (peek() == '-' || (peek() >= '0' && peek() <= '9')) return parse_number(out);
                return fail("unexpected character");
        }
    }

    bool parse_array(Json& out, int depth) {
        ++pos_;  // '['
        out = Json::array();
        skip_ws();
        if (!at_end() && peek() == ']') { ++pos_; return true; }
        for (;;) {
            skip_ws();
            Json v;
            if (!parse_value(v, depth + 1)) return false;
            out.push_back(std::move(v));
            skip_ws();
            if (at_end()) return fail("unterminated array");
            if (peek() == ',') { ++pos_; continue; }
            if (peek() == ']') { ++pos_; return true; }
            return fail("expected ',' or ']' in array");
        }
    }

    bool parse_object(Json& out, int depth) {
        ++pos_;  // '{'
        std::vector<std::pair<std::string, Json>> members;
        // Duplicate detection: a linear scan is fastest for the usual handful of
        // keys; past kScanLimit members switch to a hash set so that an object
        // with hundreds of thousands of keys cannot cost O(n^2).
        constexpr std::size_t kScanLimit = 16;
        std::unordered_set<std::string> seen;
        auto is_duplicate = [&](const std::string& key) {
            if (members.size() < kScanLimit) {
                for (const auto& m : members) {
                    if (m.first == key) return true;
                }
                return false;
            }
            if (seen.empty()) {
                for (const auto& m : members) seen.insert(m.first);
            }
            return seen.count(key) != 0;
        };

        skip_ws();
        if (!at_end() && peek() == '}') {
            ++pos_;
            out = Json::object();
            return true;
        }
        for (;;) {
            skip_ws();
            if (at_end() || peek() != '"') return fail("expected string key in object");
            const std::size_t key_pos = pos_;
            std::string key;
            if (!parse_string(key)) return false;
            if (is_duplicate(key)) {
                pos_ = key_pos;
                return fail("duplicate object key \"" + key + "\"");
            }
            skip_ws();
            if (at_end() || peek() != ':') return fail("expected ':' after object key");
            ++pos_;
            skip_ws();
            Json v;
            if (!parse_value(v, depth + 1)) return false;
            if (!seen.empty()) seen.insert(key);  // keep the index in sync once it exists
            members.emplace_back(std::move(key), std::move(v));
            skip_ws();
            if (at_end()) return fail("unterminated object");
            if (peek() == ',') { ++pos_; continue; }
            if (peek() == '}') {
                ++pos_;
                out = Json::object_from_unique_members(std::move(members));
                return true;
            }
            return fail("expected ',' or '}' in object");
        }
    }

    bool parse_number(Json& out) {
        const std::size_t start = pos_;
        std::size_t i = pos_;
        auto digit = [&](std::size_t k) { return k < text_.size() && text_[k] >= '0' && text_[k] <= '9'; };
        if (i < text_.size() && text_[i] == '-') ++i;
        if (!digit(i)) return fail("invalid number");
        if (text_[i] == '0') {
            ++i;
            if (digit(i)) return fail("leading zeros are not allowed in numbers");
        } else {
            while (digit(i)) ++i;
        }
        if (i < text_.size() && text_[i] == '.') {
            ++i;
            if (!digit(i)) return fail("digits required after decimal point");
            while (digit(i)) ++i;
        }
        if (i < text_.size() && (text_[i] == 'e' || text_[i] == 'E')) {
            ++i;
            if (i < text_.size() && (text_[i] == '+' || text_[i] == '-')) ++i;
            if (!digit(i)) return fail("digits required in exponent");
            while (digit(i)) ++i;
        }
        double value = 0.0;
        const auto r = std::from_chars(text_.data() + start, text_.data() + i, value);
        if (r.ec == std::errc::result_out_of_range) {
            pos_ = start;
            return fail("number out of range for a double");
        }
        if (r.ec != std::errc() || r.ptr != text_.data() + i) return fail("invalid number");
        pos_ = i;
        out = Json::number(value);
        return true;
    }

    static void append_utf8(std::string& s, std::uint32_t cp) {
        if (cp < 0x80) {
            s.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            s.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            s.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    bool parse_hex4(std::uint32_t& out) {
        if (pos_ + 4 > text_.size()) return fail("truncated \\u escape");
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_ + static_cast<std::size_t>(i)];
            std::uint32_t d;
            if (c >= '0' && c <= '9') d = static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') d = static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') d = static_cast<std::uint32_t>(c - 'A' + 10);
            else return fail("invalid hex digit in \\u escape");
            v = (v << 4) | d;
        }
        pos_ += 4;
        out = v;
        return true;
    }

    // Validates one UTF-8 sequence starting at pos_ and appends it unchanged.
    bool copy_utf8_sequence(std::string& s) {
        const auto b0 = static_cast<unsigned char>(text_[pos_]);
        std::size_t len;
        std::uint32_t cp;
        if (b0 < 0x80) { len = 1; cp = b0; }
        else if (b0 >= 0xC2 && b0 <= 0xDF) { len = 2; cp = b0 & 0x1Fu; }
        else if (b0 >= 0xE0 && b0 <= 0xEF) { len = 3; cp = b0 & 0x0Fu; }
        else if (b0 >= 0xF0 && b0 <= 0xF4) { len = 4; cp = b0 & 0x07u; }
        else return fail("invalid UTF-8 byte in string");
        if (pos_ + len > text_.size()) return fail("truncated UTF-8 sequence in string");
        for (std::size_t i = 1; i < len; ++i) {
            const auto b = static_cast<unsigned char>(text_[pos_ + i]);
            if ((b & 0xC0) != 0x80) return fail("invalid UTF-8 continuation byte in string");
            cp = (cp << 6) | (b & 0x3Fu);
        }
        if ((len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000))
            return fail("overlong UTF-8 encoding in string");
        if (cp >= 0xD800 && cp <= 0xDFFF) return fail("UTF-8 encoded surrogate in string");
        if (cp > 0x10FFFF) return fail("UTF-8 code point out of range in string");
        s.append(text_.substr(pos_, len));
        pos_ += len;
        return true;
    }

    bool parse_string(std::string& out) {
        ++pos_;  // opening quote
        for (;;) {
            if (at_end()) return fail("unterminated string");
            const char c = peek();
            if (c == '"') { ++pos_; return true; }
            if (static_cast<unsigned char>(c) < 0x20) return fail("unescaped control character in string");
            if (c != '\\') {
                if (!copy_utf8_sequence(out)) return false;
                continue;
            }
            ++pos_;
            if (at_end()) return fail("unterminated escape sequence");
            const char e = peek();
            ++pos_;
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    std::uint32_t cp;
                    if (!parse_hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (text_.substr(pos_, 2) != "\\u") return fail("lone high surrogate in \\u escape");
                        pos_ += 2;
                        std::uint32_t lo;
                        if (!parse_hex4(lo)) return false;
                        if (lo < 0xDC00 || lo > 0xDFFF) return fail("invalid low surrogate in \\u escape");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return fail("lone low surrogate in \\u escape");
                    }
                    append_utf8(out, cp);
                    break;
                }
                default:
                    --pos_;
                    return fail("invalid escape sequence");
            }
        }
    }
};

// --------------------------------------------------------------- writer ---

void write_string(std::string& out, const std::string& s) {
    static constexpr char kHex[] = "0123456789abcdef";
    out.push_back('"');
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out.push_back(kHex[c >> 4]);
                    out.push_back(kHex[c & 0x0F]);
                } else {
                    out.push_back(ch);  // UTF-8 passes through unescaped
                }
        }
    }
    out.push_back('"');
}

bool write_number(std::string& out, double d) {
    if (!std::isfinite(d)) return false;
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof buf, d);  // shortest round-trip form
    out.append(buf, r.ptr);
    return true;
}

bool write_value(std::string& out, const Json& v, int indent, int level) {
    auto newline = [&](int lvl) {
        if (indent < 0) return;
        out.push_back('\n');
        out.append(static_cast<std::size_t>(indent) * static_cast<std::size_t>(lvl), ' ');
    };
    switch (v.type()) {
        case Json::Type::Null: out += "null"; return true;
        case Json::Type::Bool: out += v.as_bool() ? "true" : "false"; return true;
        case Json::Type::Number: return write_number(out, v.as_number());
        case Json::Type::String: write_string(out, v.as_string()); return true;
        case Json::Type::Array: {
            if (v.items().empty()) { out += "[]"; return true; }
            out.push_back('[');
            bool first = true;
            for (const Json& item : v.items()) {
                if (!first) out.push_back(',');
                first = false;
                newline(level + 1);
                if (!write_value(out, item, indent, level + 1)) return false;
            }
            newline(level);
            out.push_back(']');
            return true;
        }
        case Json::Type::Object: {
            if (v.members().empty()) { out += "{}"; return true; }
            out.push_back('{');
            bool first = true;
            for (const auto& m : v.members()) {
                if (!first) out.push_back(',');
                first = false;
                newline(level + 1);
                write_string(out, m.first);
                out.push_back(':');
                if (indent >= 0) out.push_back(' ');
                if (!write_value(out, m.second, indent, level + 1)) return false;
            }
            newline(level);
            out.push_back('}');
            return true;
        }
    }
    return false;
}

}  // namespace

Result<Json> Json::parse(std::string_view text) {
    // Reject a UTF-8 BOM explicitly: RFC 8259 says parsers MAY ignore it but
    // silently accepting it hides encoding mistakes in hand-edited files.
    if (text.size() >= 3 && text.substr(0, 3) == "\xEF\xBB\xBF") {
        return make_error("json.syntax",
                          "JSON syntax error at line 1, column 1: UTF-8 byte-order mark is not allowed");
    }
    return Parser(text).run();
}

Result<std::string> Json::dump(int indent) const {
    std::string out;
    if (!write_value(out, *this, indent, 0)) {
        return make_error("json.non_finite_number", "cannot serialize NaN or Infinity as JSON");
    }
    if (indent >= 0) out.push_back('\n');
    return out;
}

}  // namespace k

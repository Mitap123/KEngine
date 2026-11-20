// K Engine — Engine/Serialization/Json.h
//
// Small, strict JSON (RFC 8259) value, parser and writer. Exists so the
// Engine core has zero third-party dependencies at M0 (see ADR-0006 and
// DEPENDENCIES.md). It is deliberately minimal; replace it behind this
// interface if profiling or feature needs ever justify a library.
//
// Parser policy (all violations are errors, never silently accepted):
//   * No comments, no trailing commas, no NaN/Infinity, no leading zeros.
//   * Strings must be valid UTF-8; control characters must be escaped;
//     \u escapes (incl. surrogate pairs) are decoded to UTF-8; lone
//     surrogates are rejected.
//   * Duplicate object keys are rejected (hash-based, so a hostile object with
//     hundreds of thousands of keys is handled in linear time).
//   * Nesting deeper than kMaxDepth is rejected (stack-exhaustion guard).
//   * Numbers are IEEE doubles. A number that overflows the double range, or
//     is non-zero but underflows to zero (e.g. 1e-999), is rejected rather
//     than silently altered. Denormals (e.g. 5e-324) and a literal zero
//     (0, 0e-999) are accepted. Integers are exact only up to 2^53.
//   * Trailing non-whitespace after the root value is rejected.
//
// Writer policy: objects keep insertion order, so output is deterministic.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Engine/Core/Result.h"

namespace k {

class Json {
public:
    enum class Type : std::uint8_t { Null, Bool, Number, String, Array, Object };

    static constexpr int kMaxDepth = 128;

    Json() = default;  // null
    static Json boolean(bool b);
    static Json number(double d);
    static Json string(std::string s);
    static Json array();
    static Json object();

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_bool() const { return type_ == Type::Bool; }
    bool is_number() const { return type_ == Type::Number; }
    bool is_string() const { return type_ == Type::String; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_object() const { return type_ == Type::Object; }

    // Accessors: preconditions are the matching is_*(); otherwise the
    // returned value is a default (false / 0 / empty) — callers validate first.
    bool as_bool() const { return b_; }
    double as_number() const { return n_; }
    const std::string& as_string() const { return s_; }
    const std::vector<Json>& items() const { return arr_; }
    const std::vector<std::pair<std::string, Json>>& members() const { return obj_; }

    // Integral value of a Number, if it is exactly an integer within int64.
    std::optional<std::int64_t> as_int() const;

    // Array / object mutation. push_back is a no-op unless is_array().
    void push_back(Json v);
    // Sets (or replaces) a member; no-op unless is_object(). O(members): meant
    // for building small objects, not for bulk construction.
    void set(std::string key, Json v);
    // Builds an object from members whose keys the CALLER guarantees are unique
    // (the parser verifies this in O(1) per key). O(1) per member.
    static Json object_from_unique_members(std::vector<std::pair<std::string, Json>> members);
    // nullptr if not an object or key absent.
    const Json* find(std::string_view key) const;

    // Parse failure: code "json.syntax"; message includes line:column.
    static Result<Json> parse(std::string_view text);

    // Fails with code "json.non_finite_number" if any number is NaN/Inf.
    // indent < 0: compact. indent >= 0: pretty-printed with that many spaces.
    Result<std::string> dump(int indent = -1) const;

private:
    Type type_ = Type::Null;
    bool b_ = false;
    double n_ = 0.0;
    std::string s_;
    std::vector<Json> arr_;
    std::vector<std::pair<std::string, Json>> obj_;
};

}  // namespace k

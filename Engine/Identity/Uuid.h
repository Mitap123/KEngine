// K Engine — Engine/Identity/Uuid.h
//
// Persistent identity (ADR-0003). A Uuid survives save/load and is the ONLY
// object identity that is ever serialized. It is unrelated to runtime
// Handles (see Handle.h), which must never be written to disk.
//
// Generated values are RFC 4122 version 4 (random). Canonical text form is
// lowercase 8-4-4-4-12 hex. parse() accepts upper- or lowercase hex but
// requires the canonical dash layout (no braces, no missing dashes).
#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace k {

class Uuid {
public:
    constexpr Uuid() = default;  // the nil UUID (all zero bits)

    // Random v4 UUID from std::random_device. Not deterministic by design.
    static Uuid generate();

    // Builds a v4 UUID from 128 caller-supplied random bits, forcing the
    // version and variant bits. Exists so tests can be deterministic.
    static Uuid from_random_words(std::uint64_t hi, std::uint64_t lo);

    // Returns nullopt for anything that is not a canonical UUID string.
    static std::optional<Uuid> parse(std::string_view text);

    std::string to_string() const;
    bool is_nil() const { return *this == Uuid(); }

    const std::array<std::uint8_t, 16>& bytes() const { return bytes_; }

    friend bool operator==(const Uuid&, const Uuid&) = default;
    friend std::strong_ordering operator<=>(const Uuid&, const Uuid&) = default;

    struct Hash {
        std::size_t operator()(const Uuid& u) const noexcept;
    };

private:
    std::array<std::uint8_t, 16> bytes_{};
};

}  // namespace k

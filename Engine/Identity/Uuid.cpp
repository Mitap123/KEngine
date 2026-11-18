#include "Engine/Identity/Uuid.h"

#include <random>

namespace k {

namespace {

constexpr char kHexDigits[] = "0123456789abcdef";

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

Uuid Uuid::from_random_words(std::uint64_t hi, std::uint64_t lo) {
    Uuid u;
    for (int i = 0; i < 8; ++i) {
        u.bytes_[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(hi >> (56 - 8 * i));
        u.bytes_[static_cast<std::size_t>(8 + i)] = static_cast<std::uint8_t>(lo >> (56 - 8 * i));
    }
    u.bytes_[6] = static_cast<std::uint8_t>((u.bytes_[6] & 0x0F) | 0x40);  // version 4
    u.bytes_[8] = static_cast<std::uint8_t>((u.bytes_[8] & 0x3F) | 0x80);  // variant 10xx
    return u;
}

Uuid Uuid::generate() {
    // One std::random_device per call: slower than a cached engine, but
    // UUID creation is an authoring-time operation, not a hot path, and this
    // avoids shared mutable state (thread-safety without locks).
    std::random_device rd;
    auto word = [&rd] {
        return (static_cast<std::uint64_t>(rd()) << 32) | static_cast<std::uint64_t>(rd());
    };
    const std::uint64_t hi = word();
    const std::uint64_t lo = word();
    return from_random_words(hi, lo);
}

std::optional<Uuid> Uuid::parse(std::string_view text) {
    if (text.size() != 36) return std::nullopt;
    Uuid u;
    std::size_t byte = 0;
    for (std::size_t i = 0; i < text.size();) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (text[i] != '-') return std::nullopt;
            ++i;
            continue;
        }
        const int hi = hex_value(text[i]);
        const int lo = hex_value(text[i + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        // Dashes sit only at offsets 8/13/18/23 (checked above), so a digit
        // pair never straddles a dash.
        u.bytes_[byte++] = static_cast<std::uint8_t>((hi << 4) | lo);
        i += 2;
    }
    return u;
}

std::string Uuid::to_string() const {
    std::string out;
    out.reserve(36);
    for (std::size_t i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out.push_back('-');
        out.push_back(kHexDigits[bytes_[i] >> 4]);
        out.push_back(kHexDigits[bytes_[i] & 0x0F]);
    }
    return out;
}

std::size_t Uuid::Hash::operator()(const Uuid& u) const noexcept {
    // FNV-1a over the 16 bytes. Not cryptographic; only for hash containers.
    std::uint64_t h = 14695981039346656037ull;
    for (std::uint8_t b : u.bytes_) {
        h ^= b;
        h *= 1099511628211ull;
    }
    return static_cast<std::size_t>(h);
}

}  // namespace k

#include <set>
#include <unordered_set>

#include "Engine/Identity/Uuid.h"
#include "Tests/Unit/TestFramework.h"

using k::Uuid;

KTEST(uuid_default_is_nil) {
    Uuid u;
    KCHECK(u.is_nil());
    KCHECK_EQ(u.to_string(), std::string("00000000-0000-0000-0000-000000000000"));
}

KTEST(uuid_from_random_words_sets_version_and_variant) {
    // Known vector computed by hand: byte6 0xcd -> 0x4d, byte8 0xfe -> 0xbe.
    const Uuid u = Uuid::from_random_words(0x0123456789abcdefull, 0xfedcba9876543210ull);
    KCHECK_EQ(u.to_string(), std::string("01234567-89ab-4def-bedc-ba9876543210"));
    KCHECK(!u.is_nil());
    // All-zero and all-one inputs still produce v4/variant-1 UUIDs.
    KCHECK_EQ(Uuid::from_random_words(0, 0).to_string(), std::string("00000000-0000-4000-8000-000000000000"));
    KCHECK_EQ(Uuid::from_random_words(~0ull, ~0ull).to_string(), std::string("ffffffff-ffff-4fff-bfff-ffffffffffff"));
}

KTEST(uuid_generate_is_unique_valid_v4) {
    std::set<std::string> seen;
    for (int i = 0; i < 2000; ++i) {
        const Uuid u = Uuid::generate();
        const std::string s = u.to_string();
        KCHECK(!u.is_nil());
        KCHECK_EQ(s[14], '4');
        KCHECK(s[19] == '8' || s[19] == '9' || s[19] == 'a' || s[19] == 'b');
        seen.insert(s);
        auto back = Uuid::parse(s);
        KCHECK(back.has_value());
        if (back) KCHECK(*back == u);
    }
    KCHECK_EQ(seen.size(), static_cast<std::size_t>(2000));
}

KTEST(uuid_parse_accepts_canonical_either_case) {
    auto lower = Uuid::parse("01234567-89ab-4def-bedc-ba9876543210");
    auto upper = Uuid::parse("01234567-89AB-4DEF-BEDC-BA9876543210");
    KCHECK(lower.has_value());
    KCHECK(upper.has_value());
    if (lower && upper) {
        KCHECK(*lower == *upper);
        KCHECK_EQ(upper->to_string(), std::string("01234567-89ab-4def-bedc-ba9876543210"));  // normalizes to lowercase
    }
}

KTEST(uuid_parse_rejects_malformed_input) {
    const char* bad[] = {
        "",
        "not-a-uuid",
        "01234567-89ab-4def-bedc-ba987654321",     // one digit short
        "01234567-89ab-4def-bedc-ba98765432100",   // one digit long
        "0123456789ab4defbedcba9876543210",        // no dashes
        "{01234567-89ab-4def-bedc-ba9876543210}",  // braces
        "01234567-89ab-4def-bedc-ba9876543210 ",   // trailing space
        " 01234567-89ab-4def-bedc-ba9876543210",   // leading space
        "01234567_89ab_4def_bedc_ba9876543210",    // wrong separator
        "0123456-789ab-4def-bedc-ba9876543210",    // dash in wrong place
        "01234567-89ab-4def-bedc-ba98765432g0",    // non-hex digit
        "01234567-89ab-4def-bedc-ba9876543\xff" "0", // non-ASCII byte
    };
    for (const char* s : bad) {
        KCHECK(!Uuid::parse(s).has_value());
    }
}

KTEST(uuid_ordering_and_hashing) {
    const Uuid a = Uuid::from_random_words(1, 1);
    const Uuid b = Uuid::from_random_words(1, 2);
    KCHECK(a != b);
    KCHECK(a < b);
    KCHECK(a == Uuid::from_random_words(1, 1));
    KCHECK_EQ(Uuid::Hash{}(a), Uuid::Hash{}(Uuid::from_random_words(1, 1)));
    std::unordered_set<Uuid, Uuid::Hash> set{a, b, a};
    KCHECK_EQ(set.size(), static_cast<std::size_t>(2));
}

// Robustness against hostile input: algorithmic-complexity regressions and a
// deterministic mutation fuzzer for the JSON parser and the scene loader.
//
// Set KENGINE_FUZZ_ITERATIONS to run more random mutants (default 20000).
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include "Engine/Scene/Scene.h"
#include "Engine/Serialization/Json.h"
#include "Engine/Serialization/SceneSerializer.h"
#include "Tests/Unit/TestFramework.h"

using namespace k;

namespace {

double seconds(const std::function<void()>& fn) {
    const auto t0 = std::chrono::steady_clock::now();
    fn();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

// Asserts near-linear scaling: quadrupling the input must not cost more than 10x
// (linear ~4x, quadratic ~16x or worse). The floor keeps tiny timings from
// producing noisy ratios.
void check_scaling(const char* what, const std::function<void(std::size_t)>& run, std::size_t n) {
    const double small = seconds([&] { run(n); });
    const double large = seconds([&] { run(4 * n); });
    if (large > 10.0 * std::max(small, 0.02) + 0.05) {
        ktest::fail(__FILE__, __LINE__, std::string(what) + ": 4x input took " + std::to_string(large) + " s vs " +
                                            std::to_string(small) + " s (superlinear)");
    }
    if (large > 20.0) ktest::fail(__FILE__, __LINE__, std::string(what) + ": too slow in absolute terms");
}

std::string uuid_of(std::size_t i) {
    // 8 hex digits cover every index used here (< 2^32); the buffer fits the full 36-char UUID.
    char buf[48];
    std::snprintf(buf, sizeof buf, "%08x-0000-4000-8000-000000000000", static_cast<unsigned>(i + 1));
    return buf;
}

// A scene file whose objects form one parent chain (object i has parent i-1).
std::string chain_scene(std::size_t n, bool close_ring = false) {
    std::string s = R"({"schema":"kengine.scene","schema_version":1,"objects":[)";
    for (std::size_t i = 0; i < n; ++i) {
        if (i) s += ',';
        s += R"({"id":")" + uuid_of(i) + R"(","name":"n","kind":"empty","parent":)";
        if (i > 0) s += "\"" + uuid_of(i - 1) + "\"";
        else s += close_ring ? "\"" + uuid_of(n - 1) + "\"" : "null";
        s += "}";
    }
    return s + "]}";
}

}  // namespace

// ------------------------------------------------------- complexity guards ---

KTEST(robustness_json_object_with_many_keys_parses_in_linear_time) {
    auto run = [](std::size_t n) {
        std::string text = "{";
        for (std::size_t i = 0; i < n; ++i) text += (i ? ",\"k" : "\"k") + std::to_string(i) + "\":1";
        text += "}";
        auto r = Json::parse(text);
        KCHECK(r.ok());
        if (r.ok()) KCHECK_EQ(r.value().members().size(), n);
    };
    check_scaling("json many keys", run, ktest::scaled(3000, 10000));
}

KTEST(robustness_json_duplicate_keys_are_detected_on_both_detection_paths) {
    // Below and above the point where the parser switches from scanning to hashing.
    for (std::size_t n : {1u, 2u, 15u, 16u, 17u, 18u, 100u}) {
        std::string unique = "{", dup_first = "{", dup_last = "{", dup_mid = "{";
        for (std::size_t i = 0; i < n; ++i) {
            const std::string kv = (i ? ",\"k" : "\"k") + std::to_string(i) + "\":0";
            unique += kv; dup_first += kv; dup_last += kv; dup_mid += kv;
        }
        KCHECK(Json::parse(unique + "}").ok());
        KCHECK(!Json::parse(dup_first + ",\"k0\":1}").ok());                               // repeats the first key
        KCHECK(!Json::parse(dup_last + ",\"k" + std::to_string(n - 1) + "\":1}").ok());    // repeats the last key
        KCHECK(!Json::parse(dup_mid + ",\"k" + std::to_string(n / 2) + "\":1}").ok());     // repeats a middle key
    }
    // Keys that differ only slightly are not duplicates.
    KCHECK(Json::parse(R"({"a":1,"A":2,"a ":3," a":4,"a\u0000":5})").ok());
    // Duplicates are found even when the first copy sits inside the "hashed" region
    // and a later non-duplicate insertion happened in between.
    std::string t = "{";
    for (int i = 0; i < 40; ++i) t += (i ? ",\"k" : "\"k") + std::to_string(i) + "\":0";
    KCHECK(!Json::parse(t + ",\"extra\":1,\"k30\":2}").ok());
}

KTEST(robustness_loading_a_deep_parent_chain_is_linear) {
    check_scaling("deep chain load", [](std::size_t n) {
        auto r = load_scene_from_string(chain_scene(n));
        KCHECK(r.ok());
        if (r.ok()) KCHECK_EQ(r.value().scene.object_count(), n);
    }, ktest::scaled(3000, 10000));
}

KTEST(robustness_loading_a_huge_ring_reports_a_cycle_quickly) {
    check_scaling("ring cycle detection", [](std::size_t n) {
        auto r = load_scene_from_string(chain_scene(n, /*close_ring=*/true));
        KCHECK(!r.ok());
        if (!r.ok()) KCHECK_EQ(r.error().code, std::string("scene.cycle"));
    }, ktest::scaled(3000, 10000));
}

// 100,000 objects in full mode (8,000 in the default fast mode).
KTEST(robustness_a_large_object_chain_loads_and_round_trips) {
    const std::size_t n = ktest::scaled(8000, 100000);
    auto r = load_scene_from_string(chain_scene(n));
    KCHECK(r.ok());
    if (!r.ok()) return;
    KCHECK_EQ(r.value().scene.object_count(), n);
    auto text = save_scene_to_string(r.value().scene);     // iterative traversal: no stack overflow
    KCHECK(text.ok());
    if (text.ok()) {
        auto again = load_scene_from_string(text.value());
        KCHECK(again.ok());
        if (again.ok()) KCHECK_EQ(save_scene_to_string(again.value().scene).value(), text.value());
    }
}

// ----------------------------------------------------------- cycle reporting ---

KTEST(robustness_cycle_error_points_at_an_object_on_the_cycle_not_one_hanging_off_it) {
    auto obj = [](std::size_t i, int parent) {
        return R"({"id":")" + uuid_of(i) + R"(","name":"n","kind":"empty","parent":)" +
               (parent < 0 ? std::string("null") : "\"" + uuid_of(static_cast<std::size_t>(parent)) + "\"") + "}";
    };
    auto load = [](const std::string& objects) {
        return load_scene_from_string(R"({"schema":"kengine.scene","schema_version":1,"objects":[)" + objects + "]}");
    };
    // 0 <-> 1 form the cycle; 2 hangs off it; 3 is a healthy root.
    auto r = load(obj(2, 0) + "," + obj(0, 1) + "," + obj(1, 0) + "," + obj(3, -1));
    // order in file: index0 = uuid2 (child of uuid0), index1 = uuid0, index2 = uuid1, index3 = uuid3
    KCHECK(!r.ok());
    if (!r.ok()) {
        KCHECK_EQ(r.error().code, std::string("scene.cycle"));
        KCHECK(r.error().message.find("/objects/0/") == std::string::npos);   // the dangling child is not blamed
        KCHECK(r.error().message.find("/objects/1/parent") != std::string::npos);  // smallest index on the cycle
    }
    auto self = load(obj(0, 0));
    KCHECK(!self.ok());
    if (!self.ok()) KCHECK_EQ(self.error().code, std::string("scene.cycle"));
    KCHECK(load(obj(0, -1) + "," + obj(1, 0) + "," + obj(2, 1)).ok());      // a healthy chain still loads
}

// ------------------------------------------------------------ warning limits ---

KTEST(robustness_unknown_field_warnings_are_capped) {
    std::string top = R"({"schema":"kengine.scene","schema_version":1,"objects":[])";
    for (int i = 0; i < 1000; ++i) top += ",\"x" + std::to_string(i) + "\":1";
    top += "}";
    auto r = load_scene_from_string(top);
    KCHECK(r.ok());
    if (!r.ok()) return;
    const auto& w = r.value().warnings;
    KCHECK_EQ(w.size(), static_cast<std::size_t>(101));                      // 100 kept + one summary line
    KCHECK(w.back().find("900 more warnings suppressed") != std::string::npos);
    KCHECK(w.front().find("\"x0\"") != std::string::npos);                   // the first ones are the ones kept

    // Few warnings: nothing suppressed, no summary line.
    auto few = load_scene_from_string(R"({"schema":"kengine.scene","schema_version":1,"objects":[],"a":1,"b":2})");
    KCHECK(few.ok());
    if (few.ok()) KCHECK_EQ(few.value().warnings.size(), static_cast<std::size_t>(2));
}

// -------------------------------------------------------------------- fuzzer ---

namespace {

struct Rng {
    std::uint64_t s;
    std::uint32_t next(std::uint32_t n) {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<std::uint32_t>((s >> 33) % n);
    }
};

struct FuzzStats {
    std::size_t json_ok = 0, json_rejected = 0, scene_ok = 0, scene_rejected = 0;
};

bool known_code(const std::string& code) { return code.rfind("json.", 0) == 0 || code.rfind("scene.", 0) == 0; }

// One mutant through every public entry point; checks invariants, never crashes.
void exercise(const std::string& text, FuzzStats& st) {
    auto j = Json::parse(text);
    if (j.ok()) {
        ++st.json_ok;
        auto a = j.value().dump(-1);
        KCHECK(a.ok());
        if (a.ok()) {
            auto back = Json::parse(a.value());
            KCHECK(back.ok());
            if (back.ok()) KCHECK_EQ(back.value().dump(-1).value(), a.value());     // dump is a fixed point
        }
    } else {
        ++st.json_rejected;
        KCHECK(known_code(j.error().code) && !j.error().message.empty());
    }

    auto s = load_scene_from_string(text);
    if (s.ok()) {
        ++st.scene_ok;
        KCHECK(s.value().warnings.size() <= 101);
        auto t1 = save_scene_to_string(s.value().scene);
        KCHECK(t1.ok());
        if (!t1.ok()) return;
        auto s2 = load_scene_from_string(t1.value());
        KCHECK(s2.ok());                                                            // what we write we can read
        if (s2.ok()) {
            KCHECK(s2.value().warnings.empty());
            KCHECK_EQ(save_scene_to_string(s2.value().scene).value(), t1.value());  // and it is stable
            KCHECK_EQ(s2.value().scene.object_count(), s.value().scene.object_count());
        }
    } else {
        ++st.scene_rejected;
        if (!(known_code(s.error().code) && !s.error().message.empty())) {
            ktest::fail(__FILE__, __LINE__, "unexpected error code: " + s.error().code);
        }
    }
}

std::vector<std::string> seeds() {
    Scene sc;
    auto root = sc.create(ObjectKind::Empty, "Root \xC3\xA9 \"q\"").value();
    auto cube = sc.create(ObjectKind::Cube, "Cube", root).value();
    sc.create(ObjectKind::Sphere, "Sphere", root).ok();
    sc.create(ObjectKind::Plane, "Plane").ok();
    Transform t;
    t.position = {0.1, -1.0 / 3.0, 1e-300};
    t.rotation_euler_deg = {45, 0, 123456.789e3};
    t.scale = {1, 2.5, 5e-324};
    sc.set_transform(cube, t).ok();
    return {
        save_scene_to_string(sc).value(),
        R"({"schema":"kengine.scene","schema_version":1,"objects":[]})",
        R"({"schema":"kengine.scene","schema_version":1,"extra":{"a":[1,2,{"b":null}]},"objects":[)"
        R"({"id":"11111111-1111-4111-8111-111111111111","name":"n","kind":"cube","parent":null,"x":true,)"
        R"("transform":{"position":[1,2,3],"skew":[0]}},)"
        R"({"id":"22222222-2222-4222-8222-222222222222","name":"c","kind":"empty","parent":"11111111-1111-4111-8111-111111111111"}]})",
        R"({"a":[1,-2.5e3,true,false,null,"\u00e9\ud83d\ude00\n"],"b":{"c":{"d":[]}}})",
    };
}

}  // namespace

KTEST(fuzz_systematic_single_edits_never_crash_or_break_invariants) {
    FuzzStats st;
    const std::size_t stride = ktest::full_mode() ? 1 : 3;
    // Full mode: 16 replacement bytes at every position. Fast mode: the 5 most structurally disruptive.
    std::vector<char> replacements = {'{', '}', '[', ']', ',', ':', '"', '\\', '0', '-', 'e', '.', 'n', '\x00', '\xff', ' '};
    if (!ktest::full_mode()) replacements = {'{', ',', '"', '\\', '\xff'};
    for (const std::string& seed : seeds()) {
        exercise(seed, st);
        // Every prefix (every 3rd in fast mode).
        for (std::size_t len = 0; len < seed.size(); len += stride) exercise(seed.substr(0, len), st);
        // Delete each byte; replace each byte by structurally interesting ones (every 3rd position in fast mode).
        for (std::size_t i = 0; i < seed.size(); i += stride) {
            std::string del = seed;
            del.erase(i, 1);
            exercise(del, st);
            for (char r : replacements) {
                std::string rep = seed;
                rep[i] = r;
                exercise(rep, st);
            }
        }
    }
    // The run must have hit both outcomes on both parsers, or it proves nothing.
    KCHECK(st.json_ok > 100 && st.json_rejected > 1000);
    KCHECK(st.scene_ok > 50 && st.scene_rejected > 1000);
}

KTEST(fuzz_random_multi_edit_mutants_never_crash_or_break_invariants) {
    std::size_t iterations = ktest::scaled(4000, 20000);
    if (const char* env = std::getenv("KENGINE_FUZZ_ITERATIONS")) {
        iterations = static_cast<std::size_t>(std::strtoull(env, nullptr, 10));
    }
    const auto base = seeds();
    Rng rng{0xC0FFEE1234567ull};
    FuzzStats st;
    const char* tokens[] = {"{", "}", "[", "]", ",", ":", "\"", "\\u", "\\ud800", "null", "true", "1e999", "-0", "00",
                            "\"parent\":", "\"id\":\"", "11111111-1111-4111-8111-111111111111", "\\n", "\xC0\x80", "\xED\xA0\x80"};
    for (std::size_t it = 0; it < iterations; ++it) {
        std::string m = base[rng.next(static_cast<std::uint32_t>(base.size()))];
        const std::uint32_t edits = 1 + rng.next(6);
        for (std::uint32_t e = 0; e < edits && !m.empty(); ++e) {
            const std::size_t pos = rng.next(static_cast<std::uint32_t>(m.size()));
            switch (rng.next(6)) {
                case 0: m[pos] = static_cast<char>(rng.next(256)); break;                                   // random byte
                case 1: m.erase(pos, 1 + rng.next(8)); break;                                              // delete a run
                case 2: m.insert(pos, tokens[rng.next(sizeof tokens / sizeof tokens[0])]); break;          // insert a token
                case 3: m.insert(pos, m.substr(rng.next(static_cast<std::uint32_t>(m.size())), 1 + rng.next(40))); break;  // duplicate a run
                case 4: m.resize(pos); break;                                                              // truncate
                default: m[pos] ^= static_cast<char>(1u << rng.next(8)); break;                            // bit flip
            }
        }
        exercise(m, st);
    }
    KCHECK(st.json_ok > 0 && st.json_rejected > 0 && st.scene_rejected > 0);
}

// K Engine CLI — `k`
//
// Implemented commands (and ONLY these; see BUILDING.md / TESTING.md):
//   k --version
//   k --help | -h | help
//   k doctor [--json]
//
// Exit codes: 0 success; 1 a command ran and reported failure (doctor check
// failed); 2 usage error (unknown command/flag). Results go to stdout,
// diagnostics about misuse go to stderr.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Engine/Core/Version.h"
#include "Engine/Identity/Handle.h"
#include "Engine/Identity/Uuid.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Serialization/Json.h"
#include "Engine/Serialization/SceneSerializer.h"

namespace {

constexpr int kExitOk = 0;
constexpr int kExitFailure = 1;
constexpr int kExitUsage = 2;

void print_usage(std::FILE* out) {
    std::fputs(
        "K Engine command-line tool\n"
        "\n"
        "usage:\n"
        "  k --version        print the version\n"
        "  k doctor [--json]  report build/platform facts and run engine self-checks\n"
        "  k --help           show this help\n"
        "\n"
        "exit codes: 0 ok, 1 command failed, 2 usage error\n",
        out);
}

// ------------------------------------------------------------- build facts ---

std::string compiler_string() {
#if defined(__clang__)
    return std::string("clang ") + __clang_version__;
#elif defined(__GNUC__)
    return std::string("gcc ") + __VERSION__;
#elif defined(_MSC_VER)
    return "msvc " + std::to_string(_MSC_VER);
#else
    return "unknown";
#endif
}

const char* os_string() {
#if defined(__linux__)
    return "linux";
#elif defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "unknown";
#endif
}

const char* arch_string() {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#else
    return "unknown";
#endif
}

const char* build_type_string() {
#ifdef NDEBUG
    return "release (NDEBUG defined)";
#else
    return "debug (NDEBUG not defined)";
#endif
}

bool address_sanitizer_enabled() {
#if defined(__SANITIZE_ADDRESS__)
    return true;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
    return true;
#else
    return false;
#endif
#else
    return false;
#endif
}

// ------------------------------------------------------------ self-checks ---

struct Check {
    std::string name;
    bool ok;
    std::string detail;
};

Check check_uuid() {
    const k::Uuid a = k::Uuid::generate();
    const k::Uuid b = k::Uuid::generate();
    const auto text = a.to_string();
    const auto back = k::Uuid::parse(text);
    if (a.is_nil() || a == b) return {"uuid_generate", false, "generated UUIDs are nil or not unique"};
    if (!back || !(*back == a)) return {"uuid_generate", false, "UUID did not survive a text round trip"};
    if (text[14] != '4') return {"uuid_generate", false, "generated UUID is not version 4"};
    return {"uuid_generate", true, "random v4 UUIDs are unique and round-trip through text"};
}

Check check_handles() {
    k::SlotMap<int> m;
    const k::Handle a = m.insert(1);
    m.erase(a);
    const k::Handle b = m.insert(2);
    if (m.get(a) != nullptr) return {"handle_invalidation", false, "stale handle resolved after its slot was reused"};
    if (!m.get(b) || *m.get(b) != 2) return {"handle_invalidation", false, "valid handle did not resolve"};
    if (a.index != b.index || a.generation >= b.generation)
        return {"handle_invalidation", false, "slot reuse did not advance the generation"};
    return {"handle_invalidation", true, "stale handles stay invalid across slot reuse"};
}

Check check_scene_round_trip() {
    k::Scene s;
    auto root = s.create(k::ObjectKind::Empty, "Root");
    auto cube = s.create(k::ObjectKind::Cube, "Cube", root.value());
    k::Transform t;
    t.position = {1.5, -2.0, 0.1};
    if (auto st = s.set_transform(cube.value(), t); !st) return {"scene_round_trip", false, st.error().message};
    const k::Uuid cube_id = s.get(cube.value())->id;

    auto text = k::save_scene_to_string(s);
    if (!text) return {"scene_round_trip", false, text.error().message};
    auto loaded = k::load_scene_from_string(text.value());
    if (!loaded) return {"scene_round_trip", false, loaded.error().message};

    const k::Scene& r = loaded.value().scene;
    const k::Handle h = r.find_by_uuid(cube_id);
    if (r.object_count() != 2 || h.is_null() || !(r.get(h)->transform == t))
        return {"scene_round_trip", false, "loaded scene differs from the saved scene"};
    return {"scene_round_trip", true, "scene survives save/load with UUIDs, hierarchy and transforms"};
}

std::vector<Check> run_checks() { return {check_uuid(), check_handles(), check_scene_round_trip()}; }

// ---------------------------------------------------------------- doctor ---

int run_doctor(bool json) {
    const std::vector<Check> checks = run_checks();
    bool all_ok = true;
    for (const Check& c : checks) all_ok = all_ok && c.ok;

    if (json) {
        k::Json root = k::Json::object();
        root.set("version", k::Json::string(k::kEngineVersion));
        k::Json build = k::Json::object();
        build.set("compiler", k::Json::string(compiler_string()));
        build.set("cplusplus", k::Json::number(static_cast<double>(__cplusplus)));
        build.set("build_type", k::Json::string(build_type_string()));
        build.set("address_sanitizer", k::Json::boolean(address_sanitizer_enabled()));
        root.set("build", std::move(build));
        k::Json platform = k::Json::object();
        platform.set("os", k::Json::string(os_string()));
        platform.set("arch", k::Json::string(arch_string()));
        platform.set("pointer_bits", k::Json::number(static_cast<double>(sizeof(void*) * 8)));
        root.set("platform", std::move(platform));
        k::Json schema = k::Json::object();
        schema.set("scene_schema", k::Json::string(k::kSceneSchemaName));
        schema.set("scene_schema_version", k::Json::number(k::kSceneSchemaVersion));
        root.set("formats", std::move(schema));
        k::Json arr = k::Json::array();
        for (const Check& c : checks) {
            k::Json j = k::Json::object();
            j.set("name", k::Json::string(c.name));
            j.set("ok", k::Json::boolean(c.ok));
            j.set("detail", k::Json::string(c.detail));
            arr.push_back(std::move(j));
        }
        root.set("checks", std::move(arr));
        root.set("ok", k::Json::boolean(all_ok));
        auto text = root.dump(2);
        if (!text) {  // cannot happen with the finite values above; fail loudly anyway
            std::fprintf(stderr, "k: internal error: %s\n", text.error().message.c_str());
            return kExitFailure;
        }
        std::fputs(text.value().c_str(), stdout);
    } else {
        std::printf("K Engine doctor\n");
        std::printf("  version      %s\n", k::kEngineVersion);
        std::printf("  compiler     %s\n", compiler_string().c_str());
        std::printf("  c++ standard %ld\n", static_cast<long>(__cplusplus));
        std::printf("  build type   %s\n", build_type_string());
        std::printf("  asan         %s\n", address_sanitizer_enabled() ? "yes" : "no");
        std::printf("  platform     %s/%s, %zu-bit\n", os_string(), arch_string(), sizeof(void*) * 8);
        std::printf("  scene format %s v%d\n", k::kSceneSchemaName, k::kSceneSchemaVersion);
        std::printf("self-checks:\n");
        for (const Check& c : checks) {
            std::printf("  [%s] %-20s %s\n", c.ok ? " OK " : "FAIL", c.name.c_str(), c.detail.c_str());
        }
        std::printf("result: %s\n", all_ok ? "all checks passed" : "SOME CHECKS FAILED");
    }
    return all_ok ? kExitOk : kExitFailure;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(stderr);
        return kExitUsage;
    }
    const std::string cmd = argv[1];

    if (cmd == "--version") {
        if (argc != 2) { std::fputs("k: --version takes no arguments\n", stderr); return kExitUsage; }
        std::printf("k %s\n", k::kEngineVersion);
        return kExitOk;
    }
    if (cmd == "--help" || cmd == "-h" || cmd == "help") {
        print_usage(stdout);
        return kExitOk;
    }
    if (cmd == "doctor") {
        bool json = false;
        for (int i = 2; i < argc; ++i) {
            if (std::strcmp(argv[i], "--json") == 0) {
                json = true;
            } else {
                std::fprintf(stderr, "k: doctor: unknown option '%s'\n", argv[i]);
                return kExitUsage;
            }
        }
        return run_doctor(json);
    }

    std::fprintf(stderr, "k: unknown command '%s' (try 'k --help')\n", cmd.c_str());
    return kExitUsage;
}

// K Engine CLI — `k`
//
// Implemented commands (and ONLY these; see BUILDING.md / TESTING.md):
//   k --version
//   k --help | -h | help
//   k doctor [--json]
//   k scene validate <file> [--json]
//   k scene dump <file> [--json]
//   (doctor and scene also accept --log-file <path>: JSON Lines log, see write_log below)
//
// Exit codes: 0 success; 1 a command ran and reported failure (doctor check
// failed, scene file invalid); 2 usage error (unknown command/flag). Results go
// to stdout, error and warning diagnostics go to stderr (with --json, scene
// errors are reported as JSON on stdout instead).
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <unordered_map>
#include <vector>

#include "Engine/Core/Version.h"
#include "Engine/Identity/Handle.h"
#include "Engine/Identity/Uuid.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Serialization/Json.h"
#include "Engine/Serialization/SceneFile.h"
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
        "  k scene validate <file> [--json]  check a scene file; exit 1 if it is invalid\n"
        "  k scene dump <file> [--json]      print the scene tree (or canonical JSON)\n"
        "  --log-file <path>  (doctor, scene) append a JSON Lines log of this run to <path>\n"
        "  k --help           show this help\n"
        "\n"
        "exit codes: 0 ok, 1 command failed, 2 usage error\n",
        out);
}

// -------------------------------------------------------------------- log ---
//
// `--log-file <path>` appends one compact JSON object per line (JSON Lines) per event:
//   {"ts":"2026-11-23T12:00:00Z","level":"info|warn|error","event":"start|check|warning|error|result|finish",...}
// Every line is flushed as it is written, so a crash still leaves the earlier lines. The file is
// opened (append) before the command runs; if that fails the command does not run (exit 1).
// If a write fails later (disk full) the run exits 1 even if the command succeeded.
// Usage errors (exit 2) are logged too (start, finish with exit_code 2), since the log is opened
// first. Strings are sanitised to valid UTF-8 (bad bytes become U+FFFD): file names and quoted file
// content are untrusted and must not make a log line invalid JSON.

std::string utf8_sanitized(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    std::size_t i = 0;
    while (i < in.size()) {
        const auto b = static_cast<unsigned char>(in[i]);
        std::size_t len = 0;
        std::uint32_t cp = 0;
        if (b < 0x80) { len = 1; cp = b; }
        else if (b >= 0xC2 && b <= 0xDF) { len = 2; cp = b & 0x1Fu; }
        else if (b >= 0xE0 && b <= 0xEF) { len = 3; cp = b & 0x0Fu; }
        else if (b >= 0xF0 && b <= 0xF4) { len = 4; cp = b & 0x07u; }
        bool ok = len != 0 && i + len <= in.size();
        for (std::size_t j = 1; ok && j < len; ++j) {
            const auto c = static_cast<unsigned char>(in[i + j]);
            if ((c & 0xC0u) != 0x80u) ok = false;
            else cp = (cp << 6) | (c & 0x3Fu);
        }
        if (ok && ((len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000) || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))) ok = false;
        if (ok) {
            out.append(in, i, len);
            i += len;
        } else {
            out += "\xEF\xBF\xBD";  // U+FFFD, one per bad byte
            ++i;
        }
    }
    return out;
}

k::Json jstr(const std::string& s) { return k::Json::string(utf8_sanitized(s)); }

std::string utc_timestamp() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

class Log {
public:
    Log() = default;
    Log(const Log&) = delete;
    Log& operator=(const Log&) = delete;
    ~Log() {
        if (f_) std::fclose(f_);
    }
    bool open(const std::string& path) {
        f_ = std::fopen(path.c_str(), "ab");
        return f_ != nullptr;
    }
    bool failed() const { return failed_; }

    // No-op unless a log file is open. `fields` must be an object; its members follow ts/level/event.
    void write(const char* level, const char* event, const k::Json& fields = k::Json::object()) {
        if (!f_) return;
        k::Json line = k::Json::object();
        line.set("ts", k::Json::string(utc_timestamp()));
        line.set("level", k::Json::string(level));
        line.set("event", k::Json::string(event));
        for (const auto& member : fields.members()) line.set(member.first, member.second);
        const auto text = line.dump();  // compact: control characters are escaped, so this is one line
        if (!text) {
            failed_ = true;
            return;
        }
        const std::string& t = text.value();
        if (std::fwrite(t.data(), 1, t.size(), f_) != t.size() || std::fputc('\n', f_) == EOF || std::fflush(f_) != 0)
            failed_ = true;
    }

private:
    std::FILE* f_ = nullptr;
    bool failed_ = false;
};

Log g_log;

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
    const k::ObjectHandle h = r.find_by_uuid(cube_id);
    if (r.object_count() != 2 || h.is_null() || !(r.get(h)->transform == t))
        return {"scene_round_trip", false, "loaded scene differs from the saved scene"};
    return {"scene_round_trip", true, "scene survives save/load with UUIDs, hierarchy and transforms"};
}

std::vector<Check> run_checks() { return {check_uuid(), check_handles(), check_scene_round_trip()}; }

// ---------------------------------------------------------------- doctor ---

int run_doctor(bool json) {
    const std::vector<Check> checks = run_checks();
    bool all_ok = true;
    for (const Check& c : checks) {
        all_ok = all_ok && c.ok;
        k::Json f = k::Json::object();
        f.set("name", jstr(c.name));
        f.set("ok", k::Json::boolean(c.ok));
        f.set("detail", jstr(c.detail));
        g_log.write(c.ok ? "info" : "error", "check", f);
    }

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


// ----------------------------------------------------------------- scene ---

// Scene files may legally contain control characters in names (and error
// messages quote file content). Printing them raw would let a file forge
// output lines or inject terminal escape sequences, so text output escapes them.
std::string printable(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (const char ch : in) {
        const auto c = static_cast<unsigned char>(ch);
        if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20 || c == 0x7F) {
            static const char kHex[] = "0123456789abcdef";
            out += "\\x";
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0F]);
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

std::string number_text(double d) {
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof buf, d);  // shortest round-trip form
    return std::string(buf, r.ptr);
}

std::string vec_text(const k::Vec3& v) {
    return "(" + number_text(v[0]) + ", " + number_text(v[1]) + ", " + number_text(v[2]) + ")";
}

int report_scene_error(const k::Error& e, bool json) {
    {
        k::Json f = k::Json::object();
        f.set("code", jstr(e.code));
        f.set("message", jstr(e.message));
        g_log.write("error", "error", f);
    }
    if (json) {
        k::Json err = k::Json::object();
        err.set("code", k::Json::string(e.code));
        err.set("message", k::Json::string(e.message));
        k::Json root = k::Json::object();
        root.set("ok", k::Json::boolean(false));
        root.set("error", std::move(err));
        auto text = root.dump(2);
        std::fputs(text ? text.value().c_str() : "{\"ok\":false}\n", stdout);
    } else {
        std::fprintf(stderr, "error[%s]: %s\n", e.code.c_str(), printable(e.message).c_str());
    }
    return kExitFailure;
}

int run_scene(int argc, char** argv) {
    if (argc < 3) {
        std::fputs("k: scene: missing subcommand (validate | dump)\n", stderr);
        return kExitUsage;
    }
    const std::string sub = argv[2];
    if (sub != "validate" && sub != "dump") {
        std::fprintf(stderr, "k: scene: unknown subcommand '%s' (validate | dump)\n", sub.c_str());
        return kExitUsage;
    }
    std::string file;
    bool json = false;
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "--json") == 0) {
            json = true;
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            std::fprintf(stderr, "k: scene %s: unknown option '%s'\n", sub.c_str(), argv[i]);
            return kExitUsage;
        } else if (file.empty()) {
            file = argv[i];
        } else {
            std::fprintf(stderr, "k: scene %s: unexpected extra argument '%s'\n", sub.c_str(), argv[i]);
            return kExitUsage;
        }
    }
    if (file.empty()) {
        std::fprintf(stderr, "k: scene %s: missing <file>\n", sub.c_str());
        return kExitUsage;
    }

    auto loaded = k::load_scene_file(file);
    if (!loaded) return report_scene_error(loaded.error(), json);
    const k::Scene& scene = loaded.value().scene;
    const auto& warnings = loaded.value().warnings;
    for (const auto& msg : warnings) {
        k::Json f = k::Json::object();
        f.set("message", jstr(msg));
        g_log.write("warn", "warning", f);
    }
    {
        k::Json f = k::Json::object();
        f.set("objects", k::Json::number(static_cast<double>(scene.object_count())));
        f.set("warnings", k::Json::number(static_cast<double>(warnings.size())));
        g_log.write("info", "result", f);
    }

    if (sub == "validate") {
        if (json) {
            k::Json root = k::Json::object();
            root.set("ok", k::Json::boolean(true));
            root.set("objects", k::Json::number(static_cast<double>(scene.object_count())));
            k::Json w = k::Json::array();
            for (const auto& msg : warnings) w.push_back(k::Json::string(msg));
            root.set("warnings", std::move(w));
            std::fputs(root.dump(2).value().c_str(), stdout);
        } else {
            for (const auto& msg : warnings) std::fprintf(stderr, "warning: %s\n", printable(msg).c_str());
            std::printf("OK: %s: %zu objects, %zu warnings\n", printable(file).c_str(), scene.object_count(), warnings.size());
        }
        return kExitOk;
    }

    // dump
    for (const auto& msg : warnings) {
        if (!json) std::fprintf(stderr, "warning: %s\n", printable(msg).c_str());
    }
    if (json) {
        // Canonical form: exactly what a save would write (not the input bytes).
        auto text = k::save_scene_to_string(scene);
        if (!text) return report_scene_error(text.error(), true);
        std::fputs(text.value().c_str(), stdout);
        return kExitOk;
    }
    std::printf("scene: %zu objects (%s v%d)\n", scene.object_count(), k::kSceneSchemaName, k::kSceneSchemaVersion);
    std::unordered_map<std::uint32_t, std::size_t> depth;  // handle slot -> depth; preorder visits parents first
    scene.visit_preorder([&](k::ObjectHandle h, const k::SceneObject& o) {
        const std::size_t d = o.parent.is_null() ? 0 : depth.at(o.parent.index) + 1;
        depth.emplace(h.index, d);
        std::printf("%s%s [%s] %s  pos=%s rot=%s scale=%s\n", std::string(d * 2, ' ').c_str(), printable(o.name).c_str(),
                    k::to_string(o.kind), o.id.to_string().c_str(), vec_text(o.transform.position).c_str(),
                    vec_text(o.transform.rotation_euler_deg).c_str(), vec_text(o.transform.scale).c_str());
    });
    return kExitOk;
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
    if (cmd == "doctor" || cmd == "scene") {
        // Take --log-file <path> out of the arguments; the command parsers never see it.
        std::vector<char*> args{argv[0], argv[1]};
        std::string log_path;
        bool have_log = false;
        for (int i = 2; i < argc; ++i) {
            if (std::strcmp(argv[i], "--log-file") != 0) {
                args.push_back(argv[i]);
                continue;
            }
            if (have_log) { std::fputs("k: --log-file given twice\n", stderr); return kExitUsage; }
            if (i + 1 >= argc || argv[i + 1][0] == '\0') { std::fputs("k: --log-file needs a <path>\n", stderr); return kExitUsage; }
            log_path = argv[++i];
            have_log = true;
        }
        if (have_log && !g_log.open(log_path)) {
            std::fprintf(stderr, "k: cannot open log file '%s': %s\n", printable(log_path).c_str(), std::strerror(errno));
            return kExitFailure;
        }

        {
            k::Json f = k::Json::object();
            f.set("version", k::Json::string(k::kEngineVersion));
            k::Json a = k::Json::array();
            for (std::size_t i = 1; i < args.size(); ++i) a.push_back(jstr(args[i]));  // command words and arguments
            f.set("args", std::move(a));
            g_log.write("info", "start", f);
        }
        int code = kExitOk;
        if (cmd == "doctor") {
            bool json = false;
            for (std::size_t i = 2; i < args.size() && code == kExitOk; ++i) {
                if (std::strcmp(args[i], "--json") == 0) {
                    json = true;
                } else {
                    std::fprintf(stderr, "k: doctor: unknown option '%s'\n", args[i]);
                    code = kExitUsage;
                }
            }
            if (code == kExitOk) code = run_doctor(json);
        } else {
            code = run_scene(static_cast<int>(args.size()), args.data());
        }
        {
            k::Json f = k::Json::object();
            f.set("ok", k::Json::boolean(code == kExitOk));
            f.set("exit_code", k::Json::number(code));
            g_log.write(code == kExitOk ? "info" : "error", "finish", f);
        }
        if (g_log.failed()) {
            std::fputs("k: writing the log file failed; the log is incomplete\n", stderr);
            return code == kExitOk ? kExitFailure : code;
        }
        return code;
    }

    std::fprintf(stderr, "k: unknown command '%s' (try 'k --help')\n", cmd.c_str());
    return kExitUsage;
}

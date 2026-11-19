#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "Engine/Scene/Scene.h"
#include "Engine/Serialization/SceneFile.h"
#include "Tests/Unit/TestFramework.h"

using namespace k;
namespace fs = std::filesystem;

namespace {

// Unique scratch directory, removed on scope exit.
class TempDir {
public:
    TempDir() : path_(fs::temp_directory_path() / ("kengine_test_" + Uuid::generate().to_string())) {
        fs::create_directories(path_);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path_, ec); }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    const fs::path& path() const { return path_; }
    fs::path operator/(const std::string& name) const { return path_ / name; }

    std::vector<std::string> listing() const {
        std::vector<std::string> names;
        for (const auto& e : fs::directory_iterator(path_)) names.push_back(e.path().filename().string());
        std::sort(names.begin(), names.end());
        return names;
    }

private:
    fs::path path_;
};

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void spit(const fs::path& p, const std::string& data) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << data;
}

Scene sample_scene(const char* tag = "A") {
    Scene s;
    auto root = s.create(ObjectKind::Empty, std::string("Root") + tag);
    auto cube = s.create(ObjectKind::Cube, "Cube", root.value());
    Transform t;
    t.position = {1.5, -2.25, 0.1};
    t.scale = {2, 2, 2};
    (void)s.set_transform(cube.value(), t);
    return s;
}

std::vector<std::string> uuids_of(const Scene& s) {
    std::vector<std::string> v;
    s.visit_preorder([&](ObjectHandle, const SceneObject& o) { v.push_back(o.id.to_string() + "|" + o.name); });
    return v;
}

std::string code_of(const Status& st) { return st.ok() ? std::string() : st.error().code; }
template <class T>
std::string code_of(const Result<T>& r) { return r.ok() ? std::string() : r.error().code; }

}  // namespace

KTEST(scene_file_round_trip_preserves_state_and_leaves_no_extra_files) {
    TempDir dir;
    const Scene s = sample_scene();
    KCHECK(save_scene_file(s, dir / "a.kscene").ok());
    KCHECK(dir.listing() == std::vector<std::string>{"a.kscene"});  // no temp, no backup on first save

    auto loaded = load_scene_file(dir / "a.kscene");
    KCHECK(loaded.ok());
    if (!loaded.ok()) return;
    KCHECK(loaded.value().warnings.empty());
    KCHECK(uuids_of(loaded.value().scene) == uuids_of(s));

    // The file holds exactly the in-memory serialization.
    KCHECK_EQ(slurp(dir / "a.kscene"), save_scene_to_string(s).value());
}

KTEST(scene_file_second_save_keeps_previous_version_as_backup) {
    TempDir dir;
    const Scene a = sample_scene("A");
    const Scene b = sample_scene("B");
    KCHECK(save_scene_file(a, dir / "s.kscene").ok());
    KCHECK(save_scene_file(b, dir / "s.kscene").ok());
    KCHECK(dir.listing() == (std::vector<std::string>{"s.kscene", "s.kscene.bak"}));

    auto main_file = load_scene_file(dir / "s.kscene");
    auto bak_file = load_scene_file(dir / "s.kscene.bak");
    KCHECK(main_file.ok() && bak_file.ok());
    if (main_file.ok()) KCHECK(uuids_of(main_file.value().scene) == uuids_of(b));
    if (bak_file.ok()) KCHECK(uuids_of(bak_file.value().scene) == uuids_of(a));
}

KTEST(scene_file_keep_backup_false_creates_no_backup) {
    TempDir dir;
    SaveOptions opt;
    opt.keep_backup = false;
    KCHECK(save_scene_file(sample_scene("A"), dir / "s.kscene", opt).ok());
    KCHECK(save_scene_file(sample_scene("B"), dir / "s.kscene", opt).ok());
    KCHECK(dir.listing() == std::vector<std::string>{"s.kscene"});
}

// For every stage of the save sequence: the save must fail with the mapped
// code, the existing file must be byte-identical, and no temp file may remain.
KTEST(scene_file_failure_at_any_stage_leaves_existing_file_untouched) {
    struct Case { const char* stage; const char* code; };
    const Case cases[] = {
        {"create_temp", "file.temp_create_failed"},
        {"write", "file.write_failed"},
        {"sync", "file.sync_failed"},
        {"backup", "file.backup_failed"},
        {"rename", "file.rename_failed"},
    };
    for (const Case& c : cases) {
        TempDir dir;
        const fs::path target = dir / "s.kscene";
        KCHECK(save_scene_file(sample_scene("OLD"), target).ok());
        const std::string before = slurp(target);

        SaveOptions opt;
        opt.fault_injection = [&](std::string_view stage) { return stage == c.stage; };
        const Status st = save_scene_file(sample_scene("NEW"), target, opt);
        if (st.ok() || st.error().code != c.code) {
            ktest::fail(__FILE__, __LINE__, std::string("stage ") + c.stage + ": expected " + c.code + ", got " +
                                                (st.ok() ? "success" : st.error().code));
        }
        KCHECK_EQ(slurp(target), before);                       // original intact
        for (const std::string& name : dir.listing()) {
            KCHECK(name.find(".tmp") == std::string::npos);     // temp file cleaned up
        }
        KCHECK(load_scene_file(target).ok());                   // and still loadable

        // After a failure the very next save (without faults) works normally.
        KCHECK(save_scene_file(sample_scene("NEW"), target).ok());
    }
}

KTEST(scene_file_failed_first_save_creates_nothing) {
    for (const char* stage : {"create_temp", "write", "sync", "rename"}) {
        TempDir dir;
        SaveOptions opt;
        opt.fault_injection = [&](std::string_view s) { return s == stage; };
        KCHECK(!save_scene_file(sample_scene(), dir / "new.kscene", opt).ok());
        KCHECK(dir.listing().empty());
    }
}

KTEST(scene_file_save_into_missing_directory_fails_cleanly) {
    TempDir dir;
    const Status st = save_scene_file(sample_scene(), dir / "no_such_dir" / "s.kscene");
    KCHECK_EQ(code_of(st), std::string("file.temp_create_failed"));
    KCHECK(dir.listing().empty());  // does not create directories implicitly
}

KTEST(scene_file_save_rejects_bad_targets) {
    TempDir dir;
    KCHECK_EQ(code_of(save_scene_file(sample_scene(), fs::path())), std::string("file.invalid_path"));
    KCHECK_EQ(code_of(save_scene_file(sample_scene(), dir.path() / "")), std::string("file.invalid_path"));

    fs::create_directory(dir / "adir");
    KCHECK_EQ(code_of(save_scene_file(sample_scene(), dir / "adir")), std::string("file.not_regular"));

    // A symlink must not be silently replaced by a regular file. Creating a
    // symlink can be impossible (for example on Windows without the privilege);
    // that part is then reported as skipped rather than failing the suite.
    KCHECK(save_scene_file(sample_scene("REAL"), dir / "real.kscene").ok());
    const std::string real_before = slurp(dir / "real.kscene");
    std::error_code link_ec;
    fs::create_symlink(dir / "real.kscene", dir / "link.kscene", link_ec);
    if (link_ec) {
        std::printf("    note: symlink creation unavailable (%s); symlink checks SKIPPED\n", link_ec.message().c_str());
    } else {
        KCHECK_EQ(code_of(save_scene_file(sample_scene("X"), dir / "link.kscene")), std::string("file.symlink_unsupported"));
        KCHECK(fs::is_symlink(dir / "link.kscene"));
        KCHECK_EQ(slurp(dir / "real.kscene"), real_before);
        // ...but loading through a symlink is fine.
        KCHECK(load_scene_file(dir / "link.kscene").ok());
    }

    for (const std::string& name : dir.listing()) KCHECK(name.find(".tmp") == std::string::npos);
}

KTEST(scene_file_load_reports_missing_and_non_regular_files) {
    TempDir dir;
    auto missing = load_scene_file(dir / "nope.kscene");
    KCHECK_EQ(code_of(missing), std::string("file.not_found"));
    if (!missing.ok()) KCHECK(missing.error().message.find("nope.kscene") != std::string::npos);

    fs::create_directory(dir / "adir");
    KCHECK_EQ(code_of(load_scene_file(dir / "adir")), std::string("file.not_regular"));
    KCHECK_EQ(code_of(load_scene_file(fs::path())), std::string("file.invalid_path"));
}

KTEST(scene_file_load_rejects_empty_truncated_and_garbage_files) {
    TempDir dir;
    const std::string good = save_scene_to_string(sample_scene()).value();

    spit(dir / "empty.kscene", "");
    KCHECK_EQ(code_of(load_scene_file(dir / "empty.kscene")), std::string("json.syntax"));

    // Every proper prefix that cuts into the document (i.e. removes the final
    // '}' or more) must be rejected, never crash. Cutting only the trailing
    // newline leaves a valid document, which is correct JSON behaviour.
    KCHECK_EQ(good.back(), '\n');
    KCHECK_EQ(good[good.size() - 2], '}');
    std::size_t rejected = 0;
    for (std::size_t len = 1; len + 2 <= good.size(); ++len) {
        spit(dir / "cut.kscene", good.substr(0, len));
        auto r = load_scene_file(dir / "cut.kscene");
        if (r.ok()) {
            ktest::fail(__FILE__, __LINE__, "accepted a truncated file, length " + std::to_string(len));
        } else {
            ++rejected;
            if (r.error().message.find("cut.kscene") == std::string::npos) {
                ktest::fail(__FILE__, __LINE__, "error does not name the file, length " + std::to_string(len));
            }
        }
    }
    KCHECK_EQ(rejected, good.size() - 2);
    spit(dir / "nl.kscene", good.substr(0, good.size() - 1));  // only the trailing newline removed
    KCHECK(load_scene_file(dir / "nl.kscene").ok());

    spit(dir / "bin.kscene", std::string("\x00\x01\x02\xff\xfe garbage", 13));
    KCHECK_EQ(code_of(load_scene_file(dir / "bin.kscene")), std::string("json.syntax"));

    spit(dir / "bom.kscene", "\xEF\xBB\xBF" + good);
    KCHECK_EQ(code_of(load_scene_file(dir / "bom.kscene")), std::string("json.syntax"));
}

KTEST(scene_file_load_passes_content_errors_through_with_path) {
    TempDir dir;
    spit(dir / "newer.kscene", R"({"schema":"kengine.scene","schema_version":9,"objects":[]})");
    auto newer = load_scene_file(dir / "newer.kscene");
    KCHECK_EQ(code_of(newer), std::string("scene.schema.unsupported_version"));
    if (!newer.ok()) KCHECK(newer.error().message.find("newer.kscene") != std::string::npos);

    spit(dir / "dangling.kscene",
         R"({"schema":"kengine.scene","schema_version":1,"objects":[{"id":"11111111-1111-4111-8111-111111111111",)"
         R"("name":"n","kind":"cube","parent":"22222222-2222-4222-8222-222222222222"}]})");
    auto dangling = load_scene_file(dir / "dangling.kscene");
    KCHECK_EQ(code_of(dangling), std::string("scene.object.missing_parent"));
    if (!dangling.ok()) KCHECK(dangling.error().message.find("/objects/0/parent") != std::string::npos);
}

KTEST(scene_file_load_enforces_size_limit_exactly) {
    TempDir dir;
    KCHECK(save_scene_file(sample_scene(), dir / "s.kscene").ok());
    const std::size_t size = static_cast<std::size_t>(fs::file_size(dir / "s.kscene"));
    KCHECK(load_scene_file(dir / "s.kscene", size).ok());                              // exactly at the limit
    KCHECK_EQ(code_of(load_scene_file(dir / "s.kscene", size - 1)), std::string("file.too_large"));
    KCHECK_EQ(code_of(load_scene_file(dir / "s.kscene", 0)), std::string("file.too_large"));

    // A file larger than the read buffer (64 KiB) is still capped correctly.
    spit(dir / "big.kscene", std::string(200 * 1024, ' '));
    KCHECK_EQ(code_of(load_scene_file(dir / "big.kscene", 100 * 1024)), std::string("file.too_large"));
}

KTEST(scene_file_load_refuses_devices_instead_of_reading_them) {
    // /dev/zero is an endless source; a FIFO/device must be rejected before any read.
    if (!fs::exists("/dev/zero")) return;  // not applicable on this platform (counted as pass, not a claim)
    KCHECK_EQ(code_of(load_scene_file("/dev/zero")), std::string("file.not_regular"));
}

KTEST(scene_file_large_scene_round_trips) {
    TempDir dir;
    Scene s;
    ObjectHandle parent = kNullObjectHandle;
    for (int i = 0; i < 5000; ++i) {
        auto h = s.create(i % 3 == 0 ? ObjectKind::Cube : ObjectKind::Sphere, "obj" + std::to_string(i),
                          i % 10 == 0 ? kNullObjectHandle : parent);
        parent = h.value();
    }
    KCHECK(save_scene_file(s, dir / "big.kscene").ok());
    auto loaded = load_scene_file(dir / "big.kscene");
    KCHECK(loaded.ok());
    if (loaded.ok()) KCHECK(uuids_of(loaded.value().scene) == uuids_of(s));
}

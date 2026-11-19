#include <string>
#include <tuple>
#include <vector>

#include "Engine/Scene/Scene.h"
#include "Engine/Serialization/SceneSerializer.h"
#include "Tests/Unit/TestFramework.h"

using namespace k;

namespace {

// ObjectHandle-independent snapshot of a scene, for before/after comparison.
struct Snap {
    std::string id, name, kind, parent_id;
    Transform t;
    bool operator==(const Snap&) const = default;
};

std::vector<Snap> snapshot(const Scene& s) {
    std::vector<Snap> out;
    s.visit_preorder([&](ObjectHandle, const SceneObject& o) {
        const SceneObject* p = s.get(o.parent);
        out.push_back({o.id.to_string(), o.name, to_string(o.kind), p ? p->id.to_string() : "", o.transform});
    });
    return out;
}

std::string error_code(const Result<LoadedScene>& r) { return r.ok() ? std::string() : r.error().code; }
std::string error_msg(const Result<LoadedScene>& r) { return r.ok() ? std::string() : r.error().message; }

// A scene file with one object, for mutation by string replacement.
const char* kUuidA = "11111111-1111-4111-8111-111111111111";
const char* kUuidB = "22222222-2222-4222-8222-222222222222";

std::string scene_text(const std::string& objects, const std::string& header =
                                                       R"("schema":"kengine.scene","schema_version":1)") {
    return "{" + header + ",\"objects\":[" + objects + "]}";
}
std::string object_text(const std::string& id, const std::string& extra = "") {
    return "{\"id\":\"" + id + "\",\"name\":\"n\",\"kind\":\"cube\"" + extra + "}";
}

}  // namespace

KTEST(scene_serialization_round_trip_preserves_uuids_hierarchy_and_transforms) {
    Scene s;
    auto root = s.create(ObjectKind::Empty, "Root");
    auto cube = s.create(ObjectKind::Cube, "Cube \"quoted\" \xC3\xA9", root.value());
    auto sphere = s.create(ObjectKind::Sphere, "Sphere", root.value());
    auto nested = s.create(ObjectKind::Plane, "Plane", cube.value());
    auto lone = s.create(ObjectKind::Cube, "Lone");
    Transform t;
    t.position = {0.1, -1.0 / 3.0, 1e-300};
    t.rotation_euler_deg = {45.5, -90, 123456.789e3};
    t.scale = {1.0, 2.5, 0.001};
    KCHECK(s.set_transform(cube.value(), t).ok());
    KCHECK(s.set_transform(lone.value(), t).ok());
    (void)sphere; (void)nested;

    auto text = save_scene_to_string(s);
    KCHECK(text.ok());
    if (!text.ok()) return;
    auto loaded = load_scene_from_string(text.value());
    KCHECK(loaded.ok());
    if (!loaded.ok()) { ktest::fail(__FILE__, __LINE__, error_msg(loaded)); return; }

    const Scene& r = loaded.value().scene;
    KCHECK(loaded.value().warnings.empty());
    KCHECK_EQ(r.object_count(), s.object_count());
    KCHECK(snapshot(r) == snapshot(s));                   // UUIDs, names, kinds, parents, transforms, order
    KCHECK_EQ(r.roots().size(), s.roots().size());

    // UUIDs resolve to live objects with the same data in the loaded scene.
    ObjectHandle h = r.find_by_uuid(s.get(cube.value())->id);
    KCHECK(!h.is_null());
    KCHECK(r.get(h) != nullptr && r.get(h)->transform == t);

    // Saving the loaded scene reproduces the same bytes (stable output).
    auto again = save_scene_to_string(r);
    KCHECK(again.ok());
    if (again.ok()) KCHECK_EQ(again.value(), text.value());
}

KTEST(scene_serialization_empty_scene_round_trips) {
    Scene s;
    auto text = save_scene_to_string(s);
    KCHECK(text.ok());
    if (!text.ok()) return;
    auto loaded = load_scene_from_string(text.value());
    KCHECK(loaded.ok());
    if (loaded.ok()) KCHECK_EQ(loaded.value().scene.object_count(), static_cast<std::size_t>(0));
}

KTEST(scene_serialization_writes_only_persistent_data) {
    Scene s;
    auto h = s.create(ObjectKind::Cube, "c");
    auto json = scene_to_json(s);
    KCHECK(json.find("schema") && json.find("schema")->as_string() == kSceneSchemaName);
    KCHECK(json.find("schema_version") && json.find("schema_version")->as_int() == kSceneSchemaVersion);
    KCHECK_EQ(json.members().size(), static_cast<std::size_t>(3));  // schema, schema_version, objects
    const Json& obj = json.find("objects")->items()[0];
    KCHECK_EQ(obj.members().size(), static_cast<std::size_t>(5));   // id,name,kind,parent,transform — no handle/index/generation
    KCHECK(obj.find("generation") == nullptr && obj.find("index") == nullptr && obj.find("handle") == nullptr);
    KCHECK(obj.find("id")->as_string() == s.get(h.value())->id.to_string());
}

KTEST(scene_serialization_rejects_malformed_json_with_actionable_error) {
    auto r = load_scene_from_string("{\"schema\": ");
    KCHECK_EQ(error_code(r), std::string("json.syntax"));
    KCHECK(error_msg(r).find("line 1") != std::string::npos);
    KCHECK_EQ(error_code(load_scene_from_string("")), std::string("json.syntax"));
}

KTEST(scene_serialization_validates_schema_and_version) {
    KCHECK_EQ(error_code(load_scene_from_string("[]")), std::string("scene.schema.invalid_root"));
    KCHECK_EQ(error_code(load_scene_from_string(R"({"schema_version":1,"objects":[]})")), std::string("scene.schema.missing_field"));
    KCHECK_EQ(error_code(load_scene_from_string(R"({"schema":"kengine.scene","objects":[]})")), std::string("scene.schema.missing_field"));
    KCHECK_EQ(error_code(load_scene_from_string(R"({"schema":"kengine.scene","schema_version":1})")), std::string("scene.schema.missing_field"));
    KCHECK_EQ(error_code(load_scene_from_string(R"({"schema":"other.thing","schema_version":1,"objects":[]})")), std::string("scene.schema.unknown_schema"));
    KCHECK_EQ(error_code(load_scene_from_string(R"({"schema":7,"schema_version":1,"objects":[]})")), std::string("scene.schema.wrong_type"));
    KCHECK_EQ(error_code(load_scene_from_string(R"({"schema":"kengine.scene","schema_version":"1","objects":[]})")), std::string("scene.schema.wrong_type"));
    KCHECK_EQ(error_code(load_scene_from_string(R"({"schema":"kengine.scene","schema_version":1.5,"objects":[]})")), std::string("scene.schema.wrong_type"));
    KCHECK_EQ(error_code(load_scene_from_string(R"({"schema":"kengine.scene","schema_version":1,"objects":{}})")), std::string("scene.schema.wrong_type"));

    // Newer file than this build: refused with an upgrade hint, not guessed at.
    auto newer = load_scene_from_string(R"({"schema":"kengine.scene","schema_version":2,"objects":[]})");
    KCHECK_EQ(error_code(newer), std::string("scene.schema.unsupported_version"));
    KCHECK(error_msg(newer).find("newer") != std::string::npos);
    KCHECK_EQ(error_code(load_scene_from_string(R"({"schema":"kengine.scene","schema_version":0,"objects":[]})")), std::string("scene.schema.unsupported_version"));
    KCHECK_EQ(error_code(load_scene_from_string(R"({"schema":"kengine.scene","schema_version":-1,"objects":[]})")), std::string("scene.schema.unsupported_version"));
}

KTEST(scene_serialization_rejects_bad_objects) {
    auto load_obj = [](const std::string& obj) { return load_scene_from_string(scene_text(obj)); };

    KCHECK_EQ(error_code(load_obj("5")), std::string("scene.schema.wrong_type"));
    KCHECK_EQ(error_code(load_obj(R"({"name":"n","kind":"cube"})")), std::string("scene.schema.missing_field"));
    KCHECK_EQ(error_code(load_obj(R"({"id":"11111111-1111-4111-8111-111111111111","kind":"cube"})")), std::string("scene.schema.missing_field"));
    KCHECK_EQ(error_code(load_obj(R"({"id":"11111111-1111-4111-8111-111111111111","name":"n"})")), std::string("scene.schema.missing_field"));
    KCHECK_EQ(error_code(load_obj(R"({"id":5,"name":"n","kind":"cube"})")), std::string("scene.schema.wrong_type"));
    KCHECK_EQ(error_code(load_obj(R"({"id":"11111111-1111-4111-8111-111111111111","name":5,"kind":"cube"})")), std::string("scene.schema.wrong_type"));
    KCHECK_EQ(error_code(load_obj(R"({"id":"nope","name":"n","kind":"cube"})")), std::string("scene.object.invalid_uuid"));
    KCHECK_EQ(error_code(load_obj(R"({"id":"11111111-1111-4111-8111-111111111111","name":"n","kind":"cylinder"})")), std::string("scene.object.unknown_kind"));
    KCHECK_EQ(error_code(load_obj(R"({"id":"00000000-0000-0000-0000-000000000000","name":"n","kind":"cube"})")), std::string("scene.nil_uuid"));
    KCHECK_EQ(error_code(load_obj(object_text(kUuidA) + "," + object_text(kUuidA))), std::string("scene.duplicate_uuid"));

    // The message names the failing location so a user can fix the file.
    auto r = load_obj(object_text(kUuidA) + "," + R"({"id":"bad","name":"n","kind":"cube"})");
    KCHECK(error_msg(r).find("/objects/1/id") != std::string::npos);
}

KTEST(scene_serialization_rejects_bad_transforms) {
    auto with_t = [](const std::string& t) {
        return load_scene_from_string(scene_text(object_text(kUuidA, ",\"transform\":" + t)));
    };
    KCHECK(with_t(R"({"position":[1,2,3]})").ok());
    KCHECK(with_t(R"({})").ok());
    KCHECK_EQ(error_code(with_t("5")), std::string("scene.schema.wrong_type"));
    KCHECK_EQ(error_code(with_t(R"({"position":[1,2]})")), std::string("scene.transform.invalid"));
    KCHECK_EQ(error_code(with_t(R"({"position":[1,2,3,4]})")), std::string("scene.transform.invalid"));
    KCHECK_EQ(error_code(with_t(R"({"scale":[1,"x",3]})")), std::string("scene.transform.invalid"));
    KCHECK_EQ(error_code(with_t(R"({"rotation_euler_deg":"90"})")), std::string("scene.transform.invalid"));
    // Non-finite values cannot be written in JSON, but overflow must not slip in either.
    KCHECK_EQ(error_code(with_t(R"({"position":[1e999,0,0]})")), std::string("json.syntax"));
}

KTEST(scene_serialization_reports_missing_parent_cycles_and_self_parent) {
    auto parent_of = [](const char* id, const char* parent) {
        return object_text(id, std::string(",\"parent\":\"") + parent + "\"");
    };
    // Dangling reference.
    auto missing = load_scene_from_string(scene_text(parent_of(kUuidA, kUuidB)));
    KCHECK_EQ(error_code(missing), std::string("scene.object.missing_parent"));
    KCHECK(error_msg(missing).find(kUuidB) != std::string::npos);
    KCHECK(error_msg(missing).find("/objects/0/parent") != std::string::npos);

    // Parent value that is not a UUID / wrong type.
    KCHECK_EQ(error_code(load_scene_from_string(scene_text(object_text(kUuidA, R"(,"parent":"x")")))), std::string("scene.object.invalid_uuid"));
    KCHECK_EQ(error_code(load_scene_from_string(scene_text(object_text(kUuidA, R"(,"parent":5)")))), std::string("scene.schema.wrong_type"));

    // Two-object cycle and self-parent.
    KCHECK_EQ(error_code(load_scene_from_string(scene_text(parent_of(kUuidA, kUuidB) + "," + parent_of(kUuidB, kUuidA)))), std::string("scene.cycle"));
    KCHECK_EQ(error_code(load_scene_from_string(scene_text(parent_of(kUuidA, kUuidA)))), std::string("scene.cycle"));
}

KTEST(scene_serialization_accepts_any_object_order_and_keeps_sibling_order) {
    // Child listed BEFORE its parent; siblings C1, C2 listed in that order.
    const char* P = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
    const char* C1 = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
    const char* C2 = "cccccccc-cccc-4ccc-8ccc-cccccccccccc";
    auto child = [&](const char* id, const char* name) {
        return std::string("{\"id\":\"") + id + "\",\"name\":\"" + name + "\",\"kind\":\"cube\",\"parent\":\"" + P + "\"}";
    };
    const std::string text = scene_text(child(C1, "c1") + "," + child(C2, "c2") + "," +
                                        std::string("{\"id\":\"") + P + "\",\"name\":\"p\",\"kind\":\"empty\",\"parent\":null}");
    auto r = load_scene_from_string(text);
    KCHECK(r.ok());
    if (!r.ok()) return;
    const Scene& s = r.value().scene;
    KCHECK_EQ(s.roots().size(), static_cast<std::size_t>(1));
    const SceneObject* p = s.get(s.roots()[0]);
    KCHECK(p && p->name == "p" && p->children.size() == 2);
    if (p && p->children.size() == 2) {
        KCHECK_EQ(s.get(p->children[0])->name, std::string("c1"));
        KCHECK_EQ(s.get(p->children[1])->name, std::string("c2"));
    }
}

KTEST(scene_serialization_warns_about_unknown_fields_and_does_not_preserve_them) {
    const std::string text =
        R"({"schema":"kengine.scene","schema_version":1,"extra_top":1,"objects":[)"
        R"({"id":"11111111-1111-4111-8111-111111111111","name":"n","kind":"cube","postion":[1,2,3],)"
        R"("transform":{"position":[1,2,3],"skew":[0,0,0]}}]})";
    auto r = load_scene_from_string(text);
    KCHECK(r.ok());
    if (!r.ok()) return;
    KCHECK_EQ(r.value().warnings.size(), static_cast<std::size_t>(3));
    bool saw_typo = false;
    for (const auto& w : r.value().warnings) {
        if (w.find("postion") != std::string::npos && w.find("/objects/0") != std::string::npos) saw_typo = true;
    }
    KCHECK(saw_typo);
    auto saved = save_scene_to_string(r.value().scene);
    KCHECK(saved.ok());
    if (saved.ok()) KCHECK(saved.value().find("postion") == std::string::npos);  // documented: not preserved
}

KTEST(scene_serialization_defaults_for_optional_fields) {
    auto r = load_scene_from_string(scene_text(object_text(kUuidA)));  // no parent, no transform
    KCHECK(r.ok());
    if (!r.ok()) return;
    const Scene& s = r.value().scene;
    KCHECK_EQ(s.object_count(), static_cast<std::size_t>(1));
    KCHECK(s.get(s.roots()[0])->transform == Transform{});
}

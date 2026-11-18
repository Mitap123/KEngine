#include "Engine/Serialization/SceneSerializer.h"

#include <initializer_list>
#include <string>

namespace k {

namespace {

Json vec3_to_json(const Vec3& v) {
    Json a = Json::array();
    for (double d : v) a.push_back(Json::number(d));
    return a;
}

Error wrong_type(const std::string& path, const char* expected) {
    return make_error("scene.schema.wrong_type", path + ": expected " + expected);
}

Error missing_field(const std::string& path) {
    return make_error("scene.schema.missing_field", path + ": required field is missing");
}

// Records every member of `obj` that is not in `known` as a warning.
void warn_unknown(const Json& obj, const std::string& path,
                  std::initializer_list<const char*> known, std::vector<std::string>& warnings) {
    for (const auto& m : obj.members()) {
        bool is_known = false;
        for (const char* k : known) {
            if (m.first == k) { is_known = true; break; }
        }
        if (!is_known) {
            warnings.push_back(path + ": unknown field \"" + m.first + "\" ignored (not preserved on save)");
        }
    }
}

Result<Vec3> parse_vec3(const Json& j, const std::string& path) {
    if (!j.is_array() || j.items().size() != 3) {
        return make_error("scene.transform.invalid", path + ": expected an array of exactly 3 numbers");
    }
    Vec3 v{};
    for (std::size_t i = 0; i < 3; ++i) {
        if (!j.items()[i].is_number()) {
            return make_error("scene.transform.invalid",
                              path + "/" + std::to_string(i) + ": expected a number");
        }
        v[i] = j.items()[i].as_number();
    }
    return v;
}

struct Record {
    Uuid id;
    std::string name;
    ObjectKind kind = ObjectKind::Empty;
    std::optional<Uuid> parent;
    Transform transform;
    std::string path;
};

Result<Record> parse_record(const Json& j, const std::string& path, std::vector<std::string>& warnings) {
    if (!j.is_object()) return wrong_type(path, "an object");
    warn_unknown(j, path, {"id", "name", "kind", "parent", "transform"}, warnings);

    Record r;
    r.path = path;

    const Json* id = j.find("id");
    if (!id) return missing_field(path + "/id");
    if (!id->is_string()) return wrong_type(path + "/id", "a string");
    auto uuid = Uuid::parse(id->as_string());
    if (!uuid) {
        return make_error("scene.object.invalid_uuid", path + "/id: \"" + id->as_string() + "\" is not a valid UUID");
    }
    r.id = *uuid;

    const Json* name = j.find("name");
    if (!name) return missing_field(path + "/name");
    if (!name->is_string()) return wrong_type(path + "/name", "a string");
    r.name = name->as_string();

    const Json* kind = j.find("kind");
    if (!kind) return missing_field(path + "/kind");
    if (!kind->is_string()) return wrong_type(path + "/kind", "a string");
    if (!parse_object_kind(kind->as_string(), r.kind)) {
        return make_error("scene.object.unknown_kind",
                          path + "/kind: unknown kind \"" + kind->as_string() + "\"");
    }

    if (const Json* parent = j.find("parent"); parent && !parent->is_null()) {
        if (!parent->is_string()) return wrong_type(path + "/parent", "a string or null");
        auto puuid = Uuid::parse(parent->as_string());
        if (!puuid) {
            return make_error("scene.object.invalid_uuid",
                              path + "/parent: \"" + parent->as_string() + "\" is not a valid UUID");
        }
        r.parent = *puuid;
    }

    if (const Json* t = j.find("transform")) {
        if (!t->is_object()) return wrong_type(path + "/transform", "an object");
        warn_unknown(*t, path + "/transform", {"position", "rotation_euler_deg", "scale"}, warnings);
        struct Field { const char* key; Vec3* dst; };
        for (const Field& f : {Field{"position", &r.transform.position},
                               Field{"rotation_euler_deg", &r.transform.rotation_euler_deg},
                               Field{"scale", &r.transform.scale}}) {
            if (const Json* v = t->find(f.key)) {
                auto parsed = parse_vec3(*v, path + "/transform/" + f.key);
                if (!parsed) return parsed.error();
                *f.dst = parsed.value();
            }
        }
    }
    return r;
}

}  // namespace

Json scene_to_json(const Scene& scene) {
    Json root = Json::object();
    root.set("schema", Json::string(kSceneSchemaName));
    root.set("schema_version", Json::number(kSceneSchemaVersion));

    Json objects = Json::array();
    scene.visit_preorder([&](Handle, const SceneObject& o) {
        Json j = Json::object();
        j.set("id", Json::string(o.id.to_string()));
        j.set("name", Json::string(o.name));
        j.set("kind", Json::string(to_string(o.kind)));
        const SceneObject* parent = scene.get(o.parent);
        j.set("parent", parent ? Json::string(parent->id.to_string()) : Json());
        Json t = Json::object();
        t.set("position", vec3_to_json(o.transform.position));
        t.set("rotation_euler_deg", vec3_to_json(o.transform.rotation_euler_deg));
        t.set("scale", vec3_to_json(o.transform.scale));
        j.set("transform", std::move(t));
        objects.push_back(std::move(j));
    });
    root.set("objects", std::move(objects));
    return root;
}

Result<std::string> save_scene_to_string(const Scene& scene) {
    return scene_to_json(scene).dump(2);
}

Result<LoadedScene> scene_from_json(const Json& root) {
    if (!root.is_object()) {
        return make_error("scene.schema.invalid_root", "/: scene file root must be a JSON object");
    }

    const Json* schema = root.find("schema");
    if (!schema) return missing_field("/schema");
    if (!schema->is_string()) return wrong_type("/schema", "a string");
    if (schema->as_string() != kSceneSchemaName) {
        return make_error("scene.schema.unknown_schema",
                          "/schema: \"" + schema->as_string() + "\" is not a K Engine scene (expected \"" +
                              kSceneSchemaName + "\")");
    }

    const Json* version = root.find("schema_version");
    if (!version) return missing_field("/schema_version");
    const auto v = version->as_int();
    if (!v) return wrong_type("/schema_version", "an integer");
    if (*v > kSceneSchemaVersion) {
        return make_error("scene.schema.unsupported_version",
                          "/schema_version: file version " + std::to_string(*v) +
                              " is newer than this build supports (" + std::to_string(kSceneSchemaVersion) +
                              "); upgrade K Engine to open it");
    }
    if (*v < 1) {
        return make_error("scene.schema.unsupported_version",
                          "/schema_version: " + std::to_string(*v) + " is not a valid version (minimum 1)");
    }
    // v == kSceneSchemaVersion. Older versions would be routed through a
    // migration registry here once a second version exists (ADR-0006).

    LoadedScene out;
    warn_unknown(root, "/", {"schema", "schema_version", "objects"}, out.warnings);

    const Json* objects = root.find("objects");
    if (!objects) return missing_field("/objects");
    if (!objects->is_array()) return wrong_type("/objects", "an array");

    std::vector<Record> records;
    records.reserve(objects->items().size());
    for (std::size_t i = 0; i < objects->items().size(); ++i) {
        auto rec = parse_record(objects->items()[i], "/objects/" + std::to_string(i), out.warnings);
        if (!rec) return rec.error();
        records.push_back(std::move(rec).value());
    }

    // Pass 1: create every object (as a root) so parents may appear anywhere.
    std::vector<Handle> handles;
    handles.reserve(records.size());
    for (const Record& r : records) {
        auto h = out.scene.create_with_uuid(r.id, r.kind, r.name);
        if (!h) {
            return make_error(h.error().code, r.path + "/id: " + h.error().message);
        }
        handles.push_back(h.value());
        if (auto st = out.scene.set_transform(h.value(), r.transform); !st) {
            return make_error(st.error().code, r.path + "/transform: " + st.error().message);
        }
    }

    // Pass 2: link parents in file order, which preserves sibling order.
    for (std::size_t i = 0; i < records.size(); ++i) {
        if (!records[i].parent) continue;
        const Handle parent = out.scene.find_by_uuid(*records[i].parent);
        if (parent.is_null()) {
            return make_error("scene.object.missing_parent",
                              records[i].path + "/parent: no object has UUID " + records[i].parent->to_string());
        }
        if (auto st = out.scene.reparent(handles[i], parent); !st) {
            return make_error(st.error().code, records[i].path + "/parent: " + st.error().message);
        }
    }
    return out;
}

Result<LoadedScene> load_scene_from_string(std::string_view text) {
    auto json = Json::parse(text);
    if (!json) return json.error();
    return scene_from_json(json.value());
}

}  // namespace k

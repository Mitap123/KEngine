#include "Engine/Scene/Scene.h"

#include <algorithm>
#include <cmath>

namespace k {

const char* to_string(ObjectKind kind) {
    switch (kind) {
        case ObjectKind::Empty: return "empty";
        case ObjectKind::Cube: return "cube";
        case ObjectKind::Sphere: return "sphere";
        case ObjectKind::Plane: return "plane";
    }
    return "unknown";
}

bool parse_object_kind(std::string_view text, ObjectKind& out) {
    for (ObjectKind k : {ObjectKind::Empty, ObjectKind::Cube, ObjectKind::Sphere, ObjectKind::Plane}) {
        if (text == to_string(k)) { out = k; return true; }
    }
    return false;
}

bool is_finite(const Transform& t) {
    for (const Vec3* v : {&t.position, &t.rotation_euler_deg, &t.scale}) {
        for (double d : *v) {
            if (!std::isfinite(d)) return false;
        }
    }
    return true;
}

namespace {
Error invalid_handle(Handle h, HandleStatus s) {
    return make_error("scene.invalid_handle",
                      std::string("handle {index=") + std::to_string(h.index) +
                          ", generation=" + std::to_string(h.generation) + "} is " + to_string(s));
}
}  // namespace

Result<Handle> Scene::create(ObjectKind kind, std::string name, Handle parent) {
    // Regenerate on the (astronomically unlikely) collision rather than fail.
    Uuid id = Uuid::generate();
    while (by_uuid_.count(id) != 0) id = Uuid::generate();
    return create_with_uuid(id, kind, std::move(name), parent);
}

Result<Handle> Scene::create_with_uuid(const Uuid& id, ObjectKind kind, std::string name,
                                       Handle parent) {
    if (id.is_nil()) {
        return make_error("scene.nil_uuid", "an object cannot have the nil UUID");
    }
    if (by_uuid_.count(id) != 0) {
        return make_error("scene.duplicate_uuid", "UUID " + id.to_string() + " is already used in this scene");
    }
    if (!parent.is_null()) {
        const HandleStatus st = objects_.status(parent);
        if (st != HandleStatus::Valid) return invalid_handle(parent, st);
    }

    SceneObject obj;
    obj.id = id;
    obj.name = std::move(name);
    obj.kind = kind;
    obj.parent = parent;
    const Handle h = objects_.insert(std::move(obj));  // may reallocate: fetch parent AFTER this
    by_uuid_.emplace(id, h);
    if (parent.is_null()) {
        roots_.push_back(h);
    } else {
        objects_.get(parent)->children.push_back(h);
    }
    return h;
}

void Scene::detach_from_parent(Handle h, const SceneObject& o) {
    std::vector<Handle>* siblings = &roots_;
    if (!o.parent.is_null()) {
        SceneObject* p = objects_.get(o.parent);
        siblings = &p->children;
    }
    siblings->erase(std::remove(siblings->begin(), siblings->end(), h), siblings->end());
}

Status Scene::destroy(Handle h) {
    const HandleStatus st = objects_.status(h);
    if (st != HandleStatus::Valid) return invalid_handle(h, st);

    detach_from_parent(h, *objects_.get(h));

    // Collect the subtree first (iteratively), then erase, so no pointer into
    // the slot storage is used while it is being modified.
    std::vector<Handle> doomed{h};
    for (std::size_t i = 0; i < doomed.size(); ++i) {
        const SceneObject* o = objects_.get(doomed[i]);
        doomed.insert(doomed.end(), o->children.begin(), o->children.end());
    }
    for (Handle d : doomed) {
        by_uuid_.erase(objects_.get(d)->id);
        objects_.erase(d);
    }
    return Status::success();
}

Status Scene::reparent(Handle h, Handle new_parent) {
    if (const HandleStatus st = objects_.status(h); st != HandleStatus::Valid) return invalid_handle(h, st);
    if (!new_parent.is_null()) {
        if (const HandleStatus st = objects_.status(new_parent); st != HandleStatus::Valid) {
            return invalid_handle(new_parent, st);
        }
        // Reject h itself or any descendant of h as the new parent.
        for (Handle cur = new_parent; !cur.is_null(); cur = objects_.get(cur)->parent) {
            if (cur == h) {
                return make_error("scene.cycle", "cannot make an object a descendant of itself");
            }
        }
    }
    SceneObject* o = objects_.get(h);
    detach_from_parent(h, *o);
    o->parent = new_parent;
    if (new_parent.is_null()) {
        roots_.push_back(h);
    } else {
        objects_.get(new_parent)->children.push_back(h);
    }
    return Status::success();
}

Status Scene::set_name(Handle h, std::string name) {
    SceneObject* o = objects_.get(h);
    if (!o) return invalid_handle(h, objects_.status(h));
    o->name = std::move(name);
    return Status::success();
}

Status Scene::set_transform(Handle h, const Transform& t) {
    SceneObject* o = objects_.get(h);
    if (!o) return invalid_handle(h, objects_.status(h));
    if (!is_finite(t)) {
        return make_error("scene.non_finite_transform", "transform contains NaN or Infinity");
    }
    o->transform = t;
    return Status::success();
}

Handle Scene::find_by_uuid(const Uuid& id) const {
    auto it = by_uuid_.find(id);
    return it == by_uuid_.end() ? kNullHandle : it->second;
}

}  // namespace k

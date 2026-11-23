#include "Engine/Editing/SceneCommands.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include "Engine/Scene/SceneTransforms.h"

namespace k {

namespace {

Error not_found(const Uuid& id) {
    return make_error("command.object_not_found", "no object with UUID " + id.to_string() + " in the scene");
}

// nil -> null handle (root); otherwise the live handle or nullopt if absent.
std::optional<ObjectHandle> resolve_parent(const Scene& scene, const Uuid& parent) {
    if (parent.is_nil()) return kNullObjectHandle;
    const ObjectHandle h = scene.find_by_uuid(parent);
    if (h.is_null()) return std::nullopt;
    return h;
}

// ------------------------------------------------------------------ create ---

class CreateCommand final : public Command {
public:
    CreateCommand(const Uuid& id, ObjectKind kind, std::string name, const Uuid& parent)
        : id_(id), kind_(kind), name_(std::move(name)), parent_(parent) {}

    std::string name() const override { return "Create '" + name_ + "'"; }

    Status apply(Scene& scene) override {
        const auto parent = resolve_parent(scene, parent_);
        if (!parent) return not_found(parent_);
        auto created = scene.create_with_uuid(id_, kind_, name_, *parent);
        if (!created) return created.error();
        return Status::success();
    }

    Status revert(Scene& scene) override {
        const ObjectHandle h = scene.find_by_uuid(id_);
        if (h.is_null()) return not_found(id_);
        return scene.destroy(h);
    }

private:
    Uuid id_;
    ObjectKind kind_;
    std::string name_;
    Uuid parent_;
};

// ------------------------------------------------------------------ delete ---

class DeleteCommand final : public Command {
public:
    explicit DeleteCommand(const Uuid& id) : id_(id) {}

    std::string name() const override { return "Delete '" + label_ + "'"; }

    Status apply(Scene& scene) override {
        const ObjectHandle h = scene.find_by_uuid(id_);
        if (h.is_null()) return not_found(id_);

        // Snapshot the subtree in preorder BEFORE destroying anything.
        std::vector<Node> nodes;
        std::vector<ObjectHandle> stack{h};
        while (!stack.empty()) {
            const ObjectHandle cur = stack.back();
            stack.pop_back();
            const SceneObject* o = scene.get(cur);
            const SceneObject* parent = scene.get(o->parent);
            nodes.push_back({o->id, o->name, o->kind, o->transform, parent ? parent->id : Uuid()});
            stack.insert(stack.end(), o->children.rbegin(), o->children.rend());
        }
        const std::size_t index = scene.sibling_index(h).value_or(Scene::kAppend);

        if (Status st = scene.destroy(h); !st) return st;
        nodes_ = std::move(nodes);
        top_index_ = index;
        label_ = nodes_.front().name;
        return Status::success();
    }

    Status revert(Scene& scene) override {
        std::vector<Uuid> created;  // for rollback if the scene was modified behind the history's back
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            const Node& n = nodes_[i];
            const auto parent = resolve_parent(scene, n.parent);
            Status failure;
            if (!parent) {
                failure = not_found(n.parent);
            } else {
                auto h = scene.create_with_uuid(n.id, n.kind, n.name, *parent, i == 0 ? top_index_ : Scene::kAppend);
                if (!h) {
                    failure = h.error();
                } else if (Status st = scene.set_transform(h.value(), n.transform); !st) {
                    failure = st;
                } else {
                    created.push_back(n.id);
                }
            }
            if (!failure) {
                if (!created.empty()) {  // roll back whatever was restored so far
                    const ObjectHandle first = scene.find_by_uuid(created.front());
                    if (!first.is_null()) (void)scene.destroy(first);
                }
                return failure;
            }
        }
        return Status::success();
    }

private:
    struct Node {
        Uuid id;
        std::string name;
        ObjectKind kind;
        Transform transform;
        Uuid parent;  // nil for the subtree root's own parent when it was a root object
    };

    Uuid id_;
    std::vector<Node> nodes_;  // preorder; nodes_[0] is the deleted object
    std::size_t top_index_ = Scene::kAppend;
    std::string label_ = "object";
};

// ----------------------------------------------------------- set transform ---

class SetTransformCommand final : public Command {
public:
    SetTransformCommand(const Uuid& id, const Transform& t) : id_(id), new_(t) {}

    std::string name() const override { return "Transform '" + label_ + "'"; }

    Status apply(Scene& scene) override {
        const ObjectHandle h = scene.find_by_uuid(id_);
        if (h.is_null()) return not_found(id_);
        const SceneObject* o = scene.get(h);
        const Transform before = o->transform;
        const std::string label = o->name;
        if (Status st = scene.set_transform(h, new_); !st) return st;
        if (!captured_) {
            old_ = before;
            captured_ = true;
        }
        label_ = label;
        return Status::success();
    }

    Status revert(Scene& scene) override {
        const ObjectHandle h = scene.find_by_uuid(id_);
        if (h.is_null()) return not_found(id_);
        return scene.set_transform(h, old_);
    }

    bool try_merge(const Command& later) override {
        const auto* other = dynamic_cast<const SetTransformCommand*>(&later);
        if (!other || !(other->id_ == id_)) return false;
        new_ = other->new_;  // keep our `old_`: one undo returns to the state before the whole run
        return true;
    }

private:
    Uuid id_;
    Transform old_;
    Transform new_;
    bool captured_ = false;
    std::string label_ = "object";
};

// ------------------------------------------------------------------ rename ---

class RenameCommand final : public Command {
public:
    RenameCommand(const Uuid& id, std::string name) : id_(id), new_(std::move(name)) {}

    std::string name() const override { return "Rename '" + old_ + "'"; }

    Status apply(Scene& scene) override {
        const ObjectHandle h = scene.find_by_uuid(id_);
        if (h.is_null()) return not_found(id_);
        const std::string before = scene.get(h)->name;
        if (Status st = scene.set_name(h, new_); !st) return st;
        if (!captured_) {
            old_ = before;
            captured_ = true;
        }
        return Status::success();
    }

    Status revert(Scene& scene) override {
        const ObjectHandle h = scene.find_by_uuid(id_);
        if (h.is_null()) return not_found(id_);
        return scene.set_name(h, old_);
    }

private:
    Uuid id_;
    std::string new_;
    std::string old_ = "object";
    bool captured_ = false;
};

// ---------------------------------------------------------------- reparent ---

class ReparentCommand final : public Command {
public:
    ReparentCommand(const Uuid& id, const Uuid& new_parent, std::size_t index, ReparentMode mode)
        : id_(id), new_parent_(new_parent), index_(index), mode_(mode) {}

    std::string name() const override { return "Reparent '" + label_ + "'"; }

    Status apply(Scene& scene) override {
        const ObjectHandle h = scene.find_by_uuid(id_);
        if (h.is_null()) return not_found(id_);
        const auto parent = resolve_parent(scene, new_parent_);
        if (!parent) return not_found(new_parent_);

        const SceneObject* o = scene.get(h);
        const SceneObject* old_parent = scene.get(o->parent);
        const Uuid old_parent_id = old_parent ? old_parent->id : Uuid();
        const ObjectHandle old_parent_handle = o->parent;
        const std::size_t old_index = scene.sibling_index(h).value_or(Scene::kAppend);
        const std::string label = o->name;
        const Transform old_transform = o->transform;
        // The world matrix BEFORE the move; the moved subtree's worlds only follow from it.
        const std::optional<Mat4> world = mode_ == ReparentMode::KeepWorld ? world_matrix(scene, h) : std::nullopt;

        if (Status st = scene.reparent(h, *parent, index_); !st) return st;

        bool transform_changed = false;
        if (world) {
            Status st = keep_world(scene, h, *parent, *world, transform_changed);
            if (!st) {
                // Put the object back exactly where it was; the scene must be unchanged on failure.
                if (Status back = scene.reparent(h, old_parent_handle, old_index); !back)
                    return make_error("command.rollback_failed", "could not restore the object after a failed reparent: " + back.error().message);
                return st;
            }
        }
        if (!captured_) {
            old_parent_ = old_parent_id;
            old_index_ = old_index;
            old_transform_ = old_transform;
            captured_ = true;
        }
        transform_changed_ = transform_changed;
        label_ = label;
        return Status::success();
    }

    Status revert(Scene& scene) override {
        const ObjectHandle h = scene.find_by_uuid(id_);
        if (h.is_null()) return not_found(id_);
        const auto parent = resolve_parent(scene, old_parent_);
        if (!parent) return not_found(old_parent_);
        if (Status st = scene.reparent(h, *parent, old_index_); !st) return st;
        if (transform_changed_) return scene.set_transform(h, old_transform_);
        return Status::success();
    }

private:
    // After the move: choose the local transform that keeps `world`. Leaves the transform
    // untouched (exact bytes) when the current one already gives that world matrix.
    static Status keep_world(Scene& scene, ObjectHandle h, ObjectHandle parent, const Mat4& world, bool& changed) {
        Mat4 parent_world = Mat4::identity();
        if (!parent.is_null()) {
            const auto pw = world_matrix(scene, parent);
            if (!pw) return make_error("command.singular_parent_transform", "the new parent has no valid world matrix");
            parent_world = *pw;
        }
        const auto inverse = parent_world.inverse();
        if (!inverse)
            return make_error("command.singular_parent_transform",
                              "the new parent's world matrix is singular (zero scale?), so the world position cannot be kept");
        const Mat4 local = *inverse * world;

        const SceneObject* o = scene.get(h);
        if (nearly_equal(local_matrix(o->transform), local, 1e-9 * std::max(1.0, max_abs(local)))) return Status::success();

        const auto t = decompose_trs(local);
        if (!t)
            return make_error("command.world_transform_not_representable",
                              "keeping the world transform needs shear, which position/rotation/scale cannot express");
        if (Status st = scene.set_transform(h, *t); !st) return st;
        changed = true;
        return Status::success();
    }

    static double max_abs(const Mat4& m) {
        double v = 0.0;
        for (double e : m.m) v = std::max(v, std::fabs(e));
        return v;
    }

    Uuid id_;
    Uuid new_parent_;
    std::size_t index_;
    ReparentMode mode_;
    Uuid old_parent_;
    std::size_t old_index_ = Scene::kAppend;
    Transform old_transform_;
    bool transform_changed_ = false;
    bool captured_ = false;
    std::string label_ = "object";
};

}  // namespace

std::unique_ptr<Command> make_create_command(const Uuid& id, ObjectKind kind, std::string name, const Uuid& parent) {
    return std::make_unique<CreateCommand>(id, kind, std::move(name), parent);
}
std::unique_ptr<Command> make_delete_command(const Uuid& id) { return std::make_unique<DeleteCommand>(id); }
std::unique_ptr<Command> make_set_transform_command(const Uuid& id, const Transform& transform) {
    return std::make_unique<SetTransformCommand>(id, transform);
}
std::unique_ptr<Command> make_rename_command(const Uuid& id, std::string new_name) {
    return std::make_unique<RenameCommand>(id, std::move(new_name));
}
std::unique_ptr<Command> make_reparent_command(const Uuid& id, const Uuid& new_parent, std::size_t sibling_index,
                                               ReparentMode mode) {
    return std::make_unique<ReparentCommand>(id, new_parent, sibling_index, mode);
}

}  // namespace k

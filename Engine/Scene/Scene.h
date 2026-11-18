// K Engine — Engine/Scene/Scene.h
//
// Editor/authoring-layer scene (ADR-0001, ADR-0003). This is the object
// model the Editor edits and the file format serializes. It is NOT the
// Runtime ECS world: no component storage, systems or scheduling live here.
//
// Identity:
//   * Every object has a persistent Uuid (unique within a Scene, never nil).
//   * Live references use Handle. Handles are only valid for the Scene that
//     issued them and are invalidated when the object is destroyed.
//
// Hierarchy: objects form a forest. Destroying an object destroys all of its
// descendants. Child order is preserved and is part of the saved state.
//
// Thread affinity: a Scene is not thread-safe; use it from one thread (the
// editor/authoring thread) at a time.
//
// Units/representation (scene format v1): positions in scene units; rotation
// is Euler angles in degrees; scale is per-axis. All components must be
// finite (NaN/Inf are rejected), so scenes are always serializable.
#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Engine/Core/Result.h"
#include "Engine/Identity/Handle.h"
#include "Engine/Identity/Uuid.h"

namespace k {

using Vec3 = std::array<double, 3>;

enum class ObjectKind : std::uint8_t { Empty, Cube, Sphere, Plane };

// Stable lowercase names used by the file format ("empty", "cube", ...).
const char* to_string(ObjectKind kind);
// Returns false if `text` is not a known kind name.
bool parse_object_kind(std::string_view text, ObjectKind& out);

struct Transform {
    Vec3 position{0.0, 0.0, 0.0};
    Vec3 rotation_euler_deg{0.0, 0.0, 0.0};
    Vec3 scale{1.0, 1.0, 1.0};

    friend bool operator==(const Transform&, const Transform&) = default;
};

bool is_finite(const Transform& t);

struct SceneObject {
    Uuid id;
    std::string name;
    ObjectKind kind = ObjectKind::Empty;
    Transform transform;
    Handle parent = kNullHandle;       // null for root objects
    std::vector<Handle> children;      // ordered
};

class Scene {
public:
    // Creates an object with a freshly generated UUID. `parent` must be null
    // (root) or a valid handle. Error codes: scene.invalid_handle.
    Result<Handle> create(ObjectKind kind, std::string name, Handle parent = kNullHandle);

    // Creates an object with a caller-supplied UUID (used by load and by
    // undo of a delete). Error codes: scene.nil_uuid, scene.duplicate_uuid,
    // scene.invalid_handle.
    Result<Handle> create_with_uuid(const Uuid& id, ObjectKind kind, std::string name,
                                    Handle parent = kNullHandle);

    // Destroys the object and all descendants. Error: scene.invalid_handle.
    Status destroy(Handle h);

    // Moves `h` under `new_parent` (null = make root), appended as last child.
    // Errors: scene.invalid_handle, scene.cycle (new_parent is h or a
    // descendant of h).
    Status reparent(Handle h, Handle new_parent);

    Status set_name(Handle h, std::string name);
    // Errors: scene.invalid_handle, scene.non_finite_transform.
    Status set_transform(Handle h, const Transform& t);

    // Lookup. get() returns nullptr unless the handle is Valid. The pointer
    // is invalidated by create*/destroy; do not retain it.
    const SceneObject* get(Handle h) const { return objects_.get(h); }
    HandleStatus status(Handle h) const { return objects_.status(h); }

    // Returns kNullHandle if no object has this UUID.
    Handle find_by_uuid(const Uuid& id) const;

    const std::vector<Handle>& roots() const { return roots_; }
    std::size_t object_count() const { return objects_.size(); }

    // Preorder traversal (parent before children, siblings in order).
    // Iterative, so hierarchy depth cannot exhaust the call stack.
    // Do not mutate the scene from inside `fn`.
    template <class Fn>
    void visit_preorder(Fn&& fn) const {
        std::vector<Handle> stack(roots_.rbegin(), roots_.rend());
        while (!stack.empty()) {
            const Handle h = stack.back();
            stack.pop_back();
            const SceneObject* o = objects_.get(h);
            if (!o) continue;
            fn(h, *o);
            stack.insert(stack.end(), o->children.rbegin(), o->children.rend());
        }
    }

private:
    void detach_from_parent(Handle h, const SceneObject& o);

    SlotMap<SceneObject> objects_;
    std::unordered_map<Uuid, Handle, Uuid::Hash> by_uuid_;
    std::vector<Handle> roots_;
};

}  // namespace k

// K Engine — Engine/Serialization/SceneSerializer.h
//
// Scene <-> JSON, scene format "kengine.scene" version 1 (ADR-0006,
// Documentation/Formats/scene-v1.md). Pure in-memory: file I/O and atomic
// saves are a separate task (M05-T05).
//
// Only persistent data is written: UUIDs, names, kinds, hierarchy and
// transforms. Runtime Handles and memory addresses are never serialized.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Engine/Core/Result.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Serialization/Json.h"

namespace k {

inline constexpr const char* kSceneSchemaName = "kengine.scene";
inline constexpr int kSceneSchemaVersion = 1;

struct LoadedScene {
    Scene scene;
    // Non-fatal findings, e.g. unknown fields that were ignored (and will
    // NOT be preserved if the scene is saved again).
    std::vector<std::string> warnings;
};

// Objects are written in hierarchy preorder; output is deterministic for a
// given scene state.
Json scene_to_json(const Scene& scene);
Result<std::string> save_scene_to_string(const Scene& scene);

// Error codes (stable): json.syntax, scene.schema.invalid_root,
// scene.schema.missing_field, scene.schema.wrong_type, scene.schema.unknown_schema,
// scene.schema.unsupported_version, scene.object.invalid_uuid,
// scene.object.unknown_kind, scene.object.missing_parent, scene.transform.invalid,
// scene.nil_uuid, scene.duplicate_uuid, scene.cycle.
// Messages carry a JSON-pointer-style path such as "/objects/3/parent".
Result<LoadedScene> scene_from_json(const Json& root);
Result<LoadedScene> load_scene_from_string(std::string_view text);

}  // namespace k

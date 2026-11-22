// K Engine — Editor/Core/DemoScene.h
#pragma once

#include "Engine/Scene/Scene.h"

namespace k {

// Fills `scene` with a small demo: a wide ground plane, a cube, a sphere and an empty "Group"
// holding a tall child cube. Used by `k_editor --demo` and the automated screenshot test.
void build_demo_scene(Scene& scene);

}  // namespace k

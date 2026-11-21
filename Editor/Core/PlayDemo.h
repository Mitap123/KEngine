// K Engine — Editor/Core/PlayDemo.h
//
// The M0.5 "runtime": there is no gameplay loop yet (M1), so Play runs a tiny demonstration
// behaviour on the PLAY COPY of the scene so that isolation is visible: cubes and spheres spin
// around Y. Stop discards the copy, so the authored scene is unchanged.
#pragma once

#include "Engine/Scene/Scene.h"

namespace k {

inline constexpr double kPlayDemoDegreesPerSecond = 45.0;

// Advances the demo by dt seconds (dt <= 0 or non-finite does nothing). Only Cube and Sphere objects
// are affected; the rotation wraps into [0, 360).
void advance_play_demo(Scene& scene, double dt_seconds);

}  // namespace k

// K Engine — Editor/Core/CameraFraming.h
#pragma once

#include "Engine/Rendering/OrbitCamera.h"
#include "Engine/Scene/Scene.h"

namespace k {

// "Frame All": aims the camera at the centre of every drawable object's bounds and picks the
// distance so the whole bounding sphere fits in view (yaw/pitch/fov are kept). With nothing to
// frame the target becomes the origin and the distance 6.
// `aspect` = viewport width / height: the narrower of the vertical and horizontal fields of view is used,
// so a tall window still shows everything. Non-positive aspect is treated as 1.
void frame_scene(OrbitCamera& camera, const Scene& scene, double aspect = 1.0);

}  // namespace k

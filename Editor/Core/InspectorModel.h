// K Engine — Editor/Core/InspectorModel.h
// Transform-field helpers for the Inspector (Qt-free). The UI replaces exactly ONE axis of the
// object's current transform per edit, so values it merely displays (rounded to a few decimals)
// are never written back.
#pragma once

#include "Engine/Scene/Scene.h"

namespace k {

enum class TransformField { Position, Rotation, Scale };

inline constexpr double kInspectorMaxAbs = 1.0e6;
inline constexpr double kInspectorMinScale = 0.001;  // 0 would make the object unpickable (singular matrix)

double axis_value(const Transform& t, TransformField field, int axis);

// Returns `t` with one axis replaced by clamp_for_field(value). axis must be 0..2 (otherwise `t` is returned unchanged).
Transform with_axis(const Transform& t, TransformField field, int axis, double value);

// NaN -> 0 (or kInspectorMinScale for scale); clamps to +-kInspectorMaxAbs; scale additionally to >= kInspectorMinScale.
double clamp_for_field(TransformField field, double value);

}  // namespace k

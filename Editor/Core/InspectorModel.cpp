#include "Editor/Core/InspectorModel.h"

#include <algorithm>
#include <cmath>

namespace k {

namespace {
const Vec3& vec_of(const Transform& t, TransformField f) {
    switch (f) {
        case TransformField::Position: return t.position;
        case TransformField::Rotation: return t.rotation_euler_deg;
        case TransformField::Scale: return t.scale;
    }
    return t.position;
}
Vec3& vec_of(Transform& t, TransformField f) { return const_cast<Vec3&>(vec_of(static_cast<const Transform&>(t), f)); }
}  // namespace

double axis_value(const Transform& t, TransformField field, int axis) {
    if (axis < 0 || axis > 2) return 0.0;
    return vec_of(t, field)[static_cast<std::size_t>(axis)];
}

double clamp_for_field(TransformField field, double value) {
    if (std::isnan(value)) return field == TransformField::Scale ? kInspectorMinScale : 0.0;
    value = std::clamp(value, -kInspectorMaxAbs, kInspectorMaxAbs);
    if (field == TransformField::Scale) value = std::max(value, kInspectorMinScale);
    return value;
}

Transform with_axis(const Transform& t, TransformField field, int axis, double value) {
    if (axis < 0 || axis > 2) return t;
    Transform out = t;
    vec_of(out, field)[static_cast<std::size_t>(axis)] = clamp_for_field(field, value);
    return out;
}

}  // namespace k

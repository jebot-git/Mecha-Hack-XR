#pragma once
#include "math.hpp"
#include <algorithm>
namespace avp {
// One head-relative plane for both eyes, fitted to their common visible FOV.
inline XrFovf commonHudFov(XrFovf a, XrFovf b) {
    return {std::max(a.angleLeft, b.angleLeft), std::min(a.angleRight, b.angleRight),
            std::min(a.angleUp, b.angleUp), std::max(a.angleDown, b.angleDown)};
}
inline Matrix bottomBarPanel(const Matrix &head, XrFovf fov, float width, float distance) {
    float left = std::tan(fov.angleLeft) * distance, right = std::tan(fov.angleRight) * distance;
    width = std::min(width, (right - left) * .90f);
    float height = width * 228.f / 320;
    // The visible bar ends at image row214; the remaining14rows are transparent.
    float bottom = std::tan(fov.angleDown) * distance + distance * .035f;
    return multiply(head, multiply(translation((left + right - width) / 2,
                                               bottom - height * (14.f / 228), -distance),
                                   scaling(width, height, 1)));
}
} // namespace avp

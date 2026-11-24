#pragma once
#include "matrix.hpp"
#include <openxr/openxr.h>
namespace avp {
inline Matrix poseMatrix(const XrPosef &p) {
    auto q = p.orientation;
    float x = q.x, y = q.y, z = q.z, w = q.w;
    return {1 - 2 * y * y - 2 * z * z,
            2 * x * y + 2 * w * z,
            2 * x * z - 2 * w * y,
            0,
            2 * x * y - 2 * w * z,
            1 - 2 * x * x - 2 * z * z,
            2 * y * z + 2 * w * x,
            0,
            2 * x * z + 2 * w * y,
            2 * y * z - 2 * w * x,
            1 - 2 * x * x - 2 * y * y,
            0,
            p.position.x,
            p.position.y,
            p.position.z,
            1};
}
inline Matrix projection(const XrFovf &f, float nearPlane = .035f, float farPlane = 160) {
    float l = std::tan(f.angleLeft), r = std::tan(f.angleRight), b = std::tan(f.angleDown),
          t = std::tan(f.angleUp);
    return {2 / (r - l),
            0,
            0,
            0,
            0,
            2 / (t - b),
            0,
            0,
            (r + l) / (r - l),
            (t + b) / (t - b),
            -(farPlane + nearPlane) / (farPlane - nearPlane),
            -1,
            0,
            0,
            -2 * farPlane * nearPlane / (farPlane - nearPlane),
            0};
}
} // namespace avp

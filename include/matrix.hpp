#pragma once
#include <array>
#include <cmath>
namespace avp {
using Matrix = std::array<float, 16>;
inline constexpr Matrix identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
inline Matrix multiply(const Matrix &a, const Matrix &b) {
    Matrix m{};
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            for (int k = 0; k < 4; ++k)
                m[c * 4 + r] += a[k * 4 + r] * b[c * 4 + k];
    return m;
}
// Conservative homogeneous clip test for the unit quad used by original art.
// Reject only when all four corners lie outside the same frustum plane.
inline bool visibleQuad(const Matrix &vp, const Matrix &model) {
    auto clip = multiply(vp, model);
    unsigned outside = 63;
    for (float u : {0.f, 1.f})
        for (float v : {0.f, 1.f}) {
            float c[4];
            for (int i = 0; i < 4; ++i)
                c[i] = clip[i] * u + clip[4 + i] * v + clip[12 + i];
            unsigned mask = 0;
            for (int i = 0; i < 3; ++i) {
                if (c[i] < -c[3]) mask |= 1u << (2 * i);
                if (c[i] > c[3]) mask |= 1u << (2 * i + 1);
            }
            outside &= mask;
        }
    return outside == 0;
}
inline Matrix translation(float x, float y, float z) {
    auto m = identity;
    m[12] = x;
    m[13] = y;
    m[14] = z;
    return m;
}
inline Matrix rotation(float yaw, float pitch = 0) {
    float c = std::cos(yaw), s = std::sin(yaw), a = std::cos(pitch), b = std::sin(pitch);
    Matrix y{c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, 0, 0, 0, 1},
        p{1, 0, 0, 0, 0, a, b, 0, 0, -b, a, 0, 0, 0, 0, 1};
    return multiply(y, p);
}
inline Matrix scaling(float x, float y, float z) {
    auto m = identity;
    m[0] = x;
    m[5] = y;
    m[10] = z;
    return m;
}
inline Matrix inverseRigid(const Matrix &m) {
    Matrix r{m[0], m[4], m[8], 0, m[1], m[5], m[9], 0, m[2], m[6], m[10], 0, 0, 0, 0, 1};
    for (int i = 0; i < 3; ++i)
        r[12 + i] = -(r[i] * m[12] + r[4 + i] * m[13] + r[8 + i] * m[14]);
    return r;
}
inline std::array<float, 3> point(const Matrix &m, const std::array<float, 3> &p = {0, 0, 0}) {
    return {m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12],
            m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
            m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]};
}
} // namespace avp

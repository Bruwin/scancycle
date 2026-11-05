#pragma once
// Just enough matrix maths for a first-person camera, and the frustum extraction
// the compute cull shader depends on. Kept free of any graphics API so it can be
// tested headlessly, which is the only way this project verifies anything.
#include "core/types.hpp"

#include <cmath>

namespace sc {

struct Vec3 {
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;
};

inline Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator*(const Vec3& v, f32 s) { return {v.x * s, v.y * s, v.z * s}; }

inline f32 dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline Vec3 normalize(const Vec3& v) {
    const f32 length = std::sqrt(dot(v, v));
    if (length <= 0.0f) {
        return {0.0f, 0.0f, 0.0f};
    }
    return {v.x / length, v.y / length, v.z / length};
}

// Column-major 4x4, matching both Vulkan and the shader's mat4 layout.
struct Mat4 {
    f32 m[16] = {};
};

inline Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 out;
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            f32 sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[k * 4 + row] * b.m[column * 4 + k];
            }
            out.m[column * 4 + row] = sum;
        }
    }
    return out;
}

inline Mat4 perspective(f32 fov_y_radians, f32 aspect, f32 near_plane, f32 far_plane) {
    Mat4 out;
    const f32 f = 1.0f / std::tan(fov_y_radians * 0.5f);
    out.m[0] = f / aspect;
    out.m[5] = f;
    out.m[10] = far_plane / (near_plane - far_plane);
    out.m[11] = -1.0f;
    out.m[14] = (near_plane * far_plane) / (near_plane - far_plane);
    return out;
}

inline Mat4 look_at(const Vec3& eye, const Vec3& target, const Vec3& up) {
    const Vec3 forward = normalize(target - eye);
    const Vec3 right = normalize(cross(forward, up));
    const Vec3 true_up = cross(right, forward);

    Mat4 out;
    out.m[0] = right.x;
    out.m[1] = true_up.x;
    out.m[2] = -forward.x;
    out.m[4] = right.y;
    out.m[5] = true_up.y;
    out.m[6] = -forward.y;
    out.m[8] = right.z;
    out.m[9] = true_up.z;
    out.m[10] = -forward.z;
    out.m[12] = -dot(right, eye);
    out.m[13] = -dot(true_up, eye);
    out.m[14] = dot(forward, eye);
    out.m[15] = 1.0f;
    return out;
}

// Gribb-Hartmann plane extraction. Each plane is (normal.xyz, distance) with the
// normal normalised, so a sphere test is dot(n, c) + d < -r.
inline void extract_frustum_planes(const Mat4& view_projection, f32 planes[6][4]) {
    const f32* m = view_projection.m;

    auto set_plane = [&](int index, f32 a, f32 b, f32 c, f32 d) {
        const f32 length = std::sqrt(a * a + b * b + c * c);
        const f32 inverse = length > 0.0f ? 1.0f / length : 0.0f;
        planes[index][0] = a * inverse;
        planes[index][1] = b * inverse;
        planes[index][2] = c * inverse;
        planes[index][3] = d * inverse;
    };

    set_plane(0, m[3] + m[0], m[7] + m[4], m[11] + m[8], m[15] + m[12]);    // left
    set_plane(1, m[3] - m[0], m[7] - m[4], m[11] - m[8], m[15] - m[12]);    // right
    set_plane(2, m[3] + m[1], m[7] + m[5], m[11] + m[9], m[15] + m[13]);    // bottom
    set_plane(3, m[3] - m[1], m[7] - m[5], m[11] - m[9], m[15] - m[13]);    // top
    set_plane(4, m[3] + m[2], m[7] + m[6], m[11] + m[10], m[15] + m[14]);   // near
    set_plane(5, m[3] - m[2], m[7] - m[6], m[11] - m[10], m[15] - m[14]);   // far
}

// Whether a sphere survives the frustum. Mirrors the compute shader's test, so a
// headless test can prove the shader's assumption before a GPU ever sees it.
inline bool sphere_inside_frustum(const f32 planes[6][4], const f32 center[3], f32 radius) {
    for (int i = 0; i < 6; ++i) {
        const f32 distance = planes[i][0] * center[0] + planes[i][1] * center[1] +
                             planes[i][2] * center[2] + planes[i][3];
        if (distance < -radius) {
            return false;
        }
    }
    return true;
}

}  // namespace sc

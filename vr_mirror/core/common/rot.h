// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Small quaternion helpers (the orientation filter is
// vr_mirror\core\pipeline\4_smoothing\instant_smoothing.h). Convention (same as PVR/LibOVR): right-handed, +X
// right, +Y up, -Z forward, quaternions rotate head-local vectors into world space.
#pragma once

#include <algorithm>
#include <cmath>

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
};

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

inline Quat Mul(const Quat& a, const Quat& b) {
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
    };
}

inline Quat Conj(const Quat& q) {
    return {-q.x, -q.y, -q.z, q.w};
}

inline float Dot(const Quat& a, const Quat& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

inline Quat Normalize(const Quat& q) {
    const float n = std::sqrt(Dot(q, q));
    if (n < 1e-8f) {
        return {};
    }
    return {q.x / n, q.y / n, q.z / n, q.w / n};
}

inline Vec3 Rotate(const Quat& q, const Vec3& v) {
    Quat p = Mul(Mul(q, Quat{v.x, v.y, v.z, 0}), Conj(q));
    return {p.x, p.y, p.z};
}

// Angle in radians of the rotation taking a to b.
inline float AngleBetween(const Quat& a, const Quat& b) {
    const float d = std::fabs(Dot(a, b));
    return 2.0f * std::acos(std::min(1.0f, d));
}

inline Quat Slerp(const Quat& a, Quat b, float t) {
    float d = Dot(a, b);
    if (d < 0) {
        b = {-b.x, -b.y, -b.z, -b.w};
        d = -d;
    }

    if (d > 0.9995f) {
        return Normalize(
            {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t}
        );
    }

    const float theta = std::acos(d);
    const float s = std::sin(theta);
    float wa = std::sin((1 - t) * theta) / s, wb = std::sin(t * theta) / s;
    return {a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
}

// Orientation looking along `fwd` with world +Y as up (removes roll).
inline Quat LookRotation(Vec3 fwd) {
    const float n = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
    if (n < 1e-6f) {
        return {};
    }
    fwd = {fwd.x / n, fwd.y / n, fwd.z / n};

    // Yaw about +Y, then pitch about the local +X.
    const float yaw = std::atan2(-fwd.x, -fwd.z);
    const float pitch = std::asin(std::clamp(fwd.y, -1.0f, 1.0f));
    Quat qy{0, std::sin(yaw / 2), 0, std::cos(yaw / 2)};
    Quat qx{std::sin(pitch / 2), 0, 0, std::cos(pitch / 2)};
    return Mul(qy, qx);
}

// Row-major 3x3 rotation matrix.
inline void ToMatrix(const Quat& q, float m[3][3]) {
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    m[0][0] = 1 - 2 * (yy + zz);
    m[0][1] = 2 * (xy - wz);
    m[0][2] = 2 * (xz + wy);
    m[1][0] = 2 * (xy + wz);
    m[1][1] = 1 - 2 * (xx + zz);
    m[1][2] = 2 * (yz - wx);
    m[2][0] = 2 * (xz - wy);
    m[2][1] = 2 * (yz + wx);
    m[2][2] = 1 - 2 * (xx + yy);
}

// A small rotation as a rotation vector (axis times angle, radians), and back: exact for small
// angles, where 2 acos(w) loses most of its digits.
inline Vec3 RotationVector(const Quat& q) {
    const float sign = q.w < 0 ? -1.0f : 1.0f;
    const float x = sign * q.x, y = sign * q.y, z = sign * q.z, w = sign * q.w;
    const float s = std::sqrt(x * x + y * y + z * z);
    const float k = s < 1e-9f ? 2.0f : 2 * std::atan2(s, w) / s;
    return {x * k, y * k, z * k};
}

inline Quat FromRotationVector(const Vec3& v) {
    const float a = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    const float k = a < 1e-9f ? 0.5f : std::sin(0.5f * a) / a;
    return Normalize({v.x * k, v.y * k, v.z * k, std::cos(0.5f * a)});
}

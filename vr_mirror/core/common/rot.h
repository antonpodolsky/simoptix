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

// The angle between two directions (radians): atan2 of the cross product's length over the dot, which
// holds to float rounding where they nearly match (acos of the dot reads about 0.05 deg apart there).
// The measures and fakes in vr_mirror\e2e use it and RotationAngleAtan2 (below), and no other.
inline double DirectionAngle(const Vec3& a, const Vec3& b) {
    const double cx = double(a.y) * b.z - double(a.z) * b.y;
    const double cy = double(a.z) * b.x - double(a.x) * b.z;
    const double cz = double(a.x) * b.y - double(a.y) * b.x;
    const double dot = double(a.x) * b.x + double(a.y) * b.y + double(a.z) * b.z;
    return std::atan2(std::sqrt(cx * cx + cy * cy + cz * cz), dot);
}

// Two readings of the angle of the rotation taking a to b (radians). They agree to 0.01 deg from about
// 0.1 deg up, and part below it:
//
// | True turn | RotationAngleAcos | RotationAngleAtan2 |
// |-----------|-------------------|--------------------|
// | 0.02 deg  | 0                 | 0.0200 deg         |
// | 0.06 deg  | 0.0396 deg        | 0.0600 deg         |
// | 0.1 deg   | 0.1047 deg        | 0.1000 deg         |

// 2 atan2(|v|, |w|) of the relative rotation, in double. |v| is the sine of half the turn, which keeps its
// float precision however small the turn. The measures and fakes in vr_mirror\e2e use it.
inline double RotationAngleAtan2(const Quat& a, const Quat& b) {
    const Quat r = Mul(Conj(a), b);
    const double v = std::sqrt(double(r.x) * r.x + double(r.y) * r.y + double(r.z) * r.z);
    return 2 * std::atan2(v, std::fabs(double(r.w)));
}

// 2 acos of the float dot. The dot is the cosine of half the turn, so a small turn is a gap from 1 that
// a float holds in steps of 6e-8: turns under 0.04 deg read 0, and the next readings are 0.056 and
// 0.069 deg. The step is 0.008 deg at 0.1 deg and 0.002 deg at 0.5 deg. Two equal poses that aren't quite
// unit length read 0.02-0.05 deg apart. The smoothings use it, and stage_replay --golden's expected output is
// made with it, so moving them to RotationAngleAtan2 changes every golden run.
inline float RotationAngleAcos(const Quat& a, const Quat& b) {
    const float d = std::fabs(Dot(a, b));
    return 2.0f * std::acos(std::min(1.0f, d));
}

// The rotation taking a to b as an angle (radians) and a unit axis.
struct AngleAxis {
    float angle = 0;
    Vec3 axis;             // zero without hasAxis
    bool hasAxis = false;  // the sine of half the angle is at least 1e-6, so the axis can be read
};

// The angle and axis by acos, like RotationAngleAcos: 2 acos(w) of the normalized relative rotation, and
// the axis from sqrt(1 - w^2), in float. Below about 0.1 deg the angle is as coarse as RotationAngleAcos's,
// and the axis loses its digits too. The image room uses it, and stage_replay --golden's expected output
// is made with it.
inline AngleAxis RotationAngleAxisAcos(const Quat& a, const Quat& b) {
    Quat r = Normalize(Mul(Conj(a), b));
    if (r.w < 0) {
        r = {-r.x, -r.y, -r.z, -r.w};
    }

    AngleAxis turn;
    turn.angle = 2 * std::acos(std::min(1.0f, r.w));
    const float sinHalf = std::sqrt(std::max(0.0f, 1 - r.w * r.w));

    if (sinHalf >= 1e-6f) {
        turn.axis = {r.x / sinHalf, r.y / sinHalf, r.z / sinHalf};
        turn.hasAxis = true;
    }

    return turn;
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

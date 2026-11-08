// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Shared by the e2e runner's files: the standard headers they use, the filesystem alias, angle
// constants and conversions between view directions and degrees. No state.
#pragma once

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "common/rot.h"

namespace fs = std::filesystem;

constexpr double kPi = 3.14159265358979;
constexpr double kDeg = kPi / 180;

inline Vec3 Normalized(Vec3 v) {
    const double n = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return n > 1e-9 ? Vec3{float(v.x / n), float(v.y / n), float(v.z / n)} : v;
}

inline double AngleDeg(Vec3 a, Vec3 b) {
    const double d = a.x * b.x + a.y * b.y + a.z * b.z;
    return std::acos(std::clamp(d, -1.0, 1.0)) / kDeg;
}

inline Vec3 Forward(const Quat& q) {
    return Rotate(q, {0, 0, -1});
}

inline double YawDeg(Vec3 f) {
    return std::atan2(-f.x, -f.z) / kDeg;
}
inline double PitchDeg(Vec3 f) {
    return std::asin(std::clamp(double(f.y), -1.0, 1.0)) / kDeg;
}
inline double RollDeg(Vec3 right) {
    return std::asin(std::clamp(double(right.y), -1.0, 1.0)) / kDeg;
}

// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The game eye's geometry and a game frame's record as a frame source (vr_mirror\frame_sources\)
// hands them out: plain data, shared by the sources and the pipeline, and the eye's description for the
// log. Knows nothing about PVR, OpenXR, D3D or threads.
// Times are source time: seconds on the frame source's clock (the Pimax runtime's, or the
// performance counter's for the OpenXR layer), which the pipeline and the hosts use throughout.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "common/rot.h"

struct EyeInfo {
    float leftTan = 1, rightTan = 1, upTan = 1, downTan = 1;  // FOV half-angle tangents
    Quat hmdToEye;                                            // eye orientation relative to head (canting)
    // The image's bytes are sRGB-encoded twice (Pimax's client mirror: an _SRGB texture holding
    // already-encoded values), so one encoding is undone before it is shown or looked at; false: once,
    // as the game drew it.
    bool encodedTwice = false;
};

// The eye's field of view in degrees from straight ahead, and its turn against the head, for the log, the
// frame trace and the file beside a recording: "left 52.59 right 47.56 up 44.55 down 44.55 deg, turned
// 1.98 deg against the head". The turn is 2 atan2(|v|, |w|), which stays exact for small angles.
inline void DescribeEye(const EyeInfo& eye, char* text, size_t size) {
    constexpr float kDeg = 57.2957795f;
    const Quat& q = eye.hmdToEye;
    const float turnDeg = 2 * std::atan2(std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z), std::fabs(q.w)) * kDeg;
    std::snprintf(
        text,
        size,
        "left %.2f right %.2f up %.2f down %.2f deg, turned %.2f deg against the head",
        std::atan(eye.leftTan) * kDeg,
        std::atan(eye.rightTan) * kDeg,
        std::atan(eye.upTan) * kDeg,
        std::atan(eye.downTan) * kDeg,
        turnDeg
    );
}

struct GameFrame {
    int buffer = -1;        // which of the source's images holds it (Pimax: 0, its one mirror; the OpenXR
                            // layer: its texture); -1: none yet
    uint64_t serial = 0;    // increments on every new frame
    double time = 0;        // source time the frame arrived
    double smoothTime = 0;  // arrival time with timing noise removed (locked to the display grid)
    bool synced = false;    // false: no sync events, buffer choice is a guess
    double slotLead = 0;    // the runtime's next display slot minus the arrival time (seconds; normally
                            // ~21.8 ms, shifts when the game falls behind), 0 if unknown
    double slotTime = 0;    // the display slot the arrival timing assigned it, which it is shown in (source
                            // time), 0 if unknown; the drawn pose says which pose it was rendered
                            // with (vr_mirror\core\pipeline\2_drawn_pose)
    double drawnSlot = 0;   // the display slot the frame source says the frame was drawn for (the OpenXR
                            // layer: the display time the game passed with it), 0 if it doesn't say
};

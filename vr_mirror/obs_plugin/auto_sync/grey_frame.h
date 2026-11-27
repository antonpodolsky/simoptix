// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The small grey copies of the game's frames that a VR Mirror source makes for auto sync to look at
// (FrameRing::SetGreyCopies): their size, the part of the eye image they cover, and what each holds.
// Data only.
#pragma once

#include <vector>

#include "common/game_frame.h"
#include "common/rot.h"

// The grey copies' size, and the part of the eye image they cover (u0, v0, u1, v1). Below the middle,
// where the cockpit's displays are.
constexpr int kGreyWidth = 256, kGreyHeight = 256;
constexpr float kGreyArea[4] = {0.1f, 0.15f, 0.9f, 0.95f};

// A small grey copy of part of a frame's eye image, the time the frame stands for, and the head pose
// it was drawn with.
struct GreyFrame {
    double time = 0;            // performance-counter seconds
    Quat pose;                  // the head's orientation in tracking space
    std::vector<float> values;  // luminance 0-1, row by row
};

// Where the grey copies come from: their size in texels, the part of the eye image they cover (u0,
// v0, u1, v1; 0-1, top-left origin) and the eye.
struct GreyLayout {
    int width = 0, height = 0;
    float area[4] = {0, 0, 1, 1};
    EyeInfo eye;
};

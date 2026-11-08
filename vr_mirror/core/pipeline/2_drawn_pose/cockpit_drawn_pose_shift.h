// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// CockpitDrawnPose's helpers: where its patches of the eye sit, how far one patch's picture moved
// between two game frames (to a small fraction of a texel), and the one small rotation that most
// patches' moves agree on. Pure logic: patches, poses and the eye's geometry come in as
// parameters; it knows nothing about D3D, PVR, threads or which frames are compared.
//
// Why: the cockpit is fixed in the headset's tracking space (a sim rig: no car motion reaches the
// head), so between two game frames it moves in the eye image only by the rotation between the
// head poses the two were drawn with. Where it moves by more than the poses the tool placed them
// with explain, one of those poses is off, by that much. Content that moves on its own (the road
// and scenery through the windows, the wheel, screens, mirrors) and featureless areas (a dark
// roof) are left out: the first by not agreeing with the rotation most patches follow, the second
// by their lack of detail.
#pragma once

#include "common/game_frame.h"
#include "common/rot.h"

// The patches: a grid of 5 x 4 small squares over the eye (centres at u 0.14 to 0.86, v 0.14 to
// 0.86), each 32 x 32 texels of about 4 x 4 pixels of the game eye (about 5.6 degrees, 0.18 degree
// per texel). Spread out, so that some fall on the roof, the pillars and the dashboard whatever
// car and head pose; together 160 x 128 texels.
constexpr int kCockpitColumns = 5, kCockpitRows = 4, kCockpitPatches = kCockpitColumns * kCockpitRows;
constexpr int kCockpitPatchSize = 32;
extern const float kCockpitAreas[kCockpitPatches][4];  // u0, v0, u1, v1 (fractions of the eye)

// One patch's move between the previous frame's picture and this frame's.
struct PatchShift {
    bool ok = false;
    // Texels (x right, y down): the content at texel (i, j) of this frame's patch is at (i + dx, j + dy)
    // in the previous frame's.
    float dx = 0, dy = 0;
    float info[3] = {};  // how precisely: the information matrix (xx, xy, yy), 1/texel^2
};

// Measures the move of patch `cur` against `prev` (each kCockpitPatchSize square, rows `stride`
// floats apart), searched within about 10 texels of (guessX, guessY), the move the poses predict.
// Not ok when the patch is too plain to measure, the move is out of reach, or the two don't match
// after the move (content that changed: a screen, a mirror, something passing). Parts of the patch
// that move on their own count less (robust weights).
PatchShift MeasurePatchShift(const float* prev, const float* cur, int stride, float guessX, float guessY);

// What the poses predict for a patch, and its measured move against that.
struct PatchMove {
    bool ok = false;
    float dx = 0, dy = 0;       // measured minus predicted move (texels)
    float info[3] = {};         // PatchShift::info
    float jacobian[2][3] = {};  // texels of move per radian of correction about the eye's x, y, z
};

// The eye-frame correction (rotation vector, radians) the patches' moves agree on: the least median
// of squares over pairs of patches (yaw, pitch and roll), then a fit weighted by each patch's
// precision to those that agree with it. False when fewer than 4 patches, or under half of those
// measured (ok), agree.
bool FitCorrection(const PatchMove* moves, int count, Vec3* omega, int* agreeing);

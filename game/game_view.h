// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Where the chart sits and how one eye sees it: the rotation from an eye's axes to the chart's, and the
// eye's field of view, as the grid's shader takes them. The chart is fixed in the room, far away, so
// only rotations matter and both eyes see it alike. Its centre is straight ahead of the head's yaw when
// it was last centred, on the horizon. Plain logic on OpenXR's data types: no OpenXR or D3D calls.
// Axes are OpenXR's: x right, y up, z back (the head looks along -z).
#pragma once

#define XR_NO_PROTOTYPES
#include "openxr/openxr.h"

struct GridView {
    float chartFromEye[3][4];  // rows of the rotation; the fourth column is unused (shader padding)
    float tanLeft = 0, tanRight = 0, tanUp = 0, tanDown = 0;  // left and down negative
};

// The yaw of a head looking with `head`: radians about +y, 0 looking along -z, positive to the left.
float HeadYaw(const XrQuaternionf& head);

// How an eye with orientation `eye` and field of view `fov` sees a chart centred at `chartYaw`.
GridView ViewOfEye(const XrQuaternionf& eye, const XrFovf& fov, float chartYaw);

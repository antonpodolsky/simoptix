// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// ReportedDrawnPose: for a frame source that says which display slot each frame was drawn for and
// records the pose it was drawn with for that slot (the OpenXR layer: the game's own pose for the
// display time it passed). A frame's drawn pose is the pose of the slot it was drawn for
// (FrameTimes::drawnSlot), exactly, whichever slot it is shown in; the slot it arrived for where it
// doesn't say; the head at its pose time where neither is recorded. For moments of the head's path, the
// pose recorded for each moment's slot (between slots, interpolated), as the images shown then were
// drawn with. Looks at no images (the feed makes none). Stateless. Knows nothing about smoothing,
// edges, OpenXR or PVR (poses come from a PoseSource).
#pragma once

#include "pipeline/2_drawn_pose/drawn_pose.h"

class ReportedDrawnPose : public DrawnPose {
public:
    Quat PoseOf(const FrameTimes& f, PoseSource& poses) const override;
    void PosesAt(const double* times, const Quat* heads, int n, PoseSource& poses, Quat* out) const override;
    void
    PosesBetween(const double* times, const Quat* heads, int n, PoseSource& poses, Quat* out) const override;
};

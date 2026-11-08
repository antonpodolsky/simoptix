// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// for.
#include "pipeline/2_drawn_pose/reported_drawn_pose.h"

Quat ReportedDrawnPose::PoseOf(const FrameTimes& f, PoseSource& poses) const {
    const double slot = f.drawnSlot > 0 ? f.drawnSlot : f.shownSlot;
    Quat drawn;
    if (f.synced && slot > 0 && poses.PredictedHeadAt(slot, &drawn)) {
        return drawn;
    }
    return poses.HeadAt(f.poseTime);
}

void ReportedDrawnPose::PosesAt(const double* times, const Quat*, int n, PoseSource& poses, Quat* out) const {
    poses.PredictedHeadsAt(times, n, out);
}

void ReportedDrawnPose::PosesBetween(
    const double* times,
    const Quat*,
    int n,
    PoseSource& poses,
    Quat* out
) const {
    poses.PredictedHeadsBetween(times, n, out);
}

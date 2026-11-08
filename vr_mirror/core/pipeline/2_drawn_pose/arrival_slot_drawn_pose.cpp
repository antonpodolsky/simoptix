// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "pipeline/2_drawn_pose/arrival_slot_drawn_pose.h"

Quat PredictedPose(const FrameTimes& f, int slotsBefore, const Quat& correction, PoseSource& poses) {
    // A game renders each frame with the head pose the runtime predicted for its display slot, not
    // the pose at any moment we can see; the PoseSource records those predictions.
    const double slot = f.shownSlot > 0 ? f.shownSlot - slotsBefore * poses.Period() : 0;
    Quat predicted;
    if (f.synced && slot > 0 && poses.PredictedHeadAt(slot, &predicted)) {
        return correction.w < 1 ? Normalize(Mul(predicted, correction)) : predicted;
    }

    return poses.HeadAt(f.poseTime);
}

void ArrivalSlotDrawnPose::PosesAt(
    const double* times,
    const Quat*,
    int n,
    PoseSource& poses,
    Quat* out
) const {
    // The window's moments sit on display slots, and with the look-ahead the runtime has predicted
    // nearly all of them already: what each moment's frame will be warped from.
    poses.PredictedHeadsAt(times, n, out);
}

void ArrivalSlotDrawnPose::PosesBetween(
    const double* times,
    const Quat*,
    int n,
    PoseSource& poses,
    Quat* out
) const {
    poses.PredictedHeadsBetween(times, n, out);
}

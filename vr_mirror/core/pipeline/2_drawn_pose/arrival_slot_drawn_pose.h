// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// ArrivalSlotDrawnPose: Pimax's prediction for the slot each frame arrived for. A frame's drawn
// pose is the pose the runtime predicted for the display slot its arrival timing assigned it, which
// the game rendered it with while it keeps up (setting drawn_pose 0); the measured head at its
// pose time where no prediction is recorded. Looks at no pictures (the feed makes none). Stateless.
// Knows nothing about smoothing, edges or PVR (poses come from a PoseSource).
//
// PredictedPose is also how the slot check's implementations turn the slot they judged into a pose.
#pragma once

#include "pipeline/2_drawn_pose/drawn_pose.h"

// The pose the runtime predicted for the display slot `slotsBefore` periods before frame `f`'s
// arrival slot, followed by `correction` (a rotation in the head's frame; the identity is left out
// entirely); the measured head at f's pose time where none is recorded or f isn't synced.
Quat PredictedPose(const FrameTimes& f, int slotsBefore, const Quat& correction, PoseSource& poses);

class ArrivalSlotDrawnPose : public DrawnPose {
public:
    Quat PoseOf(const FrameTimes& f, PoseSource& poses) const override {
        return PredictedPose(f, 0, {}, poses);
    }

    // The runtime's prediction for each slot where recorded, else the head then (PoseSource).
    void PosesAt(const double* times, const Quat* heads, int n, PoseSource& poses, Quat* out) const override;
    // Between two slots, the slerp of their predictions, else the head then (PoseSource).
    void
    PosesBetween(const double* times, const Quat* heads, int n, PoseSource& poses, Quat* out) const override;
};

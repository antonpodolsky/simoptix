// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// PoseSource: where the pipeline gets head poses from: the head at any time, and the pose the
// runtime predicted for a display slot (what the game rendered that slot's frame with). PimaxSource
// gives them from the live session; sims and replays can give them from recorded poses, so they run
// the real pipeline. Interface only: knows nothing about PVR, threads or D3D.
#pragma once

#include "common/rot.h"

class PoseSource {
public:
    virtual double Period() const = 0;  // the display's (seconds)
    // Head orientation at time t (seconds, the source's clock).
    virtual Quat HeadAt(double t) = 0;
    virtual void HeadsAt(const double* t, int n, Quat* out) = 0;
    // The pose predicted for display slot `slot`; false if none is known.
    virtual bool PredictedHeadAt(double slot, Quat* q) = 0;
    // For `n` slots in increasing order: the predicted pose where known, else the head then.
    virtual void PredictedHeadsAt(const double* slots, int n, Quat* out) = 0;
    // For `n` times in increasing order, anywhere between slots: the slerp of the predictions for the
    // two slots around each (at most 1.5 periods apart), else the head then.
    virtual void PredictedHeadsBetween(const double* t, int n, Quat* out) = 0;

protected:
    ~PoseSource() = default;
};

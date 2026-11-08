// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// PvrPoses: the head poses and the runtime's per-slot predictions as the poll thread records
// them, and the lookups the hosts make: the head at any time (interpolated, PoseHistory), and the
// pose a game rendered a display slot's frame with. Its own lock, held only to copy values in or
// out, so lookups never wait behind frames or runtime calls. Knows nothing about the runtime itself.
#pragma once

#include <mutex>

#include "common/pose_history.h"
#include "common/rot.h"

class PvrPoses {
public:
    // `period`: the display's (the slot lookups' tolerance is a quarter of it).
    void SetPeriod(double period) { period_ = period; }

    // Poll thread: a head pose sample at source time t, and/or the pose predicted for display slot
    // `slot` the moment it became the next one.
    void Add(bool tracked, double t, const Quat& pose, bool predicted, double slot, const Quat& slotPose) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (tracked) {
            poses_.AddPose(t, pose);
        }
        if (predicted) {
            poses_.AddPrediction(slot, slotPose);
        }
    }

    // A new session may count time from a new origin.
    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        poses_.Clear();
    }

    // Head orientation at source time `t` (PoseHistory::At: interpolated, or extrapolated a little past
    // the newest sample).
    Quat HeadAt(double t) {
        std::lock_guard<std::mutex> lock(mutex_);
        return poses_.At(t);
    }

    void HeadsAt(const double* t, int n, Quat* out) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (int i = 0; i < n; i++) {
            out[i] = poses_.At(t[i]);
        }
    }

    // The pose predicted for display slot `slot`; false if none was recorded. count: include the
    // call in TakePredictionStats.
    bool PredictedHeadAt(double slot, Quat* q, bool count) {
        std::lock_guard<std::mutex> lock(mutex_);
        const bool found = poses_.PredictionFor(slot, 0.25 * period_, q);
        (found ? found_ : missing_) += count;
        return found;
    }

    // For `n` slots in increasing order: the predicted pose where recorded, else the head then.
    void PredictedHeadsAt(const double* slots, int n, Quat* out) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (int i = 0; i < n; i++) {
            out[i] = poses_.At(slots[i]);
        }
        poses_.PredictionsFor(slots, n, 0.25 * period_, out);
    }

    // For `n` times in increasing order: between two slots, the slerp of their predictions, else the
    // head then.
    void PredictedHeadsBetween(const double* t, int n, Quat* out) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (int i = 0; i < n; i++) {
            out[i] = poses_.At(t[i]);
        }
        poses_.PredictionsBetween(t, n, 1.5 * period_, out);
    }

    void TakePredictionStats(int* found, int* missing) {
        std::lock_guard<std::mutex> lock(mutex_);
        *found = found_;
        *missing = missing_;
        found_ = missing_ = 0;
    }

private:
    std::mutex mutex_;
    PoseHistory poses_;
    int found_ = 0, missing_ = 0;
    double period_ = 1.0 / 90;
};

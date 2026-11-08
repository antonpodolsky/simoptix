// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// PoseHistory: the recent head poses (sampled at about 1 kHz) and the poses the runtime predicted
// for each display slot, with their lookups: the head at a moment (interpolated between samples)
// and the prediction for a slot. Logic only: times (source seconds, the runtime's clock) and poses
// come in as parameters; no runtime calls and no locking (the owner guards it: PimaxSource).
#pragma once

#include <algorithm>
#include <cmath>

#include "common/rot.h"

class PoseHistory {
public:
    // About 4 s of samples at the poll thread's ~1 kHz: the look-ahead smoothing (pipeline\4_smoothing) needs
    // its window behind the frame and up to a second after it.
    static constexpr int kPoses = 4096;
    // About 2.8 s of display slots: with look-ahead smoothing a frame is drawn up to ~1 s after its
    // slot, and still needs the pose predicted for it.
    static constexpr int kPredictions = 256;

    struct Sample {
        double t = 0;  // source seconds; for a prediction: the display slot it is for
        Quat q;
    };

    void AddPose(double t, const Quat& q) {
        poses_[poseHead_] = {t, q};
        poseHead_ = (poseHead_ + 1) % kPoses;
        poseCount_ = std::min(poseCount_ + 1, kPoses);
    }

    void AddPrediction(double slot, const Quat& q) {
        predicted_[predictedHead_] = {slot, q};
        predictedHead_ = (predictedHead_ + 1) % kPredictions;
        predictedCount_ = std::min(predictedCount_ + 1, kPredictions);
    }

    void Clear() { poseCount_ = poseHead_ = predictedCount_ = predictedHead_ = 0; }

    // The newest pose sample; false if there is none.
    bool Newest(Sample* s) const {
        if (poseCount_ == 0) {
            return false;
        }
        *s = poses_[(poseHead_ - 1 + kPoses) % kPoses];
        return true;
    }

    // Head orientation at `t`, interpolated between the two samples around it, the oldest sample
    // for an earlier `t`, identity with no samples. Past the newest sample (a positive latency, or
    // a time just ahead of the last sample): extrapolated at the head's turning rate over the last
    // kRateSpan, for at most kMaxAhead past the newest sample.
    static constexpr double kRateSpan = 0.004;  // seconds: the rate is noisy over a single 1 ms step
    static constexpr double kMaxAhead = 0.020;  // seconds
    Quat At(double t) const {
        if (poseCount_ == 0) {
            return {};
        }

        // Walk back from the newest sample to the pair that brackets t.
        const int newest = (poseHead_ - 1 + kPoses) % kPoses;
        const Sample* later = &poses_[newest];
        if (t >= later->t) {
            for (int k = 1; k < poseCount_; k++) {
                const Sample& older = poses_[(newest - k + kPoses) % kPoses];
                if (later->t - older.t < kRateSpan) {
                    continue;
                }
                const double ahead = std::min(t, later->t + kMaxAhead);
                return Normalize(Slerp(older.q, later->q, float((ahead - older.t) / (later->t - older.t))));
            }

            return later->q;  // too few samples for a rate
        }

        // Binary search over the samples, oldest (logical index 0) to newest, for the first later than t.
        auto at = [&](int i) -> const Sample& {
            return poses_[(poseHead_ - poseCount_ + i + kPoses) % kPoses];
        };

        if (t <= at(0).t) {
            return at(0).q;  // older than the whole history
        }
        int lo = 0, hi = poseCount_ - 1;  // at(lo).t < t <= at(hi).t
        while (hi - lo > 1) {
            const int mid = (lo + hi) / 2;
            (at(mid).t < t ? lo : hi) = mid;
        }

        const Sample& earlier = at(lo);
        const Sample& after = at(hi);
        const float f = float((t - earlier.t) / std::max(1e-6, after.t - earlier.t));
        return Normalize(Slerp(earlier.q, after.q, f));
    }

    // The pose predicted for the display slot within `tolerance` seconds of `slot`, newest first.
    bool PredictionFor(double slot, double tolerance, Quat* q) const {
        for (int k = 0; k < predictedCount_; k++) {
            const Sample& p = predicted_[(predictedHead_ - 1 - k + kPredictions) % kPredictions];
            if (std::fabs(p.t - slot) < tolerance) {
                *q = p.q;
                return true;
            }
        }

        return false;
    }

    // PredictionFor for `n` slots in increasing order at once, in one pass over the predictions
    // (also in slot order): out[i] is left as it is where none is recorded. Returns how many found.
    int PredictionsFor(const double* slots, int n, double tolerance, Quat* out) const {
        int found = 0, k = 0;  // k: logical index into the predictions, oldest first
        auto at = [&](int i) -> const Sample& {
            return predicted_[(predictedHead_ - predictedCount_ + i + kPredictions) % kPredictions];
        };

        for (int i = 0; i < n; i++) {
            while (k < predictedCount_ && at(k).t < slots[i] - tolerance) {
                k++;
            }
            if (k < predictedCount_ && std::fabs(at(k).t - slots[i]) < tolerance) {
                out[i] = at(k).q, found++;
            }
        }

        return found;
    }

    // The predicted pose at `n` times in increasing order, anywhere on the timeline, in one pass over
    // the predictions: between two slots at most `gap` apart the slerp of theirs; before the first or
    // after the last recorded slot, that slot's own within 1 ms. out[i] is left as it is where none is
    // recorded around it. Returns how many found. (PredictionsFor gives exact slots' own predictions.)
    int PredictionsBetween(const double* t, int n, double gap, Quat* out) const {
        int found = 0, k = 0;  // k: logical index of the first prediction later than t[i]
        auto at = [&](int i) -> const Sample& {
            return predicted_[(predictedHead_ - predictedCount_ + i + kPredictions) % kPredictions];
        };

        for (int i = 0; i < n; i++) {
            while (k < predictedCount_ && at(k).t <= t[i]) {
                k++;
            }
            if (k == 0 || k == predictedCount_) {
                const int j = k == 0 ? 0 : k - 1;
                if (predictedCount_ && std::fabs(at(j).t - t[i]) < 0.001) {
                    out[i] = at(j).q, found++;
                }
                continue;
            }

            const Sample& a = at(k - 1);
            const Sample& b = at(k);
            if (b.t - a.t > gap) {
                continue;
            }
            out[i] = Normalize(Slerp(a.q, b.q, float((t[i] - a.t) / std::max(1e-9, b.t - a.t))));
            found++;
        }

        return found;
    }

private:
    Sample poses_[kPoses];
    int poseCount_ = 0, poseHead_ = 0;
    Sample predicted_[kPredictions];
    int predictedHead_ = 0, predictedCount_ = 0;
};

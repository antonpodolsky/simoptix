// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "pipeline/4_smoothing/lookahead_smoothing.h"

void LookaheadSmoothing::Window(double period, int* behind, int* ahead) const {
    *behind = LookaheadSmoother::Behind(params_, period);
    *ahead = LookaheadSmoother::Ahead(params_, period);
}

Smoothed LookaheadSmoothing::Smooth(const SmoothingInput& in) {
    int behind = 0, ahead = 0;
    Window(in.period, &behind, &ahead);

    const int n = behind + ahead + 1;
    solver_.View(in.heads, behind, ahead, in.period, params_);

    bool steered = false;
    if (in.pathImages) {
        if (int(targets_.size()) < n) {
            targets_.resize(n), weights_.resize(n);
        }
        for (int i = 0; i < n; i++) {
            targets_[i] = in.heads[i], weights_[i] = 1;
        }
        for (int pass = 0; pass < kPlanPasses; pass++) {
            if (!Plan(in, n)) {
                break;
            }
            steered = true;
            solver_.View(in.heads, behind, ahead, in.period, params_, targets_.data(), weights_.data());
        }
    }

    const Quat v = solver_.PathView(behind);

    // The last resort, for what the plan missed (a turn faster than the margin allows, or an image it
    // didn't plan against): stop at the edge. Nothing hidden to pull along: no state between frames.
    const EdgeStop stop = StopAtEdge(in.shown, LevelHorizon(v), in.geometry);
    return {stop.view, steered, stop.stopped, stop.movedDeg};
}

bool LookaheadSmoothing::Plan(const SmoothingInput& in, int n) {
    bool added = false;
    for (int i = 0; i < n; i++) {
        const Quat v = LevelHorizon(solver_.PathView(i));
        const Quat& image = in.pathImages[i];  // the same for every frame whose window holds it
        EdgeRoom r;
        if (Within(image, v, kPlanRoom, in.geometry, &r)) {
            continue;
        }
        targets_[i] = TrailingView(image, r.axis, kPlanRoom * r.room);
        if (weights_[i] == 1) {
            added = true;
        }
        weights_[i] = kPlanWeight;
    }

    return added;
}

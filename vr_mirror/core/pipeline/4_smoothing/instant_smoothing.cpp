// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "pipeline/4_smoothing/instant_smoothing.h"

namespace {

    // Extra smoothing cutoff when the view nears the edge of its room: none up to `start` of the
    // room, then rising quadratically to `maxHz` at the edge. The view keeps being a smoothed
    // signal (tremor and vibration stay filtered), it just catches up faster.
    float CatchUpHz(float lag, float room, float maxHz, float start) {
        if (room <= 0) {
            return maxHz;
        }
        const float x = std::clamp((lag / room - start) / std::max(0.05f, 0.9f - start), 0.0f, 1.0f);
        return maxHz * x * x;
    }

}

Smoothed InstantSmoothing::Smooth(const SmoothingInput& in) {
    // How close the previous view is to running out of room against the new head pose:
    // the closer, the faster the smoothing catches up.
    float extraHz = 0;
    if (in.previous.has) {
        const EdgeRoom r = RoomIn(in.shown, in.previous.view, in.geometry);

        // Head turning speed, smoothed over ~50 ms so vibration and tremor don't count.
        const float speed = AngleBetween(in.previous.followed, in.followed) / in.dt;
        headSpeed_ += (speed - headSpeed_) * (1 - std::exp(-in.dt / 0.05f));

        // The cutoff at which the two smoothing stages trail a turn at this speed by 70% of
        // the room (each first-order stage lags by speed / (2 pi f)). Fast turns get the
        // strong catch-up they need; slow glances and vibration only the gentle one.
        const float neededHz = r.room > 0 ? 2 * headSpeed_ / (2 * 3.14159265f * 0.7f * r.room) : kCatchUpHz;
        extraHz = CatchUpHz(r.lag, r.room, std::max(kCatchUpHz, neededHz), kCatchUpStart);
    }

    const float deg = 3.14159265f / 180.0f;
    const Quat view = filter_.Update(
        in.followed,
        in.dt,
        kMinCutoffHz,
        kBeta,
        kDeadzoneDeg * deg,
        kMaxAngleDeg * deg,
        extraHz
    );

    // The last resort, when the catch-up wasn't enough (an extreme head flick): stop at the edge, and
    // pull the filter's state along so it keeps no hidden lag behind the stop.
    const EdgeStop stop = StopAtEdge(in.shown, LevelHorizon(view), in.geometry);
    if (stop.keep < 1) {
        filter_.Constrain(in.shown, stop.keep);
    }
    return {stop.view, false, stop.stopped, stop.movedDeg};
}

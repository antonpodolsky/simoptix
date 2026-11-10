// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// InstantSmoothing: the view with no delay (setting smoothing 1, for live streaming): a causal
// One-Euro style filter on orientation (OrientationFilter, below) that follows the drawn pose, and
// catches up faster as the previous view runs out of room in the image (image_room), scaled to
// the head's turning speed (kCatchUpHz, kCatchUpStart). Its last resort, when that isn't enough:
// the view stopped at the edge of the frame's images (StopAtEdge), and the filter's state pulled
// along at once, so it can't keep hidden lag behind the stop.
//
// Logic only. Knows nothing about the room's geometry, frames or PVR.
#pragma once

#include <algorithm>
#include <cmath>

#include "common/rot.h"
#include "pipeline/4_smoothing/smoothing.h"
#include "pipeline/image_room/image_room.h"

// One-Euro style filter on orientation: heavy smoothing when the head is nearly still,
// following quickly during deliberate head turns. minCutoffHz sets the baseline
// smoothing (lower = smoother), beta how fast the cutoff rises with angular speed.
// Two cascaded stages roll off small fast motion (heartbeat, breathing) much more
// steeply than one, and a soft dead zone holds the view while the head only trembles.
// maxLagRad limits how far the view may trail the head. The limit is applied to the
// filter's own state with a soft knee, so a fast turn never builds up hidden lag that
// shows up later as the view suddenly slowing down and then catching up.
class OrientationFilter {
public:
    // extraCutoffHz raises the cutoff on top of the speed-based one, e.g. to catch up faster
    // when the view is about to run out of room; the output stays a smoothed signal.
    Quat Update(
        const Quat& raw,
        float dt,
        float minCutoffHz,
        float beta,
        float deadzoneRad,
        float maxLagRad,
        float extraCutoffHz = 0
    ) {
        if (!init_ || dt <= 0) {
            init_ = true;
            prevRaw_ = stage1_ = filtered_ = raw;
            speed_ = 0;
            return filtered_;
        }

        const float rawSpeed = AngleBetween(prevRaw_, raw) / dt;
        speed_ += Alpha(dt, 1.0f) * (rawSpeed - speed_);
        prevRaw_ = raw;
        const float a = Alpha(dt, minCutoffHz + beta * speed_ + extraCutoffHz);

        // Soft dead zone: barely follow while the head stays within deadzoneRad of the view.
        float follow = a;
        if (deadzoneRad > 0) {
            const float x = std::min(1.0f, AngleBetween(stage1_, raw) / deadzoneRad);
            follow *= x * x * (3 - 2 * x);
        }

        stage1_ = LimitLag(raw, Normalize(Slerp(stage1_, raw, follow)), maxLagRad);
        filtered_ = LimitLag(raw, Normalize(Slerp(filtered_, stage1_, a)), maxLagRad);
        return filtered_;
    }

    void Reset() { init_ = false; }

    // Pulls the smoothed state toward `raw`, keeping fraction `keep` (0..1) of its lag: used when
    // the view must not trail the head that far, so the filter doesn't keep the excess hidden.
    void Constrain(const Quat& raw, float keep) {
        if (!init_) {
            return;
        }
        stage1_ = Normalize(Slerp(raw, stage1_, keep));
        filtered_ = Normalize(Slerp(raw, filtered_, keep));
    }

private:
    // Lag up to 60% of the limit passes unchanged; beyond that it is compressed smoothly
    // so it approaches, but never exceeds, maxLag, without a sudden change in speed.
    static Quat LimitLag(const Quat& raw, const Quat& q, float maxLag) {
        const float lag = AngleBetween(raw, q);
        const float knee = 0.6f * maxLag;
        if (maxLag <= 0 || lag <= knee) {
            return q;
        }
        const float allowed = knee + (maxLag - knee) * std::tanh((lag - knee) / (maxLag - knee));
        return Normalize(Slerp(raw, q, allowed / lag));
    }

    static float Alpha(float dt, float cutoffHz) {
        const float tau = 1.0f / (2.0f * 3.14159265f * cutoffHz);
        return 1.0f / (1.0f + tau / dt);
    }

    bool init_ = false;
    Quat prevRaw_, stage1_, filtered_;
    float speed_ = 0;
};

class InstantSmoothing : public Smoothing {
public:
    // The filter's tuning, fixed. The output is always level.
    static constexpr float kMinCutoffHz = 0.5f;  // the filter's cutoff while still (Hz): lower = smoother
    // how fast the cutoff rises with the head's speed (Hz per rad/s)
    static constexpr float kBeta = 2.0f;
    static constexpr float kDeadzoneDeg = 0.5f;   // tremor below this is held (degrees)
    static constexpr float kMaxAngleDeg = 20;     // the most the view trails the image (degrees)
    static constexpr float kCatchUpHz = 6;        // extra cutoff as the view runs out of room (Hz)...
    static constexpr float kCatchUpStart = 0.4f;  // ...from this share of the room

    void Reset() override { filter_.Reset(); }  // not headSpeed_
    double DelayS() const override { return 0; }
    void Window(double, int* behind, int* ahead) const override { *behind = *ahead = 0; }
    Smoothed Smooth(const SmoothingInput& in) override;

private:
    OrientationFilter filter_;
    float headSpeed_ = 0;  // smoothed head turning speed (rad/s)
};

// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "pipeline/2_drawn_pose/slot_check_drawn_pose.h"

ImageNeeds SlotCheckDrawnPose::Needs() const {
    ImageNeeds n;
    n.thumbnail = true;
    return n;
}

void SlotCheckDrawnPose::Reset() {
    prev_.reserve(size_t(64) * 32 * 3);  // a thumbnail (Needs); copied into without reallocating
    hasPrev_ = false;
    runUpdated_ = false;
    gate_.Reset();
    for (Gate& g : gates_) {
        g = {};
    }
    std::lock_guard<std::mutex> lock(mutex_);
    filter_.Reset();
    lag_ = 0;
    for (Decision& d : decisions_) {
        d = {};
    }
}

int SlotCheckDrawnPose::Arrived(
    const GameFrame& f,
    const PoseLookup&,
    const EyeInfo&,
    double period,
    float (*)[4]
) {
    gates_[f.serial % kGates] = {f.serial, gate_.Update(f.time, f.slotLead, period)};
    return 0;
}

void SlotCheckDrawnPose::Look(
    const GameFrame& f,
    const std::vector<float>& thumb,
    const std::vector<float>&,
    const PoseLookup& poses,
    const EyeInfo& eye,
    double period
) {
    const Gate& g = gates_[f.serial % kGates];
    const bool gateOpen = g.serial == f.serial && g.open;

    if (!gateOpen) {
        // Timing undisturbed: the frame keeps its slot; start over from lag 0 next time.
        std::lock_guard<std::mutex> lock(mutex_);
        filter_.Reset();
        runUpdated_ = false;
        lag_ = 0;
    } else if (hasPrev_ && f.serial == prevFrame_.serial + 1 && f.time - prevFrame_.time < 0.1) {
        // Consecutive frames only (a skipped serial means a frame was never copied).
        ThumbView view;
        view.left = eye.leftTan - kThumbArea[0] * (eye.leftTan + eye.rightTan);
        view.right = -eye.leftTan + kThumbArea[2] * (eye.leftTan + eye.rightTan);
        view.up = eye.upTan - kThumbArea[1] * (eye.upTan + eye.downTan);
        view.down = -eye.upTan + kThumbArea[3] * (eye.upTan + eye.downTan);
        view.cant = eye.hmdToEye;

        Quat prevPose[LagFilter::kLags], curPose[LagFilter::kLags];
        bool ok = true;
        for (int i = 0; i < LagFilter::kLags && ok; i++) {
            ok = poses.PredictedHeadAt(prevFrame_.slotTime - i * period, &prevPose[i])
                 && poses.PredictedHeadAt(f.slotTime - i * period, &curPose[i]);
        }

        const float sensitivity = ok ? RotationSensitivity(thumb, curPose[0], view) : 0;
        if (sensitivity > 0) {
            float step[LagFilter::kLags][LagFilter::kLags];
            for (int a = 0; a < LagFilter::kLags && ok; a++) {
                for (int b = 0; b < LagFilter::kLags && ok; b++) {
                    const float e = ReprojectionError(prev_, prevPose[a], thumb, curPose[b], view);
                    ok = e >= 0;
                    step[a][b] = e / sensitivity;  // square degrees
                }
            }

            if (ok) {
                std::lock_guard<std::mutex> lock(mutex_);
                lag_ = filter_.Update(step);
                runUpdated_ = true;
                statDecided_++;
                statLate_ += lag_ != 0;
            }
        }
    }

    {
        // A frame the filter couldn't judge keeps the lag of the last frame it did (as lag_ does).
        std::lock_guard<std::mutex> lock(mutex_);
        decisions_[f.serial % kDecisions] = {
            f.serial,
            lag_,
            runUpdated_ ? int64_t(filter_.Updates()) - 1 : int64_t(-1)
        };
    }

    prev_.assign(thumb.begin(), thumb.end());
    prevFrame_ = f;
    hasPrev_ = true;
}

bool SlotCheckDrawnPose::HindsightLocked(uint64_t serial, int* lag) const {
    const Decision& d = decisions_[serial % kDecisions];
    if (d.serial != serial || serial == 0) {
        return false;
    }
    if (d.update < 0 || !filter_.HindsightLag(uint64_t(d.update), lag)) {
        *lag = d.lag;
    }
    return true;
}

Quat SlotCheckDrawnPose::PoseOf(const FrameTimes& f, PoseSource& poses) const {
    int lag = 0;
    {
        // Its own slot in hindsight once judged; until then (drawn before its thumbnail was read) the
        // slot judged last.
        std::lock_guard<std::mutex> lock(mutex_);
        if (!HindsightLocked(f.serial, &lag)) {
            lag = lag_;
        }
    }

    return PredictedPose(f, lag, {}, poses);
}

bool SlotCheckDrawnPose::SlotsBeforeOf(uint64_t serial, int* slots) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return HindsightLocked(serial, slots);
}

bool SlotCheckDrawnPose::OwnSlotsBeforeOf(uint64_t serial, int* slots) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const Decision& d = decisions_[serial % kDecisions];
    if (d.serial != serial || serial == 0) {
        return false;
    }
    *slots = d.lag;
    return true;
}

int SlotCheckDrawnPose::LastSlotsBefore() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lag_;
}

void SlotCheckDrawnPose::Describe(const FrameTimes& f, StatsReport& r) const {
    int used = 0, own = 0;
    bool judged = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        judged = HindsightLocked(f.serial, &used);
        if (judged) {
            own = decisions_[f.serial % kDecisions].lag;
        } else {
            used = lag_;
        }
    }

    r.Line("slot check");
    r.Value("slots before arrival", used);  // the slot its pose is for, counted back from its arrival slot
    r.Value("judged", judged ? 1 : 0);      // 1: its own (in hindsight); 0: not yet looked at, the last one's
    if (judged) {
        r.Value("own judgement", own);  // from the frames up to it, before hindsight
    }
}

void SlotCheckDrawnPose::Report(StatsReport& r) {
    int late = 0, judged = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        late = statLate_, judged = statDecided_;
        statLate_ = statDecided_ = 0;
    }

    if (judged == 0) {
        return;
    }
    r.Line("game behind");
    r.Value("frames rendered for an earlier slot (warped from that slot's pose)", late);
    r.Value("frames judged by image", judged);
}

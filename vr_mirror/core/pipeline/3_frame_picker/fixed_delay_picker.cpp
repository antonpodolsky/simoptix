// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "pipeline/3_frame_picker/fixed_delay_picker.h"

#include <algorithm>
#include <cmath>

namespace {

    // The frames of a display `period` that can arrive before the host's next tick, `tickPeriod`
    // apart (0 if the host has no regular ticks, or ticks at least as often as the display).
    int BetweenTicksSlots(double period, double tickPeriod) {
        return tickPeriod > period ? int(std::ceil(tickPeriod / period)) : 0;
    }

}

int WarmupSlots(double smoothingDelayS, double period, double tickPeriod, int maxSlots) {
    return std::clamp(
        int(std::ceil((0.045 + smoothingDelayS) / period)) + BetweenTicksSlots(period, tickPeriod) + 3,
        4,
        maxSlots
    );
}

void FixedDelayPicker::Restart(double smoothingDelayS, double period, double tickPeriod, int maxSlots) {
    smoothingDelay_ = smoothingDelayS;
    maxSlots_ = std::min(maxSlots, kMaxOrder);
    copyDelay_ = warmStart_ = 0;
    delays_.clear();
    delays_.reserve(32768);  // 2 s of warm-up at up to 144 ticks/s over up to ~110 stored frames
    lastShown_ = 0;
    lastSlot_ = -1;
    lastTick_ = 0;
    tickPeriod_ = tickPeriod;
    slots_ = WarmupSlots(smoothingDelayS, period, tickPeriod, maxSlots_);
    fixedPending_ = false;
    ahead_.Reset();
}

bool FixedDelayPicker::WillShow(const StoredFrame& frame, const StoredFrame& next) {
    ahead_.Add(frame.serial, frame.smoothTime);

    if (copyDelay_ == 0 || tickPeriod_ <= 0 || lastTick_ == 0) {
        return true;
    }

    if (!ahead_.Started() && !ahead_.Start(lastShown_, lastTick_, tickPeriod_, OutDelay())) {
        return true;
    }

    return ahead_.WillShowNewest(next.smoothTime);
}

void FixedDelayPicker::Report(StatsReport& r) {
    if (!fixedPending_) {
        return;
    }
    fixedPending_ = false;
    r.Line("copy delay fixed");
    r.Value("ms", copyDelay_ * 1000, 1);
    r.Value("ms copy ready p95", fixedP95_ * 1000, 1);
}

void FixedDelayPicker::WarmUp(double tick, double period, const StoredFrame* frames, int total) {
    // The OpenXR source learns the display's period from the layer, which can come after Restart.
    slots_ = WarmupSlots(smoothingDelay_, period, tickPeriod_, maxSlots_);

    for (int i = 0; i < total; i++) {
        if (frames[i].valid) {
            delays_.push_back(frames[i].ready - frames[i].arrival);
        }
    }

    if (warmStart_ == 0) {
        warmStart_ = tick;
    }
    if (tick - warmStart_ > 2.0 && delays_.size() > 50) {
        std::sort(delays_.begin(), delays_.end());
        const double p95 = delays_[size_t(delays_.size() * 0.95)];
        copyDelay_ = std::min(0.045, std::max(0.004, p95 + 0.002));
        fixedP95_ = p95;
        fixedPending_ = true;

        // The store needs the frames inside the delay, the frames that can arrive before the host's
        // next tick, and room for a late copy: entries = frames in the delay + frames between ticks
        // + 3, at least 4.
        slots_ = std::clamp(
            int(std::ceil((copyDelay_ + smoothingDelay_) / period)) + BetweenTicksSlots(period, tickPeriod_)
                + 3,
            4,
            maxSlots_
        );
        delays_.clear();
    }
}

Pick FixedDelayPicker::Tick(double tick, double period, const StoredFrame* frames, int total) {
    // The delay from a game frame's arrival to its output: fixed after a short warm-up (every
    // change would show a frame twice or skip one), long enough that the copy is nearly always done.
    if (copyDelay_ == 0) {
        WarmUp(tick, period, frames, total);
    }

    ahead_.Follow(tick);

    const double outDelay = (copyDelay_ > 0 ? copyDelay_ : 0.040) + smoothingDelay_;

    // Ready entries in game-frame order.
    int count = 0;
    for (int i = 0; i < slots_ && i < total; i++) {  // not the entries being freed (SlotsInUse shrank)
        if (frames[i].valid) {
            order_[count++] = i;
        }
    }

    std::sort(order_, order_ + count, [&](int a, int b) { return frames[a].serial < frames[b].serial; });

    auto due = [&](int i) { return frames[i].smoothTime + outDelay; };
    int shown = -1;
    for (int k = 0; k < count; k++) {
        due_[k] = due(order_[k]);
        if (frames[order_[k]].serial == lastShown_) {
            shown = k;
        }
    }

    Pick p;
    p.moment = tick - outDelay;
    int chosen = -1;  // entry

    if (shown >= 0) {
        const int pick = FixedDelayStep(shown, due_, count, tick);
        p.kind = pick == shown ? PickKind::kRepeat : pick > shown + 1 ? PickKind::kJump : PickKind::kNext;
        chosen = order_[pick];
    } else {
        for (int k = 0; k < count; k++) {
            if (due(order_[k]) <= tick) {
                chosen = order_[k];
            }
        }

        p.kind = PickKind::kNext;
    }

    if (chosen < 0) {
        // Nothing due is ready (a copy running late): show the last image again, re-projected to
        // this tick's head pose, rather than an empty frame.
        if (lastSlot_ >= 0 && lastSlot_ < slots_ && lastSlot_ < total && frames[lastSlot_].valid
            && frames[lastSlot_].serial == lastShown_) {
            chosen = lastSlot_;
            p.kind = PickKind::kHeld;
        } else {
            p.kind = PickKind::kNone;
            return p;
        }
    }

    lastShown_ = frames[chosen].serial;
    lastSlot_ = chosen;
    lastTick_ = tick;
    p.index = chosen;
    p.due = due(chosen);
    return p;
}

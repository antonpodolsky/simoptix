// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "pipeline/3_frame_picker/tick_plan_picker.h"

#include <algorithm>
#include <cmath>

namespace {

    // The grid's tick `k`. Tick and WillShow both reach the grid's times through it, so a frame's due
    // time compares the same with a tick in both.
    double GridTickAt(double start, double period, int64_t k) {
        return start + static_cast<double>(k) * period;
    }

}

void TickPlanPicker::Restart(double smoothingDelayS, double period, double tickPeriod, int maxSlots) {
    delay_.Restart(smoothingDelayS, period, tickPeriod, maxSlots);
    tickPeriod_ = tickPeriod;
    gridStart_ = 0;
    lastShown_ = 0;
}

Pick TickPlanPicker::Tick(double tick, double period, const StoredFrame* frames, int total) {
    if (!delay_.DelayFixed() || tickPeriod_ <= 0) {
        const Pick p = delay_.Tick(tick, period, frames, total);

        if (p.index >= 0) {
            lastShown_ = frames[p.index].serial;
        }

        return p;
    }

    return Planned(GridTick(tick), tick, frames, total);
}

bool TickPlanPicker::WillShow(const StoredFrame& frame, const StoredFrame& next) const {
    if (!Planning()) {
        return true;
    }

    const double from = Due(frame);
    const double to = Due(next);

    // The first grid tick at or after `from`, found on the grid's own times (rounding can put the
    // quotient's ceiling one tick off).
    auto k = static_cast<int64_t>(std::ceil((from - gridStart_) / tickPeriod_));
    while (GridTickAt(gridStart_, tickPeriod_, k - 1) >= from) {
        k--;
    }
    while (GridTickAt(gridStart_, tickPeriod_, k) < from) {
        k++;
    }

    return GridTickAt(gridStart_, tickPeriod_, k) < to;
}

double TickPlanPicker::Due(const StoredFrame& frame) const {
    return frame.smoothTime + OutDelay();
}

double TickPlanPicker::GridTick(double tick) {
    if (gridStart_ > 0) {
        const auto k = static_cast<int64_t>(std::llround((tick - gridStart_) / tickPeriod_));
        const double gridTick = GridTickAt(gridStart_, tickPeriod_, k);

        if (std::abs(tick - gridTick) <= 0.25 * tickPeriod_) {
            return gridTick;
        }
    }

    gridStart_ = tick;
    return tick;
}

Pick TickPlanPicker::Planned(double gridTick, double tick, const StoredFrame* frames, int total) {
    const double outDelay = OutDelay();
    const int inUse = std::min(SlotsInUse(), total);

    // The newest ready frame due by this tick, never one older than the last shown. When the next
    // one's copy is late, that is the last shown again.
    int chosen = -1;
    for (int i = 0; i < inUse; i++) {
        const StoredFrame& f = frames[i];

        if (!f.valid || f.serial < lastShown_ || Due(f) > gridTick) {
            continue;
        }

        if (chosen < 0 || f.serial > frames[chosen].serial) {
            chosen = i;
        }
    }

    Pick p;
    p.moment = tick - outDelay;

    if (chosen < 0) {
        return p;
    }

    const uint64_t serial = frames[chosen].serial;
    p.kind = serial == lastShown_                          ? PickKind::kRepeat
             : lastShown_ == 0 || serial == lastShown_ + 1 ? PickKind::kNext
                                                           : PickKind::kJump;
    lastShown_ = serial;
    p.index = chosen;
    p.due = frames[chosen].smoothTime + outDelay;
    return p;
}

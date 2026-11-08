// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "pipeline/1_arrival_timing/grid_arrival_timing.h"

#include <algorithm>
#include <cmath>

ArrivalResult GridArrivalTiming::Add(const ArrivalInput& in) {
    const double arrival = in.arrival, currentSlot = in.nextSlot, period = in.period;

    offsets_[head_] = currentSlot - arrival;
    head_ = (head_ + 1) % kOffsets;
    count_ = std::min(count_ + 1, kOffsets);

    double sorted[kOffsets];
    std::copy(offsets_, offsets_ + count_, sorted);
    std::nth_element(sorted, sorted + count_ / 2, sorted + count_);

    ArrivalResult r;
    r.offset = sorted[count_ / 2];
    const double x = (arrival + r.offset - currentSlot) / period;  // display periods from currentSlot
    const double n = std::round(x);
    r.slot = currentSlot + n * period;
    double position = x - n;  // display periods from the slot it gets

    // Well before its nearest slot, the slot before still free and the frame before on time: a single
    // frame running late for the free slot (not one in a queue: the drawn pose stage judges those).
    if (position < -kLateBefore && r.slot - period > lastSlot_ + 0.5 * period
        && std::fabs(lastPosition_) < kOnTime) {
        r.slot -= period;
        position += 1;
    }

    r.smooth = r.slot - r.offset;
    lastSlot_ = r.slot;
    lastPosition_ = position;
    return r;
}

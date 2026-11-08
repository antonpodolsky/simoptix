// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// ClockSync: the offset between a frame source's clock (Pimax's runtime clock) and the performance
// counter (the clock window capture and OBS use), from back-to-back readings. The runtime call can take a
// while or be preempted, so a reading only counts when its two counter reads were about as close together as
// the quickest pair seen lately.
#pragma once

#include <algorithm>

class ClockSync {
public:
    // pvrNow read between counter readings q0 and q1 (seconds).
    void Sample(double pvrNow, double q0, double q1) {
        const double gap = q1 - q0;
        best_ = best_ < 0 ? gap : std::min(best_ * 1.002 + 1e-7, gap);  // relaxes slowly over time
        if (gap > best_ * 1.5 + 0.00005) {
            return;
        }
        const double off = pvrNow - 0.5 * (q0 + q1);
        offset_ = has_ ? offset_ + 0.05 * (off - offset_) : off;
        has_ = true;
    }

    bool Valid() const { return has_; }
    // source time minus performance-counter time (seconds).
    double Offset() const { return offset_; }
    void Reset() {
        best_ = -1;
        offset_ = 0;
        has_ = false;
    }

private:
    double best_ = -1, offset_ = 0;
    bool has_ = false;
};

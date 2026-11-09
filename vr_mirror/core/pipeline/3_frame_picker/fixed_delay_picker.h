// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// FixedDelayPicker: the OBS plugin's picking. Each frame is shown a fixed delay after its
// de-jittered arrival: the copy delay, measured over a short warm-up (the 95th percentile of copy
// times plus 2 ms, 4-45 ms; 40 ms until then) and then kept (every change would show a frame twice or
// skip one), plus the smoothing's delay. One frame per tick, in order: the next one unless it is
// clearly not due yet (4 ms of slack: the clocks slipped, show the current one again), catching up
// only when two are clearly overdue; when nothing due is ready, the last image again. Logic only;
// its warm-up buffer is reserved at Restart. Knows nothing about textures or OBS.
#pragma once

#include <cstdint>
#include <vector>

#include "pipeline/3_frame_picker/frame_picker.h"

// Store entries while the delay is being measured (allowing 45 ms for the copy), before the
// display's period is known (90 Hz assumed).
int WarmupSlots(double smoothingDelayS, int maxSlots);

class FixedDelayPicker : public FramePicker {
public:
    void Restart(double smoothingDelayS, int maxSlots) override;
    Pick Tick(double tick, double period, const StoredFrame* frames, int total) override;
    double OutDelay() const override { return (copyDelay_ > 0 ? copyDelay_ : 0.040) + smoothingDelay_; }
    int SlotsInUse() const override { return slots_; }
    // Once, after the delay is fixed: "copy delay fixed", the copy delay chosen and the copy times'
    // 95th percentile it was chosen from.
    void Report(StatsReport& r) override;

private:
    // Collects the copy delays seen this tick and, once the warm-up is over, fixes the delay and the
    // store's size.
    void WarmUp(double tick, double period, const StoredFrame* frames, int total);

    static constexpr int kMaxOrder = 256;  // store entries ordered per tick (FrameRing has 128)

    double smoothingDelay_ = 0;
    int maxSlots_ = 128;
    double copyDelay_ = 0, warmStart_ = 0;  // the copy delay (0 until fixed), when the warm-up began
    std::vector<double> delays_;            // copy ready minus arrival, during warm-up
    int slots_ = 128;
    uint64_t lastShown_ = 0;  // the frame shown at the previous tick
    int lastSlot_ = -1;       // ...and its store entry
    bool fixedPending_ = false;
    double fixedP95_ = 0;
    int order_[kMaxOrder] = {};  // scratch: entries in use, in frame order
};

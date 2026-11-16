// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// FixedDelayPicker: the OBS plugin's picking. Each frame is shown a fixed delay after its
// de-jittered arrival: the copy delay, measured over a short warm-up (the 95th percentile of copy
// times plus 2 ms, 4-45 ms; 40 ms until then) and then kept (every change would show a frame twice or
// skip one), plus the smoothing's delay. One frame per tick, in order: the next one unless it is
// clearly not due yet (4 ms of slack: the clocks slipped, show the current one again), catching up
// only when two are clearly overdue (FixedDelayStep); when nothing due is ready, the last image
// again. Once the delay is fixed, the same rule run ahead on the host's ticks says whether a frame
// will be shown (its forecast, FixedDelayAhead, fed with Record). The store needs the frames within
// the delay, plus the frames that can arrive before the next tick (the host's tick period can be
// slower than the display's), plus 3 for a late copy. A host that copies only the frames the forecast
// says a tick shows needs room only for those, at most one a tick, once the frames copied before the
// forecast began are past. Logic only; its warm-up buffer is reserved at Restart. Knows nothing about
// textures or OBS.
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "pipeline/3_frame_picker/frame_picker.h"

// Store entries while the delay is being measured: the frames of a display `period` (s) inside
// the smoothing's delay plus 45 ms for the copy, the frames that can arrive before the host's next
// tick (`tickPeriod`, 0: none counted), and 3 more.
int WarmupSlots(double smoothingDelayS, double period, double tickPeriod, int maxSlots);

class FixedDelayPicker : public FramePicker {
public:
    void Restart(
        double smoothingDelayS,
        double period,
        double tickPeriod,
        int maxSlots,
        bool copiesShownOnly
    ) override;
    Pick Tick(double tick, double period, const StoredFrame* frames, int total) override;
    double OutDelay() const override { return (copyDelay_ > 0 ? copyDelay_ : 0.040) + smoothingDelay_; }
    int SlotsInUse() const override { return slots_; }
    std::unique_ptr<ShowForecast> MakeForecast() const override;
    TickRecord Record() const override;
    // Once, after the delay is fixed: "copy delay fixed", the copy delay chosen and the copy times'
    // 95th percentile it was chosen from.
    void Report(StatsReport& r) override;

private:
    // Collects the copy delays seen this tick and, once the warm-up is over, fixes the delay and the
    // store's size.
    void WarmUp(double tick, double period, const StoredFrame* frames, int total);

    static constexpr int kMaxOrder = 256;  // store entries ordered per tick (FrameRing has 128)
    // The copies measured, by serial: over the frames a store can span (half a second at 400 Hz).
    static constexpr int kMeasured = 512;

    double smoothingDelay_ = 0;
    int maxSlots_ = 128;
    double copyDelay_ = 0, warmStart_ = 0;  // the copy delay (0 until fixed), when the warm-up began
    std::vector<double> delays_;            // copy ready minus arrival, during warm-up
    uint64_t firstSerial_ = 0;              // the first frame the warm-up measures
    uint64_t measured_[kMeasured] = {};     // each copy measured, at its serial % kMeasured
    int slots_ = 128;
    uint64_t lastShown_ = 0;  // the frame shown at the previous tick
    int lastSlot_ = -1;       // ...and its store entry
    double lastTick_ = 0;     // ...and that tick (0: none since Restart)
    double period_ = 0;       // the display's, at the last tick
    bool fixedPending_ = false;
    double fixedP95_ = 0;
    double tickPeriod_ = 0;  // the host's (0: no regular ticks, nothing run ahead)
    uint32_t restarts_ = 0;
    bool copiesShownOnly_ = false;  // the host copies only the frames the forecast says a tick shows
    double shownOnlyFrom_ = 0;      // the tick from which the store holds only those (0: not set)
    int order_[kMaxOrder] = {};     // scratch: entries in use, in frame order
    double due_[kMaxOrder] = {};    // scratch: ...and their due times
};

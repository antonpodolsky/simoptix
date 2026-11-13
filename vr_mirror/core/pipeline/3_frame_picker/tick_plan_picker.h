// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// TickPlanPicker: the OBS plugin's picking planned ahead on OBS's ticks. Its delay is the fixed
// delay's (FixedDelayPicker measures the copy delay over its warm-up and picks until then). Once the
// delay is fixed, OBS's ticks lie on a grid: the first tick after that, plus whole output frame
// periods (OBS schedules its video frames so, to within microseconds: findings\pipeline.md). Each
// tick shows the newest frame due by its grid time, never one older than the last shown. So a frame
// is shown if a grid tick falls between its due time and the next frame's, which WillShow tells
// once the next frame has arrived. Logic only. Knows nothing about textures or OBS.
#pragma once

#include <cstdint>

#include "pipeline/3_frame_picker/fixed_delay_picker.h"
#include "pipeline/3_frame_picker/frame_picker.h"

class TickPlanPicker : public FramePicker {
public:
    void Restart(double smoothingDelayS, double period, double tickPeriod, int maxSlots) override;
    Pick Tick(double tick, double period, const StoredFrame* frames, int total) override;
    double OutDelay() const override { return delay_.OutDelay(); }
    int SlotsInUse() const override { return delay_.SlotsInUse(); }
    bool WillShow(const StoredFrame& frame, const StoredFrame& next) const override;
    void Report(StatsReport& r) override { delay_.Report(r); }

private:
    bool Planning() const { return delay_.DelayFixed() && tickPeriod_ > 0 && gridStart_ > 0; }
    // When `frame` is due (source s): the first tick from then may show it.
    double Due(const StoredFrame& frame) const;
    // The grid tick nearest `tick`; a tick farther off than a quarter period starts the grid again
    // there (OBS's frame rate changed).
    double GridTick(double tick);
    Pick Planned(double gridTick, double tick, const StoredFrame* frames, int total);

    FixedDelayPicker delay_;
    double tickPeriod_ = 0;   // OBS's frame period (s); 0: no plan
    double gridStart_ = 0;    // the grid's first tick (0: none yet)
    uint64_t lastShown_ = 0;  // the frame shown at the previous tick
};

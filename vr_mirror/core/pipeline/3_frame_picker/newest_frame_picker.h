// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// NewestFramePicker: the mirror app's picking with Instant smoothing: the newest game frame as soon as
// it is new (a new serial; without a frame signal, at most 60 times a second), due a fixed delay
// after its de-jittered arrival (the window's present delay, so presents are evenly spaced; not
// held if that is implausibly far ahead, a clock glitch). The store is one entry: the newest frame.
// Logic only. Knows nothing about textures or windows.
#pragma once

#include <cstdint>

#include "pipeline/3_frame_picker/frame_picker.h"

class NewestFramePicker : public FramePicker {
public:
    void Restart(double delayS, int maxSlots) override;
    Pick Tick(double tick, double period, const StoredFrame* frames, int total) override;
    double OutDelay() const override { return delay_; }
    int SlotsInUse() const override { return maxSlots_; }

private:
    double delay_ = 0;
    int maxSlots_ = 1;
    uint64_t lastSerial_ = 0;  // the frame shown last
    double lastShown_ = 0;     // ...and when (source s)
};

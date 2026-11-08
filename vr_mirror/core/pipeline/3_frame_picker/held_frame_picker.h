// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// HeldFramePicker: the mirror app's picking with look-ahead smoothing (Smooth, Smoothest): each held
// frame is shown once its view can be computed, a fixed delay after its de-jittered arrival (the
// smoothing's delay plus the window's present delay), picked 4 ms ahead so the host can wait and
// present at that moment. The store is the held frames in arrival order, then the newest frame; when
// that has no frame signal (unsynced), it is shown at once instead, at most 60 times a second.
// Logic only. Knows nothing about textures or windows.
#pragma once

#include "pipeline/3_frame_picker/frame_picker.h"

class HeldFramePicker : public FramePicker {
public:
    void Restart(double delayS, int maxSlots) override;
    Pick Tick(double tick, double period, const StoredFrame* frames, int total) override;
    double OutDelay() const override { return delay_; }
    int SlotsInUse() const override { return maxSlots_; }

private:
    double delay_ = 0;
    int maxSlots_ = 1;
    double lastShown_ = 0;  // when a frame was last shown (source s)
};

// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "pipeline/3_frame_picker/newest_frame_picker.h"

void NewestFramePicker::Restart(double delayS, int maxSlots) {
    delay_ = delayS;
    maxSlots_ = maxSlots;
    lastSerial_ = 0;
    lastShown_ = 0;
}

Pick NewestFramePicker::Tick(double tick, double, const StoredFrame* frames, int total) {
    Pick p;
    if (total <= 0) {
        return p;
    }
    const StoredFrame& f = frames[total - 1];
    const bool isNew = f.synced ? f.serial != lastSerial_ : tick - lastShown_ > 1.0 / 60;
    if (!isNew) {
        return p;
    }

    // Paced present: the de-jittered arrival time is locked to the headset's 90 Hz grid, so
    // presenting a fixed delay after it gives evenly spaced presents.
    if (f.synced && f.smoothTime > 0 && delay_ > 0) {
        p.due = f.smoothTime + delay_;
        if (p.due > tick + 0.015) {
            p.due = 0;  // implausible (clock glitch): don't hold the frame
        }
    }

    lastSerial_ = f.serial;
    lastShown_ = tick;
    p.index = total - 1;
    p.kind = PickKind::kNext;
    p.moment = f.smoothTime;
    return p;
}

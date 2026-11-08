// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "pipeline/3_frame_picker/held_frame_picker.h"

void HeldFramePicker::Restart(double delayS, int maxSlots) {
    delay_ = delayS;
    maxSlots_ = maxSlots;
    lastShown_ = 0;
}

Pick HeldFramePicker::Tick(double tick, double, const StoredFrame* frames, int total) {
    Pick p;
    if (total <= 0) {
        return p;
    }
    const StoredFrame& newest = frames[total - 1];
    if (!newest.synced) {
        // No frame signal: no arrival times to hold frames to; the newest is shown at once.
        if (tick - lastShown_ <= 1.0 / 60) {
            return p;
        }
        lastShown_ = tick;
        p.index = total - 1;
        p.kind = PickKind::kNext;
        p.moment = newest.arrival;
        return p;
    }

    if (total < 2) {
        return p;  // nothing held yet
    }

    // The oldest held frame, once its view can be computed: picked a little ahead, so the host can
    // wait precisely and present at `due`.
    const StoredFrame& front = frames[0];
    const double due = front.smoothTime + delay_;
    if (tick < due - 0.004) {
        return p;
    }
    lastShown_ = tick;
    p.index = 0;
    p.kind = PickKind::kNext;
    p.moment = front.smoothTime;
    p.due = due;
    return p;
}

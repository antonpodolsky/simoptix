// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// PreciseWait: the main thread's waits on a high-resolution waitable timer, which keeps its
// precision when Windows throttles a background window's timer resolution (Sleep(1) becomes ~15 ms
// then): a wait until a source time (the timer for most of it, then a short spin, so a present lands
// within a few tens of microseconds) and a short nap. Knows nothing about frames, drawing or the pipeline.
// Main thread.
#pragma once

#include <windows.h>

#include "adapters/d3d/frame_source.h"
#include "shared/unique_handle.h"

class PreciseWait {
public:
    PreciseWait();
    PreciseWait(const PreciseWait&) = delete;
    PreciseWait& operator=(const PreciseWait&) = delete;

    // Until time `t` on `clock` (the frame source's).
    void Until(const FrameSource& clock, double t);
    // About `seconds` (sub-millisecond).
    void Nap(double seconds);

private:
    UniqueHandle timer_;
};

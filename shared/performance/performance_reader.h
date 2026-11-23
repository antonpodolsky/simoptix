// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// PerformanceReader: the game's frame rate and CPU and GPU headroom for a plugin, read from what the
// OpenXR layer shares (performance_share.h) and worked out by performance_reading.h. Each call signs that
// a reader is there, which is what makes the layer time the game's frames; a reader that stops calling
// stops the timing 1.5 s later. A new reading comes once a second, from the second that ended 0.1 s before (a
// frame's GPU time comes in a few frames after it ended). A second the layer didn't time all of (the first
// after the reader came, or the game's first) is unknown.
//
// Without the layer (no game, the Pimax source alone, or a layer of another build) every reading is
// unknown. It looks for the layer's memory at most once a second and keeps it open once found, so a
// game started again is read at once.
//
// It knows nothing of OBS or drawing. One thread at a time.
#pragma once

#include <vector>

#include "performance_reading.h"
#include "performance_share.h"
#include "shared/unique_handle.h"

class PerformanceReader {
public:
    PerformanceReader();

    // Signs, and returns the reading of the last second, renewed once a second.
    const PerformanceReading& Read();

private:
    bool Open(double now);
    PerformanceReading ReadWindow(double windowEndS);

    UniqueHandle mapping_;
    MappedView<performance_share::Header> header_;
    int64_t frequency_ = 1;  // performance-counter ticks per second
    double nextOpen_ = 0;    // performance-counter seconds
    double nextReading_ = 0;
    bool loggedLayout_ = false;
    PerformanceReading reading_;
    std::vector<PerformanceFrame> frames_;  // sized once: nothing allocated per reading
};

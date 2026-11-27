// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// PerformanceView: what the performance overlay shows of a reading
// (shared\performance\performance_reading.h), cell by cell: FPS, GPU headroom, CPU headroom. Each cell has
// a whole-number reading and a level, which turns the reading red when critical:
// - FPS: the frame rate, with no bar. Good unless the target is missed (more than 2 frames a second
//   below it), critical then.
// - GPU and CPU: the headroom in percent, with a bar of the headroom over 100 in the level's colour. Good
//   from 10 %, warning from 5 %, critical below. A negative headroom is shown as it is, with an empty bar.
// A reading that isn't known is left out: the cell shows its label and an empty bar.
//
// Plain logic: it knows nothing of OBS, drawing or the shared memory.
#pragma once

#include "shared/performance/performance_reading.h"

enum class PerformanceLevel { kGood, kWarning, kCritical };

struct PerformanceCell {
    bool known = false;
    int value = 0;
    bool percent = false;  // shown with a % after it
    bool bar = false;      // it has a bar
    float fill = 0;        // the bar, 0..1
    PerformanceLevel level = PerformanceLevel::kGood;
};

struct PerformanceView {
    static constexpr int kFps = 0, kGpu = 1, kCpu = 2, kCells = 3;
    PerformanceCell cells[kCells];
};

PerformanceView ViewOf(const PerformanceReading& r);

// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "performance_view.h"

#include <algorithm>

namespace {

    // Headroom levels, percent: a warning below the first, critical below the second.
    constexpr int kWarningBelow = 10;
    constexpr int kCriticalBelow = 5;

    float Clamp01(double v) {
        return static_cast<float>(std::min(1.0, std::max(0.0, v)));
    }

    PerformanceCell HeadroomCell(bool known, int headroom) {
        PerformanceCell c;

        if (!known) {
            return c;
        }

        c.known = true;
        c.value = headroom;
        c.percent = true;
        c.bar = true;
        c.fill = Clamp01(headroom / 100.0);
        c.level = headroom < kCriticalBelow  ? PerformanceLevel::kCritical
                  : headroom < kWarningBelow ? PerformanceLevel::kWarning
                                             : PerformanceLevel::kGood;
        return c;
    }

}

PerformanceView ViewOf(const PerformanceReading& r) {
    PerformanceView v;

    if (!r.known) {
        return v;
    }

    PerformanceCell& fps = v.cells[PerformanceView::kFps];
    fps.known = true;
    fps.value = r.fps;
    fps.level = r.targetMissed ? PerformanceLevel::kCritical : PerformanceLevel::kGood;

    v.cells[PerformanceView::kGpu] = HeadroomCell(r.gpuKnown, r.gpuHeadroom);
    v.cells[PerformanceView::kCpu] = HeadroomCell(r.cpuKnown, r.cpuHeadroom);
    return v;
}

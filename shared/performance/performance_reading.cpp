// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "performance_reading.h"

#include <algorithm>

namespace {

    constexpr double kWindowS = 1.0;
    // The frame rate may be this far below the target before it counts as missed (frames a second).
    constexpr double kMissedMargin = 2;
    // A side holds the game back when it takes longer than the other, or less than this much shorter
    // (seconds).
    constexpr double kBoundMarginS = 0.0005;

    struct Sums {
        int frames = 0;
        int cpuFrames = 0, gpuFrames = 0;
        double cpuS = 0, gpuS = 0;
        double newestEndS = -1;
        const PerformanceFrame* newest = nullptr;
    };

    Sums SumWindow(const PerformanceFrame* frames, size_t count, double windowEndS) {
        Sums s;

        for (size_t i = 0; i < count; i++) {
            const PerformanceFrame& f = frames[i];

            if (f.endS <= windowEndS - kWindowS || f.endS > windowEndS) {
                continue;
            }

            s.frames++;

            if (f.cpuS >= 0) {
                s.cpuFrames++;
                s.cpuS += f.cpuS;
            }

            if (f.gpuS >= 0) {
                s.gpuFrames++;
                s.gpuS += f.gpuS;
            }

            if (f.endS > s.newestEndS) {
                s.newestEndS = f.endS;
                s.newest = &f;
            }
        }

        return s;
    }

    // Pipelining can't be judged. Otherwise the CPU holds the game back unless the GPU takes at least
    // 500 us longer.
    void JudgeBound(PerformanceReading* r, bool pipelined) {
        if (pipelined || !r->gpuKnown || !r->cpuKnown) {
            r->bound = PerformanceBound::kUnknown;
            return;
        }

        const double cpuS = r->cpuMs / 1000, gpuS = r->gpuMs / 1000;

        if (cpuS + kBoundMarginS > gpuS) {
            r->bound = PerformanceBound::kCpu;
            r->boundByMs = std::max(0.0, r->cpuMs - r->gpuMs);
            return;
        }

        r->bound = PerformanceBound::kGpu;
        r->boundByMs = std::max(0.0, r->gpuMs - r->cpuMs);
    }

}

int HeadroomPercent(double timeS, double budgetS) {
    if (budgetS <= 0) {
        return 0;
    }

    return static_cast<int>(100 * (budgetS - timeS) / budgetS);
}

PerformanceReading ReadingOf(const PerformanceFrame* frames, size_t count, double windowEndS) {
    const Sums s = SumWindow(frames, count, windowEndS);
    PerformanceReading r;

    if (s.frames == 0) {
        return r;
    }

    // The newest frame says whether the game pipelines.
    const bool pipelined = s.newest->pipelined;
    const double budgetS = s.newest->periodS;
    r.known = true;
    r.fps = s.frames;
    r.targetFps = budgetS > 0 ? 1 / budgetS : 0;

    if (s.cpuFrames > 0 && !pipelined) {
        r.cpuKnown = true;
        r.cpuMs = 1000 * s.cpuS / s.cpuFrames;
        r.cpuHeadroom = HeadroomPercent(s.cpuS / s.cpuFrames, budgetS);
    }

    if (s.gpuFrames > 0) {
        r.gpuKnown = true;
        r.gpuMs = 1000 * s.gpuS / s.gpuFrames;
        r.gpuHeadroom = HeadroomPercent(s.gpuS / s.gpuFrames, budgetS);
    }

    if (r.targetFps > 0 && r.fps + kMissedMargin < r.targetFps) {
        r.targetMissed = true;
        JudgeBound(&r, pipelined);
    }

    return r;
}

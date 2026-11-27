// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// PerformanceReading: the game's frame rate and how much of each frame's time is left on the CPU and on
// the GPU, from the frames of one second:
// - The frame rate is the number of frames the game ended in the second.
// - The CPU and GPU times are the averages of the frames' times (performance_share.h says what they
//   measure).
// - The budget is one frame's time at the target rate: the display period the headset's runtime gives
//   the game in xrWaitFrame (11.1 ms at 90 Hz).
// - Headroom is the part of the budget left, 100 x (budget - time) / budget, in whole percent cut
//   towards zero; negative when a frame takes longer than its budget.
// - The CPU time isn't known when the game starts its next frame before it ends this one (pipelining),
//   because its time is then cut short.
// - The target counts as missed when the frame rate is more than 2 below it (a margin against flicker).
//   It then says which side holds the game back.
//
// Plain logic: the frames and the window come in as parameters. It knows nothing of the shared memory,
// clocks or drawing.
#pragma once

#include <cstddef>

// One frame of the game, times in performance-counter seconds; a negative time: not known.
struct PerformanceFrame {
    double endS = 0;     // when the game ended it
    double periodS = 0;  // the display period xrWaitFrame gave it
    double cpuS = -1;
    double gpuS = -1;
    bool pipelined = false;
};

// When the target is missed: what holds the game back.
enum class PerformanceBound {
    kNone,     // the target is met
    kUnknown,  // missed, but which side can't be told (pipelining, or the GPU time unknown)
    kCpu,
    kGpu,
};

struct PerformanceReading {
    bool known = false;  // the game ended frames in the second
    int fps = 0;         // frames in the second
    double targetFps = 0;
    bool targetMissed = false;
    PerformanceBound bound = PerformanceBound::kNone;
    double boundByMs = 0;  // how much longer the bound side takes than the other (kCpu, kGpu)

    bool cpuKnown = false, gpuKnown = false;
    double cpuMs = 0, gpuMs = 0;           // average time per frame
    int cpuHeadroom = 0, gpuHeadroom = 0;  // percent of the budget left
};

// The frames that ended in the second up to `windowEndS` (after windowEndS - 1, up to it), from `frames`
// in any order (frames outside that second are left out).
PerformanceReading ReadingOf(const PerformanceFrame* frames, size_t count, double windowEndS);

// Headroom: the percent of `budgetS` that `timeS` leaves, cut towards zero.
int HeadroomPercent(double timeS, double budgetS);

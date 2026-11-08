// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// ThreadCycles, CpuClock and CpuPercent: how much CPU a thread has used, for the footprint stats of
// both plugins and the mirror app. A thin adapter over QueryThreadCycleTime and the timestamp
// counter, and the percentage from them.
//
// Cycles, not GetThreadTimes: Windows charges thread times a whole timer tick (15.6 ms) at a time,
// which can't see a thread that wakes 4,000 times a second for a few microseconds. The cycle count
// is exact, in timestamp-counter cycles, so the counter's own advance over the same stretch is the
// 100% mark.
#pragma once

#include <windows.h>
#include <intrin.h>

#include <cstdint>
#include <thread>

// Timestamp-counter cycles `t` has run so far; 0 for a thread that isn't running.
inline uint64_t ThreadCycles(std::thread& t) {
    ULONG64 cycles = 0;
    if (!t.joinable() || !QueryThreadCycleTime(t.native_handle(), &cycles)) {
        return 0;
    }
    return cycles;
}

// The same for a thread handle (GetCurrentThread() for the calling thread).
inline uint64_t ThreadCycles(HANDLE t) {
    ULONG64 cycles = 0;
    return QueryThreadCycleTime(t, &cycles) ? cycles : 0;
}

// The timestamp counter now (cycles).
inline uint64_t CpuClock() {
    return __rdtsc();
}

// CPU use of a thread as a percentage of one core: the cycles it ran during a stretch (its total
// now and at the start; a thread that restarted counts from 0) against the cycles the clock
// advanced meanwhile (both in timestamp-counter cycles).
inline double CpuPercent(uint64_t threadNow, uint64_t threadBefore, uint64_t clockCycles) {
    if (threadNow < threadBefore) {
        threadBefore = 0;
    }
    return clockCycles > 0 ? double(threadNow - threadBefore) / double(clockCycles) * 100 : 0;
}

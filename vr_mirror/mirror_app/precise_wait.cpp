// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "precise_wait.h"

PreciseWait::PreciseWait() {
    timer_.Reset(
        CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS)
    );
}

void PreciseWait::Until(const FrameSource& clock, double t) {
    for (;;) {
        const double left = t - clock.Now();
        if (left <= 0) {
            return;
        }
        if (left > 0.0012 && timer_) {
            LARGE_INTEGER due;
            due.QuadPart = -LONGLONG((left - 0.001) * 1e7);  // relative, 100 ns units
            SetWaitableTimer(timer_.Get(), &due, 0, nullptr, nullptr, FALSE);
            WaitForSingleObject(timer_.Get(), 20);
        } else {
            YieldProcessor();
        }
    }
}

void PreciseWait::Nap(double seconds) {
    if (!timer_) {
        Sleep(1);
        return;
    }

    LARGE_INTEGER due;
    due.QuadPart = -LONGLONG(seconds * 1e7);  // relative, 100 ns units
    SetWaitableTimer(timer_.Get(), &due, 0, nullptr, nullptr, FALSE);
    WaitForSingleObject(timer_.Get(), 5);
}

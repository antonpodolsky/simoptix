// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Demo lap: a made-up 12 s lap's telemetry, for showing the G-meter without the game: braking with ABS
// (2.75-3.45 s), a long right-hander, an exit with TC (6.05-6.85 s), a shorter brake into a left-hander,
// then throttle again. Used by the G-meter's demo (overlays\gmeter\gmeter_source.cpp) and by the fake
// LMU (games\lmu\fake).
//
// Logic only: time in, values out. Knows nothing about OBS, LMU's shared memory or the clock.
#pragma once

#include <cstddef>

#include "shared/telemetry/telemetry_model.h"

constexpr double kDemoLapS = 12;
constexpr unsigned kDemoSession = 0xFFFFFFFFu;  // the demo's driving session (never a game's)

struct DemoSample {
    double lateralG;         // + to the right on the meter (the felt force in a left turn)
    double longitudinalG;    // + forward on the meter (braking)
    double brake, throttle;  // 0-1
    bool abs, tc;
};

// The lap `t` seconds in (wraps every kDemoLapS), with a little road noise.
DemoSample DemoLapAt(double t);

// The demo's samples on a 100 Hz grid counted from `start`, after `since` and up to `now` (all
// performance-counter seconds), at most the last 2 s of them and at most `max`, oldest first, as a game
// adapter would publish them (acceleration, pedals and aids; session kDemoSession). Returns how many.
size_t DemoSamples(double start, double since, double now, CarSample* out, size_t max);

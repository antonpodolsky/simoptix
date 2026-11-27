// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// HoverTick: when a menu's hover ticks start in a stretch of sound. A tick is short and sharp: its high
// frequencies rise by tens of decibels within a few milliseconds, out of the music and noise around it. So
// the sound's first difference (which keeps the high frequencies) is cut into 2 ms frames, and a frame's
// rise is its energy over the median of the 40 ms before it.
//
// Logic only: the sound comes in. Knows nothing about OBS, frames or threads.
#pragma once

#include <cstddef>
#include <vector>

// Mono sound and the performance-counter time of its first sample.
struct SoundStretch {
    const float* samples;
    size_t count;
    double start;  // s
    double rate;   // samples a second
};

// A tick: when it starts (performance-counter seconds) and how far it rose (dB).
struct HoverTickFound {
    double when;
    float riseDb;
};

// The ticks that start in `sound` from `from` to `to` (performance-counter seconds): each rise of at least
// kMinRiseDb (hover_tick.cpp) that rises the most within kApartS of it. The sound must start 40 ms before
// `from`.
std::vector<HoverTickFound> FindHoverTicks(const SoundStretch& sound, double from, double to);

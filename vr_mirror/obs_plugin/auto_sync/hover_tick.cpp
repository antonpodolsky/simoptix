// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hover_tick.h"

#include <algorithm>
#include <cmath>

namespace {

    constexpr double kFrameS = 0.002;
    constexpr int kBefore = 20;  // frames: the 40 ms a frame's rise is measured over
    // A tick rises at least this far. LMU's rose 32-52 dB in a menu with music, where the music's own
    // notes rose less than 20 dB.
    constexpr float kMinRiseDb = 25;
    constexpr double kApartS = 0.08;  // ticks closer together than this are one

}

std::vector<HoverTickFound> FindHoverTicks(const SoundStretch& sound, double from, double to) {
    const size_t frame = static_cast<size_t>(std::lround(kFrameS * sound.rate));
    const size_t frames = sound.count > 1 ? (sound.count - 1) / frame : 0;
    std::vector<float> level(frames), rise(frames, 0.0f), earlier(kBefore);

    for (size_t f = 0; f < frames; f++) {
        double energy = 0;

        for (size_t i = f * frame + 1; i <= (f + 1) * frame; i++) {
            const double d = static_cast<double>(sound.samples[i]) - sound.samples[i - 1];
            energy += d * d;
        }

        level[f] = static_cast<float>(10 * std::log10(energy / frame + 1e-14));
    }

    for (size_t f = kBefore + 1; f < frames; f++) {
        std::copy(level.begin() + (f - kBefore - 1), level.begin() + (f - 1), earlier.begin());
        std::nth_element(earlier.begin(), earlier.begin() + kBefore / 2, earlier.end());
        rise[f] = level[f] - earlier[kBefore / 2];
    }

    const auto centre = [&](size_t f) { return sound.start + (f * frame + frame / 2.0) / sound.rate; };
    const size_t apart = static_cast<size_t>(std::lround(kApartS / kFrameS));
    std::vector<HoverTickFound> ticks;

    for (size_t f = kBefore + 1; f < frames; f++) {
        if (rise[f] < kMinRiseDb || centre(f) < from || centre(f) > to) {
            continue;
        }

        const size_t lo = f > apart ? f - apart : 0, hi = std::min(frames - 1, f + apart);
        const bool most = std::all_of(rise.begin() + lo, rise.begin() + hi + 1, [&](float r) {
            return r <= rise[f];
        });

        if (most && (ticks.empty() || centre(f) - ticks.back().when > kApartS)) {
            ticks.push_back({centre(f), rise[f]});
        }
    }

    return ticks;
}

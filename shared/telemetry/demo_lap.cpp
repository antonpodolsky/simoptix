// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "demo_lap.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

    constexpr double kG = 9.80665;
    constexpr double kStep = 0.01;  // seconds: LMU's 100 updates a second

    // [s, lateral G, longitudinal G, brake, throttle], eased between keys. Full brake is held from 2.35 to
    // 3 s (the design's 0.95 eased up to 1 showed a full bar only for an instant).
    constexpr double kKeys[][5] = {
        {0, 0, -0.35, 0, 1},
        {2, 0.05, -0.3, 0, 1},
        {2.35, 0, 1.7, 1, 0},
        {3, 0.1, 2.62, 1, 0},
        {3.8, 0.9, 1.95, 0.6, 0},
        {4.6, 2.05, 0.7, 0.18, 0},
        {5.6, 2.32, 0.05, 0, 0.3},
        {6.4, 1.6, -0.42, 0, 0.85},
        {7.2, 0.4, -0.52, 0, 1},
        {8.2, -0.15, -0.3, 0, 1},
        {8.7, -0.2, 1.45, 0.8, 0},
        {9.4, -1.45, 0.95, 0.35, 0},
        {10.2, -2.08, -0.08, 0, 0.5},
        {11.2, -0.6, -0.46, 0, 1},
        {12, 0, -0.35, 0, 1}
    };

    constexpr int kKeyCount = sizeof(kKeys) / sizeof(kKeys[0]);

}

DemoSample DemoLapAt(double t) {
    t = std::fmod(std::fmod(t, kDemoLapS) + kDemoLapS, kDemoLapS);

    int i = 0;

    while (i < kKeyCount - 2 && kKeys[i + 1][0] <= t) {
        i++;
    }

    const double* fromKey = kKeys[i];
    const double* toKey = kKeys[i + 1];

    double fraction = (t - fromKey[0]) / (toKey[0] - fromKey[0]);

    fraction = fraction * fraction * (3 - 2 * fraction);

    auto lerp = [&](int k) { return fromKey[k] + (toKey[k] - fromKey[k]) * fraction; };
    const double roadNoise = 0.035 * std::sin(t * 21) + 0.025 * std::sin(t * 34 + 1);

    return {
        lerp(1) + roadNoise,
        lerp(2) + 0.8 * roadNoise * std::cos(t * 13),
        lerp(3),
        lerp(4),
        t > 2.75 && t < 3.45,
        t > 6.05 && t < 6.85
    };
}

size_t DemoSamples(double start, double since, double now, CarSample* out, size_t max) {
    since = std::max(since, now - 2);

    // The first grid step after `since`, then every step up to `now`. `since` is usually the last sample
    // given, on the grid: the tolerance keeps rounding (2.9999999 steps) from giving it again.
    double k = std::floor((since - start) / kStep + 1e-6) + 1;
    size_t n = 0;

    for (; n < max; n++, k++) {
        const double t = start + k * kStep;

        if (t > now) {
            break;
        }

        const DemoSample demo = DemoLapAt(t - start);
        CarSample& sample = out[n];

        memset(&sample, 0, sizeof(sample));

        sample.t = t;

        // Car axes: x forward, y left. A left turn accelerates the car left (+y) and pushes the driver
        // right; braking accelerates it backwards (-x) and pushes the driver forward.
        sample.accel.y = float(demo.lateralG * kG);
        sample.accel.x = float(-demo.longitudinalG * kG);

        sample.brake = float(demo.brake);
        sample.throttle = float(demo.throttle);
        sample.absActive = demo.abs;
        sample.tcActive = demo.tc;

        sample.session = kDemoSession;
    }

    return n;
}

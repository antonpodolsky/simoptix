// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Meter dot: the G-meter's dot, the same in every theme (meter_chrono.h, meter_oled.h): a glow under it,
// its comet tail (the dot's recent positions, shrinking and fading), and the dot, in design pixels of the
// 480 px board. Where the dot goes is the theme's: its dial says where 1 G is on screen.
//
// Knows nothing about OBS sources, settings, telemetry or a theme's rings, bars and labels.
// Graphics thread only.
#pragma once

#include <vector>

#include "meter_draw.h"
#include "meter_view.h"

// The dot's radius, design pixels: 160 % of the first design's 7.8 (picked on the canvas). The glow and
// tail scale with it.
constexpr float kDotR = 12.5f;

// A meter's size for a source: k scales design pixels to screen pixels, (cx, cy) is its centre, and
// `scale` turns G into screen pixels (the 3G ring's radius / 3).
struct MeterDial {
    float k, cx, cy, scale;

    // Where acceleration (x, y) in G is on screen, pinned at the 3G ring.
    void ToScreen(float x, float y, float* sx, float* sy) const;
};

// Adds the glow, the tail and the dot to `batch` (with `shapes` as the drawing buffer). Without telemetry
// the dot and its glow sit dimmed at the centre, with no tail.
void AddMeterDot(const MeterView& m, const MeterDial& d, MeterBatch& batch, std::vector<float>& shapes);

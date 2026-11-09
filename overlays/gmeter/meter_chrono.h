// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The G-meter's Chrono look (the design canvas's "Chrono Inner", overlays\gmeter\design\README.md): a black
// bezel, a dark face with 1G/2G/3G rings under domed glass, the pedal bars just inside the 3G ring, and a
// live-G window. The static parts are the design's two picture layers (meter_image.h): the base under the
// live parts and the glass glare over them. Drawn live between them: the dot with its glow and comet tail,
// the pedal bars, the highest values, the live G and the ring labels (rasterised at the meter's size, which
// is sharper than shrinking them with the picture). The scale is fixed at 3 G (the rings are in the picture).
// Sizes follow the design's 480 px board, scaled to the source.
//
// Knows nothing about OBS sources, settings or telemetry: it draws the MeterView it is given.
// Graphics thread only.
#pragma once

#include <vector>

#include "meter_draw.h"
#include "meter_image.h"
#include "meter_text.h"
#include "meter_view.h"

class ChronoMeter {
public:
    // Outside drawing: builds the picture layers for the source's size and the labels' textures.
    void Tick(int size, double now);
    // Draws into `batch` (cleared and submitted here) with `shapes` as the drawing buffer, between the
    // picture layers. Blending is set by the caller.
    void Draw(const MeterView& m, MeterBatch& batch, std::vector<float>& shapes);
    void Destroy();  // inside obs_enter_graphics, or on the graphics thread

private:
    // Draw's labels: the highest values, the ring labels and the live G. k scales design pixels to screen
    // pixels.
    void DrawValues(const MeterView& m, float k);

    // The 4 highest values (forward, back, right, left), the live G, then the ring labels (1G, 2G, 3G).
    static constexpr int kLiveG = 4, kRing = 5, kLabels = 8;
    MeterLabel labels_[kLabels];
    MeterImage base_{L"GMETER_BASE"}, glass_{L"GMETER_GLASS"};
};

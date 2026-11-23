// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The G-meter's OLED look (overlays\gmeter\design\README.md, "OLED"): a
// self-lit display, a solid black disc with a 1 px edge and a soft dark halo, dotted 1G and 2G rings, a
// solid 3G ring, and segmented pedal bars just outside it. No bezel, axes, glass or live G. The static
// parts are the design's one image layer (meter_image.h), drawn first. Drawn live over it: the segmented
// pedal bars, the dot with its glow and comet tail (Chrono's, meter_dot.h), the ring labels and the
// highest values. The scale is
// fixed at 3 G (the rings are in the image). Sizes follow the design's 480 px board, scaled to the source.
//
// Knows nothing about OBS sources, settings or telemetry: it draws the MeterView it is given.
// Graphics thread only.
#pragma once

#include <vector>

#include "meter_dot.h"
#include "meter_draw.h"
#include "meter_image.h"
#include "meter_look.h"
#include "meter_text.h"
#include "meter_view.h"

class OledMeter : public MeterLook {
public:
    void Tick(int size, double now) override;
    // Draws the live parts over the image layer.
    void Draw(const MeterView& m, MeterBatch& batch, std::vector<float>& shapes) override;
    void Destroy() override;

private:
    // Draw's labels: the highest values and the ring labels. k scales design pixels to screen pixels.
    void DrawValues(const MeterView& m, float k);

    // The 4 highest values (forward, back, right, left), then the ring labels (1G, 2G, 3G).
    static constexpr int kRing = 4, kLabels = 7;
    MeterLabel labels_[kLabels];
    MeterImage base_{L"GMETER_OLED_BASE"};
};

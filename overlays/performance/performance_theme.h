// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The performance overlay's two themes (design\README.md): Chrono, a dark bezel with a sunken
// face under glass, and OLED, a black display with segmented bars. The static parts are the design's image
// layers (meter_image.h): the theme's base under the live parts, and in Chrono the glass over them. Drawn
// live between them, at the overlay's size: each cell's label, reading, % and bar fill. Sizes follow the
// design's 312 x 164 box, in design pixels scaled to screen pixels by `k`.
//
// Knows nothing about OBS sources, settings or where the readings come from: it draws the PerformanceView
// it is given. Graphics thread only.
#pragma once

#include <vector>

#include "meter_draw.h"
#include "meter_image.h"
#include "meter_text.h"
#include "performance_view.h"

enum class ThemeChoice { kChrono, kOled };

class PerformanceTheme {
public:
    // The design's box, design pixels (canvas pixels on a 1080-pixel canvas).
    static constexpr int kBoxWidth = 312, kBoxHeight = 164;

    // Outside drawing: builds the theme's image layers at the box's size times k, and the labels' textures.
    void Tick(ThemeChoice choice, float k, double now);
    // Draws into `batch` (cleared and submitted here) with `shapes` as the drawing buffer, between the
    // image layers, k screen pixels to a design pixel. Blending is set by the caller.
    void Draw(
        const PerformanceView& view,
        ThemeChoice choice,
        float k,
        MeterBatch& batch,
        std::vector<float>& shapes
    );
    void Destroy();  // inside obs_enter_graphics, or on the graphics thread

private:
    void DrawBars(
        const PerformanceView& view,
        ThemeChoice choice,
        float k,
        MeterBatch& batch,
        std::vector<float>& shapes
    );
    void DrawText(const PerformanceView& view, ThemeChoice choice, float k);

    MeterImage chronoBase_{L"PERF_CHRONO_BASE"}, chronoGlass_{L"PERF_CHRONO_GLASS"};
    MeterImage oledBase_{L"PERF_OLED_BASE"};
    MeterLabel labels_[PerformanceView::kCells], values_[PerformanceView::kCells];
    MeterLabel units_[PerformanceView::kCells];
};

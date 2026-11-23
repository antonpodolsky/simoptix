// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Meter look: what every look of the G-meter does, so the source can draw any of them (the Meter type
// setting): build its images and labels for a size outside drawing, draw a MeterView, and free what it
// holds on the GPU. The looks are ChronoMeter (meter_chrono.h) and OledMeter (meter_oled.h).
//
// Knows nothing about OBS sources, settings or telemetry, nor about any one look.
// Graphics thread only.
#pragma once

#include <vector>

#include "meter_draw.h"
#include "meter_view.h"

class MeterLook {
public:
    virtual ~MeterLook() = default;

    // Outside drawing: builds the image layers for the source's size and the labels' textures.
    virtual void Tick(int size, double now) = 0;
    // Draws into `batch` (cleared and submitted here) with `shapes` as the drawing buffer. Blending is set
    // by the caller.
    virtual void Draw(const MeterView& m, MeterBatch& batch, std::vector<float>& shapes) = 0;
    virtual void Destroy() = 0;  // inside obs_enter_graphics, or on the graphics thread
};

// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_memory.h"

#include <algorithm>
#include <cstdio>

#include "pipeline/pipeline_config.h"
#include "pipeline/pipeline_types.h"

std::string VideoMemoryText(const MemoryFacts& facts, const MirrorSettings& settings, FrameSource& source) {
    if (!facts.running) {
        return kVideoMemoryNotMeasured;
    }

    const PipelineConfig config = ConfigFor(settings, OutputClock::kTicks, source.Report());
    const int slots = facts.slots[config.smoothing == SmoothingKind::kLookahead ? 1 : 0];
    const float aspect = static_cast<float>(facts.canvasWidth)
                         / static_cast<float>(std::max<uint32_t>(1, facts.canvasHeight));
    const float
        needWidth = NativeImageWidth(facts.eye, settings, static_cast<int>(facts.canvasWidth), aspect);
    uint32_t width = 0, height = 0;

    if (!source.CopySize(needWidth, kImageMemory / std::max(1, slots), &width, &height)) {
        return kVideoMemoryNotMeasured;
    }

    if (slots == 0) {
        return kVideoMemoryNotMeasured;
    }

    const double bytes = static_cast<double>(slots) * width * height * 4;
    char text[160];
    snprintf(
        text,
        sizeof(text),
        "Expected VRAM usage: %.1f GB (%d copies of %u x %u)",
        bytes / 1e9,
        slots,
        width,
        height
    );

    return text;
}

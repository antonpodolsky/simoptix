// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_memory.h"

#include <algorithm>
#include <cmath>

#include "frame_ring.h"
#include "pipeline/3_frame_picker/fixed_delay_picker.h"
#include "pipeline/pipeline_builder.h"
#include "pipeline/pipeline_config.h"
#include "pipeline/pipeline_types.h"

MemoryFacts ReportedFacts(const HeadsetReport& report) {
    MemoryFacts f;

    if (report.imageWidth == 0) {
        return f;
    }

    f.hasEye = report.hasEye;
    f.eye = report.eye;
    f.imageWidth = report.imageWidth;
    f.imageHeight = report.imageHeight;
    f.period = report.period;
    return f;
}

bool SameHeadset(const MemoryFacts& reading, const MemoryFacts& facts) {
    // A rate measured from the game's frames needn't match the reported one to the last digit. Refresh
    // rates a headset offers lie further apart (72, 80, 90, 120 Hz).
    constexpr double kPeriodTolerance = 0.01;  // a fraction of the period

    return reading.imageWidth == facts.imageWidth && reading.imageHeight == facts.imageHeight
           && std::fabs(reading.period - facts.period) <= kPeriodTolerance * facts.period;
}

bool StoreSize(
    const MemoryFacts& facts,
    const MirrorSettings& settings,
    const FrameSource& source,
    const ObsVideo& obs,
    int* slots,
    uint32_t* copyWidth,
    uint32_t* copyHeight
) {
    const bool reported = facts.imageWidth > 0 && facts.period > 0 && facts.hasEye;

    if (!(facts.measured || reported) || obs.framePeriod <= 0) {
        return false;
    }

    // As the frame picker sizes its store for the smoothing (FixedDelayPicker::SlotsFor).
    const PipelineConfig config = ConfigFor(settings, OutputClock::kTicks, source.Report());
    const double delay = SmoothingDelayS(config.smoothing);
    const bool shownOnly = FixedDelayCopiesShownOnly(source.CanCopyAfterNextFrame(), delay, obs.framePeriod);
    *slots = FixedDelaySlots(
        facts.measured ? facts.copyDelay : kMaxCopyDelayS,
        delay,
        facts.period,
        obs.framePeriod,
        shownOnly,
        FrameRing::kMaxSlots
    );

    const float aspect = static_cast<float>(obs.canvasWidth)
                         / static_cast<float>(std::max<uint32_t>(1, obs.canvasHeight));
    const float needWidth = NativeImageWidth(facts.eye, settings, static_cast<int>(obs.canvasWidth), aspect);
    source.StoredSize(
        facts.imageWidth,
        facts.imageHeight,
        needWidth,
        kImageMemory / *slots,
        copyWidth,
        copyHeight
    );
    return true;
}

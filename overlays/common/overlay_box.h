// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// OverlayBox: an overlay source's box on OBS's canvas. The overlay is always drawn at the size it is
// shown on the canvas, so it stays sharp however it is scaled. In OBS it has a layout size (its width and
// height before the scene item's scale), and OBS keeps the item's scale in proportion to the canvas.
// While drawing, the box reads that scale (Fit) and the overlay is built and drawn in canvas pixels, at
// the shown height once it has held for 0.3 s (Tick; its images take about 20 ms to build). Meanwhile the
// last build is stretched. It also puts a new overlay's scene items at their default place and size,
// scaled with the canvas's height from their place on a 1080-pixel canvas (Place).
//
// Knows nothing about what the overlay draws. Graphics thread, but for the draw height and the preview
// size, which the UI thread reads too.
#pragma once

#include <graphics/vec2.h>
#include <obs-module.h>

#include <atomic>
#include <cstdint>

class OverlayBox {
public:
    // The default place of the box's top left corner on a canvas of this size, unscaled (pixels).
    using DefaultPosition = vec2 (*)(uint32_t canvasW, uint32_t canvasH);

    // `name`: the source's name in OBS, for the log.
    OverlayBox(uint32_t layoutW, uint32_t layoutH, const char* name)
        : layoutW_(layoutW),
          layoutH_(layoutH),
          name_(name) {}

    uint32_t LayoutWidth() const { return layoutW_; }
    uint32_t LayoutHeight() const { return layoutH_; }

    // video_tick: picks the height to build at, from the scale the box was last drawn at. True when it
    // changed.
    bool Tick(double now);
    // video_render: notes the canvas scale it is drawn at and pushes a matrix that maps the build size
    // onto the layout size, 1:1 in canvas pixels when the build is the shown size. False, with nothing
    // pushed, until the first build. The caller pops the matrix.
    bool Fit();

    // The size it is built and drawn at, pixels; 0 before the first build.
    int DrawHeight() const { return drawHeight_; }
    int DrawWidth() const;
    // The size it was last shown at in a preview window (its smaller side), pixels; 0: not known.
    int PreviewPixels() const { return previewPixels_; }

    // video_tick: twice a second for the first 5 s, looks for the source's new scene items and moves
    // them from OBS's spot for new sources to `position` at the canvas's scale, once ("placed" in its
    // settings).
    void Place(obs_source_t* source, double now, DefaultPosition position);

private:
    const uint32_t layoutW_, layoutH_;
    const char* name_;

    float shownScale_ = 0;            // the largest canvas scale it was drawn at since the last tick
    std::atomic<int> drawHeight_{0};  // pixels
    int wantHeight_ = 0;              // the shown height, and since when (performance-counter seconds)
    double wantSince_ = 0;
    std::atomic<int> previewPixels_{0};
    int placeTries_ = 0;
    double nextPlaceTry_ = 0;
};

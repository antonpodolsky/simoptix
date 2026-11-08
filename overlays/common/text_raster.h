// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Text raster: one line of text rasterised by DirectWrite into a coverage bitmap (0-255 per pixel), for
// the meter's labels (meter_text.h). Grayscale anti-aliasing (no ClearType: its colour fringes don't
// survive video encoding), outlines grid-fitted vertically when the text is upright (horizontal strokes
// and the tops and bottoms of letters land on whole pixels; horizontally the text keeps its exact
// position), the font's own tabular figures, letter spacing, and any rotation applied while
// rasterising, so a turned label is smoothed once rather than drawn upright and resampled.
//
// Knows nothing about OBS, textures, colour or what a label is for: text and style in, coverage out.
// Thread-safe (DirectWrite's shared factory); used on OBS's graphics thread.
#pragma once

#include <cstdint>
#include <vector>

struct TextStyle {
    const wchar_t* family = L"Bahnschrift";
    int weight = 400;      // 400 regular, 600 semibold, 700 bold
    float size = 12;       // em size, pixels
    float tracking = 0;    // extra space after each letter, em (CSS letter-spacing)
    bool tabular = false;  // the font's tabular figures (every digit as wide)
};

struct TextRaster {
    int x0 = 0, y0 = 0, w = 0, h = 0;  // the bitmap's place in the pen's pixel space (see RasterText)
    std::vector<uint8_t> coverage;     // w x h, row by row
    float advance = 0;                 // the text's advance width, pixels (along its baseline)
};

// Rasterises `text` with its pen origin (the start of its baseline) at (ox, oy), turned by `angle`
// (radians, clockwise on screen) about that origin. The bitmap covers pixels x0..x0 + w - 1 and
// y0..y0 + h - 1 of that space. Returns false for no ink, or if DirectWrite can't be used.
bool RasterText(
    const wchar_t* text,
    const TextStyle& style,
    float ox,
    float oy,
    float angle,
    TextRaster* out
);

// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Meter text: labels rasterised by DirectWrite (text_raster.h) into textures, cropped to their ink plus a
// 1 px clear border, and placed by their ink, so gaps between a label and what it labels are exact to the
// pixel (centred and rotated labels, to a fraction of one). Every label is drawn 1:1, unfiltered: a
// turned one is rasterised turned. A label is asked for while drawing (WantLabel, DrawLabel...) and its
// texture is rebuilt on the next tick when the text, its look or its placement changed, at most 10 times
// a second (UpdateLabels).
//
// Knows nothing about what a meter shows or about OBS sources: only text rasterising and libobs graphics.
// Graphics thread only.
#pragma once

#include <graphics/graphics.h>

#include <cstdint>
#include <string>

// Where a label's ink is in its texture, to a fraction of a pixel (from its edge pixels' coverage).
struct LabelInk {
    float cx = 0, cy = 0;  // the label's centre (see DrawLabelCentred, DrawLabelRotated)
    float left = 0, top = 0, right = 0, bottom = 0;  // the ink's box
    float uprightW = 0;  // the ink's width upright (for a turned label, before turning)
};

struct MeterLabel {
    // Wanted (set while drawing) and shown (built on the next tick).
    std::string text, shownText;
    int size = 0, shownSize = 0;           // em size in pixels
    const wchar_t* face = L"Bahnschrift";  // family, with GDI's weight suffix ("Bahnschrift SemiBold")
    const wchar_t* shownFace = nullptr;
    int opacity = 100, shownOpacity = 0;
    uint32_t rgb = 0xFFFFFF, shownRgb = 0xFFFFFF;  // text colour, 0xRRGGBB
    float tracking = 0, shownTracking = 0;       // extra space after each letter, in em (CSS letter-spacing)
    bool tabular = false, shownTabular = false;  // the font's tabular figures (DrawLabelCentred)
    // A placed label (DrawLabelCentred, DrawLabelRotated, PlaceLabel): where its centre goes, in source
    // pixels (-1: not placed), turned by `angle` (radians, clockwise on screen), and where that centre is
    // in the texture, which is built so the two differ by whole pixels.
    float centreX = -1, centreY = -1, shownCentreX = -1, shownCentreY = -1;
    float angle = 0, shownAngle = 0;
    // Which point of the label goes at centreX: 0 its left, 0.5 its middle, 1 its right, of the ink, or for
    // tabular text of its advance (so a number's digits keep their places as it changes).
    float anchor = 0.5f, shownAnchor = 0.5f;
    LabelInk ink;         // as shown
    double updated = -1;  // performance-counter seconds
    gs_texture_t* tex = nullptr;
    int w = 0, h = 0;  // the texture: the ink is at (1, 1) to (w - 1, h - 1)
};

// Builds the textures of labels whose text, look or placement changed (outside drawing; enters graphics).
void UpdateLabels(MeterLabel* labels, int count, double now);

// Asks for `text` in `l` without drawing it (so it is ready when it is first drawn). Keeps the label's
// placement: give a label that isn't drawn yet its place with PlaceLabel.
void WantLabel(
    MeterLabel& l,
    const char* text,
    int size,
    const wchar_t* face,
    int opacity,
    uint32_t rgb = 0xFFFFFF,
    float tracking = 0
);

// Places `l` as DrawLabelRotated would (angle 0: as DrawLabelCentred, without tabular figures), without
// drawing it.
void PlaceLabel(MeterLabel& l, float x, float y, float angle);

// Asks for `text` in `l` and draws what it shows now. Its ink starts at x (align 0), is centred on
// it (0.5) or ends at it (1); its top is at y (valign 0), its middle (0.5) or its bottom (1).
void DrawLabel(
    MeterLabel& l,
    const char* text,
    int size,
    const wchar_t* face,
    int opacity,
    float x,
    float y,
    float align,
    float valign,
    uint32_t rgb = 0xFFFFFF
);

// As DrawLabel with the ink's middle on y, and its point `align` (0 left, 0.5 middle, 1 right) on x to a
// fraction of a pixel: the text is rasterised at the sub-pixel offset that puts it there, instead of the
// sharpest one, for text in a tight box or numbers that must not shift. (Vertically the grid-fitting keeps
// strokes on whole pixels.) tabular: the font's tabular figures, placed by the text's advance instead of
// its ink, so a changing number's digits stay in place.
void DrawLabelCentred(
    MeterLabel& l,
    const char* text,
    int size,
    const wchar_t* face,
    int opacity,
    float x,
    float y,
    uint32_t rgb = 0xFFFFFF,
    bool tabular = false,
    float align = 0.5f
);

// Draws what `l` shows now (asked for with WantLabel) with its upright ink centre on (x, y), turned by
// `angle` (radians, clockwise on screen).
void DrawLabelRotated(MeterLabel& l, float x, float y, float angle);

// The ink's width, upright (a turned label's too: for setting letters along a curve).
inline float InkWidth(const MeterLabel& l) {
    return l.tex ? l.ink.uprightW : 0;
}

void DestroyLabel(MeterLabel& l);  // inside obs_enter_graphics, or on the graphics thread

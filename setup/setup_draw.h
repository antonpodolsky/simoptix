// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The shapes and icons of SimOptix Setup's design, drawn smooth (anti-aliased) with GDI+, which is part
// of Windows: rounded rectangles, the three parts' line glyphs, the round marks (tick, warning, error)
// and the spinner. Sizes and radii are in pixels at 96 DPI and scaled to the canvas. Positions and
// rectangles are the canvas's own pixels. Knows nothing about where on the window a shape goes.
#pragma once

#include <windows.h>

#include "setup_gdi.h"

// Starts GDI+ for the process and shuts it down. One lives around everything that draws.
class GdiplusSession {
public:
    GdiplusSession();
    GdiplusSession(const GdiplusSession&) = delete;
    GdiplusSession& operator=(const GdiplusSession&) = delete;
    ~GdiplusSession();

private:
    ULONG_PTR token_ = 0;
};

enum class Glyph { kVrMirror, kOverlays, kOpenXrLayer };

enum class Mark { kTick, kWarning, kError };

struct MarkColours {
    COLORREF circle;
    COLORREF symbol;  // the check, the exclamation mark or the cross
};

// A standard button's body: a 1 px stroke, darker along the bottom edge, around the fill.
struct StrokedColours {
    COLORREF fill;
    COLORREF stroke;
    COLORREF bottom;
};

void FillRoundRect(const Canvas& canvas, const RECT& rect, int radius, COLORREF fill);
void FillStrokedRoundRect(const Canvas& canvas, const RECT& rect, int radius, const StrokedColours& colours);

// A 2 px ring, 1 px outside `rect`, for the keyboard focus.
void DrawFocusRing(const Canvas& canvas, const RECT& rect, int radius, COLORREF colour);

// A part's glyph in a 24 px box whose top left is `at`.
void DrawGlyph(const Canvas& canvas, Glyph glyph, POINT at, COLORREF colour);

// A round mark `size` px wide, whose top left is `at`.
void DrawMark(const Canvas& canvas, Mark mark, POINT at, int size, const MarkColours& colours);

// The spinner in a 16 px box whose top left is `at`: an arc of 14 px on a circle of radius 6.5, starting
// at `angleDeg`.
void DrawSpinner(const Canvas& canvas, POINT at, COLORREF colour, float angleDeg);

// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The shapes and icons of SimOptix Setup's design, drawn smooth (anti-aliased) with GDI+, which is part
// of Windows: rounded rectangles, the three parts' line glyphs, the tick, the warning and error marks,
// and the spinner. Every size is in pixels at 96 DPI and scaled to `dpi`; every position is in the
// device's pixels. Knows nothing about where on the window a shape goes.
#pragma once

#include <windows.h>

// Starts GDI+ for the process and shuts it down. One lives in wWinMain, around everything that draws.
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

// `radius` in the device's pixels.
void FillRoundRect(HDC dc, const RECT& rect, int radius, COLORREF fill);

// A standard button's body: a 1 px stroke, darker along the bottom edge, around the fill.
void FillStrokedRoundRect(
    HDC dc,
    const RECT& rect,
    int radius,
    COLORREF fill,
    COLORREF stroke,
    COLORREF bottom
);

// A 2 px ring, 1 px outside `rect`, for the keyboard focus.
void DrawFocusRing(HDC dc, const RECT& rect, int radius, COLORREF colour, UINT dpi);

// A part's glyph in a 24 px box with its top left at `left`, `top`.
void DrawGlyph(HDC dc, Glyph glyph, int left, int top, UINT dpi, COLORREF colour);

// The round marks, `size` px at 96 DPI: a tick, a warning (an exclamation mark) and an error (a cross).
void DrawTick(HDC dc, int left, int top, int size, UINT dpi, COLORREF circle, COLORREF mark);
void DrawWarning(HDC dc, int left, int top, int size, UINT dpi, COLORREF circle, COLORREF mark);
void DrawError(HDC dc, int left, int top, int size, UINT dpi, COLORREF circle, COLORREF mark);

// The spinner in a 16 px box: an arc of 14 px on a circle of radius 6.5, starting at `angleDeg`.
void DrawSpinner(HDC dc, int left, int top, UINT dpi, COLORREF colour, float angleDeg);

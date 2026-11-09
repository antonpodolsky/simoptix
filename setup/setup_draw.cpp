// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "setup_draw.h"

#include <algorithm>

// GDI+'s headers call min and max, which NOMINMAX (set for the whole build) leaves undefined.
namespace Gdiplus {
    using std::max;
    using std::min;
}

#include <gdiplus.h>

namespace {

    using Gdiplus::REAL;

    Gdiplus::Color ToColor(COLORREF colour) {
        return Gdiplus::Color(255, GetRValue(colour), GetGValue(colour), GetBValue(colour));
    }

    REAL Scale(UINT dpi) {
        return static_cast<REAL>(dpi) / 96;
    }

    // A Graphics on `dc` that draws smooth, with a pixel's centre at its half coordinates, so that a
    // shape from 0 to 10 covers pixels 0 to 9 exactly.
    struct Smooth {
        explicit Smooth(HDC dc) : graphics(dc) {
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        }

        Gdiplus::Graphics graphics;
    };

    // GDI+ has no rounded rectangle: four 90° arcs, joined by the path's own lines.
    void
    AddRoundRect(Gdiplus::GraphicsPath& path, REAL left, REAL top, REAL width, REAL height, REAL radius) {
        const REAL diameter = std::min(radius * 2, std::min(width, height));

        if (diameter <= 0) {
            path.AddRectangle(Gdiplus::RectF(left, top, width, height));
            return;
        }

        path.AddArc(left, top, diameter, diameter, 180, 90);
        path.AddArc(left + width - diameter, top, diameter, diameter, 270, 90);
        path.AddArc(left + width - diameter, top + height - diameter, diameter, diameter, 0, 90);
        path.AddArc(left, top + height - diameter, diameter, diameter, 90, 90);
        path.CloseFigure();
    }

    void FillRound(
        Gdiplus::Graphics& graphics,
        REAL left,
        REAL top,
        REAL width,
        REAL height,
        REAL radius,
        COLORREF fill
    ) {
        Gdiplus::GraphicsPath path;
        AddRoundRect(path, left, top, width, height, radius);
        Gdiplus::SolidBrush brush(ToColor(fill));
        graphics.FillPath(&brush, &path);
    }

    // Round caps and joins, as the design's line glyphs have.
    void MakeRound(Gdiplus::Pen& pen) {
        pen.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
    }

    // Moves and scales the drawing so that the shapes below are drawn in their own box, in pixels at
    // 96 DPI, `boxScale` times larger.
    void PlaceBox(Gdiplus::Graphics& graphics, int left, int top, UINT dpi, REAL boxScale) {
        graphics.TranslateTransform(static_cast<REAL>(left), static_cast<REAL>(top));
        graphics.ScaleTransform(Scale(dpi) * boxScale, Scale(dpi) * boxScale);
    }

    void FillCircle(Gdiplus::Graphics& graphics, REAL x, REAL y, REAL radius, COLORREF colour) {
        Gdiplus::SolidBrush brush(ToColor(colour));
        graphics.FillEllipse(&brush, x - radius, y - radius, radius * 2, radius * 2);
    }

    void DrawVrMirror(Gdiplus::Graphics& graphics, COLORREF colour) {
        Gdiplus::Pen pen(ToColor(colour), 1.5f);
        MakeRound(pen);
        Gdiplus::GraphicsPath headset;
        AddRoundRect(headset, 2.75f, 6.75f, 18.5f, 11, 3.5f);
        graphics.DrawPath(&pen, &headset);
        graphics.DrawEllipse(&pen, 8.25f - 2.25f, 12.25f - 2.25f, 4.5f, 4.5f);
        graphics.DrawEllipse(&pen, 15.75f - 2.25f, 12.25f - 2.25f, 4.5f, 4.5f);
    }

    void DrawOverlays(Gdiplus::Graphics& graphics, COLORREF colour) {
        Gdiplus::Pen ring(ToColor(colour), 1.5f);
        MakeRound(ring);
        Gdiplus::Pen cross(ToColor(colour), 1);
        MakeRound(cross);
        graphics.DrawEllipse(&ring, 12 - 8.25f, 12 - 8.25f, 16.5f, 16.5f);
        graphics.DrawLine(&cross, 12.0f, 6.5f, 12.0f, 17.5f);
        graphics.DrawLine(&cross, 6.5f, 12.0f, 17.5f, 12.0f);
        FillCircle(graphics, 15.25f, 8.75f, 2.25f, colour);
    }

    void DrawOpenXrLayer(Gdiplus::Graphics& graphics, COLORREF colour) {
        Gdiplus::Pen pen(ToColor(colour), 1.5f);
        MakeRound(pen);
        const Gdiplus::PointF top[] = {{12, 4.25f}, {20, 8.25f}, {12, 12.25f}, {4, 8.25f}};
        const Gdiplus::PointF middle[] = {{4, 12.25f}, {12, 16.25f}, {20, 12.25f}};
        const Gdiplus::PointF bottom[] = {{4, 16}, {12, 20}, {20, 16}};
        graphics.DrawPolygon(&pen, top, 4);
        graphics.DrawLines(&pen, middle, 3);
        graphics.DrawLines(&pen, bottom, 3);
    }

}

GdiplusSession::GdiplusSession() {
    const Gdiplus::GdiplusStartupInput input;

    if (Gdiplus::GdiplusStartup(&token_, &input, nullptr) != Gdiplus::Ok) {
        token_ = 0;  // the shapes then aren't drawn, and the text still is
    }
}

GdiplusSession::~GdiplusSession() {
    if (token_) {
        Gdiplus::GdiplusShutdown(token_);
    }
}

void FillRoundRect(HDC dc, const RECT& rect, int radius, COLORREF fill) {
    Smooth smooth(dc);
    FillRound(
        smooth.graphics,
        static_cast<REAL>(rect.left),
        static_cast<REAL>(rect.top),
        static_cast<REAL>(rect.right - rect.left),
        static_cast<REAL>(rect.bottom - rect.top),
        static_cast<REAL>(radius),
        fill
    );
}

void FillStrokedRoundRect(
    HDC dc,
    const RECT& rect,
    int radius,
    COLORREF fill,
    COLORREF stroke,
    COLORREF bottom
) {
    Smooth smooth(dc);
    const REAL left = static_cast<REAL>(rect.left);
    const REAL top = static_cast<REAL>(rect.top);
    const REAL width = static_cast<REAL>(rect.right - rect.left);
    const REAL height = static_cast<REAL>(rect.bottom - rect.top);
    const REAL r = static_cast<REAL>(radius);

    // The bottom colour shows only along the bottom edge, under the stroke shape moved up by 1 px.
    FillRound(smooth.graphics, left, top, width, height, r, bottom);
    FillRound(smooth.graphics, left, top, width, height - 1, r, stroke);
    FillRound(smooth.graphics, left + 1, top + 1, width - 2, height - 2, r - 1, fill);
}

void DrawFocusRing(HDC dc, const RECT& rect, int radius, COLORREF colour, UINT dpi) {
    Smooth smooth(dc);
    const REAL width = 2 * Scale(dpi);
    const REAL outside = 1 * Scale(dpi) + width / 2;
    Gdiplus::GraphicsPath path;
    AddRoundRect(
        path,
        rect.left - outside,
        rect.top - outside,
        rect.right - rect.left + 2 * outside,
        rect.bottom - rect.top + 2 * outside,
        radius + outside
    );
    Gdiplus::Pen pen(ToColor(colour), width);
    smooth.graphics.DrawPath(&pen, &path);
}

void DrawGlyph(HDC dc, Glyph glyph, int left, int top, UINT dpi, COLORREF colour) {
    Smooth smooth(dc);
    PlaceBox(smooth.graphics, left, top, dpi, 1);

    switch (glyph) {
        case Glyph::kVrMirror:
            DrawVrMirror(smooth.graphics, colour);
            break;
        case Glyph::kOverlays:
            DrawOverlays(smooth.graphics, colour);
            break;
        case Glyph::kOpenXrLayer:
            DrawOpenXrLayer(smooth.graphics, colour);
            break;
    }
}

void DrawTick(HDC dc, int left, int top, int size, UINT dpi, COLORREF circle, COLORREF mark) {
    Smooth smooth(dc);
    PlaceBox(smooth.graphics, left, top, dpi, static_cast<REAL>(size) / 16);
    FillCircle(smooth.graphics, 8, 8, 8, circle);
    Gdiplus::Pen pen(ToColor(mark), 1.6f);
    MakeRound(pen);
    const Gdiplus::PointF check[] = {{4.6f, 8.2f}, {7, 10.5f}, {11.4f, 5.8f}};
    smooth.graphics.DrawLines(&pen, check, 3);
}

void DrawWarning(HDC dc, int left, int top, int size, UINT dpi, COLORREF circle, COLORREF mark) {
    Smooth smooth(dc);
    PlaceBox(smooth.graphics, left, top, dpi, static_cast<REAL>(size) / 16);
    FillCircle(smooth.graphics, 8, 8, 8, circle);
    Gdiplus::Pen pen(ToColor(mark), 1.6f);
    MakeRound(pen);
    smooth.graphics.DrawLine(&pen, 8.0f, 4.2f, 8.0f, 9.0f);
    FillCircle(smooth.graphics, 8, 11.7f, 1, mark);
}

void DrawError(HDC dc, int left, int top, int size, UINT dpi, COLORREF circle, COLORREF mark) {
    Smooth smooth(dc);
    PlaceBox(smooth.graphics, left, top, dpi, static_cast<REAL>(size) / 16);
    FillCircle(smooth.graphics, 8, 8, 8, circle);
    Gdiplus::Pen pen(ToColor(mark), 1.6f);
    MakeRound(pen);
    smooth.graphics.DrawLine(&pen, 5.6f, 5.6f, 10.4f, 10.4f);
    smooth.graphics.DrawLine(&pen, 10.4f, 5.6f, 5.6f, 10.4f);
}

void DrawSpinner(HDC dc, int left, int top, UINT dpi, COLORREF colour, float angleDeg) {
    // 14 px of a circle of radius 6.5, whose length is 2π × 6.5 = 40.8 px: 123.4°.
    constexpr REAL kSweepDeg = 14 / 40.84f * 360;

    Smooth smooth(dc);
    PlaceBox(smooth.graphics, left, top, dpi, 1);
    Gdiplus::Pen pen(ToColor(colour), 2);
    MakeRound(pen);
    smooth.graphics.DrawArc(&pen, 8 - 6.5f, 8 - 6.5f, 13.0f, 13.0f, angleDeg, kSweepDeg);
}

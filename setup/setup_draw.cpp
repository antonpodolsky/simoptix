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

    // 14 px of a circle of radius 6.5, whose length is 2π × 6.5 = 40.84 px: 123.4°.
    constexpr REAL kSpinnerSweepDeg = 14 / 40.84f * 360;

    Gdiplus::Color ToColor(COLORREF colour) {
        return Gdiplus::Color(255, GetRValue(colour), GetGValue(colour), GetBValue(colour));
    }

    Gdiplus::RectF ToRectF(const RECT& rect) {
        return Gdiplus::RectF(
            static_cast<REAL>(rect.left),
            static_cast<REAL>(rect.top),
            static_cast<REAL>(rect.right - rect.left),
            static_cast<REAL>(rect.bottom - rect.top)
        );
    }

    REAL ScaleOf(const Canvas& canvas) {
        return static_cast<REAL>(canvas.dpi) / 96;
    }

    // A Graphics on the canvas that draws smooth, with a pixel's centre at its half coordinates, so that
    // a shape from 0 to 10 covers pixels 0 to 9 exactly.
    struct Smooth {
        explicit Smooth(const Canvas& canvas) : graphics(canvas.dc) {
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        }

        Gdiplus::Graphics graphics;
    };

    // GDI+ has no rounded rectangle: four 90° arcs, joined by the path's own lines.
    void AddRoundRect(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect, REAL radius) {
        const REAL diameter = std::min(radius * 2, std::min(rect.Width, rect.Height));

        if (diameter <= 0) {
            path.AddRectangle(rect);
            return;
        }

        const REAL right = rect.X + rect.Width - diameter;
        const REAL bottom = rect.Y + rect.Height - diameter;
        path.AddArc(rect.X, rect.Y, diameter, diameter, 180, 90);
        path.AddArc(right, rect.Y, diameter, diameter, 270, 90);
        path.AddArc(right, bottom, diameter, diameter, 0, 90);
        path.AddArc(rect.X, bottom, diameter, diameter, 90, 90);
        path.CloseFigure();
    }

    void FillRound(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect, REAL radius, COLORREF fill) {
        Gdiplus::GraphicsPath path;
        AddRoundRect(path, rect, radius);

        Gdiplus::SolidBrush brush(ToColor(fill));
        graphics.FillPath(&brush, &path);
    }

    void FillCircle(Gdiplus::Graphics& graphics, REAL x, REAL y, REAL radius, COLORREF colour) {
        Gdiplus::SolidBrush brush(ToColor(colour));
        graphics.FillEllipse(&brush, x - radius, y - radius, radius * 2, radius * 2);
    }

    // Round caps and joins, as the design's line glyphs have.
    void MakeRound(Gdiplus::Pen& pen) {
        pen.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
    }

    // Moves and scales the drawing, so that what follows is drawn in its own box, in pixels at 96 DPI,
    // `boxScale` times larger.
    void PlaceBox(Gdiplus::Graphics& graphics, const Canvas& canvas, POINT at, REAL boxScale) {
        graphics.TranslateTransform(static_cast<REAL>(at.x), static_cast<REAL>(at.y));
        graphics.ScaleTransform(ScaleOf(canvas) * boxScale, ScaleOf(canvas) * boxScale);
    }

    void DrawVrMirror(Gdiplus::Graphics& graphics, COLORREF colour) {
        Gdiplus::Pen pen(ToColor(colour), 1.5f);
        MakeRound(pen);

        Gdiplus::GraphicsPath headset;
        AddRoundRect(headset, Gdiplus::RectF(2.75f, 6.75f, 18.5f, 11), 3.5f);
        graphics.DrawPath(&pen, &headset);

        graphics.DrawEllipse(&pen, 8.25f - 2.25f, 12.25f - 2.25f, 4.5f, 4.5f);
        graphics.DrawEllipse(&pen, 15.75f - 2.25f, 12.25f - 2.25f, 4.5f, 4.5f);
    }

    void DrawOverlays(Gdiplus::Graphics& graphics, COLORREF colour) {
        Gdiplus::Pen ring(ToColor(colour), 1.5f);
        Gdiplus::Pen cross(ToColor(colour), 1);
        MakeRound(ring);
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

    // The marks are drawn in a 16 px box.
    void DrawMarkSymbol(Gdiplus::Graphics& graphics, Mark mark, COLORREF colour) {
        Gdiplus::Pen pen(ToColor(colour), 1.6f);
        MakeRound(pen);

        switch (mark) {
            case Mark::kTick: {
                const Gdiplus::PointF check[] = {{4.6f, 8.2f}, {7, 10.5f}, {11.4f, 5.8f}};
                graphics.DrawLines(&pen, check, 3);
                return;
            }
            case Mark::kWarning:
                graphics.DrawLine(&pen, 8.0f, 4.2f, 8.0f, 9.0f);
                FillCircle(graphics, 8, 11.7f, 1, colour);
                return;
            case Mark::kError:
                graphics.DrawLine(&pen, 5.6f, 5.6f, 10.4f, 10.4f);
                graphics.DrawLine(&pen, 10.4f, 5.6f, 5.6f, 10.4f);
                return;
        }
    }

}

GdiplusSession::GdiplusSession() {
    const Gdiplus::GdiplusStartupInput input;

    // Without GDI+ the shapes aren't drawn, and the text still is.
    if (Gdiplus::GdiplusStartup(&token_, &input, nullptr) != Gdiplus::Ok) {
        token_ = 0;
    }
}

GdiplusSession::~GdiplusSession() {
    if (token_) {
        Gdiplus::GdiplusShutdown(token_);
    }
}

void FillRoundRect(const Canvas& canvas, const RECT& rect, int radius, COLORREF fill) {
    Smooth smooth(canvas);
    FillRound(smooth.graphics, ToRectF(rect), static_cast<REAL>(canvas.Scale(radius)), fill);
}

void FillStrokedRoundRect(const Canvas& canvas, const RECT& rect, int radius, const StrokedColours& colours) {
    Smooth smooth(canvas);
    const Gdiplus::RectF outer = ToRectF(rect);
    const REAL r = static_cast<REAL>(canvas.Scale(radius));

    // The bottom colour shows only along the bottom edge, under the stroke shape moved up by 1 px.
    Gdiplus::RectF stroke = outer;
    stroke.Height -= 1;
    Gdiplus::RectF inner = outer;
    inner.Inflate(-1, -1);

    FillRound(smooth.graphics, outer, r, colours.bottom);
    FillRound(smooth.graphics, stroke, r, colours.stroke);
    FillRound(smooth.graphics, inner, r - 1, colours.fill);
}

void DrawFocusRing(const Canvas& canvas, const RECT& rect, int radius, COLORREF colour) {
    Smooth smooth(canvas);
    const REAL width = 2 * ScaleOf(canvas);
    const REAL outside = ScaleOf(canvas) + width / 2;

    Gdiplus::RectF ring = ToRectF(rect);
    ring.Inflate(outside, outside);

    Gdiplus::GraphicsPath path;
    AddRoundRect(path, ring, static_cast<REAL>(canvas.Scale(radius)) + outside);

    Gdiplus::Pen pen(ToColor(colour), width);
    smooth.graphics.DrawPath(&pen, &path);
}

void DrawGlyph(const Canvas& canvas, Glyph glyph, POINT at, COLORREF colour) {
    Smooth smooth(canvas);
    PlaceBox(smooth.graphics, canvas, at, 1);

    switch (glyph) {
        case Glyph::kVrMirror:
            DrawVrMirror(smooth.graphics, colour);
            return;
        case Glyph::kOverlays:
            DrawOverlays(smooth.graphics, colour);
            return;
        case Glyph::kOpenXrLayer:
            DrawOpenXrLayer(smooth.graphics, colour);
            return;
    }
}

void DrawMark(const Canvas& canvas, Mark mark, POINT at, int size, const MarkColours& colours) {
    Smooth smooth(canvas);
    PlaceBox(smooth.graphics, canvas, at, static_cast<REAL>(size) / 16);

    FillCircle(smooth.graphics, 8, 8, 8, colours.circle);
    DrawMarkSymbol(smooth.graphics, mark, colours.symbol);
}

void DrawSpinner(const Canvas& canvas, POINT at, COLORREF colour, float angleDeg) {
    Smooth smooth(canvas);
    PlaceBox(smooth.graphics, canvas, at, 1);

    Gdiplus::Pen pen(ToColor(colour), 2);
    MakeRound(pen);
    smooth.graphics.DrawArc(&pen, 8 - 6.5f, 8 - 6.5f, 13.0f, 13.0f, angleDeg, kSpinnerSweepDeg);
}

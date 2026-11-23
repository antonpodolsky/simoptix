// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "performance_theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

    // The face, inside the bezel (232 x 68 at (40, 40), inset 6), in three equal cells. Design pixels.
    constexpr float kFaceX = 46, kFaceY = 46, kFaceWidth = 220;
    constexpr float kCellWidth = kFaceWidth / PerformanceView::kCells;
    constexpr float kCellPad = 10;  // a cell's left edge to its label, reading and bar

    // The text's places, measured on design\perf.html rendered by Edge at 2x with sample readings: each
    // starts at the cell's pad (its pen, as the design's text boxes), with its ink's middle on these lines.
    // Label ink 55.5 to 62.0, digits 67.0 to 81.0, % 73.0 to 81.0.
    constexpr float kLabelInkMiddleY = 58.75f;
    constexpr float kValueInkMiddleY = 74.0f;
    constexpr float kUnitInkMiddleY = 77.0f;
    constexpr float kUnitGap = 1;  // the % after the reading's advance

    // The bars: 3 high from y 91, flat ends, over the track in the base image.
    constexpr float kBarY = 91, kBarHeight = 3;
    // OLED's bars are cut into segments counted from the track's left end, as the base image's track.
    constexpr float kSegment = 5, kSegmentGap = 2;

    // Type sizes, design pixels, and the letter spacing of the labels (em).
    constexpr float kLabelSize = 9, kValueSize = 20, kUnitSize = 11;
    constexpr float kLabelTracking = 0.1f;

    // Colours, 0xRRGGBB, and as 0..1 RGB for the bars.
    constexpr uint32_t kInk = 0xFFFFFF;
    constexpr uint32_t kCriticalRgb = 0xFF5A4E;
    constexpr float kGood[3] = {0.239f, 0.863f, 0.518f};    // #3ddc84, the G-meter's throttle
    constexpr float kWarning[3] = {1.0f, 0.690f, 0.125f};   // #ffb020
    constexpr float kCritical[3] = {1.0f, 0.353f, 0.306f};  // #ff5a4e, the G-meter's brake
    // The labels and the %: white at 60 % in Chrono, 40 % in OLED.
    constexpr int kLabelOpacityChrono = 60, kLabelOpacityOled = 40;

    // HR: headroom.
    const char* const kLabelText[PerformanceView::kCells] = {"FPS", "GPU HR", "CPU HR"};

    // The design's weights, one heavier below 9 px, where strokes get thinner than a pixel and read grey.
    const wchar_t* RegularFace(int px) {
        return px < 9 ? L"Bahnschrift SemiBold" : L"Bahnschrift";
    }
    const wchar_t* SemiBoldFace(int px) {
        return px < 9 ? L"Bahnschrift Bold" : L"Bahnschrift SemiBold";
    }

    const float* BarRgb(PerformanceLevel level) {
        switch (level) {
            case PerformanceLevel::kWarning:
                return kWarning;
            case PerformanceLevel::kCritical:
                return kCritical;
            default:
                return kGood;
        }
    }

    float CellX(int i) {
        return kFaceX + i * kCellWidth;
    }

}

void PerformanceTheme::Tick(ThemeChoice choice, float k, double now) {
    const int width = static_cast<int>(std::lround(kBoxWidth * k));
    const int height = static_cast<int>(std::lround(kBoxHeight * k));

    if (choice == ThemeChoice::kChrono) {
        chronoBase_.Build(width, height);
        chronoGlass_.Build(width, height);
    } else {
        oledBase_.Build(width, height);
    }

    UpdateLabels(labels_, PerformanceView::kCells, now);
    UpdateLabels(values_, PerformanceView::kCells, now);
    UpdateLabels(units_, PerformanceView::kCells, now);
}

void PerformanceTheme::Draw(
    const PerformanceView& view,
    ThemeChoice choice,
    float k,
    MeterBatch& batch,
    std::vector<float>& shapes
) {
    const bool chrono = choice == ThemeChoice::kChrono;

    (chrono ? chronoBase_ : oledBase_).Draw();
    DrawBars(view, choice, k, batch, shapes);
    DrawText(view, choice, k);

    if (chrono) {
        chronoGlass_.Draw();
    }
}

void PerformanceTheme::DrawBars(
    const PerformanceView& view,
    ThemeChoice choice,
    float k,
    MeterBatch& batch,
    std::vector<float>& v
) {
    const float trackWidth = kCellWidth - 2 * kCellPad;
    const float y = (kBarY + kBarHeight / 2) * k, half = kBarHeight / 2 * k;
    batch.Clear();

    for (int i = 0; i < PerformanceView::kCells; i++) {
        const PerformanceCell& c = view.cells[i];
        const float left = CellX(i) + kCellPad, fill = c.fill * trackWidth;
        v.clear();

        if (!c.known || !c.bar || fill <= 0) {
            continue;
        }

        if (choice == ThemeChoice::kChrono) {
            AddTaper(v, left * k, y, half, (left + fill) * k, y, half);
        } else {
            for (float s = 0; s < fill; s += kSegment + kSegmentGap) {
                AddTaper(v, (left + s) * k, y, half, (left + std::min(s + kSegment, fill)) * k, y, half);
            }
        }

        const float* rgb = BarRgb(c.level);
        batch.Add(v, rgb[0], rgb[1], rgb[2], 1);
    }

    batch.Submit();
}

void PerformanceTheme::DrawText(const PerformanceView& view, ThemeChoice choice, float k) {
    const int labelOpacity = choice == ThemeChoice::kChrono ? kLabelOpacityChrono : kLabelOpacityOled;
    const int labelPx = static_cast<int>(std::round(kLabelSize * k));
    const int valuePx = static_cast<int>(std::round(kValueSize * k));
    const int unitPx = static_cast<int>(std::round(kUnitSize * k));
    char text[16];

    // Each text placed by its pen (the advance's left end, as the design's text boxes), so the digits keep
    // their places as they change.
    for (int i = 0; i < PerformanceView::kCells; i++) {
        const PerformanceCell& c = view.cells[i];
        const float x = (CellX(i) + kCellPad) * k;

        DrawLabelCentred(
            labels_[i],
            kLabelText[i],
            labelPx,
            RegularFace(labelPx),
            labelOpacity,
            x,
            kLabelInkMiddleY * k,
            kInk,
            true,
            0,
            kLabelTracking
        );

        if (!c.known) {
            continue;
        }

        snprintf(text, sizeof(text), "%d", c.value);
        const uint32_t rgb = c.level == PerformanceLevel::kCritical ? kCriticalRgb : kInk;
        DrawLabelCentred(
            values_[i],
            text,
            valuePx,
            SemiBoldFace(valuePx),
            100,
            x,
            kValueInkMiddleY * k,
            rgb,
            true,
            0
        );

        if (c.percent && values_[i].tex) {
            DrawLabelCentred(
                units_[i],
                "%",
                unitPx,
                RegularFace(unitPx),
                labelOpacity,
                x + values_[i].ink.advance + kUnitGap * k,
                kUnitInkMiddleY * k,
                kInk,
                true,
                0
            );
        }
    }
}

void PerformanceTheme::Destroy() {
    for (int i = 0; i < PerformanceView::kCells; i++) {
        DestroyLabel(labels_[i]);
        DestroyLabel(values_[i]);
        DestroyLabel(units_[i]);
    }

    chronoBase_.Destroy();
    chronoGlass_.Destroy();
    oledBase_.Destroy();
}

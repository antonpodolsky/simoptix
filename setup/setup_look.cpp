// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "setup_look.h"

#include "setup_draw.h"
#include "setup_parts.h"

using namespace theme;

namespace {

    // The width of the area on the right where a row's run state stands, right-aligned to its edge.
    constexpr int kRunStateWidth = 176;

    const wchar_t* DoneWord(RunVerb verb) {
        switch (verb) {
            case RunVerb::kInstall:
                return L"Installed";
            case RunVerb::kUpdate:
                return L"Updated";
            case RunVerb::kRepair:
                return L"Repaired";
            case RunVerb::kUninstall:
                break;
        }
        return L"Uninstalled";
    }

    Glyph GlyphOf(PartId id) {
        switch (id) {
            case PartId::kVrMirror:
                return Glyph::kVrMirror;
            case PartId::kOverlays:
                return Glyph::kOverlays;
            case PartId::kOpenXrLayer:
                break;
        }
        return Glyph::kOpenXrLayer;
    }

    class Painter {
    public:
        Painter(HDC dc, UINT dpi) : dc_(dc), dpi_(dpi) { SetBkMode(dc_, TRANSPARENT); }

        int Scale(int pixels) const { return ScaleToDpi(pixels, dpi_); }
        UINT Dpi() const { return dpi_; }
        HDC Dc() const { return dc_; }

        void Fill(const RECT& area, COLORREF colour) const {
            const UniqueBrush brush(CreateSolidBrush(colour));
            FillRect(dc_, &area, brush.get());
        }

        // Draws `text` on one line from `left`, `top`, and returns its right edge.
        int Text(const std::wstring& text, int left, int top, HFONT font, COLORREF colour) const {
            SelectObject(dc_, font);
            SetTextColor(dc_, colour);
            RECT area = {left, top, left, top};
            DrawTextW(dc_, text.c_str(), -1, &area, DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
            DrawTextW(dc_, text.c_str(), -1, &area, DT_SINGLELINE | DT_NOPREFIX);
            return area.right;
        }

        // Draws `text` on one line, ending at `right`.
        void
        TextRightAligned(const std::wstring& text, int right, int top, HFONT font, COLORREF colour) const {
            Text(text, right - MeasureText(font, text), top, font, colour);
        }

        // Draws `text` wrapped into `area`.
        void Wrapped(const std::wstring& text, RECT area, HFONT font, COLORREF colour) const {
            SelectObject(dc_, font);
            SetTextColor(dc_, colour);
            DrawTextW(dc_, text.c_str(), -1, &area, DT_WORDBREAK | DT_NOPREFIX);
        }

        // Where a line of `font` drawn from `top` has its baseline.
        int Baseline(int top, HFONT font) const {
            SelectObject(dc_, font);
            TEXTMETRICW metrics = {};
            GetTextMetricsW(dc_, &metrics);
            return top + metrics.tmAscent;
        }

        // The top a line of `font` needs for its baseline to be at `baseline`.
        int TopForBaseline(int baseline, HFONT font) const { return baseline - (Baseline(0, font)); }

        // The top a line of `font` needs to stand centred on `middle`.
        int TopForMiddle(int middle, HFONT font) const {
            SelectObject(dc_, font);
            TEXTMETRICW metrics = {};
            GetTextMetricsW(dc_, &metrics);
            return middle - metrics.tmHeight / 2;
        }

    private:
        HDC dc_;
        UINT dpi_;
    };

    void PaintMainArea(const Painter& paint, const ThemeFonts& fonts, const WindowView& view) {
        const int right = paint.Scale(kActionsRight);

        if (view.running) {
            const int left = paint.Scale(kRunLeft);
            const int width = paint.Scale(kRunWidth);
            const int labelTop = paint.Scale(kRunLabelTop);
            paint.Text(view.runLabel, left, labelTop, fonts.secondary.get(), kBandText3);
            paint.TextRightAligned(view.runCount, right, labelTop, fonts.secondary.get(), kBandText2);

            const int barTop = paint.Scale(kRunBarTop);
            const int barHeight = paint.Scale(kRunBarHeight);
            const int fill = static_cast<int>(width * view.barFraction + 0.5f);
            FillRoundRect(
                paint.Dc(),
                {left, barTop, left + width, barTop + barHeight},
                barHeight / 2,
                kBandTrack
            );

            // A fill under 4 px is too small to read as a bar.
            if (fill >= paint.Scale(4)) {
                FillRoundRect(
                    paint.Dc(),
                    {left, barTop, left + fill, barTop + barHeight},
                    barHeight / 2,
                    kBandAccent
                );
            }
            return;
        }

        if (!view.upToDate) {
            return;  // the main action's button stands here
        }

        const std::wstring label = L"Up to date";
        const int labelWidth = MeasureText(fonts.strong.get(), label);
        const int tick = paint.Scale(kUpToDateTick);
        const int tickLeft = right - labelWidth - paint.Scale(kUpToDateGap) - tick;
        const int top = paint.Scale(kUpToDateTop);
        DrawTick(paint.Dc(), tickLeft, top, kUpToDateTick, paint.Dpi(), kBandAccent, kOnBandAccent);
        paint.Text(
            label,
            right - labelWidth,
            paint.TopForMiddle(top + tick / 2, fonts.strong.get()),
            fonts.strong.get(),
            kBandText
        );
    }

    void PaintBand(
        const Painter& paint,
        const WindowLayout& layout,
        const ThemeFonts& fonts,
        const WindowView& view
    ) {
        paint.Fill({0, 0, layout.client.cx, layout.bandHeight}, kBand);

        const int logo = paint.Scale(kLogoSize);
        const int logoLeft = paint.Scale(kLogoLeft);
        const int logoTop = paint.Scale(kLogoTop);
        FillRoundRect(
            paint.Dc(),
            {logoLeft, logoTop, logoLeft + logo, logoTop + logo},
            paint.Scale(kLogoRadius),
            kBandAccent
        );

        const bool reason = !view.reasons.empty();
        const int textLeft = paint.Scale(kTitleLeft);
        paint.Text(
            L"SimOptix",
            textLeft,
            paint.Scale(reason ? kTitleTopWithReason : kTitleTop),
            fonts.title.get(),
            kBandText
        );
        paint.Text(
            L"Setup " + view.setupVersion,
            textLeft,
            paint.Scale(reason ? kSubtitleTopWithReason : kSubtitleTop),
            fonts.secondary.get(),
            kBandText2
        );

        for (size_t i = 0; i < view.reasons.size(); ++i) {
            const int top = paint.Scale(kReasonTop + static_cast<int>(i) * kSecondReasonHeight);
            DrawWarning(
                paint.Dc(),
                textLeft,
                top + paint.Scale(1),
                kReasonIconSize,
                paint.Dpi(),
                kReasonToClose,
                kBand
            );
            paint.Text(
                view.reasons[i],
                paint.Scale(kReasonTextLeft),
                top,
                fonts.secondary.get(),
                kReasonToClose
            );
        }

        PaintMainArea(paint, fonts, view);
    }

    void PaintRunState(
        const Painter& paint,
        const ThemeFonts& fonts,
        const RowView& row,
        int middle,
        float spinnerDeg
    ) {
        const int right = paint.Scale(kActionsRight);
        const int icon = paint.Scale(kRunIconSize);
        const int iconTop = middle - icon / 2;
        const int gap = paint.Scale(kRunStateGap);

        if (row.run == RunState::kWaiting) {
            paint.TextRightAligned(
                L"Waiting",
                right,
                paint.TopForMiddle(middle, fonts.secondary.get()),
                fonts.secondary.get(),
                kText2
            );
            return;
        }

        const bool busy = row.run == RunState::kBusy;
        const HFONT font = busy ? fonts.secondaryStrong.get() : fonts.secondary.get();
        std::wstring word = DoneWord(row.verb);
        COLORREF colour = kText;

        if (busy) {
            word = std::wstring(RunningWord(row.verb)) + L"…";
            colour = kAccentText;
        } else if (row.run == RunState::kFailed) {
            word = L"Failed";
        }

        const int wordWidth = MeasureText(font, word);
        const int iconLeft = right - wordWidth - gap - icon;
        paint.Text(word, right - wordWidth, paint.TopForMiddle(middle, font), font, colour);

        if (busy) {
            DrawSpinner(paint.Dc(), iconLeft, iconTop, paint.Dpi(), kAccentText, spinnerDeg);
        } else if (row.run == RunState::kFailed) {
            DrawError(paint.Dc(), iconLeft, iconTop, kRunIconSize, paint.Dpi(), kError, RGB(255, 255, 255));
        } else {
            DrawTick(paint.Dc(), iconLeft, iconTop, kRunIconSize, paint.Dpi(), kSuccess, RGB(255, 255, 255));
        }
    }

    void
    PaintRow(const Painter& paint, const ThemeFonts& fonts, const WindowView& view, size_t index, int top) {
        const RowView& row = view.rows[index];
        const Part& part = kParts[index];
        const int width = paint.Scale(kWidth);
        const int height = paint.Scale(kRowHeight);

        paint.Fill({0, top, width, top + height}, kList);
        paint.Fill({0, top + height - paint.Scale(1), width, top + height}, kDivider);

        const int tile = paint.Scale(kTileSize);
        const int tileLeft = paint.Scale(kTileLeft);
        const int tileTop = top + paint.Scale(kTileTop);
        const RECT tileRect = {tileLeft, tileTop, tileLeft + tile, tileTop + tile};
        FillRoundRect(
            paint.Dc(),
            tileRect,
            paint.Scale(kTileRadius),
            row.installed ? kTileInstalled : kTileNotInstalled
        );
        DrawGlyph(
            paint.Dc(),
            GlyphOf(part.id),
            tileLeft + paint.Scale(kGlyphInset),
            tileTop + paint.Scale(kGlyphInset),
            paint.Dpi(),
            row.installed ? kAccentText : kGlyphNotInstalled
        );

        const int nameLeft = paint.Scale(kNameLeft);
        const int nameTop = top + paint.Scale(kNameTop);
        const int nameRight = paint.Text(part.shownName, nameLeft, nameTop, fonts.strong.get(), kText);
        const int baseline = paint.Baseline(nameTop, fonts.strong.get());
        paint.Text(
            row.version,
            nameRight + paint.Scale(kVersionGap),
            paint.TopForBaseline(baseline, fonts.secondary.get()),
            fonts.secondary.get(),
            kText2
        );

        const int descriptionTop = top + paint.Scale(kDescriptionTop);
        const RECT description =
            {nameLeft, descriptionTop, nameLeft + paint.Scale(kDescriptionWidth), top + height};
        paint.Wrapped(part.description, description, fonts.secondary.get(), kText2);

        if (row.run != RunState::kNone) {
            PaintRunState(paint, fonts, row, top + height / 2, view.spinnerDeg);
        }
    }

}

WindowLayout LayOut(UINT dpi, size_t reasonCount) {
    const auto scale = [dpi](int pixels) { return ScaleToDpi(pixels, dpi); };
    const int grow = reasonCount > 1 ? kSecondReasonHeight : 0;
    WindowLayout layout;
    layout.dpi = dpi;
    layout.bandHeight = scale(kBandHeight + grow);

    for (size_t i = 0; i < layout.rowTops.size(); ++i) {
        layout.rowTops[i] = scale(kBandHeight + grow + static_cast<int>(i) * kRowHeight);
    }

    layout.footerTop = scale(kBandHeight + grow + static_cast<int>(kParts.size()) * kRowHeight);
    layout.client = {scale(kWidth), layout.footerTop + scale(kFooterHeight)};
    layout.main =
        {scale(kMainLeft), scale(kMainTop), scale(kMainLeft + kMainWidth), scale(kMainTop + kMainHeight)};

    const int buttonTop = layout.footerTop + scale(kFooterButtonTop);
    layout.close =
        {scale(kCloseLeft), buttonTop, scale(kCloseLeft + kCloseWidth), buttonTop + scale(kButtonHeight)};
    return layout;
}

void PaintWindow(HDC dc, const WindowLayout& layout, const ThemeFonts& fonts, const WindowView& view) {
    const Painter paint(dc, layout.dpi);

    PaintBand(paint, layout, fonts, view);

    for (size_t i = 0; i < view.rows.size(); ++i) {
        PaintRow(paint, fonts, view, i, layout.rowTops[i]);
    }

    paint.Fill({0, layout.footerTop, layout.client.cx, layout.client.cy}, kFooter);
}

RECT RunBarRect(const WindowLayout& layout) {
    const auto scale = [&layout](int pixels) { return ScaleToDpi(pixels, layout.dpi); };
    return {scale(kRunLeft), scale(kRunLabelTop), scale(kActionsRight), scale(kRunBarTop + kRunBarHeight)};
}

RECT RunStateRect(const WindowLayout& layout, size_t row) {
    const auto scale = [&layout](int pixels) { return ScaleToDpi(pixels, layout.dpi); };
    const int top = layout.rowTops[row];
    return {scale(kActionsRight - kRunStateWidth), top, scale(kActionsRight), top + scale(kRowHeight)};
}

int MeasureText(HFONT font, const std::wstring& text) {
    const HDC screen = GetDC(nullptr);

    if (!screen) {
        return 0;
    }

    const HGDIOBJ old = SelectObject(screen, font);
    SIZE size = {};
    GetTextExtentPoint32W(screen, text.c_str(), static_cast<int>(text.size()), &size);
    SelectObject(screen, old);
    ReleaseDC(nullptr, screen);
    return size.cx;
}

const wchar_t* RunningWord(RunVerb verb) {
    switch (verb) {
        case RunVerb::kInstall:
            return L"Installing";
        case RunVerb::kUpdate:
            return L"Updating";
        case RunVerb::kRepair:
            return L"Repairing";
        case RunVerb::kUninstall:
            break;
    }
    return L"Uninstalling";
}

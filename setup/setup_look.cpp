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

    // Text and fills on the window's canvas. Positions are the canvas's pixels.
    class Painter {
    public:
        explicit Painter(const Canvas& canvas) : canvas_(canvas) { SetBkMode(canvas_.dc, TRANSPARENT); }

        const Canvas& Surface() const { return canvas_; }
        int Scale(int pixels) const { return canvas_.Scale(pixels); }

        void Fill(const RECT& area, COLORREF colour) const {
            const UniqueBrush brush(CreateSolidBrush(colour));

            if (brush) {
                FillRect(canvas_.dc, &area, brush.get());
            }
        }

        // Draws `text` on one line from its top left, and returns its right edge.
        int Text(const std::wstring& text, POINT at, HFONT font, COLORREF colour) const {
            const SelectedObject selected(canvas_.dc, font);
            SetTextColor(canvas_.dc, colour);

            RECT area = {at.x, at.y, at.x, at.y};
            DrawTextW(canvas_.dc, text.c_str(), -1, &area, DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
            DrawTextW(canvas_.dc, text.c_str(), -1, &area, DT_SINGLELINE | DT_NOPREFIX);
            return area.right;
        }

        // Draws `text` on one line from its top right.
        void TextRightAligned(const std::wstring& text, POINT topRight, HFONT font, COLORREF colour) const {
            Text(text, {topRight.x - MeasureText(font, text), topRight.y}, font, colour);
        }

        void Wrapped(const std::wstring& text, RECT area, HFONT font, COLORREF colour) const {
            const SelectedObject selected(canvas_.dc, font);
            SetTextColor(canvas_.dc, colour);
            DrawTextW(canvas_.dc, text.c_str(), -1, &area, DT_WORDBREAK | DT_NOPREFIX);
        }

        // How far a line of `font` has its baseline below its top.
        int Ascent(HFONT font) const { return Metrics(font).tmAscent; }

        // The top a line of `font` needs to stand centred on `middle`.
        int TopForMiddle(int middle, HFONT font) const { return middle - Metrics(font).tmHeight / 2; }

    private:
        TEXTMETRICW Metrics(HFONT font) const {
            const SelectedObject selected(canvas_.dc, font);
            TEXTMETRICW metrics = {};
            GetTextMetricsW(canvas_.dc, &metrics);
            return metrics;
        }

        const Canvas& canvas_;
    };

    // The run's label and count above its bar, and the bar.
    void PaintRunBar(const Painter& paint, const ThemeFonts& fonts, const WindowView& view) {
        const int left = paint.Scale(kRunLeft);
        const int right = paint.Scale(kActionsRight);
        const int labelTop = paint.Scale(kRunLabelTop);
        paint.Text(view.runLabel, {left, labelTop}, fonts.secondary.get(), kBandText3);
        paint.TextRightAligned(view.runCount, {right, labelTop}, fonts.secondary.get(), kBandText2);

        const int barTop = paint.Scale(kRunBarTop);
        const int barBottom = barTop + paint.Scale(kRunBarHeight);
        const int fill = static_cast<int>((right - left) * view.barFraction + 0.5f);
        const int radius = kRunBarHeight / 2;
        FillRoundRect(paint.Surface(), {left, barTop, right, barBottom}, radius, kBandTrack);

        // A fill under 4 px is too small to read as a bar.
        if (fill >= paint.Scale(4)) {
            FillRoundRect(paint.Surface(), {left, barTop, left + fill, barBottom}, radius, kBandAccent);
        }
    }

    // A tick and "Up to date", right-aligned where the main action stands otherwise.
    void PaintUpToDate(const Painter& paint, const ThemeFonts& fonts) {
        const std::wstring label = L"Up to date";
        const HFONT font = fonts.strong.get();
        const int right = paint.Scale(kActionsRight);
        const int labelLeft = right - MeasureText(font, label);
        const int tick = paint.Scale(kUpToDateTick);
        const int top = paint.Scale(kUpToDateTop);

        const POINT tickAt = {labelLeft - paint.Scale(kUpToDateGap) - tick, top};
        DrawMark(paint.Surface(), Mark::kTick, tickAt, kUpToDateTick, {kBandAccent, kOnBandAccent});
        paint.Text(label, {labelLeft, paint.TopForMiddle(top + tick / 2, font)}, font, kBandText);
    }

    void PaintBand(
        const Painter& paint,
        const WindowLayout& layout,
        const ThemeFonts& fonts,
        const WindowView& view
    ) {
        paint.Fill({0, 0, layout.client.cx, layout.bandHeight}, kBand);

        const int logoLeft = paint.Scale(kLogoLeft);
        const int logoTop = paint.Scale(kLogoTop);
        const int logo = paint.Scale(kLogoSize);
        const RECT logoRect = {logoLeft, logoTop, logoLeft + logo, logoTop + logo};
        FillRoundRect(paint.Surface(), logoRect, kLogoRadius, kBandAccent);

        const bool hasReason = !view.reasons.empty();
        const int textLeft = paint.Scale(kTitleLeft);
        const int titleTop = paint.Scale(hasReason ? kTitleTopWithReason : kTitleTop);
        const int subtitleTop = paint.Scale(hasReason ? kSubtitleTopWithReason : kSubtitleTop);
        paint.Text(L"SimOptix", {textLeft, titleTop}, fonts.title.get(), kBandText);
        paint.Text(L"Setup " + view.setupVersion, {textLeft, subtitleTop}, fonts.secondary.get(), kBandText2);

        for (size_t i = 0; i < view.reasons.size(); ++i) {
            const int top = paint.Scale(kReasonTop + static_cast<int>(i) * kSecondReasonHeight);
            const POINT iconAt = {textLeft, top + paint.Scale(1)};
            const POINT textAt = {paint.Scale(kReasonTextLeft), top};
            DrawMark(paint.Surface(), Mark::kWarning, iconAt, kReasonIconSize, {kReasonToClose, kBand});
            paint.Text(view.reasons[i], textAt, fonts.secondary.get(), kReasonToClose);
        }

        // Otherwise the main action's button stands here.
        if (view.running) {
            PaintRunBar(paint, fonts, view);
        } else if (view.upToDate) {
            PaintUpToDate(paint, fonts);
        }
    }

    // What a row in a run shows on its right: Waiting, a spinner while busy, then a tick or a cross.
    void PaintRunState(
        const Painter& paint,
        const ThemeFonts& fonts,
        const RowView& row,
        int middle,
        float spinnerDeg
    ) {
        const int right = paint.Scale(kActionsRight);

        if (row.run == RunState::kWaiting) {
            const HFONT font = fonts.secondary.get();
            paint.TextRightAligned(L"Waiting", {right, paint.TopForMiddle(middle, font)}, font, kText2);
            return;
        }

        const bool busy = row.run == RunState::kBusy;
        const bool failed = row.run == RunState::kFailed;
        const HFONT font = busy ? fonts.secondaryStrong.get() : fonts.secondary.get();
        const std::wstring word = busy ? std::wstring(RunningWord(row.verb)) + L"…"
                                       : (failed ? L"Failed" : DoneWord(row.verb));
        const int wordLeft = right - MeasureText(font, word);
        paint.Text(word, {wordLeft, paint.TopForMiddle(middle, font)}, font, busy ? kAccentText : kText);

        const int icon = paint.Scale(kRunIconSize);
        const POINT iconAt = {wordLeft - paint.Scale(kRunStateGap) - icon, middle - icon / 2};

        if (busy) {
            DrawSpinner(paint.Surface(), iconAt, kAccentText, spinnerDeg);
        } else if (failed) {
            DrawMark(paint.Surface(), Mark::kError, iconAt, kRunIconSize, {kError, kOnFill});
        } else {
            DrawMark(paint.Surface(), Mark::kTick, iconAt, kRunIconSize, {kSuccess, kOnFill});
        }
    }

    // The part's icon on its tile: teal when installed, grey when not.
    void PaintTile(const Painter& paint, const Part& part, bool installed, int rowTop) {
        const int tile = paint.Scale(kTileSize);
        const int left = paint.Scale(kTileLeft);
        const int top = rowTop + paint.Scale(kTileTop);
        const RECT tileRect = {left, top, left + tile, top + tile};
        const COLORREF tileColour = installed ? kTileInstalled : kTileNotInstalled;
        FillRoundRect(paint.Surface(), tileRect, kTileRadius, tileColour);

        const POINT glyphAt = {left + paint.Scale(kGlyphInset), top + paint.Scale(kGlyphInset)};
        DrawGlyph(paint.Surface(), GlyphOf(part.id), glyphAt, installed ? kAccentText : kGlyphNotInstalled);
    }

    // The name, and after it on the same baseline the installed version, then what the part adds.
    void PaintRowText(
        const Painter& paint,
        const ThemeFonts& fonts,
        const Part& part,
        const RowView& row,
        int rowTop
    ) {
        const int left = paint.Scale(kNameLeft);
        const int nameTop = rowTop + paint.Scale(kNameTop);
        const int nameRight = paint.Text(part.shownName, {left, nameTop}, fonts.strong.get(), kText);

        const int baseline = nameTop + paint.Ascent(fonts.strong.get());
        const int versionLeft = nameRight + paint.Scale(kVersionGap);
        const int versionTop = baseline - paint.Ascent(fonts.secondary.get());
        paint.Text(row.version, {versionLeft, versionTop}, fonts.secondary.get(), kText2);

        const int descriptionTop = rowTop + paint.Scale(kDescriptionTop);
        const int descriptionRight = left + paint.Scale(kDescriptionWidth);
        const RECT description = {left, descriptionTop, descriptionRight, rowTop + paint.Scale(kRowHeight)};
        paint.Wrapped(part.description, description, fonts.secondary.get(), kText2);
    }

    void
    PaintRow(const Painter& paint, const ThemeFonts& fonts, const WindowView& view, size_t index, int top) {
        const RowView& row = view.rows[index];
        const Part& part = kParts[index];
        const int width = paint.Scale(kWidth);
        const int bottom = top + paint.Scale(kRowHeight);

        paint.Fill({0, top, width, bottom}, kList);
        paint.Fill({0, bottom - paint.Scale(1), width, bottom}, kDivider);

        PaintTile(paint, part, row.installed, top);
        PaintRowText(paint, fonts, part, row, top);

        if (row.run != RunState::kNone) {
            PaintRunState(paint, fonts, row, (top + bottom) / 2, view.spinnerDeg);
        }
    }

}

WindowLayout LayOut(UINT dpi, size_t reasonCount) {
    const auto scale = [dpi](int pixels) { return ScaleToDpi(pixels, dpi); };
    const int bandHeight = kBandHeight + (reasonCount > 1 ? kSecondReasonHeight : 0);

    WindowLayout layout;
    layout.dpi = dpi;
    layout.bandHeight = scale(bandHeight);

    for (size_t i = 0; i < layout.rowTops.size(); ++i) {
        layout.rowTops[i] = scale(bandHeight + static_cast<int>(i) * kRowHeight);
    }

    layout.footerTop = scale(bandHeight + static_cast<int>(kParts.size()) * kRowHeight);
    layout.client = {scale(kWidth), layout.footerTop + scale(kFooterHeight)};
    layout.main =
        {scale(kMainLeft), scale(kMainTop), scale(kMainLeft + kMainWidth), scale(kMainTop + kMainHeight)};

    const int buttonTop = layout.footerTop + scale(kFooterButtonTop);
    layout.close =
        {scale(kCloseLeft), buttonTop, scale(kCloseLeft + kCloseWidth), buttonTop + scale(kButtonHeight)};

    return layout;
}

void PaintWindow(HDC dc, const WindowLayout& layout, const ThemeFonts& fonts, const WindowView& view) {
    const Canvas canvas = {dc, layout.dpi};
    const Painter paint(canvas);

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
    const ScreenDc screen;

    if (!screen.Get()) {
        return 0;
    }

    const SelectedObject selected(screen.Get(), font);
    SIZE size = {};

    if (!GetTextExtentPoint32W(screen.Get(), text.c_str(), static_cast<int>(text.size()), &size)) {
        return 0;
    }

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

// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "setup_look.h"

#include "setup_parts.h"

namespace {

    // The layout, in pixels at 96 DPI.
    constexpr int kWidth = 620;
    constexpr int kMargin = 24;
    constexpr int kHeaderHeight = 84;
    constexpr int kTitleTop = 18;
    constexpr int kBlockedTop = 54;
    constexpr int kRowHeight = 84;
    constexpr int kRowPadding = 16;
    constexpr int kVersionGap = 8;       // between a name and the version after it
    constexpr int kDescriptionTop = 38;  // below the row's top
    constexpr int kButtonWidth = 88;
    constexpr int kButtonHeight = 30;
    constexpr int kButtonGap = 8;
    constexpr int kWideButtonWidth = 112;
    constexpr int kFooterHeight = 60;

    constexpr double kTitleScale = 1.7;  // the title against the message font

    // Windows 11's light colours.
    constexpr COLORREF kBackground = RGB(255, 255, 255);
    constexpr COLORREF kText = RGB(26, 26, 26);
    constexpr COLORREF kSecondaryText = RGB(96, 96, 96);
    constexpr COLORREF kDivider = RGB(229, 229, 229);
    constexpr COLORREF kFooter = RGB(243, 243, 243);
    constexpr COLORREF kAttention = RGB(157, 93, 0);

    UniqueFont MakeFont(const LOGFONTW& base, double scale, LONG weight) {
        LOGFONTW font = base;
        font.lfHeight = static_cast<LONG>(base.lfHeight * scale);
        font.lfWeight = weight;
        font.lfQuality = CLEARTYPE_QUALITY;
        return UniqueFont(CreateFontIndirectW(&font));
    }

    class Painter {
    public:
        Painter(HDC dc, UINT dpi) : dc_(dc), dpi_(dpi) { SetBkMode(dc_, TRANSPARENT); }

        int Scale(int pixels) const { return MulDiv(pixels, static_cast<int>(dpi_), 96); }

        void Fill(const RECT& area, COLORREF colour) const {
            const UniqueBrush brush(CreateSolidBrush(colour));
            FillRect(dc_, &area, brush.get());
        }

        void Line(int left, int right, int y) const {
            const RECT line = {left, y, right, y + Scale(1)};
            Fill(line, kDivider);
        }

        // Draws `text` wrapped into the width of `area` from its top, and returns the bottom of the text.
        int Text(const std::wstring& text, RECT area, HFONT font, COLORREF colour) const {
            if (text.empty()) {
                return area.top;
            }

            SelectObject(dc_, font);
            SetTextColor(dc_, colour);
            RECT measured = area;
            DrawTextW(dc_, text.c_str(), -1, &measured, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
            DrawTextW(dc_, text.c_str(), -1, &area, DT_WORDBREAK | DT_NOPREFIX);
            return measured.bottom;
        }

        // Draws `text` on one line with its baseline at `baseline`, and returns its right edge. Texts in
        // two sizes drawn on one baseline line up as words of one line do.
        int Label(const std::wstring& text, int left, int baseline, HFONT font, COLORREF colour) const {
            SelectObject(dc_, font);
            SetTextColor(dc_, colour);

            TEXTMETRICW metrics = {};
            GetTextMetricsW(dc_, &metrics);

            RECT area = {left, baseline - metrics.tmAscent, left, baseline + metrics.tmDescent};
            DrawTextW(dc_, text.c_str(), -1, &area, DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
            DrawTextW(dc_, text.c_str(), -1, &area, DT_SINGLELINE | DT_NOPREFIX);
            return area.right;
        }

        // Where a line of `font` drawn from `top` has its baseline.
        int Baseline(int top, HFONT font) const {
            SelectObject(dc_, font);
            TEXTMETRICW metrics = {};
            GetTextMetricsW(dc_, &metrics);
            return top + metrics.tmAscent;
        }

    private:
        HDC dc_;
        UINT dpi_;
    };

}

LookFonts MakeFonts(UINT dpi) {
    NONCLIENTMETRICSW metrics = {};
    metrics.cbSize = sizeof(metrics);
    LookFonts fonts;

    if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi)) {
        return fonts;
    }

    fonts.body = MakeFont(metrics.lfMessageFont, 1, FW_NORMAL);
    fonts.strong = MakeFont(metrics.lfMessageFont, 1, FW_SEMIBOLD);
    fonts.title = MakeFont(metrics.lfMessageFont, kTitleScale, FW_SEMIBOLD);
    return fonts;
}

WindowLayout LayOut(UINT dpi) {
    const auto scale = [dpi](int pixels) { return MulDiv(pixels, static_cast<int>(dpi), 96); };
    const int width = scale(kWidth);
    const int margin = scale(kMargin);
    const int buttonWidth = scale(kButtonWidth);
    const int buttonHeight = scale(kButtonHeight);
    WindowLayout layout;
    layout.dpi = dpi;

    layout.headerBottom = scale(kHeaderHeight);
    int top = layout.headerBottom;

    for (size_t i = 0; i < kParts.size(); ++i) {
        const RECT row = {0, top, width, top + scale(kRowHeight)};
        const int buttonTop = top + (row.bottom - row.top - buttonHeight) / 2;
        const int sideLeft = width - margin - buttonWidth;
        const int mainLeft = sideLeft - scale(kButtonGap) - buttonWidth;

        layout.rows[i] = row;
        layout.sideSlots[i] = {sideLeft, buttonTop, sideLeft + buttonWidth, buttonTop + buttonHeight};
        layout.mainSlots[i] = {mainLeft, buttonTop, mainLeft + buttonWidth, buttonTop + buttonHeight};
        top = row.bottom;
    }

    layout.footer = {0, top, width, top + scale(kFooterHeight)};
    layout.client = {width, layout.footer.bottom};

    const int footerTop = layout.footer.top + (scale(kFooterHeight) - buttonHeight) / 2;
    const int footerBottom = footerTop + buttonHeight;
    const int wideWidth = scale(kWideButtonWidth);
    const int secondLeft = margin + wideWidth + scale(kButtonGap);

    layout.installAll = {margin, footerTop, margin + wideWidth, footerBottom};
    layout.uninstallAll = {secondLeft, footerTop, secondLeft + wideWidth, footerBottom};
    layout.close = {width - margin - buttonWidth, footerTop, width - margin, footerBottom};
    return layout;
}

void PaintWindow(HDC dc, const WindowLayout& layout, const LookFonts& fonts, const WindowTexts& texts) {
    const Painter paint(dc, layout.dpi);
    const int margin = paint.Scale(kMargin);
    const int right = layout.client.cx - margin;

    paint.Fill({0, 0, layout.client.cx, layout.client.cy}, kBackground);

    const int titleBaseline = paint.Baseline(paint.Scale(kTitleTop), fonts.title.get());
    const int titleRight = paint.Label(L"SimOptix Setup", margin, titleBaseline, fonts.title.get(), kText);
    paint.Label(
        texts.setupVersion,
        titleRight + paint.Scale(kVersionGap),
        titleBaseline,
        fonts.body.get(),
        kSecondaryText
    );

    const RECT blocked = {margin, paint.Scale(kBlockedTop), right, layout.headerBottom};
    paint.Text(texts.blockedBy, blocked, fonts.body.get(), kAttention);

    for (size_t i = 0; i < kParts.size(); ++i) {
        const RECT& row = layout.rows[i];
        const int textRight = layout.mainSlots[i].left - margin;
        const int nameBaseline = paint.Baseline(row.top + paint.Scale(kRowPadding), fonts.strong.get());
        const RECT description = {margin, row.top + paint.Scale(kDescriptionTop), textRight, row.bottom};

        paint.Line(0, layout.client.cx, row.top);
        const int
            nameRight = paint.Label(kParts[i].shownName, margin, nameBaseline, fonts.strong.get(), kText);
        paint.Label(
            texts.installedVersions[i],
            nameRight + paint.Scale(kVersionGap),
            nameBaseline,
            fonts.body.get(),
            kSecondaryText
        );
        paint.Text(kParts[i].description, description, fonts.body.get(), kSecondaryText);
    }

    paint.Fill(layout.footer, kFooter);
    paint.Line(0, layout.client.cx, layout.footer.top);
}

COLORREF FooterColour() {
    return kFooter;
}

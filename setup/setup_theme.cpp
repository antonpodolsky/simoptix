// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "setup_theme.h"

#include <dwmapi.h>

#include <cwchar>

namespace {

    // GDI lists each weight of Segoe UI Variable as a family of its own, its name cut at 31 characters
    // (LF_FACESIZE). Windows 10 has none of them, so each comes with Segoe UI's face of the same weight.
    struct Face {
        const wchar_t* variable;
        const wchar_t* fallback;
    };

    constexpr Face kDisplaySemibold = {L"Segoe UI Variable Display Semib", L"Segoe UI Semibold"};
    constexpr Face kTextRegular = {L"Segoe UI Variable Text", L"Segoe UI"};
    constexpr Face kTextSemibold = {L"Segoe UI Variable Text Semibold", L"Segoe UI Semibold"};

    HFONT CreateFace(const wchar_t* name, int pixels, UINT dpi) {
        LOGFONTW font = {};
        font.lfHeight = -ScaleToDpi(pixels, dpi);  // negative: the em size, not the cell height
        font.lfWeight = FW_NORMAL;
        font.lfCharSet = DEFAULT_CHARSET;
        font.lfQuality = CLEARTYPE_QUALITY;
        wcsncpy_s(font.lfFaceName, name, _TRUNCATE);
        return CreateFontIndirectW(&font);
    }

    // True when Windows gave the font the face it was asked for, and not a substitute.
    bool HasFace(HFONT font, const wchar_t* name) {
        const HDC screen = GetDC(nullptr);

        if (!screen) {
            return false;
        }

        const HGDIOBJ old = SelectObject(screen, font);
        wchar_t chosen[LF_FACESIZE] = {};
        GetTextFaceW(screen, LF_FACESIZE, chosen);
        SelectObject(screen, old);
        ReleaseDC(nullptr, screen);
        return _wcsicmp(chosen, name) == 0;
    }

    UniqueFont MakeFont(const Face& face, int pixels, UINT dpi) {
        UniqueFont font(CreateFace(face.variable, pixels, dpi));

        if (font && HasFace(font.get(), face.variable)) {
            return font;
        }

        return UniqueFont(CreateFace(face.fallback, pixels, dpi));
    }

}

ThemeFonts MakeThemeFonts(UINT dpi) {
    ThemeFonts fonts;
    fonts.title = MakeFont(kDisplaySemibold, 24, dpi);
    fonts.dialogTitle = MakeFont(kDisplaySemibold, 16, dpi);
    fonts.action = MakeFont(kTextSemibold, 15, dpi);
    fonts.strong = MakeFont(kTextSemibold, 14, dpi);
    fonts.body = MakeFont(kTextRegular, 14, dpi);
    fonts.secondary = MakeFont(kTextRegular, 12, dpi);
    fonts.secondaryStrong = MakeFont(kTextSemibold, 12, dpi);
    return fonts;
}

void ApplyBandCaption(HWND window) {
    // Windows 11's attributes (build 22000 and later), by number: the SDK in use is older than them.
    constexpr DWORD kUseImmersiveDarkMode = 20;  // light caption buttons on the dark band
    constexpr DWORD kBorderColour = 34;
    constexpr DWORD kCaptionColour = 35;
    constexpr DWORD kTextColour = 36;

    const BOOL dark = TRUE;
    const COLORREF border = theme::kBandBorder;
    const COLORREF caption = theme::kBand;
    const COLORREF text = theme::kBandText3;

    // Windows 10 refuses them and keeps its own title bar, which is fine.
    (void)DwmSetWindowAttribute(window, kUseImmersiveDarkMode, &dark, sizeof(dark));
    (void)DwmSetWindowAttribute(window, kBorderColour, &border, sizeof(border));
    (void)DwmSetWindowAttribute(window, kCaptionColour, &caption, sizeof(caption));
    (void)DwmSetWindowAttribute(window, kTextColour, &text, sizeof(text));
}

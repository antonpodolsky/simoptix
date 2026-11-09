// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The look of SimOptix Setup in numbers: its colours, the sizes and places of everything at 96 DPI, and
// its fonts. The design is "Pit wall": a dark band with the logo, the name and the one big action, a
// white list of the parts, and a light footer. Light mode only. Knows nothing about what is drawn where
// or what the buttons do.
#pragma once

#include <windows.h>

#include "setup_gdi.h"

namespace theme {

    // The band and everything on it.
    constexpr COLORREF kBand = RGB(0x14, 0x1A, 0x21);
    constexpr COLORREF kBandBorder = RGB(0x0B, 0x0F, 0x14);  // the window's frame
    constexpr COLORREF kBandText = RGB(0xFF, 0xFF, 0xFF);
    constexpr COLORREF kBandText2 = RGB(0xA4, 0xAE, 0xB8);  // setup's version, "n of N"
    constexpr COLORREF kBandText3 = RGB(0xE6, 0xEA, 0xEE);  // the caption's title, the run label
    constexpr COLORREF kBandAccent = RGB(0x3C, 0xC4, 0xCC);
    constexpr COLORREF kBandAccentHover = RGB(0x5C, 0xD0, 0xD7);
    constexpr COLORREF kBandAccentPressed = RGB(0x2F, 0xAE, 0xB6);
    constexpr COLORREF kOnBandAccent = RGB(0x06, 0x29, 0x2C);
    constexpr COLORREF kBandDisabled = RGB(0x26, 0x2E, 0x37);
    constexpr COLORREF kOnBandDisabled = RGB(0x75, 0x80, 0x8B);
    constexpr COLORREF kBandTrack = RGB(0x2B, 0x35, 0x40);
    constexpr COLORREF kReasonToClose = RGB(0xFF, 0xC0, 0x61);

    // The list, the footer and the dialogs' bodies.
    constexpr COLORREF kList = RGB(0xFF, 0xFF, 0xFF);
    constexpr COLORREF kDivider = RGB(0xEB, 0xEB, 0xEB);
    constexpr COLORREF kFooter = RGB(0xF6, 0xF6, 0xF6);
    constexpr COLORREF kText = RGB(0x1A, 0x1A, 0x1A);
    constexpr COLORREF kText2 = RGB(0x5F, 0x5F, 0x5F);
    constexpr COLORREF kAccentText = RGB(0x0F, 0x7B, 0x83);
    constexpr COLORREF kAccentTextPressed = RGB(0x0B, 0x5F, 0x65);
    constexpr COLORREF kTextDisabled = RGB(0xAB, 0xAB, 0xAB);
    constexpr COLORREF kTextActionHover = RGB(0xF2, 0xF2, 0xF2);
    constexpr COLORREF kTextActionPressed = RGB(0xEA, 0xEA, 0xEA);
    constexpr COLORREF kTileInstalled = RGB(0xDC, 0xEF, 0xF0);
    constexpr COLORREF kTileNotInstalled = RGB(0xEA, 0xEA, 0xEA);
    constexpr COLORREF kGlyphNotInstalled = RGB(0x6E, 0x6E, 0x6E);
    constexpr COLORREF kDangerText = RGB(0xC4, 0x2B, 0x1C);
    constexpr COLORREF kDangerTextPressed = RGB(0x92, 0x1F, 0x14);
    constexpr COLORREF kDangerTextDisabled = RGB(0xE0, 0xA9, 0xA3);
    constexpr COLORREF kFooterActionHover = RGB(0xEB, 0xEB, 0xEB);
    constexpr COLORREF kFooterActionPressed = RGB(0xE3, 0xE3, 0xE3);
    constexpr COLORREF kOnFill = RGB(0xFF, 0xFF, 0xFF);  // a filled button's label, ticks and crosses
    constexpr COLORREF kDangerFill = RGB(0xC4, 0x2B, 0x1C);
    constexpr COLORREF kDangerFillHover = RGB(0xAE, 0x26, 0x18);
    constexpr COLORREF kDangerFillPressed = RGB(0x92, 0x1F, 0x14);
    constexpr COLORREF kDangerFillDisabled = RGB(0xF5, 0xD7, 0xD4);
    constexpr COLORREF kOnDangerFillDisabled = RGB(0xB0, 0x64, 0x5C);
    constexpr COLORREF kAccentFill = RGB(0x0F, 0x7B, 0x83);
    constexpr COLORREF kAccentFillHover = RGB(0x0C, 0x6B, 0x72);
    constexpr COLORREF kAccentFillPressed = RGB(0x0A, 0x5C, 0x62);
    constexpr COLORREF kAccentFillDisabled = RGB(0xC2, 0xC2, 0xC2);
    constexpr COLORREF kStandard = RGB(0xFE, 0xFE, 0xFE);
    constexpr COLORREF kStandardHover = RGB(0xF6, 0xF6, 0xF6);
    constexpr COLORREF kStandardPressed = RGB(0xF0, 0xF0, 0xF0);
    constexpr COLORREF kStandardStroke = RGB(0xE3, 0xE3, 0xE3);
    constexpr COLORREF kStandardBottom = RGB(0xC8, 0xC8, 0xC8);
    constexpr COLORREF kStandardDisabled = RGB(0xF7, 0xF7, 0xF7);
    constexpr COLORREF kStandardDisabledStroke = RGB(0xE8, 0xE8, 0xE8);
    constexpr COLORREF kStandardDisabledText = RGB(0xA3, 0xA3, 0xA3);
    constexpr COLORREF kSuccess = RGB(0x0F, 0x7B, 0x0F);
    constexpr COLORREF kError = RGB(0xC4, 0x2B, 0x1C);
    constexpr COLORREF kFocusRing = RGB(0x1A, 0x1A, 0x1A);  // on the band it is white

    // The main window, in pixels at 96 DPI, x and y from the client area's top left.
    constexpr int kWidth = 600;
    constexpr int kMargin = 24;
    constexpr int kBandHeight = 112;
    constexpr int kSecondReasonHeight = 16;  // the band grows by this for a second reason to close
    constexpr int kLogoLeft = 24, kLogoTop = 32, kLogoSize = 48, kLogoRadius = 11;
    constexpr int kTitleLeft = 88, kTitleTop = 31, kTitleTopWithReason = 22;
    constexpr int kSubtitleTop = 65, kSubtitleTopWithReason = 56;
    constexpr int kReasonTop = 80, kReasonIconSize = 14, kReasonTextLeft = 108;
    constexpr int kMainLeft = 408, kMainTop = 34, kMainWidth = 168, kMainHeight = 44, kMainRadius = 6;
    constexpr int kUpToDateTop = 46, kUpToDateTick = 20, kUpToDateGap = 8;
    constexpr int kRunLeft = 376, kRunLabelTop = 38, kRunWidth = 200, kRunBarTop = 62, kRunBarHeight = 4;
    constexpr int kRowHeight = 80;
    constexpr int kTileLeft = 24, kTileTop = 20, kTileSize = 40, kTileRadius = 6, kGlyphInset = 8;
    constexpr int kNameLeft = 80, kNameTop = 14, kVersionGap = 8;
    constexpr int kDescriptionTop = 36, kDescriptionWidth = 304;
    constexpr int kActionsRight = 576, kActionsTop = 24, kActionGap = 4;
    constexpr int kTextActionPadding = 20, kTextActionHeight = 32, kTextActionRadius = 4;
    constexpr int kRunStateGap = 8, kRunIconSize = 16;
    constexpr int kFooterHeight = 56;
    constexpr int kUninstallAllLeft = 14, kFooterButtonTop = 12;
    constexpr int kCloseLeft = 480, kCloseWidth = 96, kButtonHeight = 32, kButtonRadius = 4;

    // A button's control is this much larger on each side than the button it draws, so that the
    // keyboard focus ring, 2 px wide and 1 px outside the button, falls inside the control.
    constexpr int kFocusOutset = 3;

    // The dialogs.
    constexpr int kDialogWidth = 440, kDialogHeaderHeight = 64, kDialogFooterHeight = 64;
    constexpr int kDialogIconLeft = 24, kDialogIconTop = 20, kDialogIconSize = 24;
    constexpr int kDialogTitleLeft = 60, kDialogTitleTop = 21;
    constexpr int kDialogBodyTop = 20, kDialogBodyBottom = 24, kDialogParagraphGap = 12;
    constexpr int kDialogButtonWidth = 132, kDialogButtonGap = 8;
    constexpr int kDialogButtonTop = 16;  // below the footer's top

    // Timing, in milliseconds.
    constexpr int kFrameMs = 16;          // the timer while anything moves
    constexpr int kBarEaseMs = 200;       // the bar's fill eases to its new width in this long
    constexpr int kSpinnerTurnMs = 1000;  // one turn of the spinner
    constexpr int kTicksShownMs = 3000;   // the results stay this long after a run

}

// The design's type at one display scale. Segoe UI Variable on Windows 11, Segoe UI where it is missing.
struct ThemeFonts {
    UniqueFont title;            // 24 px, Display Semibold: SimOptix
    UniqueFont dialogTitle;      // 16 px, Display Semibold
    UniqueFont action;           // 15 px, Text Semibold: the main action
    UniqueFont strong;           // 14 px, Text Semibold: part names, Install, Update, Repair, Up to date
    UniqueFont body;             // 14 px, Text: Uninstall, Close, dialog text
    UniqueFont secondary;        // 12 px, Text: versions, descriptions, reasons, run labels
    UniqueFont secondaryStrong;  // 12 px, Text Semibold: Installing…
};

ThemeFonts MakeThemeFonts(UINT dpi);

// Gives the window's title bar the band's colour, so that it and the band read as one. Windows 11 does
// it. Windows 10 keeps its own title bar, and the band starts under it.
void ApplyBandCaption(HWND window);

// The resource id of setup's icon (setup.rc).
constexpr int kSetupIconId = 1;

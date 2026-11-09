// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The look of SimOptix Setup's buttons. They are real Windows buttons, so the keyboard, the focus and
// screen readers work as with any button, and setup draws each one itself when Windows asks for it
// (NM_CUSTOMDRAW). The kinds are the design's: the main action on the band, the text actions in the
// rows, the red text action in the footer, the standard button, and the dialogs' filled buttons. Knows
// nothing about what a button does.
#pragma once

#include <windows.h>

#include <string>

#include "setup_theme.h"

enum class ButtonKind {
    kMain,        // Install all, Update all: the band's teal button
    kTextAccent,  // Install, Update, Repair: teal text, a fill only under the mouse
    kText,        // Uninstall: grey text, the same
    kDangerText,  // Uninstall all: red text in the footer, the same
    kStandard,    // Close, and the dialogs' other buttons
    kAccentFill,  // a dialog's default button
    kDangerFill,  // the Uninstall all dialog's confirm
};

struct ButtonState {
    bool hot = false;  // the mouse is over it
    bool pressed = false;
    bool disabled = false;
    bool focused = false;  // has the focus, and the keyboard is in use, so the ring shows
};

// The font a kind's label is drawn in.
HFONT ButtonFont(ButtonKind kind, const ThemeFonts& fonts);

// Draws the whole control in `dc`, which is the button's own: the ground it stands on (the band, the
// list or the footer) where it shows around the button, the button, its label and the focus ring.
void PaintButton(
    HDC dc,
    const RECT& control,
    ButtonKind kind,
    const ButtonState& state,
    COLORREF ground,
    const std::wstring& label,
    const ThemeFonts& fonts,
    UINT dpi
);

// A text action's width: its label and 10 px on each side.
int TextActionWidth(ButtonKind kind, const std::wstring& label, const ThemeFonts& fonts, UINT dpi);

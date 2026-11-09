// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The look of SimOptix Setup's window: where everything goes at a display scale, its colours and fonts,
// and the drawing of everything in it but the buttons. Knows nothing about what the buttons do or how
// the texts are decided.
#pragma once

#include <windows.h>

#include <array>
#include <memory>
#include <string>
#include <type_traits>

struct GdiDeleter {
    void operator()(HGDIOBJ object) const { DeleteObject(object); }
};

using UniqueFont = std::unique_ptr<std::remove_pointer_t<HFONT>, GdiDeleter>;
using UniqueBrush = std::unique_ptr<std::remove_pointer_t<HBRUSH>, GdiDeleter>;

struct LookFonts {
    UniqueFont body;
    UniqueFont strong;  // the parts' names
    UniqueFont title;
};

// Windows' message font at `dpi`, in three weights and sizes.
LookFonts MakeFonts(UINT dpi);

// Every place in the window, in pixels at one display scale.
struct WindowLayout {
    UINT dpi = 96;
    SIZE client = {};
    int headerBottom = 0;
    std::array<RECT, 3> rows = {};       // in the order of kParts
    std::array<RECT, 3> mainSlots = {};  // the left button slot of each row
    std::array<RECT, 3> sideSlots = {};  // the right one
    RECT footer = {};
    RECT installAll = {};
    RECT uninstallAll = {};
    RECT close = {};
};

WindowLayout LayOut(UINT dpi);

struct WindowTexts {
    std::wstring blockedBy;  // what to close before a part can be changed, empty when nothing
};

void PaintWindow(HDC dc, const WindowLayout& layout, const LookFonts& fonts, const WindowTexts& texts);

// The colour behind the footer's buttons. The other buttons stand on the window's white.
COLORREF FooterColour();

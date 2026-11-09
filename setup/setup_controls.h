// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The buttons of SimOptix Setup's window: made once, then placed, labelled, shown and enabled for what
// the window shows, and drawn in setup_buttons.h's look when Windows asks. The main action on the band,
// each row's text actions right-aligned with Uninstall last, Uninstall all and Close in the footer.
// Knows nothing about what a click does: the window hears it by the ids below.
#pragma once

#include <windows.h>
#include <commctrl.h>

#include <array>

#include "setup_buttons.h"
#include "setup_look.h"
#include "setup_status.h"

constexpr int kMainActionId = 100;
constexpr int kRowMainId = 110;       // + the part's index
constexpr int kRowUninstallId = 120;  // + the part's index
constexpr int kUninstallAllId = 130;
constexpr int kCloseId = 131;

// What the window allows now.
struct ButtonPermissions {
    bool busy = false;  // a run goes on: every button waits
    bool canInstallAll = false;
    bool canUninstallAll = false;
    bool anythingInstalled = false;  // the main action reads Update all, and Install all otherwise
};

class WindowButtons {
public:
    void Create(HWND window, HINSTANCE instance);

    void Place(
        const WindowLayout& layout,
        const ThemeFonts& fonts,
        const WindowView& view,
        const std::array<PartStatus, 3>& statuses,
        const ButtonPermissions& allowed
    ) const;

    bool Owns(HWND hwnd) const { return Find(hwnd) != nullptr; }

    // Answers NM_CUSTOMDRAW for one of these buttons by drawing it whole.
    LRESULT Paint(const NMCUSTOMDRAW& draw, const ThemeFonts& fonts, UINT dpi) const;

private:
    // A button and how it is drawn: its kind and the colour of the area it stands on.
    struct Button {
        HWND hwnd = nullptr;
        ButtonKind kind = ButtonKind::kStandard;
        COLORREF ground = theme::kList;
    };

    void PlaceRow(
        size_t row,
        const WindowLayout& layout,
        const ThemeFonts& fonts,
        bool inRun,
        const PartStatus& status,
        bool busy
    ) const;
    const Button* Find(HWND hwnd) const;

    Button main_;
    std::array<Button, 3> rowMain_;
    std::array<Button, 3> rowUninstall_;
    Button uninstallAll_;
    Button close_;
};

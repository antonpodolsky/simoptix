// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "setup_controls.h"

#include <iterator>

using namespace theme;

namespace {

    HWND MakeButton(HWND window, HINSTANCE instance, const wchar_t* label, int id) {
        return CreateWindowExW(
            0,
            L"BUTTON",
            label,
            WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
            0,
            0,
            0,
            0,
            window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            instance,
            nullptr
        );
    }

    // The control is larger than the button it draws by the focus ring's room on each side.
    void PlaceControl(HWND control, const RECT& button, UINT dpi) {
        const int outset = ScaleToDpi(kFocusOutset, dpi);
        MoveWindow(
            control,
            button.left - outset,
            button.top - outset,
            button.right - button.left + 2 * outset,
            button.bottom - button.top + 2 * outset,
            TRUE
        );
    }

    void Show(HWND control, bool shown, bool enabled) {
        EnableWindow(control, enabled);
        ShowWindow(control, shown ? SW_SHOW : SW_HIDE);
    }

}

void WindowButtons::Create(HWND window, HINSTANCE instance) {
    main_ = {MakeButton(window, instance, L"Install all", kMainActionId), ButtonKind::kMain, kBand};

    for (size_t i = 0; i < kParts.size(); ++i) {
        const int index = static_cast<int>(i);
        rowMain_[i] = {
            MakeButton(window, instance, L"Install", kRowMainId + index),
            ButtonKind::kTextAccent,
            kList
        };
        rowUninstall_[i] = {
            MakeButton(window, instance, L"Uninstall", kRowUninstallId + index),
            ButtonKind::kText,
            kList
        };
    }

    uninstallAll_ = {
        MakeButton(window, instance, L"Uninstall all", kUninstallAllId),
        ButtonKind::kDangerText,
        kFooter
    };
    close_ = {MakeButton(window, instance, L"Close", kCloseId), ButtonKind::kStandard, kFooter};
}

void WindowButtons::Place(
    const WindowLayout& layout,
    const ThemeFonts& fonts,
    const WindowView& view,
    const std::array<PartStatus, 3>& statuses,
    const ButtonPermissions& allowed
) const {
    const UINT dpi = layout.dpi;

    SetWindowTextW(main_.hwnd, allowed.anythingInstalled ? L"Update all" : L"Install all");
    PlaceControl(main_.hwnd, layout.main, dpi);
    Show(main_.hwnd, !view.running && !view.upToDate, !allowed.busy && allowed.canInstallAll);

    for (size_t i = 0; i < kParts.size(); ++i) {
        PlaceRow(i, layout, fonts, view.rows[i].run != RunState::kNone, statuses[i], allowed.busy);
    }

    const int width = TextActionWidth(ButtonKind::kDangerText, L"Uninstall all", fonts, dpi);
    const int left = ScaleToDpi(kUninstallAllLeft, dpi);
    PlaceControl(uninstallAll_.hwnd, {left, layout.close.top, left + width, layout.close.bottom}, dpi);
    Show(uninstallAll_.hwnd, true, !allowed.busy && allowed.canUninstallAll);

    PlaceControl(close_.hwnd, layout.close, dpi);
    Show(close_.hwnd, true, !allowed.busy);
}

// A row's text actions stand right-aligned, Uninstall last. A row in a run shows its state instead.
void WindowButtons::PlaceRow(
    size_t row,
    const WindowLayout& layout,
    const ThemeFonts& fonts,
    bool inRun,
    const PartStatus& status,
    bool busy
) const {
    const bool showMain = !inRun && status.action != PartAction::kNone;
    const bool showUninstall = !inRun && status.hasSomething;
    const UINT dpi = layout.dpi;
    const int top = layout.rowTops[row] + ScaleToDpi(kActionsTop, dpi);
    const int bottom = top + ScaleToDpi(kTextActionHeight, dpi);
    int right = ScaleToDpi(kActionsRight, dpi);

    if (showUninstall) {
        const int width = TextActionWidth(ButtonKind::kText, L"Uninstall", fonts, dpi);
        PlaceControl(rowUninstall_[row].hwnd, {right - width, top, right, bottom}, dpi);
        right -= width + ScaleToDpi(kActionGap, dpi);
    }

    if (showMain) {
        const wchar_t* label = ActionLabel(status.action);
        const int width = TextActionWidth(ButtonKind::kTextAccent, label, fonts, dpi);
        SetWindowTextW(rowMain_[row].hwnd, label);
        PlaceControl(rowMain_[row].hwnd, {right - width, top, right, bottom}, dpi);
    }

    Show(rowMain_[row].hwnd, showMain, !busy && status.installBlockedBy.empty());
    Show(rowUninstall_[row].hwnd, showUninstall, !busy && status.removeBlockedBy.empty());
}

LRESULT WindowButtons::Paint(const NMCUSTOMDRAW& draw, const ThemeFonts& fonts, UINT dpi) const {
    const Button* button = Find(draw.hdr.hwndFrom);

    if (!button || draw.dwDrawStage != CDDS_PREPAINT) {
        return CDRF_DODEFAULT;
    }

    const LRESULT buttonState = SendMessageW(button->hwnd, BM_GETSTATE, 0, 0);
    const LRESULT cues = SendMessageW(button->hwnd, WM_QUERYUISTATE, 0, 0);

    ButtonState state;
    state.hot = (buttonState & BST_HOT) != 0;
    state.pressed = (buttonState & BST_PUSHED) != 0;
    state.disabled = !IsWindowEnabled(button->hwnd);
    state.focused = (buttonState & BST_FOCUS) != 0 && !(cues & UISF_HIDEFOCUS);

    wchar_t label[64] = {};
    GetWindowTextW(button->hwnd, label, static_cast<int>(std::size(label)));
    PaintButton(draw.hdc, draw.rc, button->kind, state, button->ground, label, fonts, dpi);
    return CDRF_SKIPDEFAULT;
}

const WindowButtons::Button* WindowButtons::Find(HWND hwnd) const {
    for (const Button* button : {&main_, &uninstallAll_, &close_}) {
        if (button->hwnd == hwnd) {
            return button;
        }
    }

    for (size_t i = 0; i < kParts.size(); ++i) {
        if (rowMain_[i].hwnd == hwnd) {
            return &rowMain_[i];
        }

        if (rowUninstall_[i].hwnd == hwnd) {
            return &rowUninstall_[i];
        }
    }

    return nullptr;
}

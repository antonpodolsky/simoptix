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
        const int width = button.right - button.left + 2 * outset;
        const int height = button.bottom - button.top + 2 * outset;

        // A button that can't be moved stays where it was, which shows: nothing to recover here.
        (void)MoveWindow(control, button.left - outset, button.top - outset, width, height, TRUE);
    }

    void SetLabel(HWND control, const wchar_t* label) {
        // A label that can't be set keeps its old text, and the button still works.
        (void)SetWindowTextW(control, label);
    }

    // EnableWindow and ShowWindow return the control's earlier state, not an error.
    void Show(HWND control, bool shown, bool enabled) {
        EnableWindow(control, enabled);
        ShowWindow(control, shown ? SW_SHOW : SW_HIDE);
    }

}

void WindowButtons::Create(HWND window, HINSTANCE instance) {
    main_ = {MakeButton(window, instance, L"Install all", kMainActionId), ButtonKind::kMain, kBand};

    for (size_t i = 0; i < kParts.size(); ++i) {
        const int index = static_cast<int>(i);
        const HWND install = MakeButton(window, instance, L"Install", kRowMainId + index);
        const HWND uninstall = MakeButton(window, instance, L"Uninstall", kRowUninstallId + index);
        rowMain_[i] = {install, ButtonKind::kTextAccent, kList};
        rowUninstall_[i] = {uninstall, ButtonKind::kText, kList};
    }

    const HWND uninstallAll = MakeButton(window, instance, L"Uninstall all", kUninstallAllId);
    uninstallAll_ = {uninstallAll, ButtonKind::kDangerText, kFooter};
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

    SetLabel(main_.hwnd, allowed.anythingInstalled ? L"Update all" : L"Install all");
    PlaceControl(main_.hwnd, layout.main, dpi);
    Show(main_.hwnd, !view.running && !view.upToDate, !allowed.busy && allowed.canInstallAll);

    const RowPlacement placement = {layout, fonts, allowed.busy};

    for (size_t i = 0; i < kParts.size(); ++i) {
        PlaceRow(i, placement, view.rows[i].run != RunState::kNone, statuses[i]);
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
    const RowPlacement& placement,
    bool inRun,
    const PartStatus& status
) const {
    const bool showMain = !inRun && status.action != PartAction::kNone;
    const bool showUninstall = !inRun && status.hasSomething;
    const UINT dpi = placement.layout.dpi;
    const int top = placement.layout.rowTops[row] + ScaleToDpi(kActionsTop, dpi);
    const int bottom = top + ScaleToDpi(kTextActionHeight, dpi);
    int right = ScaleToDpi(kActionsRight, dpi);

    if (showUninstall) {
        const int width = TextActionWidth(ButtonKind::kText, L"Uninstall", placement.fonts, dpi);
        PlaceControl(rowUninstall_[row].hwnd, {right - width, top, right, bottom}, dpi);
        right -= width + ScaleToDpi(kActionGap, dpi);
    }

    if (showMain) {
        const wchar_t* label = ActionLabel(status.action);
        const int width = TextActionWidth(ButtonKind::kTextAccent, label, placement.fonts, dpi);
        SetLabel(rowMain_[row].hwnd, label);
        PlaceControl(rowMain_[row].hwnd, {right - width, top, right, bottom}, dpi);
    }

    Show(rowMain_[row].hwnd, showMain, !placement.busy && status.installBlockedBy.empty());
    Show(rowUninstall_[row].hwnd, showUninstall, !placement.busy && status.removeBlockedBy.empty());
}

LRESULT WindowButtons::Paint(const NMCUSTOMDRAW& draw, const ThemeFonts& fonts, UINT dpi) const {
    const Button* button = Find(draw.hdr.hwndFrom);

    if (!button || draw.dwDrawStage != CDDS_PREPAINT) {
        return CDRF_DODEFAULT;
    }

    wchar_t label[64] = {};
    GetWindowTextW(button->hwnd, label, static_cast<int>(std::size(label)));

    const ButtonLook look = {button->kind, ReadButtonState(button->hwnd), button->ground, label};
    PaintButton({draw.hdc, dpi}, draw.rc, look, fonts);

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

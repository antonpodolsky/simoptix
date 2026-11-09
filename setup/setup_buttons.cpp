// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "setup_buttons.h"

#include <commctrl.h>

#include "setup_draw.h"
#include "setup_look.h"

using namespace theme;

namespace {

    // A fill of kNoFill draws no body: the text actions at rest.
    constexpr COLORREF kNoFill = CLR_INVALID;

    // How a button looks in one state.
    struct Colours {
        COLORREF fill = kNoFill;
        COLORREF stroke = kNoFill;  // with a darker bottom edge: the standard button only
        COLORREF bottom = kNoFill;
        COLORREF text = kText;
    };

    // A text action's colours: its text in each state, and a fill only under the mouse.
    struct TextActionSet {
        COLORREF rest;
        COLORREF hot;
        COLORREF pressed;
        COLORREF disabled;
        COLORREF hotFill;
        COLORREF pressedFill;
    };

    // A filled button's colours: its fill in each state, and its label.
    struct FillSet {
        COLORREF rest;
        COLORREF hot;
        COLORREF pressed;
        COLORREF disabled;
        COLORREF text;
        COLORREF disabledText;
    };

    constexpr TextActionSet kAccentTextSet = {
        kAccentText,
        kAccentText,
        kAccentTextPressed,
        kTextDisabled,
        kTextActionHover,
        kTextActionPressed,
    };

    constexpr TextActionSet kGreyTextSet = {
        kText2,
        kText,
        kText,
        kTextDisabled,
        kTextActionHover,
        kTextActionPressed,
    };

    constexpr TextActionSet kDangerTextSet = {
        kDangerText,
        kDangerText,
        kDangerTextPressed,
        kDangerTextDisabled,
        kFooterActionHover,
        kFooterActionPressed,
    };

    constexpr FillSet kMainSet = {
        kBandAccent,
        kBandAccentHover,
        kBandAccentPressed,
        kBandDisabled,
        kOnBandAccent,
        kOnBandDisabled,
    };

    constexpr FillSet kAccentFillSet = {
        kAccentFill,
        kAccentFillHover,
        kAccentFillPressed,
        kAccentFillDisabled,
        kOnFill,
        kOnFill,
    };

    constexpr FillSet kDangerFillSet = {
        kDangerFill,
        kDangerFillHover,
        kDangerFillPressed,
        kDangerFillDisabled,
        kOnFill,
        kOnDangerFillDisabled,
    };

    Colours TextActionColours(const ButtonState& state, const TextActionSet& set) {
        if (state.disabled) {
            return {kNoFill, kNoFill, kNoFill, set.disabled};
        }

        if (state.pressed) {
            return {set.pressedFill, kNoFill, kNoFill, set.pressed};
        }

        if (state.hot) {
            return {set.hotFill, kNoFill, kNoFill, set.hot};
        }

        return {kNoFill, kNoFill, kNoFill, set.rest};
    }

    Colours FillColours(const ButtonState& state, const FillSet& set) {
        if (state.disabled) {
            return {set.disabled, kNoFill, kNoFill, set.disabledText};
        }

        const COLORREF fill = state.pressed ? set.pressed : (state.hot ? set.hot : set.rest);
        return {fill, kNoFill, kNoFill, set.text};
    }

    Colours StandardColours(const ButtonState& state) {
        if (state.disabled) {
            return {
                kStandardDisabled,
                kStandardDisabledStroke,
                kStandardDisabledStroke,
                kStandardDisabledText
            };
        }

        if (state.pressed) {
            return {kStandardPressed, kStandardStroke, kStandardStroke, kText2};
        }

        return {state.hot ? kStandardHover : kStandard, kStandardStroke, kStandardBottom, kText};
    }

    Colours ColoursOf(ButtonKind kind, const ButtonState& state) {
        switch (kind) {
            case ButtonKind::kMain:
                return FillColours(state, kMainSet);
            case ButtonKind::kTextAccent:
                return TextActionColours(state, kAccentTextSet);
            case ButtonKind::kText:
                return TextActionColours(state, kGreyTextSet);
            case ButtonKind::kDangerText:
                return TextActionColours(state, kDangerTextSet);
            case ButtonKind::kStandard:
                return StandardColours(state);
            case ButtonKind::kAccentFill:
                return FillColours(state, kAccentFillSet);
            case ButtonKind::kDangerFill:
                break;
        }
        return FillColours(state, kDangerFillSet);
    }

    int RadiusOf(ButtonKind kind) {
        return kind == ButtonKind::kMain ? kMainRadius : kButtonRadius;
    }

    void DrawLabel(
        const Canvas& canvas,
        RECT body,
        const ButtonLook& look,
        const ThemeFonts& fonts,
        COLORREF colour
    ) {
        const SelectedObject font(canvas.dc, ButtonFont(look.kind, fonts));
        SetBkMode(canvas.dc, TRANSPARENT);
        SetTextColor(canvas.dc, colour);
        constexpr UINT kCentred = DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
        DrawTextW(canvas.dc, look.label.c_str(), -1, &body, kCentred);
    }

}

ButtonState ReadButtonState(HWND button) {
    const LRESULT buttonState = SendMessageW(button, BM_GETSTATE, 0, 0);
    const LRESULT cues = SendMessageW(button, WM_QUERYUISTATE, 0, 0);

    ButtonState state;
    state.hot = (buttonState & BST_HOT) != 0;
    state.pressed = (buttonState & BST_PUSHED) != 0;
    state.disabled = !IsWindowEnabled(button);
    state.focused = (buttonState & BST_FOCUS) != 0 && !(cues & UISF_HIDEFOCUS);
    return state;
}

HFONT ButtonFont(ButtonKind kind, const ThemeFonts& fonts) {
    switch (kind) {
        case ButtonKind::kMain:
            return fonts.action.get();
        case ButtonKind::kTextAccent:
        case ButtonKind::kAccentFill:
            return fonts.strong.get();
        case ButtonKind::kText:
        case ButtonKind::kDangerText:
        case ButtonKind::kStandard:
        case ButtonKind::kDangerFill:
            break;
    }
    return fonts.body.get();
}

void PaintButton(const Canvas& canvas, const RECT& control, const ButtonLook& look, const ThemeFonts& fonts) {
    const UniqueBrush ground(CreateSolidBrush(look.ground));

    if (ground) {
        FillRect(canvas.dc, &control, ground.get());
    }

    const int outset = canvas.Scale(kFocusOutset);
    const RECT body =
        {control.left + outset, control.top + outset, control.right - outset, control.bottom - outset};
    const int radius = RadiusOf(look.kind);
    const Colours colours = ColoursOf(look.kind, look.state);

    if (colours.stroke != kNoFill) {
        FillStrokedRoundRect(canvas, body, radius, {colours.fill, colours.stroke, colours.bottom});
    } else if (colours.fill != kNoFill) {
        FillRoundRect(canvas, body, radius, colours.fill);
    }

    DrawLabel(canvas, body, look, fonts, colours.text);

    if (look.state.focused && !look.state.disabled) {
        DrawFocusRing(canvas, body, radius, look.ground == kBand ? kBandText : kFocusRing);
    }
}

int TextActionWidth(ButtonKind kind, const std::wstring& label, const ThemeFonts& fonts, UINT dpi) {
    return MeasureText(ButtonFont(kind, fonts), label) + ScaleToDpi(kTextActionPadding, dpi);
}

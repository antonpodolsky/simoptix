// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "setup_buttons.h"

#include "setup_draw.h"
#include "setup_look.h"

using namespace theme;

namespace {

    // How a button looks in one state. A fill of kNoFill draws no body: the text actions at rest.
    constexpr COLORREF kNoFill = CLR_INVALID;

    struct Colours {
        COLORREF fill = kNoFill;
        COLORREF stroke = kNoFill;  // with a darker bottom edge: the standard button only
        COLORREF bottom = kNoFill;
        COLORREF text = kText;
    };

    Colours MainColours(const ButtonState& state) {
        if (state.disabled) {
            return {kBandDisabled, kNoFill, kNoFill, kOnBandDisabled};
        }

        const COLORREF fill = state.pressed ? kBandAccentPressed
                                            : (state.hot ? kBandAccentHover : kBandAccent);
        return {fill, kNoFill, kNoFill, kOnBandAccent};
    }

    Colours
    TextColours(const ButtonState& state, COLORREF rest, COLORREF hot, COLORREF pressed, COLORREF disabled) {
        if (state.disabled) {
            return {kNoFill, kNoFill, kNoFill, disabled};
        }

        if (state.pressed) {
            return {kTextActionPressed, kNoFill, kNoFill, pressed};
        }

        if (state.hot) {
            return {kTextActionHover, kNoFill, kNoFill, hot};
        }

        return {kNoFill, kNoFill, kNoFill, rest};
    }

    Colours DangerTextColours(const ButtonState& state) {
        if (state.disabled) {
            return {kNoFill, kNoFill, kNoFill, kDangerTextDisabled};
        }

        if (state.pressed) {
            return {kFooterActionPressed, kNoFill, kNoFill, kDangerTextPressed};
        }

        return {state.hot ? kFooterActionHover : kNoFill, kNoFill, kNoFill, kDangerText};
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

    Colours FillColours(
        const ButtonState& state,
        COLORREF rest,
        COLORREF hot,
        COLORREF pressed,
        COLORREF disabled,
        COLORREF disabledText
    ) {
        if (state.disabled) {
            return {disabled, kNoFill, kNoFill, disabledText};
        }

        const COLORREF fill = state.pressed ? pressed : (state.hot ? hot : rest);
        return {fill, kNoFill, kNoFill, RGB(255, 255, 255)};
    }

    Colours ColoursOf(ButtonKind kind, const ButtonState& state) {
        switch (kind) {
            case ButtonKind::kMain:
                return MainColours(state);
            case ButtonKind::kTextAccent:
                return TextColours(state, kAccentText, kAccentText, kAccentTextPressed, kTextDisabled);
            case ButtonKind::kText:
                return TextColours(state, kText2, kText, kText, kTextDisabled);
            case ButtonKind::kDangerText:
                return DangerTextColours(state);
            case ButtonKind::kStandard:
                return StandardColours(state);
            case ButtonKind::kAccentFill:
                return FillColours(
                    state,
                    kAccentFill,
                    kAccentFillHover,
                    kAccentFillPressed,
                    RGB(0xC2, 0xC2, 0xC2),
                    RGB(255, 255, 255)
                );
            case ButtonKind::kDangerFill:
                break;
        }
        return FillColours(
            state,
            kDangerFill,
            kDangerFillHover,
            kDangerFillPressed,
            RGB(0xF5, 0xD7, 0xD4),
            RGB(0xB0, 0x64, 0x5C)
        );
    }

    int RadiusOf(ButtonKind kind) {
        return kind == ButtonKind::kMain ? kMainRadius : kButtonRadius;
    }

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

void PaintButton(
    HDC dc,
    const RECT& control,
    ButtonKind kind,
    const ButtonState& state,
    COLORREF ground,
    const std::wstring& label,
    const ThemeFonts& fonts,
    UINT dpi
) {
    const UniqueBrush groundBrush(CreateSolidBrush(ground));
    FillRect(dc, &control, groundBrush.get());

    const int outset = ScaleToDpi(kFocusOutset, dpi);
    const RECT body =
        {control.left + outset, control.top + outset, control.right - outset, control.bottom - outset};
    const int radius = ScaleToDpi(RadiusOf(kind), dpi);
    const Colours colours = ColoursOf(kind, state);

    if (colours.stroke != kNoFill) {
        FillStrokedRoundRect(dc, body, radius, colours.fill, colours.stroke, colours.bottom);
    } else if (colours.fill != kNoFill) {
        FillRoundRect(dc, body, radius, colours.fill);
    }

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, colours.text);
    SelectObject(dc, ButtonFont(kind, fonts));
    RECT text = body;
    DrawTextW(dc, label.c_str(), -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    if (state.focused && !state.disabled) {
        DrawFocusRing(dc, body, radius, ground == kBand ? kBandText : kFocusRing, dpi);
    }
}

int TextActionWidth(ButtonKind kind, const std::wstring& label, const ThemeFonts& fonts, UINT dpi) {
    return MeasureText(ButtonFont(kind, fonts), label) + ScaleToDpi(kTextActionPadding, dpi);
}

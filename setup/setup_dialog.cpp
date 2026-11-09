// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "setup_dialog.h"

#include <commctrl.h>

#include "setup_draw.h"
#include "setup_theme.h"

using namespace theme;

namespace {

    const wchar_t* const kClassName = L"simoptix_setup_question";
    const DWORD kStyle = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    constexpr int kFirstButtonId = 100;

    class Question {
    public:
        Question(const DialogSpec& spec, size_t* answer) : spec_(spec), answer_(answer) {}

        bool Create(HWND owner);
        bool IsOpen() const { return open_; }
        HWND Window() const { return hwnd_; }

    private:
        static LRESULT CALLBACK Procedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
        LRESULT Handle(UINT message, WPARAM wParam, LPARAM lParam);

        int Scale(int pixels) const { return ScaleToDpi(pixels, dpi_); }
        int BodyHeight() const;
        void Paint(HDC dc) const;
        LRESULT PaintButton(NMCUSTOMDRAW* draw) const;
        void Answer(size_t button);

        const DialogSpec& spec_;
        size_t* answer_;
        HWND hwnd_ = nullptr;
        UINT dpi_ = 96;
        ThemeFonts fonts_;
        std::array<HWND, 2> buttons_ = {};
        bool open_ = true;
    };

    int Question::BodyHeight() const {
        const HDC screen = GetDC(nullptr);
        int height = 0;

        if (!screen) {
            return height;
        }

        const int width = Scale(kDialogWidth - 2 * kMargin);
        const HGDIOBJ old = SelectObject(screen, fonts_.body.get());

        for (size_t i = 0; i < spec_.paragraphs.size(); ++i) {
            RECT area = {0, 0, width, 0};
            DrawTextW(
                screen,
                spec_.paragraphs[i].c_str(),
                -1,
                &area,
                DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT
            );
            height += area.bottom + (i > 0 ? Scale(kDialogParagraphGap) : 0);
        }

        SelectObject(screen, old);
        ReleaseDC(nullptr, screen);
        return Scale(kDialogBodyTop) + height + Scale(kDialogBodyBottom);
    }

    bool Question::Create(HWND owner) {
        dpi_ = GetDpiForWindow(owner);
        fonts_ = MakeThemeFonts(dpi_);

        WNDCLASSEXW windowClass = {};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = Procedure;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszClassName = kClassName;
        RegisterClassExW(&windowClass);  // fails harmlessly when an earlier question registered it

        const int width = Scale(kDialogWidth);
        const int height = Scale(kDialogHeaderHeight) + BodyHeight() + Scale(kDialogFooterHeight);
        RECT frame = {0, 0, width, height};
        AdjustWindowRectExForDpi(&frame, kStyle, FALSE, 0, dpi_);

        RECT ownerRect = {};
        GetWindowRect(owner, &ownerRect);
        const int frameWidth = frame.right - frame.left;
        const int frameHeight = frame.bottom - frame.top;
        const int left = (ownerRect.left + ownerRect.right - frameWidth) / 2;
        const int top = (ownerRect.top + ownerRect.bottom - frameHeight) / 2;

        hwnd_ = CreateWindowExW(
            WS_EX_DLGMODALFRAME,
            kClassName,
            L"SimOptix Setup",
            kStyle,
            left,
            top,
            frameWidth,
            frameHeight,
            owner,
            nullptr,
            windowClass.hInstance,
            this
        );

        if (!hwnd_) {
            return false;
        }

        ApplyBandCaption(hwnd_);

        // The buttons, right-aligned in the footer, the default one with the focus.
        const int buttonWidth = Scale(kDialogButtonWidth);
        const int outset = Scale(kFocusOutset);
        const int buttonTop = height - Scale(kDialogFooterHeight) + Scale(16);
        int right = width - Scale(kMargin);

        for (size_t i = buttons_.size(); i-- > 0;) {
            const DWORD defaultStyle = i == spec_.defaultButton ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON;
            buttons_[i] = CreateWindowExW(
                0,
                L"BUTTON",
                spec_.buttons[i].label.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | defaultStyle,
                right - buttonWidth - outset,
                buttonTop - outset,
                buttonWidth + 2 * outset,
                Scale(kButtonHeight) + 2 * outset,
                hwnd_,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFirstButtonId + i)),
                windowClass.hInstance,
                nullptr
            );
            right -= buttonWidth + Scale(kDialogButtonGap);
        }

        ShowWindow(hwnd_, SW_SHOW);
        SetFocus(buttons_[spec_.defaultButton]);
        return true;
    }

    LRESULT CALLBACK Question::Procedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }

        auto* question = reinterpret_cast<Question*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        if (!question || !question->hwnd_) {
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        // An exception escaping into Windows would end setup on the spot.
        try {
            return question->Handle(message, wParam, lParam);
        } catch (...) {
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
    }

    LRESULT Question::Handle(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
            case WM_COMMAND: {
                const int id = LOWORD(wParam);

                // Enter, Escape and the default button all give the default answer.
                if (id == IDOK || id == IDCANCEL) {
                    Answer(spec_.defaultButton);
                } else if (id >= kFirstButtonId && id < kFirstButtonId + static_cast<int>(buttons_.size())) {
                    Answer(static_cast<size_t>(id - kFirstButtonId));
                }
                return 0;
            }

            case WM_CLOSE:
                Answer(spec_.defaultButton);
                return 0;

            case WM_NOTIFY: {
                auto* draw = reinterpret_cast<NMCUSTOMDRAW*>(lParam);

                if (draw->hdr.code == NM_CUSTOMDRAW) {
                    return PaintButton(draw);
                }
                break;
            }

            case WM_ERASEBKGND:
                return 1;  // WM_PAINT covers the whole window

            case WM_PAINT: {
                PAINTSTRUCT paint;
                const HDC dc = BeginPaint(hwnd_, &paint);
                Paint(dc);
                EndPaint(hwnd_, &paint);
                return 0;
            }
        }

        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }

    void Question::Paint(HDC dc) const {
        RECT client = {};
        GetClientRect(hwnd_, &client);
        const int header = Scale(kDialogHeaderHeight);
        const int footerTop = client.bottom - Scale(kDialogFooterHeight);

        const UniqueBrush band(CreateSolidBrush(kBand));
        const UniqueBrush list(CreateSolidBrush(kList));
        const UniqueBrush footer(CreateSolidBrush(kFooter));
        const RECT headerRect = {0, 0, client.right, header};
        const RECT bodyRect = {0, header, client.right, footerTop};
        const RECT footerRect = {0, footerTop, client.right, client.bottom};
        FillRect(dc, &headerRect, band.get());
        FillRect(dc, &bodyRect, list.get());
        FillRect(dc, &footerRect, footer.get());

        DrawWarning(
            dc,
            Scale(kDialogIconLeft),
            Scale(kDialogIconTop),
            kDialogIconSize,
            dpi_,
            kReasonToClose,
            kBand
        );

        SetBkMode(dc, TRANSPARENT);
        SelectObject(dc, fonts_.dialogTitle.get());
        SetTextColor(dc, kBandText);
        RECT title = {Scale(kDialogTitleLeft), Scale(kDialogTitleTop), client.right - Scale(kMargin), header};
        DrawTextW(dc, spec_.title.c_str(), -1, &title, DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        SelectObject(dc, fonts_.body.get());
        int top = header + Scale(kDialogBodyTop);

        for (size_t i = 0; i < spec_.paragraphs.size(); ++i) {
            SetTextColor(dc, i == 0 ? kText : kText2);
            RECT area = {Scale(kMargin), top, client.right - Scale(kMargin), footerTop};
            RECT measured = area;
            DrawTextW(
                dc,
                spec_.paragraphs[i].c_str(),
                -1,
                &measured,
                DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT
            );
            DrawTextW(dc, spec_.paragraphs[i].c_str(), -1, &area, DT_WORDBREAK | DT_NOPREFIX);
            top = measured.bottom + Scale(kDialogParagraphGap);
        }
    }

    LRESULT Question::PaintButton(NMCUSTOMDRAW* draw) const {
        if (draw->dwDrawStage != CDDS_PREPAINT) {
            return CDRF_DODEFAULT;
        }

        const HWND button = draw->hdr.hwndFrom;
        const size_t index = button == buttons_[0] ? 0 : 1;
        const LRESULT buttonState = SendMessageW(button, BM_GETSTATE, 0, 0);
        const LRESULT cues = SendMessageW(button, WM_QUERYUISTATE, 0, 0);

        ButtonState state;
        state.hot = (buttonState & BST_HOT) != 0;
        state.pressed = (buttonState & BST_PUSHED) != 0;
        state.disabled = !IsWindowEnabled(button);
        state.focused = (buttonState & BST_FOCUS) != 0 && !(cues & UISF_HIDEFOCUS);

        ::PaintButton(
            draw->hdc,
            draw->rc,
            spec_.buttons[index].kind,
            state,
            kFooter,
            spec_.buttons[index].label,
            fonts_,
            dpi_
        );
        return CDRF_SKIPDEFAULT;
    }

    void Question::Answer(size_t button) {
        *answer_ = button;
        open_ = false;
    }

}

size_t AskSetupQuestion(HWND owner, const DialogSpec& spec) {
    size_t answer = spec.defaultButton;
    Question question(spec, &answer);

    EnableWindow(owner, FALSE);

    if (!question.Create(owner)) {
        EnableWindow(owner, TRUE);
        return answer;
    }

    // The question's own message loop, as a modal dialog has: the owner stays disabled until it ends.
    MSG message;

    while (question.IsOpen() && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(question.Window(), &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    // A WM_QUIT taken from the queue here belongs to the main loop.
    if (question.IsOpen()) {
        PostQuitMessage(static_cast<int>(message.wParam));
    }

    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    DestroyWindow(question.Window());
    return answer;
}

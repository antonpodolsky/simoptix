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

    // Registers the questions' window class. Another question may have registered it already, and that
    // registration serves as well.
    void RegisterQuestionClass(WNDPROC procedure, HINSTANCE instance) {
        WNDCLASSEXW windowClass = {};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = procedure;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszClassName = kClassName;

        // Fails only when the class is registered already, or when the window can't be made either:
        // CreateWindowExW then reports it.
        (void)RegisterClassExW(&windowClass);
    }

    // The window's frame for a client area of `client`, centred over `owner`.
    RECT FrameOver(HWND owner, SIZE client, UINT dpi) {
        RECT frame = {0, 0, client.cx, client.cy};
        AdjustWindowRectExForDpi(&frame, kStyle, FALSE, 0, dpi);

        RECT ownerRect = {};
        GetWindowRect(owner, &ownerRect);

        const int width = frame.right - frame.left;
        const int height = frame.bottom - frame.top;
        const int left = (ownerRect.left + ownerRect.right - width) / 2;
        const int top = (ownerRect.top + ownerRect.bottom - height) / 2;
        return {left, top, left + width, top + height};
    }

    class Question {
    public:
        explicit Question(const DialogSpec& spec) : spec_(spec), answer_(spec.defaultButton) {}

        bool Create(HWND owner);
        bool IsOpen() const { return open_; }
        HWND Window() const { return hwnd_; }
        size_t Answer() const { return answer_; }

    private:
        static LRESULT CALLBACK Procedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
        LRESULT Handle(UINT message, WPARAM wParam, LPARAM lParam);

        int Scale(int pixels) const { return ScaleToDpi(pixels, dpi_); }
        int BodyHeight() const;
        void CreateButtons(SIZE client, HINSTANCE instance);
        void Paint(HDC dc) const;
        void PaintHeader(const Canvas& canvas, const RECT& header) const;
        void PaintBody(const Canvas& canvas, const RECT& body) const;
        LRESULT PaintButtonControl(const NMCUSTOMDRAW& draw) const;
        void Choose(size_t button);

        const DialogSpec& spec_;
        size_t answer_;
        HWND hwnd_ = nullptr;
        UINT dpi_ = 96;
        ThemeFonts fonts_;
        std::array<HWND, 2> buttons_ = {};
        bool open_ = true;
    };

    int Question::BodyHeight() const {
        const ScreenDc screen;

        if (!screen.Get()) {
            return 0;
        }

        const SelectedObject font(screen.Get(), fonts_.body.get());
        const int width = Scale(kDialogWidth - 2 * kMargin);
        int height = Scale(kDialogBodyTop) + Scale(kDialogBodyBottom);

        for (size_t i = 0; i < spec_.paragraphs.size(); ++i) {
            constexpr UINT kMeasure = DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT;
            RECT area = {0, 0, width, 0};
            DrawTextW(screen.Get(), spec_.paragraphs[i].c_str(), -1, &area, kMeasure);
            height += area.bottom + (i > 0 ? Scale(kDialogParagraphGap) : 0);
        }

        return height;
    }

    bool Question::Create(HWND owner) {
        dpi_ = GetDpiForWindow(owner);
        fonts_ = MakeThemeFonts(dpi_);

        const HINSTANCE instance = GetModuleHandleW(nullptr);
        RegisterQuestionClass(Procedure, instance);

        const SIZE client = {
            Scale(kDialogWidth),
            Scale(kDialogHeaderHeight) + BodyHeight() + Scale(kDialogFooterHeight),
        };

        const RECT frame = FrameOver(owner, client, dpi_);

        hwnd_ = CreateWindowExW(
            WS_EX_DLGMODALFRAME,
            kClassName,
            L"SimOptix Setup",
            kStyle,
            frame.left,
            frame.top,
            frame.right - frame.left,
            frame.bottom - frame.top,
            owner,
            nullptr,
            instance,
            this
        );

        if (!hwnd_) {
            return false;
        }

        ApplyBandCaption(hwnd_);
        CreateButtons(client, instance);

        ShowWindow(hwnd_, SW_SHOW);
        SetFocus(buttons_[spec_.defaultButton]);
        return true;
    }

    // The buttons stand right-aligned in the footer, in the spec's order.
    void Question::CreateButtons(SIZE client, HINSTANCE instance) {
        const int outset = Scale(kFocusOutset);
        const int width = Scale(kDialogButtonWidth) + 2 * outset;
        const int height = Scale(kButtonHeight) + 2 * outset;
        const int top = client.cy - Scale(kDialogFooterHeight) + Scale(kDialogButtonTop) - outset;
        int right = client.cx - Scale(kMargin) + outset;

        for (size_t i = buttons_.size(); i-- > 0;) {
            const DWORD style = i == spec_.defaultButton ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON;

            buttons_[i] = CreateWindowExW(
                0,
                L"BUTTON",
                spec_.buttons[i].label.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | style,
                right - width,
                top,
                width,
                height,
                hwnd_,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFirstButtonId + i)),
                instance,
                nullptr
            );

            right -= Scale(kDialogButtonWidth + kDialogButtonGap);
        }
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
                const int buttonCount = static_cast<int>(buttons_.size());

                // Enter and Escape give the default answer.
                if (id == IDOK || id == IDCANCEL) {
                    Choose(spec_.defaultButton);
                } else if (id >= kFirstButtonId && id < kFirstButtonId + buttonCount) {
                    Choose(static_cast<size_t>(id - kFirstButtonId));
                }

                return 0;
            }

            case WM_CLOSE:
                Choose(spec_.defaultButton);
                return 0;

            case WM_NOTIFY: {
                const auto* draw = reinterpret_cast<const NMCUSTOMDRAW*>(lParam);

                if (draw->hdr.code == NM_CUSTOMDRAW) {
                    return PaintButtonControl(*draw);
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
        const Canvas canvas = {dc, dpi_};
        RECT client = {};
        GetClientRect(hwnd_, &client);

        const int headerBottom = Scale(kDialogHeaderHeight);
        const int footerTop = client.bottom - Scale(kDialogFooterHeight);
        PaintHeader(canvas, {0, 0, client.right, headerBottom});
        PaintBody(canvas, {0, headerBottom, client.right, footerTop});

        const UniqueBrush footer(CreateSolidBrush(kFooter));
        const RECT footerRect = {0, footerTop, client.right, client.bottom};

        if (footer) {
            FillRect(dc, &footerRect, footer.get());
        }
    }

    // The band's colour, the warning mark and the question.
    void Question::PaintHeader(const Canvas& canvas, const RECT& header) const {
        const UniqueBrush band(CreateSolidBrush(kBand));

        if (band) {
            FillRect(canvas.dc, &header, band.get());
        }

        const POINT iconAt = {Scale(kDialogIconLeft), Scale(kDialogIconTop)};
        DrawMark(canvas, Mark::kWarning, iconAt, kDialogIconSize, {kReasonToClose, kBand});

        const SelectedObject font(canvas.dc, fonts_.dialogTitle.get());
        SetBkMode(canvas.dc, TRANSPARENT);
        SetTextColor(canvas.dc, kBandText);

        RECT title =
            {Scale(kDialogTitleLeft), Scale(kDialogTitleTop), header.right - Scale(kMargin), header.bottom};
        DrawTextW(canvas.dc, spec_.title.c_str(), -1, &title, DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    // The paragraphs, the first in the main text colour and the rest in grey.
    void Question::PaintBody(const Canvas& canvas, const RECT& body) const {
        const UniqueBrush list(CreateSolidBrush(kList));

        if (list) {
            FillRect(canvas.dc, &body, list.get());
        }

        const SelectedObject font(canvas.dc, fonts_.body.get());
        SetBkMode(canvas.dc, TRANSPARENT);
        int top = body.top + Scale(kDialogBodyTop);

        for (size_t i = 0; i < spec_.paragraphs.size(); ++i) {
            const wchar_t* text = spec_.paragraphs[i].c_str();
            RECT area = {Scale(kMargin), top, body.right - Scale(kMargin), body.bottom};
            RECT measured = area;
            DrawTextW(canvas.dc, text, -1, &measured, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);

            SetTextColor(canvas.dc, i == 0 ? kText : kText2);
            DrawTextW(canvas.dc, text, -1, &area, DT_WORDBREAK | DT_NOPREFIX);
            top = measured.bottom + Scale(kDialogParagraphGap);
        }
    }

    LRESULT Question::PaintButtonControl(const NMCUSTOMDRAW& draw) const {
        if (draw.dwDrawStage != CDDS_PREPAINT) {
            return CDRF_DODEFAULT;
        }

        const size_t index = draw.hdr.hwndFrom == buttons_[0] ? 0 : 1;
        const DialogButton& button = spec_.buttons[index];
        const ButtonLook look = {button.kind, ReadButtonState(draw.hdr.hwndFrom), kFooter, button.label};
        PaintButton({draw.hdc, dpi_}, draw.rc, look, fonts_);

        return CDRF_SKIPDEFAULT;
    }

    void Question::Choose(size_t button) {
        answer_ = button;
        open_ = false;
    }

}

size_t AskSetupQuestion(HWND owner, const DialogSpec& spec) {
    Question question(spec);
    EnableWindow(owner, FALSE);

    if (!question.Create(owner)) {
        EnableWindow(owner, TRUE);
        return question.Answer();
    }

    // The question's own message loop, as a modal dialog has: the owner stays disabled until it ends.
    MSG message = {};

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

    return question.Answer();
}

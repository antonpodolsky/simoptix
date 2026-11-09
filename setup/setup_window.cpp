// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "setup_window.h"

#include <commctrl.h>

#include <array>
#include <string>
#include <vector>

#include "setup_controls.h"
#include "setup_draw.h"
#include "setup_install.h"
#include "setup_look.h"
#include "setup_payload.h"
#include "setup_probe.h"
#include "setup_progress.h"
#include "setup_questions.h"
#include "setup_run.h"
#include "setup_signature.h"
#include "setup_status.h"
#include "setup_version.h"

using namespace theme;

namespace {

    constexpr UINT_PTR kFrameTimer = 1;    // while the spinner turns or the bar moves
    constexpr UINT_PTR kResultsTimer = 2;  // the results of a run, shown for a moment

    const wchar_t* const kClassName = L"simoptix_setup";
    const wchar_t* const kTitle = L"SimOptix Setup";
    const DWORD kStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;

    class SetupWindow {
    public:
        explicit SetupWindow(HINSTANCE instance) : instance_(instance) {}

        bool Create();

    private:
        static LRESULT CALLBACK Procedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
        LRESULT Handle(UINT message, WPARAM wParam, LPARAM lParam);

        void ApplyDpi(UINT dpi);
        void FitWindow();
        void PaintBuffered();

        void Refresh();
        void UpdateView();
        void PlaceButtons();
        std::vector<PartId> PartsToInstall() const;
        std::vector<PartId> PartsToUninstall() const;
        bool AnythingInstalled() const;

        void OnCommand(int id);
        bool ConfirmLayer(const std::vector<PartId>& parts);
        void StartRun(Change change, const std::vector<PartId>& parts);
        void OnPartStarted(size_t index);
        void OnPartDone(size_t index, const std::wstring& error);
        void OnRunFinished();
        void OnResultsShown();
        void OnFrame();

        HINSTANCE instance_;
        HWND hwnd_ = nullptr;
        ThemeFonts fonts_;
        WindowLayout layout_;

        WindowButtons buttons_;

        SignatureKind setupLayerSignature_ = SignatureKind::kNone;
        Facts facts_;
        std::array<PartStatus, 3> statuses_;  // in the order of kParts
        WindowView view_;

        // A run of changes, from its start until its results have been shown.
        ChangeRun run_;
        RunProgress progress_;
        bool busy_ = false;
    };

    bool SetupWindow::Create() {
        EmbeddedFile layer;
        std::wstring error;

        if (FindEmbeddedFile(PartOf(PartId::kOpenXrLayer).dllResource, &layer, &error)) {
            setupLayerSignature_ = CheckEmbeddedSignature(layer, PartOf(PartId::kOpenXrLayer).dllName);
        }

        WNDCLASSEXW windowClass = {};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = Procedure;
        windowClass.hInstance = instance_;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(kSetupIconId));
        windowClass.lpszClassName = kClassName;

        if (!RegisterClassExW(&windowClass)) {
            return false;
        }

        hwnd_ = CreateWindowExW(
            0,
            kClassName,
            kTitle,
            kStyle,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            nullptr,
            nullptr,
            instance_,
            this
        );

        if (!hwnd_) {
            return false;
        }

        ApplyBandCaption(hwnd_);
        buttons_.Create(hwnd_, instance_);
        fonts_ = MakeThemeFonts(GetDpiForWindow(hwnd_));
        Refresh();
        ApplyDpi(GetDpiForWindow(hwnd_));
        ShowWindow(hwnd_, SW_SHOWNORMAL);
        return true;
    }

    LRESULT CALLBACK SetupWindow::Procedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }

        auto* window = reinterpret_cast<SetupWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        if (!window || !window->hwnd_) {
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        // An exception escaping into Windows would end setup on the spot, halfway through a change.
        try {
            return window->Handle(message, wParam, lParam);
        } catch (...) {
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
    }

    LRESULT SetupWindow::Handle(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
            case WM_ACTIVATE:
                if (LOWORD(wParam) != WA_INACTIVE && !busy_) {
                    Refresh();
                }
                break;

            case WM_COMMAND:
                if (HIWORD(wParam) == BN_CLICKED && !busy_) {
                    OnCommand(LOWORD(wParam));
                    return 0;
                }
                break;

            case WM_NOTIFY: {
                auto* draw = reinterpret_cast<NMCUSTOMDRAW*>(lParam);

                if (draw->hdr.code == NM_CUSTOMDRAW && buttons_.Owns(draw->hdr.hwndFrom)) {
                    return buttons_.Paint(*draw, fonts_, layout_.dpi);
                }
                break;
            }

            case kRunPartStarted:
                OnPartStarted(wParam);
                return 0;

            case kRunPartDone:
                OnPartDone(wParam, TakeRunError(lParam));
                return 0;

            case kRunFinished:
                OnRunFinished();
                return 0;

            case WM_TIMER:
                if (wParam == kFrameTimer) {
                    OnFrame();
                } else if (wParam == kResultsTimer) {
                    OnResultsShown();
                }
                return 0;

            case WM_ERASEBKGND:
                return 1;  // WM_PAINT covers the whole window

            case WM_PAINT:
                PaintBuffered();
                return 0;

            case WM_DPICHANGED: {
                const auto* suggested = reinterpret_cast<const RECT*>(lParam);
                SetWindowPos(
                    hwnd_,
                    nullptr,
                    suggested->left,
                    suggested->top,
                    suggested->right - suggested->left,
                    suggested->bottom - suggested->top,
                    SWP_NOZORDER | SWP_NOACTIVATE
                );
                ApplyDpi(HIWORD(wParam));
                return 0;
            }

            case WM_CLOSE:
                if (busy_) {
                    return 0;  // a run can't be stopped halfway, so the window waits for it
                }
                break;

            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
        }

        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }

    void SetupWindow::ApplyDpi(UINT dpi) {
        fonts_ = MakeThemeFonts(dpi);
        layout_ = LayOut(dpi, view_.reasons.size());
        FitWindow();
        PlaceButtons();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void SetupWindow::FitWindow() {
        RECT frame = {0, 0, layout_.client.cx, layout_.client.cy};
        AdjustWindowRectExForDpi(&frame, kStyle, FALSE, 0, layout_.dpi);
        SetWindowPos(
            hwnd_,
            nullptr,
            0,
            0,
            frame.right - frame.left,
            frame.bottom - frame.top,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE
        );
    }

    // Drawn into a bitmap first and copied in one go, so the window doesn't flicker while it redraws.
    void SetupWindow::PaintBuffered() {
        PAINTSTRUCT paint;
        const HDC dc = BeginPaint(hwnd_, &paint);
        const HDC buffer = CreateCompatibleDC(dc);
        const HBITMAP bitmap = CreateCompatibleBitmap(dc, layout_.client.cx, layout_.client.cy);

        if (buffer && bitmap) {
            const HGDIOBJ old = SelectObject(buffer, bitmap);
            PaintWindow(buffer, layout_, fonts_, view_);
            BitBlt(dc, 0, 0, layout_.client.cx, layout_.client.cy, buffer, 0, 0, SRCCOPY);
            SelectObject(buffer, old);
        } else {
            PaintWindow(dc, layout_, fonts_, view_);
        }

        if (bitmap) {
            DeleteObject(bitmap);
        }

        if (buffer) {
            DeleteDC(buffer);
        }

        EndPaint(hwnd_, &paint);
    }

    void SetupWindow::Refresh() {
        const size_t reasonsBefore = view_.reasons.size();
        facts_ = ReadFacts(setupLayerSignature_);

        for (size_t i = 0; i < kParts.size(); ++i) {
            statuses_[i] = DescribePart(kParts[i].id, facts_.parts[i], facts_.system);
        }

        UpdateView();

        // A second reason to close makes the band, and the window, taller.
        if ((reasonsBefore > 1) != (view_.reasons.size() > 1) && layout_.client.cx > 0) {
            layout_ = LayOut(layout_.dpi, view_.reasons.size());
            FitWindow();
        }

        PlaceButtons();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    // The rows' run states stay as they are: a run changes them, and its end clears them.
    void SetupWindow::UpdateView() {
        view_.setupVersion = SetupVersion();
        view_.reasons.clear();
        view_.upToDate = true;

        for (size_t i = 0; i < kParts.size(); ++i) {
            const PartStatus& status = statuses_[i];
            view_.rows[i].installed = facts_.parts[i].copy != InstalledCopy::kNone;
            view_.rows[i].version = facts_.parts[i].installedVersion;
            view_.upToDate = view_.upToDate && status.action == PartAction::kNone;

            const std::wstring& reason = status.installBlockedBy;
            bool known = reason.empty();

            for (const std::wstring& shown : view_.reasons) {
                known = known || shown == reason;
            }

            if (!known) {
                view_.reasons.push_back(reason);
            }
        }
    }

    void SetupWindow::PlaceButtons() {
        if (layout_.client.cx == 0) {
            return;
        }

        ButtonPermissions allowed;
        allowed.busy = busy_;
        allowed.canInstallAll = !PartsToInstall().empty();
        allowed.canUninstallAll = !PartsToUninstall().empty();
        allowed.anythingInstalled = AnythingInstalled();
        buttons_.Place(layout_, fonts_, view_, statuses_, allowed);
    }

    // The parts with something to do, when none of them is blocked. Empty otherwise, so "all" means all.
    std::vector<PartId> SetupWindow::PartsToInstall() const {
        std::vector<PartId> parts;

        for (size_t i = 0; i < kParts.size(); ++i) {
            if (statuses_[i].action == PartAction::kNone) {
                continue;
            }

            if (!statuses_[i].installBlockedBy.empty()) {
                return {};
            }

            parts.push_back(kParts[i].id);
        }

        return parts;
    }

    std::vector<PartId> SetupWindow::PartsToUninstall() const {
        std::vector<PartId> parts;

        for (size_t i = 0; i < kParts.size(); ++i) {
            if (!statuses_[i].hasSomething) {
                continue;
            }

            if (!statuses_[i].removeBlockedBy.empty()) {
                return {};
            }

            parts.push_back(kParts[i].id);
        }

        return parts;
    }

    bool SetupWindow::AnythingInstalled() const {
        for (const PartStatus& status : statuses_) {
            if (status.hasSomething) {
                return true;
            }
        }

        return false;
    }

    void SetupWindow::OnCommand(int id) {
        if (id == kCloseId) {
            DestroyWindow(hwnd_);
            return;
        }

        // What is on the PC may have changed since the window last looked.
        Refresh();

        const int partCount = static_cast<int>(kParts.size());

        if (id == kMainActionId) {
            const std::vector<PartId> parts = PartsToInstall();

            if (!parts.empty() && ConfirmLayer(parts)) {
                StartRun(Change::kInstall, parts);
            }
        } else if (id >= kRowMainId && id < kRowMainId + partCount) {
            const size_t index = static_cast<size_t>(id - kRowMainId);
            const std::vector<PartId> parts = {kParts[index].id};

            if (statuses_[index].installBlockedBy.empty() && ConfirmLayer(parts)) {
                StartRun(Change::kInstall, parts);
            }
        } else if (id >= kRowUninstallId && id < kRowUninstallId + partCount) {
            const size_t index = static_cast<size_t>(id - kRowUninstallId);

            if (statuses_[index].removeBlockedBy.empty()) {
                StartRun(Change::kUninstall, {kParts[index].id});
            }
        } else if (id == kUninstallAllId) {
            const std::vector<PartId> parts = PartsToUninstall();

            if (!parts.empty() && AskUninstallAll(hwnd_, parts)) {
                StartRun(Change::kUninstall, parts);
            }
        }
    }

    // Asks before a layer goes in that Easy Anti-Cheat would refuse. True to go on.
    bool SetupWindow::ConfirmLayer(const std::vector<PartId>& parts) {
        const std::wstring problem = UnsignedLayerProblem(facts_.system);
        bool hasLayer = false;

        for (const PartId part : parts) {
            hasLayer = hasLayer || part == PartId::kOpenXrLayer;
        }

        return !hasLayer || problem.empty() || AskInstallRefusedLayer(hwnd_, problem);
    }

    void SetupWindow::StartRun(Change change, const std::vector<PartId>& parts) {
        busy_ = true;
        progress_.Start(change, parts, statuses_, GetTickCount64(), &view_);
        PlaceButtons();
        InvalidateRect(hwnd_, nullptr, FALSE);

        run_.Start(hwnd_, change, parts, facts_.system);
        SetTimer(hwnd_, kFrameTimer, kFrameMs, nullptr);
    }

    void SetupWindow::OnPartStarted(size_t index) {
        progress_.PartStarted(index, &view_);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void SetupWindow::OnPartDone(size_t index, const std::wstring& error) {
        progress_.PartDone(index, error, GetTickCount64(), &view_);
        SetTimer(hwnd_, kFrameTimer, kFrameMs, nullptr);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    // Updates the entry in Windows' installed apps, shows what failed, and keeps the results a moment.
    void SetupWindow::OnRunFinished() {
        run_.Join();

        std::wstring errors = progress_.Finish(&view_);
        Refresh();

        std::wstring entryError;

        if (!UpdateAppEntry(AnythingInstalled(), &entryError)) {
            errors += L"\nWindows' list of installed apps: " + entryError;
        }

        if (!errors.empty()) {
            const std::wstring message = L"Not everything could be done." + errors;
            MessageBoxW(hwnd_, message.c_str(), kTitle, MB_OK | MB_ICONERROR);
        }

        SetTimer(hwnd_, kResultsTimer, kTicksShownMs, nullptr);
    }

    void SetupWindow::OnResultsShown() {
        KillTimer(hwnd_, kResultsTimer);
        KillTimer(hwnd_, kFrameTimer);
        progress_.Clear(&view_);
        busy_ = false;
        Refresh();
    }

    // Redraws only what moves, and stops the timer when nothing does.
    void SetupWindow::OnFrame() {
        const bool moving = progress_.Animate(GetTickCount64(), &view_);
        const RECT bar = RunBarRect(layout_);
        InvalidateRect(hwnd_, &bar, FALSE);

        for (size_t i = 0; i < view_.rows.size(); ++i) {
            if (view_.rows[i].run == RunState::kBusy) {
                const RECT state = RunStateRect(layout_, i);
                InvalidateRect(hwnd_, &state, FALSE);
            }
        }

        if (!moving) {
            KillTimer(hwnd_, kFrameTimer);
        }
    }

}

int RunSetupWindow(HINSTANCE instance) {
    const GdiplusSession gdiplus;
    SetupWindow window(instance);

    if (!window.Create()) {
        MessageBoxW(nullptr, L"Setup's window could not be made.", kTitle, MB_OK | MB_ICONERROR);
        return 1;
    }

    MSG message;

    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(GetActiveWindow(), &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    return static_cast<int>(message.wParam);
}

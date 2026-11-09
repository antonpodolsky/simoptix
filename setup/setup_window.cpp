// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "setup_window.h"

#include <array>
#include <string>
#include <vector>

#include "setup_install.h"
#include "setup_look.h"
#include "setup_payload.h"
#include "setup_probe.h"
#include "setup_signature.h"
#include "setup_status.h"
#include "setup_system.h"

namespace {

    constexpr int kMainId = 100;       // + the part's index
    constexpr int kUninstallId = 110;  // + the part's index
    constexpr int kInstallAllId = 200;
    constexpr int kUninstallAllId = 201;
    constexpr int kCloseId = 202;

    const wchar_t* const kClassName = L"simoptix_setup";
    const wchar_t* const kTitle = L"SimOptix Setup";

    const DWORD kStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;

    enum class Change { kInstall, kUninstall };

    // Adds `sentence` to `text`, after a space, unless it is empty or already there.
    void AddSentenceOnce(std::wstring* text, const std::wstring& sentence) {
        if (sentence.empty() || text->find(sentence) != std::wstring::npos) {
            return;
        }

        if (!text->empty()) {
            *text += L" ";
        }

        *text += sentence;
    }

    void PlaceButton(HWND button, const RECT& at) {
        MoveWindow(button, at.left, at.top, at.right - at.left, at.bottom - at.top, TRUE);
    }

    class SetupWindow {
    public:
        explicit SetupWindow(HINSTANCE instance) : instance_(instance) {}

        bool Create();

    private:
        static LRESULT CALLBACK Procedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
        LRESULT Handle(UINT message, WPARAM wParam, LPARAM lParam);

        HWND MakeButton(const wchar_t* text, int id);
        void ApplyDpi(UINT dpi);
        void Paint(HDC dc) const;
        void PaintBuffered();

        void Refresh();
        void UpdateButtons();

        void OnCommand(int id);
        bool ConfirmLayer(const std::vector<PartId>& parts);
        void Run(Change change, const std::vector<PartId>& parts);
        std::vector<PartId> PartsToInstall() const;
        std::vector<PartId> PartsToUninstall() const;
        bool AnythingInstalled() const;

        HINSTANCE instance_;
        HWND hwnd_ = nullptr;
        LookFonts fonts_;
        WindowLayout layout_;
        UniqueBrush footerBrush_;

        std::array<HWND, 3> mainButtons_ = {};
        std::array<HWND, 3> uninstallButtons_ = {};
        HWND installAll_ = nullptr;
        HWND uninstallAll_ = nullptr;
        HWND close_ = nullptr;

        SignatureKind setupLayerSignature_ = SignatureKind::kNone;
        Facts facts_;
        WindowTexts texts_;
        std::array<PartStatus, 3> statuses_;  // in the order of kParts
    };

    bool SetupWindow::Create() {
        EmbeddedFile layer;
        std::wstring error;

        if (FindEmbeddedFile(PartOf(PartId::kOpenXrLayer).dllResource, &layer, &error)) {
            setupLayerSignature_ = CheckEmbeddedSignature(layer, PartOf(PartId::kOpenXrLayer).dllName);
        }

        footerBrush_.reset(CreateSolidBrush(FooterColour()));

        WNDCLASSEXW windowClass = {};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = Procedure;
        windowClass.hInstance = instance_;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
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

        for (size_t i = 0; i < kParts.size(); ++i) {
            mainButtons_[i] = MakeButton(L"Install", kMainId + static_cast<int>(i));
            uninstallButtons_[i] = MakeButton(L"Uninstall", kUninstallId + static_cast<int>(i));
        }

        installAll_ = MakeButton(L"Install all", kInstallAllId);
        uninstallAll_ = MakeButton(L"Uninstall all", kUninstallAllId);
        close_ = MakeButton(L"Close", kCloseId);

        Refresh();
        ApplyDpi(GetDpiForWindow(hwnd_));

        RECT frame = {0, 0, layout_.client.cx, layout_.client.cy};
        AdjustWindowRectExForDpi(&frame, kStyle, FALSE, 0, layout_.dpi);
        SetWindowPos(
            hwnd_,
            nullptr,
            0,
            0,
            frame.right - frame.left,
            frame.bottom - frame.top,
            SWP_NOMOVE | SWP_NOZORDER
        );

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
                if (LOWORD(wParam) != WA_INACTIVE) {
                    Refresh();
                }
                break;

            case WM_COMMAND:
                if (HIWORD(wParam) == BN_CLICKED) {
                    OnCommand(LOWORD(wParam));
                    return 0;
                }
                break;

            case WM_ERASEBKGND:
                return 1;  // WM_PAINT covers the whole window

            case WM_PAINT:
                PaintBuffered();
                return 0;

            // Themed buttons ask their parent to draw what is behind their rounded corners.
            case WM_PRINTCLIENT:
                Paint(reinterpret_cast<HDC>(wParam));
                return 0;

            case WM_CTLCOLORBTN: {
                const HWND button = reinterpret_cast<HWND>(lParam);
                const bool inFooter = button == installAll_ || button == uninstallAll_ || button == close_;
                const HBRUSH brush = inFooter ? footerBrush_.get()
                                              : static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
                return reinterpret_cast<LRESULT>(brush);
            }

            case WM_DPICHANGED: {
                const auto* suggested = reinterpret_cast<const RECT*>(lParam);
                ApplyDpi(HIWORD(wParam));
                SetWindowPos(
                    hwnd_,
                    nullptr,
                    suggested->left,
                    suggested->top,
                    suggested->right - suggested->left,
                    suggested->bottom - suggested->top,
                    SWP_NOZORDER | SWP_NOACTIVATE
                );
                return 0;
            }

            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
        }

        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }

    HWND SetupWindow::MakeButton(const wchar_t* text, int id) {
        return CreateWindowExW(
            0,
            L"BUTTON",
            text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0,
            0,
            0,
            0,
            hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            instance_,
            nullptr
        );
    }

    void SetupWindow::ApplyDpi(UINT dpi) {
        fonts_ = MakeFonts(dpi);
        layout_ = LayOut(dpi);

        for (HWND child = GetWindow(hwnd_, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(fonts_.body.get()), FALSE);
        }

        PlaceButton(installAll_, layout_.installAll);
        PlaceButton(uninstallAll_, layout_.uninstallAll);
        PlaceButton(close_, layout_.close);
        UpdateButtons();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void SetupWindow::Paint(HDC dc) const {
        PaintWindow(dc, layout_, fonts_, texts_);
    }

    // Drawn into a bitmap first and copied in one go, so the window doesn't flicker while it redraws.
    void SetupWindow::PaintBuffered() {
        PAINTSTRUCT paint;
        const HDC dc = BeginPaint(hwnd_, &paint);
        const HDC buffer = CreateCompatibleDC(dc);
        const HBITMAP bitmap = CreateCompatibleBitmap(dc, layout_.client.cx, layout_.client.cy);

        if (buffer && bitmap) {
            const HGDIOBJ old = SelectObject(buffer, bitmap);
            Paint(buffer);
            BitBlt(dc, 0, 0, layout_.client.cx, layout_.client.cy, buffer, 0, 0, SRCCOPY);
            SelectObject(buffer, old);
        } else {
            Paint(dc);
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
        facts_ = ReadFacts(setupLayerSignature_);
        texts_.blockedBy.clear();

        for (size_t i = 0; i < kParts.size(); ++i) {
            statuses_[i] = DescribePart(kParts[i].id, facts_.parts[i], facts_.system);
            AddSentenceOnce(&texts_.blockedBy, statuses_[i].installBlockedBy);
        }

        UpdateButtons();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    // Each row shows its main button only when there is something to do, and Uninstall only when there is
    // something to remove. The main button takes the right slot when Uninstall isn't shown.
    void SetupWindow::UpdateButtons() {
        if (layout_.client.cx == 0) {
            return;
        }

        for (size_t i = 0; i < kParts.size(); ++i) {
            const PartStatus& status = statuses_[i];

            SetWindowTextW(mainButtons_[i], ActionLabel(status.action));
            PlaceButton(mainButtons_[i], status.hasSomething ? layout_.mainSlots[i] : layout_.sideSlots[i]);
            PlaceButton(uninstallButtons_[i], layout_.sideSlots[i]);
            ShowWindow(mainButtons_[i], status.action != PartAction::kNone ? SW_SHOW : SW_HIDE);
            ShowWindow(uninstallButtons_[i], status.hasSomething ? SW_SHOW : SW_HIDE);
            EnableWindow(mainButtons_[i], status.installBlockedBy.empty());
            EnableWindow(uninstallButtons_[i], status.removeBlockedBy.empty());
        }

        SetWindowTextW(installAll_, AnythingInstalled() ? L"Update all" : L"Install all");
        EnableWindow(installAll_, !PartsToInstall().empty());
        EnableWindow(uninstallAll_, !PartsToUninstall().empty());
    }

    // The parts with something to do, when none of them is blocked. Empty otherwise, so "all" means all.
    std::vector<PartId> SetupWindow::PartsToInstall() const {
        std::vector<PartId> parts;

        for (size_t i = 0; i < kParts.size(); ++i) {
            const PartStatus& status = statuses_[i];

            if (status.action == PartAction::kNone) {
                continue;
            }

            if (!status.installBlockedBy.empty()) {
                return {};
            }

            parts.push_back(kParts[i].id);
        }

        return parts;
    }

    std::vector<PartId> SetupWindow::PartsToUninstall() const {
        std::vector<PartId> parts;

        for (size_t i = 0; i < kParts.size(); ++i) {
            const PartStatus& status = statuses_[i];

            if (!status.hasSomething) {
                continue;
            }

            if (!status.removeBlockedBy.empty()) {
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

        if (id >= kMainId && id < kMainId + partCount) {
            const size_t index = static_cast<size_t>(id - kMainId);
            const std::vector<PartId> parts = {kParts[index].id};

            if (statuses_[index].installBlockedBy.empty() && ConfirmLayer(parts)) {
                Run(Change::kInstall, parts);
            }
        } else if (id >= kUninstallId && id < kUninstallId + partCount) {
            const size_t index = static_cast<size_t>(id - kUninstallId);

            if (statuses_[index].removeBlockedBy.empty()) {
                Run(Change::kUninstall, {kParts[index].id});
            }
        } else if (id == kInstallAllId) {
            const std::vector<PartId> parts = PartsToInstall();

            if (!parts.empty() && ConfirmLayer(parts)) {
                Run(Change::kInstall, parts);
            }
        } else if (id == kUninstallAllId) {
            const std::vector<PartId> parts = PartsToUninstall();

            if (parts.empty()) {
                return;
            }

            const int answer = MessageBoxW(
                hwnd_,
                L"Uninstall everything of SimOptix that is installed?",
                kTitle,
                MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2
            );

            if (answer == IDYES) {
                Run(Change::kUninstall, parts);
            }
        }
    }

    // Asks before a layer goes in that Easy Anti-Cheat would refuse. True to go on.
    bool SetupWindow::ConfirmLayer(const std::vector<PartId>& parts) {
        const std::wstring warning = UnsignedLayerWarning(facts_.system);
        bool hasLayer = false;

        for (const PartId part : parts) {
            hasLayer = hasLayer || part == PartId::kOpenXrLayer;
        }

        if (!hasLayer || warning.empty()) {
            return true;
        }

        return MessageBoxW(hwnd_, warning.c_str(), kTitle, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2)
               == IDYES;
    }

    // Makes the change and brings the entry in Windows' installed apps up to what is now installed. The
    // buttons show the result, so a message comes up only when something failed.
    void SetupWindow::Run(Change change, const std::vector<PartId>& parts) {
        const HCURSOR previous = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
        std::wstring errors;

        for (const PartId part : parts) {
            std::wstring error;
            const bool done = change == Change::kInstall ? InstallPart(part, facts_.system, &error)
                                                         : RemovePart(part, &error);

            if (!done) {
                errors += std::wstring(L"\n") + PartOf(part).shownName + L": " + error;
            }
        }

        Refresh();

        std::wstring entryError;

        if (!UpdateAppEntry(AnythingInstalled(), &entryError)) {
            errors += L"\nWindows' list of installed apps: " + entryError;
        }

        SetCursor(previous);

        if (!errors.empty()) {
            const std::wstring message = L"Not everything could be done." + errors;
            MessageBoxW(hwnd_, message.c_str(), kTitle, MB_OK | MB_ICONERROR);
        }
    }

}

int RunSetupWindow(HINSTANCE instance) {
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

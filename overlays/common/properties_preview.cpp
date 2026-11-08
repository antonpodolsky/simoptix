// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "properties_preview.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cwchar>

#include "shared/log.h"

namespace {

    // A dialog of this process (a visible window with an owner, OBS's main window) with the source's name in
    // its title: the rest of the title is translated.
    bool IsDialogFor(HWND h, const wchar_t* name) {
        if (!h || !IsWindow(h) || !IsWindowVisible(h) || !GetWindow(h, GW_OWNER)) {
            return false;
        }
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid != GetCurrentProcessId()) {
            return false;
        }
        wchar_t text[512] = {};
        GetWindowTextW(h, text, 512);
        return wcsstr(text, name) != nullptr;
    }

    // The window found before while it is still open; otherwise the one just activated on this (the UI)
    // thread: the Properties window, which OBS activates as it opens it.
    HWND FindDialog(const wchar_t* name, PreviewFit& fit) {
        HWND h = static_cast<HWND>(fit.dialog);
        if (!IsDialogFor(h, name)) {
            h = GetActiveWindow();
        }
        if (!IsDialogFor(h, name)) {
            return nullptr;
        }
        fit.dialog = h;
        return h;
    }

    // The preview: the dialog's largest visible native window with none of its own (the display OBS draws
    // into).
    HWND FindPreview(HWND dialog) {
        struct Best {
            HWND h = nullptr;
            long area = 0;
        } best;

        EnumChildWindows(
            dialog,
            [](HWND h, LPARAM p) -> BOOL {
                Best* best = reinterpret_cast<Best*>(p);
                RECT r;
                if (!IsWindowVisible(h) || GetWindow(h, GW_CHILD) || !GetClientRect(h, &r)) {
                    return TRUE;
                }
                const long area = long(r.right) * r.bottom;
                if (area > best->area) {
                    best->h = h, best->area = area;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&best)
        );

        return best.h;
    }

}

PreviewFitResult FitPropertiesPreview(const char* name, int pixels, int shownPixels, PreviewFit& fit) {
    if (!name || pixels <= 0) {
        return PreviewFitResult::NoWindow;
    }

    wchar_t wide[256] = {};
    MultiByteToWideChar(CP_UTF8, 0, name, -1, wide, 256);
    const HWND dialog = FindDialog(wide, fit);
    const HWND preview = dialog ? FindPreview(dialog) : nullptr;
    if (!preview) {
        return PreviewFitResult::NoWindow;
    }

    RECT pr, wr;
    GetClientRect(preview, &pr);
    GetWindowRect(dialog, &wr);
    const int w = pr.right, h = pr.bottom, W = wr.right - wr.left, H = wr.bottom - wr.top;

    if (!fit.logged) {
        wchar_t cls[128] = {};
        GetClassNameW(preview, cls, 128);
        CoreLog("properties preview of '%s': %dx%d px (%ls), window %dx%d", name, w, h, cls, W, H);
        fit.logged = true;
    }

    // Done when the preview's smaller side is the size and OBS showed the source at it.
    int target = pixels + fit.bias;
    if (std::min(w, h) == target) {
        if (shownPixels == pixels) {
            CoreLog(
                "properties preview of '%s': %dx%d px, shows it at %d px (1:1) after %d resizes",
                name,
                w,
                h,
                shownPixels,
                fit.steps
            );

            return PreviewFitResult::Fitted;
        }

        // OBS's fit rounds down: one pixel more for the preview.
        if (shownPixels == pixels - 1 && fit.bias == 0) {
            target = pixels + ++fit.bias;
        } else {
            return PreviewFitResult::Resizing;  // not drawn at this size yet
        }
    }

    if (fit.steps >= 12) {
        CoreLog("properties preview of '%s': gave up at %dx%d px (wanted %d)", name, w, h, target);
        return PreviewFitResult::GaveUp;
    }

    // The width follows the window's one for one; a height change is shared by the splitter's two panes.
    const int newW = std::max(w, target);
    const int wantH = newW == target ? std::max(h, target) : target;
    if (fit.lastWindowH && H != fit.lastWindowH && h != fit.lastPreviewH) {
        fit.gain = std::min(1.0f, std::max(0.2f, float(h - fit.lastPreviewH) / float(H - fit.lastWindowH)));
    }

    int dH = int(std::lround((wantH - h) / fit.gain));
    if (dH == 0 && wantH != h) {
        dH = wantH > h ? 1 : -1;
    }
    fit.lastWindowH = H;
    fit.lastPreviewH = h;
    RECT r = {wr.left, wr.top, wr.left + W + (newW - w), wr.top + H + dH};

    // Kept on its monitor's work area.
    MONITORINFO mi = {sizeof(mi)};
    if (GetMonitorInfoW(MonitorFromWindow(dialog, MONITOR_DEFAULTTONEAREST), &mi)) {
        const RECT& a = mi.rcWork;
        if (r.right > a.right) {
            OffsetRect(&r, std::max(a.left - r.left, a.right - r.right), 0);
        }
        if (r.bottom > a.bottom) {
            OffsetRect(&r, 0, std::max(a.top - r.top, a.bottom - r.bottom));
        }
    }

    SetWindowPos(
        dialog,
        nullptr,
        r.left,
        r.top,
        r.right - r.left,
        r.bottom - r.top,
        SWP_NOZORDER | SWP_NOACTIVATE
    );

    fit.steps++;
    return PreviewFitResult::Resizing;
}

void RestartPreviewFit(PreviewFit& fit) {
    PreviewFit fresh;
    fresh.dialog = fit.dialog;
    fit = fresh;
}

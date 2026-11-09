// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Owners of GDI's objects and device contexts, so that a drawing function with early returns leaks
// nothing and leaves each device context as it found it, and the canvas that drawing goes on: a device
// context and its display scale. Knows nothing about what is drawn.
#pragma once

#include <windows.h>

#include <memory>
#include <type_traits>

struct GdiDeleter {
    void operator()(HGDIOBJ object) const { DeleteObject(object); }
};

struct DcDeleter {
    void operator()(HDC dc) const { DeleteDC(dc); }
};

using UniqueFont = std::unique_ptr<std::remove_pointer_t<HFONT>, GdiDeleter>;
using UniqueBrush = std::unique_ptr<std::remove_pointer_t<HBRUSH>, GdiDeleter>;
using UniqueBitmap = std::unique_ptr<std::remove_pointer_t<HBITMAP>, GdiDeleter>;
using UniqueMemoryDc = std::unique_ptr<std::remove_pointer_t<HDC>, DcDeleter>;

// The screen's device context, for measuring text. Get() is nullptr when Windows has none to give.
class ScreenDc {
public:
    ScreenDc() : dc_(GetDC(nullptr)) {}
    ScreenDc(const ScreenDc&) = delete;
    ScreenDc& operator=(const ScreenDc&) = delete;
    ~ScreenDc() {
        if (dc_) {
            ReleaseDC(nullptr, dc_);
        }
    }

    HDC Get() const { return dc_; }

private:
    HDC dc_;
};

// Selects `object` into `dc`, and puts back what was there when it goes out of scope.
class SelectedObject {
public:
    SelectedObject(HDC dc, HGDIOBJ object) : dc_(dc), old_(SelectObject(dc, object)) {}
    SelectedObject(const SelectedObject&) = delete;
    SelectedObject& operator=(const SelectedObject&) = delete;
    ~SelectedObject() {
        if (old_) {
            SelectObject(dc_, old_);
        }
    }

private:
    HDC dc_;
    HGDIOBJ old_;
};

// Pixels at 96 DPI to pixels at `dpi`.
inline int ScaleToDpi(int pixels, UINT dpi) {
    return MulDiv(pixels, static_cast<int>(dpi), 96);
}

// Where drawing goes: a device context, and the display scale its sizes are given for.
struct Canvas {
    HDC dc = nullptr;
    UINT dpi = 96;

    int Scale(int pixels) const { return ScaleToDpi(pixels, dpi); }
};

// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "meter_image.h"

#include <windows.h>
#include <obs-module.h>
#include <shlwapi.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace Gdiplus {
    using std::max;
    using std::min;
}  // gdiplus.h expects min/max, which NOMINMAX removes

#include <gdiplus.h>

#include "shared/log.h"

namespace {

    bool StartGdiplus() {
        static const bool started = [] {
            ULONG_PTR token;
            Gdiplus::GdiplusStartupInput input;
            return Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok;  // kept for the process
        }();

        return started;
    }

    double Seconds() {  // performance-counter seconds, for the build-time log line
        LARGE_INTEGER t, f;
        QueryPerformanceCounter(&t);
        QueryPerformanceFrequency(&f);
        return double(t.QuadPart) / double(f.QuadPart);
    }

    // The plugin's own module (resources live in the DLL, not in OBS).
    HMODULE ThisModule() {
        HMODULE module = nullptr;
        GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&ThisModule),
            &module
        );

        return module;
    }

    // Decodes the PNG resource and scales it to size x size, premultiplied BGRA rows. False if it can't.
    bool Render(const wchar_t* resource, int size, std::vector<uint8_t>* pixels) {
        if (!StartGdiplus()) {
            return false;
        }

        const HMODULE module = ThisModule();
        HRSRC info = FindResourceW(module, resource, MAKEINTRESOURCEW(10));  // RT_RCDATA
        HGLOBAL data = info ? LoadResource(module, info) : nullptr;
        const void* bytes = data ? LockResource(data) : nullptr;
        if (!bytes) {
            return false;
        }

        IStream* stream = SHCreateMemStream(static_cast<const BYTE*>(bytes), SizeofResource(module, info));
        if (!stream) {
            return false;
        }

        bool ok = false;

        {
            Gdiplus::Bitmap source(stream);
            Gdiplus::Bitmap scaled(size, size, PixelFormat32bppPARGB);

            if (source.GetLastStatus() == Gdiplus::Ok && scaled.GetLastStatus() == Gdiplus::Ok) {
                {
                    Gdiplus::Graphics g(&scaled);
                    g.Clear(Gdiplus::Color(0, 0, 0, 0));
                    g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
                    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
                    Gdiplus::ImageAttributes clamp;  // no dark fringe from wrapping around at the edges
                    clamp.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
                    g.DrawImage(
                        &source,
                        Gdiplus::Rect(0, 0, size, size),
                        0,
                        0,
                        INT(source.GetWidth()),
                        INT(source.GetHeight()),
                        Gdiplus::UnitPixel,
                        &clamp
                    );
                }

                Gdiplus::BitmapData locked;
                Gdiplus::Rect all(0, 0, size, size);
                if (scaled.LockBits(&all, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &locked)
                    == Gdiplus::Ok) {
                    pixels->resize(size_t(size) * size * 4);
                    for (int y = 0; y < size; y++) {
                        memcpy(
                            &(*pixels)[size_t(y) * size * 4],
                            static_cast<const uint8_t*>(locked.Scan0) + y * locked.Stride,
                            size_t(size) * 4
                        );
                    }

                    scaled.UnlockBits(&locked);
                    ok = true;
                }
            }
        }

        stream->Release();
        return ok;
    }

}

void MeterImage::Build(int size) {
    if (size == size_ || failed_ || size <= 0) {
        return;
    }
    const double start = Seconds();
    std::vector<uint8_t> pixels;
    if (!Render(resource_, size, &pixels)) {
        CoreLog("G-Meter Overlay: couldn't read the meter image %ls from the plugin", resource_);
        failed_ = true;
        return;
    }

    obs_enter_graphics();
    if (tex_) {
        gs_texture_destroy(tex_);
    }
    const uint8_t* rows[] = {pixels.data()};
    tex_ = gs_texture_create(uint32_t(size), uint32_t(size), GS_BGRA, 1, rows, 0);
    obs_leave_graphics();
    size_ = tex_ ? size : 0;

    // Once per size: it runs on OBS's graphics thread, so its cost is worth seeing.
    CoreLog(
        "G-Meter Overlay: meter image %ls at %d px in %.1f ms",
        resource_,
        size,
        (Seconds() - start) * 1000
    );
}

void MeterImage::Draw() const {
    if (!tex_) {
        return;
    }
    gs_effect_t* effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), tex_);
    gs_blend_state_push();
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);  // the texture is premultiplied
    while (gs_effect_loop(effect, "Draw")) {
        gs_draw_sprite(tex_, 0, 0, 0);
    }
    gs_blend_state_pop();
}

void MeterImage::Destroy() {
    if (tex_) {
        gs_texture_destroy(tex_);
    }
    tex_ = nullptr;
    size_ = 0;
}

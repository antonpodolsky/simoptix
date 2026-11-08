// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// A replay's real thumbnails (<replay>.thumbs.png, made by vr_mirror\recording_analysis\export_thumbs.py):
// one 64x32 tile per game frame of the replay, in its order, stacked top to bottom. Loaded once
// with the Windows Imaging Component into 8-bit RGBA. Knows nothing about D3D or the replay.
#pragma once

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

namespace thumbs {

    constexpr int kWidth = 64, kHeight = 32;  // vr_mirror\core\adapters\d3d\thumbnail's size

    // Decodes `path` into `rgba` (kWidth x kHeight x 4 bytes per tile); returns the number of tiles, or
    // 0 with `error` set.
    inline int Load(const std::string& path, std::vector<uint8_t>* rgba, std::string* error) {
        using Microsoft::WRL::ComPtr;
        const HRESULT init = CoInitializeEx(
            nullptr,
            COINIT_MULTITHREADED
        );  // RPC_E_CHANGED_MODE: already set up
        int tiles = 0;

        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICFormatConverter> rgbaFrame;

        const std::wstring wpath(path.begin(), path.end());
        UINT w = 0, h = 0;

        if (FAILED(CoCreateInstance(
                CLSID_WICImagingFactory,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&factory)
            ))
            || FAILED(factory->CreateDecoderFromFilename(
                wpath.c_str(),
                nullptr,
                GENERIC_READ,
                WICDecodeMetadataCacheOnDemand,
                &decoder
            ))
            || FAILED(decoder->GetFrame(0, &frame)) || FAILED(factory->CreateFormatConverter(&rgbaFrame))
            || FAILED(rgbaFrame->Initialize(
                frame.Get(),
                GUID_WICPixelFormat32bppRGBA,
                WICBitmapDitherTypeNone,
                nullptr,
                0,
                WICBitmapPaletteTypeCustom
            ))
            || FAILED(rgbaFrame->GetSize(&w, &h))) {
            *error = "cannot read thumbnails " + path;
        } else if (w != kWidth || h % kHeight != 0) {
            *error = "thumbnails " + path + " are not 64 wide in 32-row tiles";
        } else {
            rgba->resize(size_t(w) * h * 4);
            if (SUCCEEDED(rgbaFrame->CopyPixels(nullptr, w * 4, UINT(rgba->size()), rgba->data()))) {
                tiles = h / kHeight;
            } else {
                *error = "cannot decode thumbnails " + path;
            }
        }

        rgbaFrame.Reset();
        frame.Reset();
        decoder.Reset();
        factory.Reset();
        if (SUCCEEDED(init)) {
            CoUninitialize();
        }
        return tiles;
    }

}

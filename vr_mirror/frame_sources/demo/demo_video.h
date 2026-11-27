// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// DemoVideo: the recorded lap's video (recorded_lap.h), decoded frame by frame on the graphics card by
// Windows' Media Foundation, on the host's own device, so no texture passes between devices. Each decoded
// frame stays a sample of the decoder's until it is copied into the host's store. Each frame keeps its own
// time: the converter to BGRA would even out the frame rate by repeating images (KeepFrameTimes). Each image
// is copied at its own size: the decoder's surfaces are larger, rounded up to whole blocks (2528x2112 for
// 2508x2084). Measured on flat test images: 1000 fps at 2508x2084, 279 fps at 5084x4168.
//
// Everything on the thread that owns the host's device context. Knows nothing about laps, poses or timing.
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <string>

class DemoVideo {
public:
    ~DemoVideo() { Close(); }

    // Whether Windows can decode `path` on this PC (a device of its own, closed again): false with the
    // reason.
    static bool Check(const std::wstring& path, std::string* error);
    // The default graphics adapter, which the demo's host's device is made on.
    static LUID DefaultAdapter();

    // Opens `path`, whose images are `width` x `height`, decoded on `device`.
    bool Open(const std::wstring& path, int width, int height, ID3D11Device* device, std::string* error);
    void Close();
    bool IsOpen() const { return reader_ != nullptr; }
    ID3D11Device* Device() const { return device_.Get(); }

    // The next frame's decoded sample, in the video's order, and its time in the video (s, the first frame
    // at 0). False at the video's end (`*end`) or on a failure.
    bool Next(Microsoft::WRL::ComPtr<IMFSample>* sample, double* t, bool* end);
    // Copies a sample's image into `target` (the image's size, BGRA), on `context`.
    bool CopyInto(IMFSample* sample, ID3D11DeviceContext* context, ID3D11Texture2D* target) const;
    // On to the key frame at or before time `t` (s): Next then decodes on from there.
    bool SeekTo(double t);

private:
    static bool OpenReader(
        const std::wstring& path,
        IMFDXGIDeviceManager* manager,
        Microsoft::WRL::ComPtr<IMFSourceReader>* reader,
        std::string* error
    );
    static bool KeepFrameTimes(IMFSourceReader* reader);

    Microsoft::WRL::ComPtr<ID3D11Device> device_;  // the host's, held while open
    Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> manager_;
    Microsoft::WRL::ComPtr<IMFSourceReader> reader_;
    int width_ = 0, height_ = 0;
    bool started_ = false;  // Media Foundation started (MFStartup)
};

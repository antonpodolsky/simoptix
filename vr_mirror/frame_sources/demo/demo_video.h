// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// DemoVideo: the recorded lap's video (recorded_lap.h), decoded frame by frame on the graphics card by
// Windows' Media Foundation, on a device of its own, into a few textures that a host's device opens through
// their shared handles (as the OpenXR layer shares its images). Each decoded image is copied in at its own
// size: the decoder's are larger, rounded up to whole blocks (2528x2112 for 2508x2084). Measured on flat
// test images: 1000 fps at 2508x2084, 279 fps at 5084x4168.
//
// Its thread (the demo source's) decodes; Done is checked without waiting or flushing. Knows nothing
// about laps, poses or hosts.
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <string>

class DemoVideo {
public:
    static constexpr int kTextures = 8;  // a host copies an image within a frame or two

    ~DemoVideo() { Close(); }

    // Opens `path`, whose images are `width` x `height`, and makes the textures.
    bool Open(const std::wstring& path, int width, int height, std::string* error);
    void Close();
    // The adapter it decodes on (the default one): a host's device must be on it too.
    LUID Adapter() const { return adapter_; }
    HANDLE Shared(int texture) const { return shared_[texture]; }

    // Decodes the next image into texture `texture`. False at the video's end (`*end`) or on a failure.
    bool DecodeNext(int texture, bool* end);
    // Back to the first image.
    bool Rewind();
    // The decode into `texture` has finished on the GPU (never waits).
    bool Done(int texture);

private:
    bool MakeTextures(int width, int height);

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> manager_;
    Microsoft::WRL::ComPtr<IMFSourceReader> reader_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> textures_[kTextures];
    Microsoft::WRL::ComPtr<ID3D11Query> done_[kTextures];
    HANDLE shared_[kTextures] = {};  // owned by the textures
    LUID adapter_ = {};
    int width_ = 0, height_ = 0;
    bool started_ = false;  // Media Foundation started (MFStartup)
};

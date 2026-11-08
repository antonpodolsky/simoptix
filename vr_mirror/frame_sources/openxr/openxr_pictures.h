// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// OpenXrPictures: the OpenXR layer's three shared textures, opened on a host's device, and the copy of a
// frame's picture from one of them into the host's store (FrameCopy) at the size the host needs: as it
// is when that is the picture's own size, else drawn smaller, averaged over each stored pixel's area in
// linear light. Stored sRGB-encoded once, as the game drew it (EyeInfo::encodedTwice false).
//
// While its GPU reads a texture the host holds the layer off it (openxr_share.h: its reader count), and lets
// go once a query after the read says it is done, checked without waiting or flushing (Poll).
// Everything on the thread that owns the host's device context; SetNeed from any thread.
// Knows nothing about the frames' times or poses (OpenXrSource), OBS or windows.
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>

#include "adapters/d3d/frame_copy.h"
#include "openxr_share.h"
#include "shared/unique_handle.h"

class OpenXrPictures {
public:
    ~OpenXrPictures() { Release(); }

    // How wide (pixels) the host needs a stored picture, and the most memory one may take (bytes); the
    // stored picture is that wide (never wider than its own), as tall as its shape asks.
    void SetNeed(float width, double maxBytes) {
        needWidth_ = width;
        maxBytes_ = maxBytes;
    }

    // On `device` (every 2 s): maps the layer's memory if it is there, and opens its textures (again
    // when the layer made new ones, or on another device).
    void Open(ID3D11Device* device);
    // Copies frame `layerFrame`'s picture (`width` x `height` at 0, 0 of texture `texture`) into `into`;
    // false if it is gone (the layer wrote another frame into the texture) or can't be opened.
    bool Copy(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        int texture,
        uint64_t layerFrame,
        int width,
        int height,
        FrameCopy* into,
        bool shared
    );

    // Lets go of the textures whose reads the GPU has finished (never waits).
    void Poll(ID3D11DeviceContext* context);
    // Lets go of everything opened on the device, and of the layer's memory.
    void Release();
    // Copies that found their picture gone, since the last call.
    uint64_t TakeGone() { return gone_.exchange(0); }

private:
    bool MakeShaders(ID3D11Device* device);
    void CloseTextures();

    std::atomic<float> needWidth_{0};
    std::atomic<double> maxBytes_{0};
    std::atomic<uint64_t> gone_{0};

    UniqueHandle mapping_;
    MappedView<openxr_share::Header> header_;
    ID3D11Device* device_ = nullptr;   // the device the below were made on (not held)
    uint32_t generation_ = 0;          // of the textures opened
    uint32_t width_ = 0, height_ = 0;  // the textures' size
    Microsoft::WRL::ComPtr<ID3D11Texture2D> textures_[openxr_share::kTextures];
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> views_[openxr_share::kTextures];  // in linear light
    Microsoft::WRL::ComPtr<ID3D11Query> read_[openxr_share::kTextures];  // after a read of the texture
    bool reading_[openxr_share::kTextures] = {};
    bool sameFamily_ = false;  // the layer's textures hold R8G8B8A8: a picture kept at its size is copied
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
};

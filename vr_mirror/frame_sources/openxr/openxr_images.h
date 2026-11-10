// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// OpenXrImages: the OpenXR layer's three shared textures, opened on a host's device, and the copy of a
// frame's image from one of them into the host's store (FrameCopy): as it is when the host needs nearly
// its own size and the memory holds it, else drawn at the size the host needs, averaged over each stored
// pixel's area in linear light. Stored sRGB-encoded once, as the game drew it (EyeInfo::encodedTwice
// false).
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

class OpenXrImages {
public:
    ~OpenXrImages() { Release(); }

    // How wide (pixels) the host needs a stored image, and the most memory one may take (bytes); the
    // stored image is its own width when that is within 1.2x of the need, else that wide, as tall as
    // its shape asks, and within the memory.
    void SetNeed(float width, double maxBytes) {
        needWidth_ = width;
        maxBytes_ = maxBytes;
    }

    // On `device` (every 2 s): maps the layer's memory if it is there, and opens its textures (again
    // when the layer made new ones, or on another device).
    void Open(ID3D11Device* device);
    // Copies frame `layerFrame`'s image (`width` x `height` at 0, 0 of texture `texture`) into `into`;
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
    // Copies that found their image gone, since the last call.
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
    bool sameFamily_ = false;  // the layer's textures hold R8G8B8A8: an image kept at its size is copied
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
};

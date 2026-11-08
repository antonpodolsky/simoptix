// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// FakeGamePicture: the fake game's picture of the left eye, drawn on the GPU, for both fakes (the Pimax
// runtime's client mirror, fake_pvr; the OpenXR layer's frames, fake_openxr). Instead of a scene,
// every pixel's colour encodes the world direction it shows (0.5 + 0.5 * direction), which the e2e
// runner reads back from the tool's window; or a car's cockpit fixed in the tracking space (what the
// cockpit correction measures); or, over the middle, a replay's real thumbnail. The code is stored
// 8-bit sRGB with up to one step of dither per pixel and frame, and black outside the drawn part of the
// field of view (`drawnFov`).
//
// Stored as the source it fakes stores it: Pimax's mirror sRGB-encodes the game's picture twice
// (`encodedTwice`), the OpenXR layer copies it as the game drew it, encoded once. Either way the tool,
// undoing what the source says (EyeInfo::encodedTwice), shows the plain code.
// Knows nothing about the runtime it fakes, timing or poses beyond the one it is drawn from.
#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

#include "common/rot.h"

class FakeGamePicture {
public:
    struct Look {
        float leftTan = 1, rightTan = 1, upTan = 1, downTan = 1;  // the eye's field of view (tangents)
        float drawnFov = 1;         // the fraction of each tangent drawn; black beyond it
        bool cockpit = false;       // the cockpit instead of the direction codes
        bool encodedTwice = false;  // stored as Pimax's mirror stores it
    };

    // The shaders, and a texture for a replay's thumbnail tiles if `thumbnails` (64x32 each).
    bool Init(ID3D11Device* device, bool thumbnails);
    // Draws the picture into `target` (width x height, an _SRGB render target) as seen by an eye turned
    // `eye` in the tracking space; `thumbnail`: a replay's 64x32 RGBA tile shown over the middle, or
    // null; `sceneTime`: the moment the game drew it for (seconds from the start), for the cockpit's
    // moving parts.
    void Draw(
        ID3D11DeviceContext* context,
        ID3D11RenderTargetView* target,
        int width,
        int height,
        const Quat& eye,
        const Look& look,
        const uint8_t* thumbnail,
        double sceneTime
    );

private:
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> cbuffer_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> thumbTex_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> thumbView_;
    unsigned frameNumber_ = 0;  // seeds each picture's dither
};

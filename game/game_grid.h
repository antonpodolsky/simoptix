// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The sharpness grid: draws the chart into one eye's image with D3D11, in one pass of a pixel shader.
// The chart is a grid of 10-degree cells around the head, each with a star of spokes, stripes at
// 6 to 18 cycles per degree in both directions, two checkerboards, and its own direction in degrees.
// Every pattern is filtered over the pixel it falls in, as a well anti-aliased game would draw it, so
// how sharp it looks depends only on the image's size and on what happens to the image afterwards. It
// knows nothing of OpenXR: the view comes in as a GridView (game_view.h).
#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>

#include "game_view.h"

class GameGrid {
public:
    bool Init(ID3D11Device* device, std::string* error);

    // Draws the whole of `target` (`width` x `height` pixels), as seen by `view`. The target's format is
    // sRGB: the shader writes linear light.
    void Draw(
        ID3D11DeviceContext* context,
        ID3D11RenderTargetView* target,
        uint32_t width,
        uint32_t height,
        const GridView& view
    ) const;

private:
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
};

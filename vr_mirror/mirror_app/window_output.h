// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// WindowOutput: the mirror app's drawing: a D3D11 device on the headset's adapter, the window's swap
// chain, the warp shader around vr_mirror\core\pipeline\5_warp\warp.hlsli, the draw of one image warped by a
// WarpParams, and the present. Knows nothing about the Pimax side,
// frame timing or the pipeline: it draws the image and the parameters it is given. Main thread.
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstdint>

#include "pipeline/5_warp/warp.h"

class WindowOutput {
public:
    // The window's client size (also before Create: the swap chain is made at it).
    void SetSize(int width, int height);
    int Width() const { return width_; }
    int Height() const { return height_; }
    // Creates the device on adapter `luid` (the headset's; its name printed), the swap chain for
    // `hwnd` and the shader. False on failure.
    bool Create(HWND hwnd, LUID luid);
    // Resizes the swap chain to the current size and makes its render target.
    void ResizeSwapChain();

    ID3D11Device* Device() const { return device_.Get(); }
    ID3D11DeviceContext* Context() const { return context_.Get(); }

    // Clears to the background and, with an `image`, draws it warped by `p`.
    void Draw(ID3D11ShaderResourceView* image, const WarpParams& p);
    // One present per mirror frame; DWM composes without tearing.
    void Present();

private:
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> cbuffer_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    int width_ = 1920, height_ = 1080;
};

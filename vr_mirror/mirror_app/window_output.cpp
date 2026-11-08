// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "window_output.h"

#include <d3d11_1.h>
#include <d3dcompiler.h>

#include <cstdio>
#include <cstring>

#include "warp_hlsli.h"  // generated from vr_mirror\core\pipeline\5_warp\warp.hlsli by build.bat

using Microsoft::WRL::ComPtr;

namespace {

    // ---------------------------------------------------------------------------------------------
    // The window's shader: a fullscreen triangle around the shared warp math.

    const char kShader[] = R"(
cbuffer Params : register(b0) {
    float4 m0, m1, m2;
    float4 outTan;
    float4 srcTan;
    float4 rect;
    float4 options;
};
#include "warp.hlsli"
Texture2D tex : register(t0);
SamplerState smp : register(s0);

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSOut VS(uint id : SV_VertexID) {
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = uv;
    return o;
}

float4 PS(VSOut i) : SV_Target {
    float3 src = WarpSrc(i.uv);
    if (src.z < 0.5) return float4(0, 0, 0, 1);
    return float4(FixColor(tex.SampleLevel(smp, src.xy, 0).rgb), 1);
}
)";

    // Serves #include "warp.hlsli" from the copy embedded at build time.
    class EmbeddedInclude : public ID3DInclude {
    public:
        HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR name, LPCVOID, LPCVOID* data, UINT* bytes) override {
            if (strcmp(name, "warp.hlsli") != 0) {
                return E_FAIL;
            }
            *data = kWarpHlsli;
            *bytes = UINT(sizeof(kWarpHlsli) - 1);
            return S_OK;
        }

        HRESULT __stdcall Close(LPCVOID) override { return S_OK; }
    };

}

void WindowOutput::SetSize(int width, int height) {
    width_ = width;
    height_ = height;
}

bool WindowOutput::Create(HWND hwnd, LUID luid) {
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return false;
    }
    ComPtr<IDXGIAdapter1> adapter, found;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        if (memcmp(&desc.AdapterLuid, &luid, sizeof(LUID)) == 0) {
            wprintf(L"GPU: %s\n", desc.Description);
            found = adapter;
            break;
        }
    }

    HRESULT hr = D3D11CreateDevice(
        found.Get(),
        found ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &device_,
        nullptr,
        &context_
    );

    if (FAILED(hr)) {
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = width_;
    sd.Height = height_;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (FAILED(factory->CreateSwapChainForHwnd(device_.Get(), hwnd, &sd, nullptr, nullptr, &swapChain_))) {
        return false;
    }

    EmbeddedInclude include;
    ComPtr<ID3DBlob> vsBlob, psBlob, err;
    if (FAILED(D3DCompile(
            kShader,
            sizeof(kShader) - 1,
            "warp",
            nullptr,
            &include,
            "VS",
            "vs_5_0",
            0,
            0,
            &vsBlob,
            &err
        ))
        || FAILED(D3DCompile(
            kShader,
            sizeof(kShader) - 1,
            "warp",
            nullptr,
            &include,
            "PS",
            "ps_5_0",
            0,
            0,
            &psBlob,
            &err
        ))) {
        printf("shader compile failed: %s\n", err ? static_cast<const char*>(err->GetBufferPointer()) : "?");
        return false;
    }

    device_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs_);
    device_->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps_);

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(WarpParams);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device_->CreateBuffer(&bd, nullptr, &cbuffer_);

    D3D11_SAMPLER_DESC smp = {};
    smp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    smp.AddressU = smp.AddressV = smp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    device_->CreateSamplerState(&smp, &sampler_);
    return true;
}

void WindowOutput::ResizeSwapChain() {
    if (!swapChain_) {
        return;
    }
    rtv_.Reset();
    swapChain_->ResizeBuffers(0, width_, height_, DXGI_FORMAT_UNKNOWN, 0);
    ComPtr<ID3D11Texture2D> back;
    swapChain_->GetBuffer(0, IID_PPV_ARGS(&back));
    device_->CreateRenderTargetView(back.Get(), nullptr, &rtv_);
}

void WindowOutput::Draw(ID3D11ShaderResourceView* image, const WarpParams& p) {
    float clear[4] = {0.05f, 0.05f, 0.05f, 1};
    context_->ClearRenderTargetView(rtv_.Get(), clear);
    if (!image) {
        return;
    }
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(context_->Map(cbuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, &p, sizeof(p));
        context_->Unmap(cbuffer_.Get(), 0);
    }

    D3D11_VIEWPORT vp = {0, 0, float(width_), float(height_), 0, 1};
    ID3D11ShaderResourceView* view = image;

    context_->RSSetViewports(1, &vp);
    context_->OMSetRenderTargets(1, rtv_.GetAddressOf(), nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vs_.Get(), nullptr, 0);
    context_->PSSetShader(ps_.Get(), nullptr, 0);
    context_->PSSetConstantBuffers(0, 1, cbuffer_.GetAddressOf());
    context_->PSSetShaderResources(0, 1, &view);
    context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
    context_->Draw(3, 0);

    ID3D11ShaderResourceView* none = nullptr;
    context_->PSSetShaderResources(0, 1, &none);
}

void WindowOutput::Present() {
    swapChain_->Present(0, 0);
}

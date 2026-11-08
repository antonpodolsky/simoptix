// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "openxr_pictures.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "shared/log.h"

using Microsoft::WRL::ComPtr;

namespace {

    // A triangle over the whole target, and each stored pixel the average of an n x n grid of bilinear
    // taps over its area of the picture, in linear light, encoded to sRGB for the UNORM target.
    constexpr const char* kShader = R"(
Texture2D picture : register(t0);
SamplerState linearClamp : register(s0);
cbuffer Params : register(b0) {
    float4 rect;  // the picture's area in the texture (u0, v0, u1, v1)
    float4 tap;   // x, y: one stored pixel's size in the texture's uv; z: taps per side
};
struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
V Vs(uint id : SV_VertexID) {
    V o;
    float2 t = float2((id << 1) & 2, id & 2);
    o.pos = float4(t * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = t;
    return o;
}
float3 Encode(float3 c) {
    c = saturate(c);
    return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}
float4 Ps(V i) : SV_Target {
    float2 uv = lerp(rect.xy, rect.zw, i.uv);
    int n = (int)tap.z;
    float3 sum = 0;
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++)
            sum += picture.SampleLevel(linearClamp, uv + ((float2(x, y) + 0.5) / n - 0.5) * tap.xy, 0).rgb;
    return float4(Encode(sum / (n * n)), 1);
}
)";

    struct Params {
        float rect[4];
        float tap[4];
    };

    // The view that reads the layer's texture in linear light: an _SRGB view of what an _SRGB swapchain
    // holds; the game's own format otherwise (OpenXR takes a plain UNORM or float swapchain as linear).
    DXGI_FORMAT LinearView(DXGI_FORMAT texture, DXGI_FORMAT game) {
        const bool srgb = game == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || game == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;

        switch (texture) {
            case DXGI_FORMAT_R8G8B8A8_TYPELESS:
            case DXGI_FORMAT_R8G8B8A8_UNORM:
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
                return srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
            case DXGI_FORMAT_B8G8R8A8_TYPELESS:
            case DXGI_FORMAT_B8G8R8A8_UNORM:
            case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
                return srgb ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB : DXGI_FORMAT_B8G8R8A8_UNORM;
            case DXGI_FORMAT_R10G10B10A2_TYPELESS:
                return DXGI_FORMAT_R10G10B10A2_UNORM;
            case DXGI_FORMAT_R16G16B16A16_TYPELESS:
                return DXGI_FORMAT_R16G16B16A16_FLOAT;
            default:
                return texture;
        }
    }

    bool R8G8B8A8(DXGI_FORMAT f) {
        return f == DXGI_FORMAT_R8G8B8A8_TYPELESS || f == DXGI_FORMAT_R8G8B8A8_UNORM
               || f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    }

}

bool OpenXrPictures::MakeShaders(ID3D11Device* device) {
    if (vs_ && ps_) {
        return true;
    }
    ComPtr<ID3DBlob> vs, ps, errors;
    if (FAILED(D3DCompile(
            kShader,
            strlen(kShader),
            "openxr_pictures",
            nullptr,
            nullptr,
            "Vs",
            "vs_5_0",
            0,
            0,
            &vs,
            &errors
        ))
        || FAILED(D3DCompile(
            kShader,
            strlen(kShader),
            "openxr_pictures",
            nullptr,
            nullptr,
            "Ps",
            "ps_5_0",
            0,
            0,
            &ps,
            &errors
        ))) {
        CoreLog(
            "OpenXR pictures: shader not compiled: %s",
            errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?"
        );

        return false;
    }

    device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vs_);
    device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &ps_);
    D3D11_BUFFER_DESC bd = {
        sizeof(Params),
        D3D11_USAGE_DYNAMIC,
        D3D11_BIND_CONSTANT_BUFFER,
        D3D11_CPU_ACCESS_WRITE,
    };

    device->CreateBuffer(&bd, nullptr, &constants_);
    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    device->CreateSamplerState(&sd, &sampler_);
    return vs_ && ps_ && constants_ && sampler_;
}

void OpenXrPictures::CloseTextures() {
    for (int k = 0; k < openxr_share::kTextures; k++) {
        if (reading_[k] && header_) {
            openxr_share::ReleaseTexture(*header_, k);
        }
        reading_[k] = false;
        textures_[k].Reset();
        views_[k].Reset();
    }

    generation_ = 0;
    width_ = height_ = 0;
}

void OpenXrPictures::Release() {
    CloseTextures();
    for (auto& q : read_) {
        q.Reset();
    }
    vs_.Reset(), ps_.Reset(), constants_.Reset(), sampler_.Reset();
    device_ = nullptr;
    header_.Reset();
    mapping_.Reset();
}

void OpenXrPictures::Open(ID3D11Device* device) {
    if (device != device_) {
        Release();
        device_ = device;
    }

    if (header_ && header_->state.load() == openxr_share::kEnded) {
        CloseTextures();  // the game's session ended: a new one makes new memory
        header_.Reset();
        mapping_.Reset();
    }

    if (!header_) {
        mapping_.Reset(OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, openxr_share::kMappingName));
        if (!mapping_) {
            return;
        }

        header_.Reset(
            static_cast<openxr_share::Header*>(MapViewOfFile(mapping_.Get(), FILE_MAP_ALL_ACCESS, 0, 0, 0))
        );

        if (!header_ || header_->magic != openxr_share::kMagic || header_->version != openxr_share::kVersion
            || header_->size != sizeof(openxr_share::Header)) {
            header_.Reset();
            mapping_.Reset();
            return;
        }
    }

    const uint32_t generation = header_->textureGeneration.load();
    if (generation == generation_) {
        return;
    }
    CloseTextures();
    if (generation == 0 || !MakeShaders(device)) {
        return;
    }
    const DXGI_FORMAT format = DXGI_FORMAT(header_->textureFormat), game = DXGI_FORMAT(header_->gameFormat);
    for (int k = 0; k < openxr_share::kTextures; k++) {
        HANDLE h = reinterpret_cast<HANDLE>(header_->textureHandle[k]);
        if (FAILED(device->OpenSharedResource(h, IID_PPV_ARGS(&textures_[k])))) {
            CoreLog("OpenXR pictures: the layer's texture %d not opened (another graphics adapter?)", k);
            CloseTextures();
            return;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC v = {};
        v.Format = LinearView(format, game);
        v.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        v.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(textures_[k].Get(), &v, &views_[k]);
        if (!read_[k]) {
            D3D11_QUERY_DESC qd = {D3D11_QUERY_EVENT, 0};
            device->CreateQuery(&qd, &read_[k]);
        }
    }

    generation_ = generation;
    width_ = header_->textureWidth, height_ = header_->textureHeight;
    sameFamily_ = R8G8B8A8(format);
    CoreLog(
        "OpenXR pictures: the layer's textures opened, %ux%u, format %u (the game's %u)",
        width_,
        height_,
        unsigned(format),
        unsigned(game)
    );
}

bool OpenXrPictures::Copy(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    int texture,
    uint64_t layerFrame,
    int width,
    int height,
    FrameCopy* into,
    bool shared
) {
    // The layer makes its textures when a reader first signs, a moment before its first frames: open them
    // now rather than at the host's next call to Open (every 2 s), or those frames would be gone.
    if (!header_ || header_->textureGeneration.load() != generation_) {
        Open(device);
    }
    if (!header_ || texture < 0 || texture >= openxr_share::kTextures || !views_[texture] || reading_[texture]
        || width <= 0 || height <= 0 || uint32_t(width) > width_ || uint32_t(height) > height_) {
        gone_++;
        return false;
    }

    // The stored size: as wide as the host needs, never wider than the picture, within the memory allowed.
    float w = std::min(float(width), needWidth_.load() > 0 ? needWidth_.load() : float(width));
    const double maxBytes = maxBytes_.load();
    if (maxBytes > 0) {
        w = std::min(w, float(std::sqrt(maxBytes / 4 * width / height)));
    }
    const UINT sw = std::max(1u, UINT(std::lround(w)));
    const UINT sh = std::max(1u, UINT(std::lround(w * height / width)));

    const bool asIs = sw == UINT(width) && sh == UINT(height) && sameFamily_;

    if (!openxr_share::ClaimTexture(*header_, texture, layerFrame)) {
        gone_++;
        return false;
    }

    FrameCopyShape shape;
    shape.width = sw, shape.height = sh, shape.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    shape.shared = shared, shape.drawn = !asIs;
    into->Fit(device, shape);
    if (!into->Texture() || (!asIs && !into->Target())) {
        openxr_share::ReleaseTexture(*header_, texture);
        return false;
    }

    if (asIs) {
        const D3D11_BOX box = {0, 0, 0, UINT(width), UINT(height), 1};
        context->CopySubresourceRegion(into->Texture(), 0, 0, 0, 0, textures_[texture].Get(), 0, &box);
    } else {
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(context->Map(constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            const float u1 = float(width) / width_, v1 = float(height) / height_;
            const float taps = std::min(4.0f, std::max(1.0f, std::ceil(float(width) / sw)));
            Params p = {{0, 0, u1, v1}, {u1 / sw, v1 / sh, taps, 0}};
            memcpy(m.pData, &p, sizeof(p));
            context->Unmap(constants_.Get(), 0);
        }

        ID3D11RenderTargetView* target = into->Target();
        const D3D11_VIEWPORT vp = {0, 0, float(sw), float(sh), 0, 1};

        context->OMSetRenderTargets(1, &target, nullptr);
        context->RSSetViewports(1, &vp);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs_.Get(), nullptr, 0);
        context->PSSetShader(ps_.Get(), nullptr, 0);
        context->PSSetConstantBuffers(0, 1, constants_.GetAddressOf());
        context->PSSetShaderResources(0, 1, views_[texture].GetAddressOf());
        context->PSSetSamplers(0, 1, sampler_.GetAddressOf());
        context->Draw(3, 0);

        ID3D11ShaderResourceView* none = nullptr;
        ID3D11RenderTargetView* noTarget = nullptr;
        context->PSSetShaderResources(0, 1, &none);
        context->OMSetRenderTargets(1, &noTarget, nullptr);
    }

    context->End(read_[texture].Get());
    reading_[texture] = true;
    return true;
}

void OpenXrPictures::Poll(ID3D11DeviceContext* context) {
    for (int k = 0; k < openxr_share::kTextures; k++) {
        if (!reading_[k]
            || context->GetData(read_[k].Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) {
            continue;
        }

        reading_[k] = false;
        if (header_) {
            openxr_share::ReleaseTexture(*header_, k);
        }
    }
}

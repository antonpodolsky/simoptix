// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "adapters/d3d/thumbnail.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "shared/log.h"

namespace {

    // Each thumbnail texel is the average of 8x8 samples across its footprint (SAMPLES: Init's; the
    // cockpit patches 4x4), so fine detail (and the two sources' different sharpness) averages out the
    // same way in both images.
    const char kShader[] = R"(
Texture2D tex : register(t0);
SamplerState smp : register(s0);
cbuffer C : register(b0) {
    float4 uv;       // the area of the source drawn into this tile
    float4 options;  // x: undo one sRGB encoding
    float4 tile;     // the tile: its first texel (xy) and size in texels (zw)
};

float4 VS(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
}

float4 PS(float4 pos : SV_Position) : SV_Target {
    float2 span = (uv.zw - uv.xy) / tile.zw;
    float2 start = uv.xy + floor(pos.xy - tile.xy) * span;
    float3 sum = 0;
    for (int y = 0; y < SAMPLES; y++) {
        for (int x = 0; x < SAMPLES; x++) {
            float3 c = tex.SampleLevel(smp, start + (float2(x, y) + 0.5) * span / SAMPLES, 0).rgb;
            if (options.x > 0.5) c = c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
            sum += c;
        }
    }
    sum /= SAMPLES * SAMPLES;
#ifdef COLOR
    return float4(sum, 1);
#else
    return dot(sum, float3(0.2126, 0.7152, 0.0722));
#endif
}
)";

}

bool Thumbnailer::Init(ID3D11Device* device, bool color, int width, int height, int samples) {
    color_ = color;
    width_ = width, height_ = height;

    char count[8];
    snprintf(count, sizeof(count), "%d", std::clamp(samples, 1, 16));
    const D3D_SHADER_MACRO colorDefines[] = {{"COLOR", "1"}, {"SAMPLES", count}, {nullptr, nullptr}};
    const D3D_SHADER_MACRO greyDefines[] = {{"SAMPLES", count}, {nullptr, nullptr}};

    Microsoft::WRL::ComPtr<ID3DBlob> vsBlob, psBlob, errors;
    if (FAILED(D3DCompile(
            kShader,
            sizeof(kShader) - 1,
            "thumbnail",
            greyDefines,
            nullptr,
            "VS",
            "vs_5_0",
            0,
            0,
            &vsBlob,
            &errors
        ))
        || FAILED(D3DCompile(
            kShader,
            sizeof(kShader) - 1,
            "thumbnail",
            color ? colorDefines : greyDefines,
            nullptr,
            "PS",
            "ps_5_0",
            0,
            0,
            &psBlob,
            &errors
        ))) {
        CoreLog(
            "thumbnail shader: %s",
            errors ? static_cast<const char*>(errors->GetBufferPointer()) : "compile failed"
        );

        return false;
    }

    device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs_);
    device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps_);

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = 48;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    device->CreateBuffer(&bd, nullptr, &constants_);

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    device->CreateSamplerState(&sd, &sampler_);

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = width_;
    td.Height = height_;
    td.MipLevels = td.ArraySize = 1;
    td.Format = color ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R32_FLOAT;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;

    device->CreateTexture2D(&td, nullptr, &target_);
    if (target_) {
        device->CreateRenderTargetView(target_.Get(), nullptr, &targetView_);
    }

    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    for (auto& s : staging_) {
        device->CreateTexture2D(&td, nullptr, &s);
    }

    D3D11_QUERY_DESC qd = {D3D11_QUERY_EVENT, 0};
    for (auto& q : done_) {
        device->CreateQuery(&qd, &q);
    }

    if (!vs_ || !ps_ || !constants_ || !sampler_ || !targetView_ || !staging_[kSlots - 1]
        || !done_[kSlots - 1]) {
        ps_.Reset();
        return false;
    }

    return true;
}

int64_t Thumbnailer::Make(
    ID3D11DeviceContext* context,
    ID3D11ShaderResourceView* src,
    const float uv[4],
    bool decode
) {
    if (!ps_ || !src) {
        return -1;
    }
    context->PSSetShaderResources(0, 1, &src);
    Draw(context, uv, 0, 0, width_, height_, decode);
    return Finish(context);
}

int64_t Thumbnailer::MakeTiles(
    ID3D11DeviceContext* context,
    ID3D11ShaderResourceView* src,
    const float (*areas)[4],
    int count,
    int columns,
    int tile,
    bool decode
) {
    if (!ps_ || !src || columns <= 0) {
        return -1;
    }
    context->PSSetShaderResources(0, 1, &src);
    for (int i = 0; i < count; i++) {
        Draw(context, areas[i], (i % columns) * tile, (i / columns) * tile, tile, tile, decode);
    }

    return Finish(context);
}

void Thumbnailer::Draw(
    ID3D11DeviceContext* context,
    const float uv[4],
    int x,
    int y,
    int w,
    int h,
    bool decode
) {
    const float c[12] = {
        uv[0],
        uv[1],
        uv[2],
        uv[3],
        decode ? 1.0f : 0.0f,
        0,
        0,
        0,
        float(x),
        float(y),
        float(w),
        float(h),
    };

    context->UpdateSubresource(constants_.Get(), 0, nullptr, c, 0, 0);

    ID3D11RenderTargetView* rtv = targetView_.Get();
    context->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT vp = {float(x), float(y), float(w), float(h), 0, 1};
    context->RSSetViewports(1, &vp);

    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vs_.Get(), nullptr, 0);
    context->PSSetShader(ps_.Get(), nullptr, 0);

    ID3D11Buffer* cb = constants_.Get();
    context->PSSetConstantBuffers(0, 1, &cb);
    ID3D11SamplerState* smp = sampler_.Get();
    context->PSSetSamplers(0, 1, &smp);

    context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    context->RSSetState(nullptr);
    context->Draw(3, 0);
}

int64_t Thumbnailer::Finish(ID3D11DeviceContext* context) {
    // Unbind, so the host's own drawing finds nothing of ours bound.
    ID3D11ShaderResourceView* noSrv = nullptr;
    context->PSSetShaderResources(0, 1, &noSrv);
    context->OMSetRenderTargets(0, nullptr, nullptr);
    const int64_t ticket = next_++;
    context->CopyResource(staging_[ticket % kSlots].Get(), target_.Get());
    context->End(done_[ticket % kSlots].Get());
    return ticket;
}

bool Thumbnailer::Read(ID3D11DeviceContext* context, int64_t ticket, std::vector<float>* out) {
    if (ticket < 0 || ticket >= next_ || next_ - ticket > kSlots) {
        return false;
    }
    if (context->GetData(done_[ticket % kSlots].Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) {
        return false;  // not finished yet (the host flushes its work once per pass)
    }

    D3D11_MAPPED_SUBRESOURCE m;
    HRESULT
    hr = context->Map(staging_[ticket % kSlots].Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m);

    if (FAILED(hr)) {
        return false;  // DXGI_ERROR_WAS_STILL_DRAWING: try again later
    }

    const int channels = Channels();
    out->resize(size_t(width_) * height_ * channels);  // no reallocation once it has this size

    for (int y = 0; y < height_; y++) {
        const float* row = reinterpret_cast<const float*>(static_cast<const char*>(m.pData) + y * m.RowPitch);
        if (channels == 1) {
            memcpy(out->data() + size_t(y) * width_, row, width_ * sizeof(float));
        } else {
            for (int x = 0; x < width_; x++) {
                for (int c = 0; c < 3; c++) {
                    (*out)[(size_t(y) * width_ + x) * 3 + c] = row[x * 4 + c];
                }
            }
        }
    }

    context->Unmap(staging_[ticket % kSlots].Get(), 0);
    return true;
}

float Thumbnailer::Similarity(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) {
        return 0;
    }
    double ma = 0, mb = 0;
    for (size_t i = 0; i < a.size(); i++) {
        ma += a[i];
        mb += b[i];
    }

    ma /= a.size();
    mb /= b.size();
    double ab = 0, aa = 0, bb = 0;
    for (size_t i = 0; i < a.size(); i++) {
        double x = a[i] - ma, y = b[i] - mb;
        ab += x * y;
        aa += x * x;
        bb += y * y;
    }

    return aa > 0 && bb > 0 ? float(ab / std::sqrt(aa * bb)) : 0;
}

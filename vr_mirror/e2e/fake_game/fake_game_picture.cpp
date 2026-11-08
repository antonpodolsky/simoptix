// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "fake_game_picture.h"

#include <d3dcompiler.h>

#include <cmath>
#include <cstring>

#include "fake_game_motion.h"

using Microsoft::WRL::ComPtr;

namespace {

    constexpr int kThumbWidth = 64, kThumbHeight = 32;  // a replay's thumbnail tile

    constexpr char kShader[] = R"(
cbuffer Params : register(b0) {
    float4 m0, m1, m2; float4 tan;  // eye rotation; tan: left, right, up, down
    float4 frame;                   // x: real thumbnail on, y: drawn part of the FOV (tangents),
                                    // z: the cockpit instead of the codes, w: frame number (dither seed)
    float4 scene;                   // the cockpit's: x the car's yaw, y the wheel's angle (radians);
                                    // z: stored encoded twice (Pimax's mirror)
};
Texture2D<float4> thumbTex : register(t0);  // this frame's real thumbnail (64x32), when frame.x
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut VS(uint id : SV_VertexID) {
    VSOut o; float2 uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); o.uv = uv; return o;
}
// The cockpit (frame.z): fixed in the tracking space like a real car's on a sim rig, by direction
// (azimuth right of ahead, elevation; degrees). Detail of 0.5 to 3 degrees, as stitching, panel
// edges and grain at the headset's resolution.
float Hash(float2 p) { return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }
float ValueNoise(float2 p) {
    float2 c = floor(p), f = frac(p), u = f * f * (3 - 2 * f);
    return lerp(lerp(Hash(c), Hash(c + float2(1, 0)), u.x),
                lerp(Hash(c + float2(0, 1)), Hash(c + float2(1, 1)), u.x), u.y);
}
float Detail(float2 a, float seed) {
    return 0.5 * ValueNoise(a / 3 + seed) + 0.3 * ValueNoise(a / 1.2 + 2.3 * seed)
           + 0.2 * ValueNoise(a / 0.5 + 4.1 * seed);
}
float3 Cockpit(float3 w) {
    float az = degrees(atan2(w.x, -w.z)), el = degrees(asin(clamp(w.y, -1, 1)));
    // The wheel, ahead and below: it turns with the steering.
    float2 hub = float2(az, el + 32);
    if (length(hub) < 14) {
        float c = cos(scene.y), s = sin(scene.y);
        return 0.08 + 0.3 * Detail(float2(c * hub.x - s * hub.y, s * hub.x + c * hub.y), 7);
    }
    bool roof = el > 16, dash = el < -12, pillar = abs(az) > 36 && abs(az) < 50;
    if (dash && az > 15 && az < 30) return 0.05;  // a plain dark panel: nothing to measure
    if (roof || dash || pillar) return 0.06 + 0.35 * Detail(float2(az, el), roof ? 1 : dash ? 3 : 5);
    // Through the windows: the world outside, turning as the car corners.
    float c = cos(scene.x), s = sin(scene.x);
    float3 o = float3(c * w.x + s * w.z, w.y, -s * w.x + c * w.z);
    return 0.2 + 0.6 * Detail(float2(degrees(atan2(o.x, -o.z)), degrees(asin(clamp(o.y, -1, 1)))), 11);
}
float3 Code(VSOut i) {
    // frame.x on: the game frame's real thumbnail (a replay's), as flat 64x32 blocks over the area
    // the tool makes its thumbnails of (kThumbArea, u 0.2-0.8, v 0.25-0.85), so the
    // tool's slot check sees the real pictures (each block averages to exactly its colour;
    // the tool's samples sit 1.5 px and more inside each block). The colours are as displayed,
    // like the direction codes elsewhere. The rest of the picture keeps its codes.
    if (frame.x > 0.5) {
        float2 a = (i.uv - float2(0.2, 0.25)) / 0.6;
        if (all(a >= 0) && all(a < 1)) return thumbTex.Load(int3(int(a.x * 64), int(a.y * 32), 0)).rgb;
    }
    float3 d = normalize(float3(lerp(-tan.x, tan.y, i.uv.x), lerp(tan.z, -tan.w, i.uv.y), -1));
    float3 w = float3(dot(m0.xyz, d), dot(m1.xyz, d), dot(m2.xyz, d));
    if (frame.z > 0.5) return Cockpit(w);
    return 0.5 + 0.5 * w;
}
float3 ToSrgb(float3 l) { return l <= 0.0031308 ? 12.92 * l : 1.055 * pow(l, 1 / 2.4) - 0.055; }
float3 ToLinear(float3 s) { return s <= 0.04045 ? s / 12.92 : pow((s + 0.055) / 1.055, 2.4); }
float Noise(float2 p, float seed) {
    return frac(sin(dot(p + seed * float2(0.37, 0.71), float2(12.9898, 78.233))) * 43758.5453);
}
// The code is stored in 8-bit sRGB, about 0.7 deg of direction per step. Without noise, the
// runner's average over a 32x32 patch spans only 3-4 steps and lands off-centre by up to ~0.2
// deg for some view directions (a fixed error while the view rests there). Up to +-1 step of
// noise per pixel and frame (dither) makes that error average out (modelled: 0.115 -> 0.047 deg).
float4 PS(VSOut i) : SV_Target {
    // Outside the drawn part of the FOV (each reported tangent times frame.y): plain black, no dither.
    float2 t = float2(lerp(-tan.x, tan.y, i.uv.x), lerp(tan.z, -tan.w, i.uv.y));  // right and up positive
    if (t.x < -frame.y * tan.x || t.x > frame.y * tan.y || t.y > frame.y * tan.z || t.y < -frame.y * tan.w)
        return float4(0, 0, 0, 1);
    // Stored as the faked source stores it (scene.z): Pimax's mirror sRGB-encodes the picture twice, so
    // its bytes are the code's encoding; the OpenXR layer's are the code itself.
    float3 c = Code(i);
    float3 s = scene.z > 0.5 ? ToSrgb(c) : c;
    float3 n = float3(Noise(i.pos.xy, frame.w), Noise(i.pos.xy + 17, frame.w), Noise(i.pos.xy + 41, frame.w));
    return float4(ToLinear(saturate(s + (2 * n - 1) / 255)), 1);
})";

}

bool FakeGamePicture::Init(ID3D11Device* device, bool thumbnails) {
    ComPtr<ID3DBlob> vs, ps, err;
    if (FAILED(D3DCompile(
            kShader,
            sizeof(kShader) - 1,
            "fake",
            nullptr,
            nullptr,
            "VS",
            "vs_5_0",
            0,
            0,
            &vs,
            &err
        ))
        || FAILED(D3DCompile(
            kShader,
            sizeof(kShader) - 1,
            "fake",
            nullptr,
            nullptr,
            "PS",
            "ps_5_0",
            0,
            0,
            &ps,
            &err
        ))) {
        return false;
    }

    device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vs_);
    device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &ps_);

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = 96;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device->CreateBuffer(&bd, nullptr, &cbuffer_);

    if (thumbnails) {
        D3D11_TEXTURE2D_DESC tt = {};
        tt.Width = kThumbWidth;
        tt.Height = kThumbHeight;
        tt.MipLevels = tt.ArraySize = 1;
        tt.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // the displayed colours, as stored
        tt.SampleDesc.Count = 1;
        tt.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        if (FAILED(device->CreateTexture2D(&tt, nullptr, &thumbTex_))
            || FAILED(device->CreateShaderResourceView(thumbTex_.Get(), nullptr, &thumbView_))) {
            return false;
        }
    }

    return vs_ && ps_ && cbuffer_;
}

void FakeGamePicture::Draw(
    ID3D11DeviceContext* context,
    ID3D11RenderTargetView* target,
    int width,
    int height,
    const Quat& eye,
    const Look& look,
    const uint8_t* thumbnail,
    double sceneTime
) {
    const bool real = thumbnail && thumbTex_;
    if (real) {
        context->UpdateSubresource(thumbTex_.Get(), 0, nullptr, thumbnail, kThumbWidth * 4, 0);
        context->PSSetShaderResources(0, 1, thumbView_.GetAddressOf());
    }

    float m[3][3];
    ToMatrix(eye, m);

    // The cockpit's car corners left and right in turn (its yaw rate up to 40 deg/s, a 7 s cycle),
    // the wheel turned with it (2.5 times the yaw rate's degrees).
    const double cycle = 2 * motion::kPi / 7;
    const double yawRate = 40 * std::sin(cycle * sceneTime);  // deg/s
    const float carYaw = float(-40 / cycle * std::cos(cycle * sceneTime)) * motion::kDeg;
    const float wheel = float(2.5 * yawRate) * motion::kDeg;
    float params[24] = {
        m[0][0],
        m[0][1],
        m[0][2],
        0,
        m[1][0],
        m[1][1],
        m[1][2],
        0,
        m[2][0],
        m[2][1],
        m[2][2],
        0,
        look.leftTan,
        look.rightTan,
        look.upTan,
        look.downTan,
        real ? 1.0f : 0.0f,
        look.drawnFov,
        look.cockpit ? 1.0f : 0.0f,
        float(frameNumber_++ % 4096),
        carYaw,
        wheel,
        look.encodedTwice ? 1.0f : 0.0f,
        0
    };

    D3D11_MAPPED_SUBRESOURCE mapped;
    context->Map(cbuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, params, sizeof(params));
    context->Unmap(cbuffer_.Get(), 0);
    D3D11_VIEWPORT vp = {0, 0, float(width), float(height), 0, 1};
    context->RSSetViewports(1, &vp);
    context->OMSetRenderTargets(1, &target, nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vs_.Get(), nullptr, 0);
    context->PSSetShader(ps_.Get(), nullptr, 0);
    context->PSSetConstantBuffers(0, 1, cbuffer_.GetAddressOf());
    context->Draw(3, 0);
}

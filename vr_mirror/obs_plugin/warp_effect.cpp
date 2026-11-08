// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "warp_effect.h"

#include "warp_hlsli.h"  // generated from vr_mirror\core\pipeline\5_warp\warp.hlsli by build.bat

std::string WarpEffectText() {
    std::string
        s = "uniform float4x4 ViewProj;\n"
            "uniform texture2d image;\n"
            "uniform float4 m0;\nuniform float4 m1;\nuniform float4 m2;\n"
            "uniform float4 outTan;\nuniform float4 srcTan;\nuniform float4 rect;\nuniform float4 options;\n"
            "uniform float4 texSize;\n"  // image width, height, 1/width, 1/height
            "sampler_state linearClamp { Filter = Linear; AddressU = Clamp; AddressV = Clamp; };\n"
            "struct VertInOut { float4 pos : POSITION; float2 uv : TEXCOORD0; };\n"
            "VertInOut VSDefault(VertInOut vert_in) {\n"
            "    VertInOut vert_out;\n"
            "    vert_out.pos = mul(float4(vert_in.pos.xyz, 1.0), ViewProj);\n"
            "    vert_out.uv = vert_in.uv;\n"
            "    return vert_out;\n"
            "}\n";

    s += kWarpHlsli;

    // Catmull-Rom (bicubic) from 9 bilinear taps: two resampling steps (the game's mirror, then
    // this warp) at about 1:1 would each soften fine detail with plain bilinear filtering.
    s += "\nfloat3 SampleSharp(float2 uv) {\n"
         "    float2 p = uv * texSize.xy;\n"
         "    float2 t1 = floor(p - 0.5) + 0.5;\n"
         "    float2 f = p - t1;\n"
         "    float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));\n"
         "    float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);\n"
         "    float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));\n"
         "    float2 w3 = f * f * (-0.5 + 0.5 * f);\n"
         "    float2 w12 = w1 + w2;\n"
         "    float2 t0 = (t1 - 1.0) * texSize.zw;\n"
         "    float2 t3 = (t1 + 2.0) * texSize.zw;\n"
         "    float2 t12 = (t1 + w2 / w12) * texSize.zw;\n"
         "    float3 r = image.Sample(linearClamp, float2(t0.x, t0.y)).rgb * (w0.x * w0.y);\n"
         "    r += image.Sample(linearClamp, float2(t12.x, t0.y)).rgb * (w12.x * w0.y);\n"
         "    r += image.Sample(linearClamp, float2(t3.x, t0.y)).rgb * (w3.x * w0.y);\n"
         "    r += image.Sample(linearClamp, float2(t0.x, t12.y)).rgb * (w0.x * w12.y);\n"
         "    r += image.Sample(linearClamp, float2(t12.x, t12.y)).rgb * (w12.x * w12.y);\n"
         "    r += image.Sample(linearClamp, float2(t3.x, t12.y)).rgb * (w3.x * w12.y);\n"
         "    r += image.Sample(linearClamp, float2(t0.x, t3.y)).rgb * (w0.x * w3.y);\n"
         "    r += image.Sample(linearClamp, float2(t12.x, t3.y)).rgb * (w12.x * w3.y);\n"
         "    r += image.Sample(linearClamp, float2(t3.x, t3.y)).rgb * (w3.x * w3.y);\n"
         "    return saturate(r);\n"
         "}\n"
         "float4 PSWarp(VertInOut vert_in) : TARGET {\n"
         "    float3 src = WarpSrc(vert_in.uv);\n"
         "    if (src.z < 0.5) return float4(0.0, 0.0, 0.0, 1.0);\n"
         "    return float4(FixColor(SampleSharp(src.xy)), 1.0);\n"
         "}\n"
         "technique Draw { pass { vertex_shader = VSDefault(vert_in); pixel_shader = PSWarp(vert_in); } }\n";

    return s;
}

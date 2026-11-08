// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Shared warp math for every host (the mirror app's D3D11 shader, the OBS plugin's effect).
//
// For each output pixel, build the view ray of the smoothed virtual camera, rotate it into
// the raw eye camera and find where that ray lands in the mirror image.
//
// The host declares these parameters with these names (constant buffer or effect uniforms),
// filled from WarpParams (warp.h), then calls WarpSrc and FixColor:
//   float4 m0, m1, m2;  rows: output camera -> raw eye camera
//   float4 outTan;      x, y: output half-FOV tangents
//   float4 srcTan;      left, right, up, down tangents of the mirror eye
//   float4 rect;        eye's area in the mirror texture (u0, v0, u1, v1)
//   float4 options;     x: 1 = decode one extra sRGB encoding

// Where output pixel `uv` (0..1, top left origin) samples the mirror: xy is the texture
// coordinate, z is 1 when the ray lands inside the eye's view and 0 when it doesn't (draw black
// there). Returned as one value, without out-parameters, so OBS effect files can use it too.
float3 WarpSrc(float2 uv) {
    float3 d = float3((uv.x * 2 - 1) * outTan.x, (1 - uv.y * 2) * outTan.y, -1);
    float3 s = float3(dot(m0.xyz, d), dot(m1.xyz, d), dot(m2.xyz, d));
    if (s.z > -1e-4) {
        return float3(0, 0, 0);
    }
    float2 t = s.xy / -s.z;
    float2 st = float2((t.x + srcTan.x) / (srcTan.x + srcTan.y), (srcTan.z - t.y) / (srcTan.z + srcTan.w));
    if (any(st < 0) || any(st > 1)) {
        return float3(0, 0, 0);
    }
    return float3(lerp(rect.xy, rect.zw, st), 1);
}

// A picture sRGB-encoded twice (the Pimax game eye: EyeInfo::encodedTwice) has options.x set: undo one
// encoding.
float3 FixColor(float3 c) {
    if (options.x > 0.5) {
        c = c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
    }
    return c;
}

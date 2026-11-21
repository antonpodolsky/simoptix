// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "game_grid.h"

#include <d3dcompiler.h>

#include <cstring>

using Microsoft::WRL::ComPtr;

namespace {

    constexpr char kShader[] = R"(
cbuffer Params : register(b0) {
    float4 m0, m1, m2;  // rows of the rotation from the eye's axes to the chart's
    float4 tan;         // the eye's field of view as tangents: left, right, up, down
    float4 size;        // xy: the image's size in pixels
};

struct VSOut { float4 pos : SV_Position; };

VSOut VS(uint id : SV_VertexID) {
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

// Sizes in degrees, frequencies in cycles per degree, shades in linear light.
static const float kCell = 10, kMaxCellElevation = 80;
static const float kBorderWidth = 0.05, kLabelHeight = 0.5, kColumnLabelHeight = 0.4, kCellLabelHeight = 0.9;
static const float kStarRadius = 2.1, kStarCycles = 32;
static const float kStripeFrequencies[5] = {6, 9, 12, 15, 18};
static const float kStripeStep = 0.86, kStripeWidth = 0.7;
static const float kBackground = 0.18, kDark = 0.02, kLight = 0.9, kBorder = 0.6;

// A font of 3x5 pixels: 0 to 9, then the minus sign. Rows from the top, 3 bits each, the left first.
static const uint kGlyphs[11] = {
    0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF, 0x01C0
};
static const uint kMinus = 10;

// The light share of a square wave of period 1 (dark, then light) from 0 to t.
float SquareIntegral(float t) { return floor(t) * 0.5 + max(frac(t) - 0.5, 0); }

// The light share of that wave averaged over [t - w/2, t + w/2]: a pixel w periods wide.
float Stripes(float t, float w) {
    w = max(w, 1e-4);
    return (SquareIntegral(t + 0.5 * w) - SquareIntegral(t - 0.5 * w)) / w;
}

// The share of a pixel w wide, centred d from a line's centre, that the line covers.
float Line(float d, float lineWidth, float w) {
    w = max(w, 1e-4);
    float overlap = min(d + 0.5 * w, 0.5 * lineWidth) - max(d - 0.5 * w, -0.5 * lineWidth);
    return saturate(overlap / w);
}

// The light share of a checkerboard of `frequency` over the pixel's footprint fp.
float Checker(float2 p, float2 fp, float frequency) {
    float a = Stripes(p.x * frequency, fp.x * frequency), b = Stripes(p.y * frequency, fp.y * frequency);
    return a * (1 - b) + (1 - a) * b;
}

float GlyphBit(uint glyph, int col, int row) {
    if (col < 0 || col > 2 || row < 0 || row > 4) return 0;
    return (kGlyphs[glyph] >> (14 - (row * 3 + col))) & 1;
}

uint Digits(uint magnitude) { return magnitude >= 100 ? 3 : magnitude >= 10 ? 2 : 1; }

// The width of the number `value` written `height` tall: 3 font pixels a character, 1 between.
float TextWidth(int value, float height) {
    uint characters = Digits(abs(value)) + (value < 0 ? 1 : 0);
    return (4 * characters - 1) * height / 5;
}

// Whether p falls on the number `value` written from `topLeft`, `height` tall.
float TextAt(int value, float2 p, float2 topLeft, float height) {
    float unit = height / 5;
    float2 q = float2(p.x - topLeft.x, topLeft.y - p.y) / unit;  // font pixels, y down
    if (q.x < 0 || q.y < 0 || q.y >= 5) return 0;

    uint magnitude = abs(value);
    uint digits = Digits(magnitude);
    uint signs = value < 0 ? 1 : 0;
    int column = (int)floor(q.x), character = column / 4;
    if (character >= (int)(signs + digits)) return 0;
    if (character < (int)signs) return GlyphBit(kMinus, column - 4 * character, (int)floor(q.y));

    uint place = digits - 1 - (character - signs), digit = magnitude;
    for (uint k = 0; k < place; k++) digit /= 10;
    return GlyphBit(digit % 10, column - 4 * character, (int)floor(q.y));
}

// TextAt over the pixel, at four points of it.
float Text(int value, float2 p, float2 dpx, float2 dpy, float2 topLeft, float height) {
    return 0.25 * (TextAt(value, p - 0.25 * dpx - 0.25 * dpy, topLeft, height)
                   + TextAt(value, p + 0.25 * dpx - 0.25 * dpy, topLeft, height)
                   + TextAt(value, p - 0.25 * dpx + 0.25 * dpy, topLeft, height)
                   + TextAt(value, p + 0.25 * dpx + 0.25 * dpy, topLeft, height));
}

float Shade(float light) { return lerp(kDark, kLight, light); }

// The cell's own patterns, at p degrees from its centre (x right, y up).
float CellPatterns(float2 p, float2 fp, float2 dpx, float2 dpy, float cellAz, float cellEl) {
    // Top left: a star of spokes. Where they turn grey towards its centre, the image can't resolve them.
    float2 fromStar = p - float2(-2.5, 2.5);
    float r = length(fromStar);
    if (r < kStarRadius) {
        float t = atan2(fromStar.y, fromStar.x) / 6.2831853 * kStarCycles;
        float w = kStarCycles / 6.2831853 * max(fp.x, fp.y) / max(r, 1e-3);
        return Shade(Stripes(t, w));
    }

    // Top right: rows of upright stripes, which judge across. Bottom right: columns of flat ones.
    for (int k = 0; k < 5; k++) {
        float f = kStripeFrequencies[k];
        float rowTop = 4.6 - kStripeStep * k, columnLeft = 0.4 + kStripeStep * k;
        if (p.y <= rowTop && p.y > rowTop - kStripeWidth && p.x >= 1.5 && p.x <= 4.6)
            return Shade(Stripes(p.x * f, fp.x * f));
        if (p.x >= columnLeft && p.x < columnLeft + kStripeWidth && p.y <= -1.4 && p.y >= -4.6)
            return Shade(Stripes(p.y * f, fp.y * f));
    }

    // Bottom left: checkerboards at 6 and 12 cycles per degree.
    if (p.y >= -4.6 && p.y <= -2.8) {
        if (p.x >= -4.6 && p.x <= -2.6) return Shade(Checker(p, fp, 6));
        if (p.x >= -2.4 && p.x <= -0.4) return Shade(Checker(p, fp, 12));
    }

    // The labels: each row's and column's frequency, and the cell's direction (right and up positive).
    float text = 0;
    for (int j = 0; j < 5; j++) {
        int f = (int)kStripeFrequencies[j];
        float columnLabelLeft = 0.4 + kStripeStep * j + 0.5 * (kStripeWidth - TextWidth(f, kColumnLabelHeight));
        text = max(text, Text(f, p, dpx, dpy, float2(0.4, 4.5 - kStripeStep * j), kLabelHeight));
        text = max(text, Text(f, p, dpx, dpy, float2(columnLabelLeft, -0.6), kColumnLabelHeight));
    }

    text = max(text, Text((int)cellAz, p, dpx, dpy, float2(-4.6, -0.4), kCellLabelHeight));
    text = max(text, Text((int)cellEl, p, dpx, dpy, float2(-4.6, -1.5), kCellLabelHeight));
    return lerp(kBackground, kLight, text);
}

float4 PS(VSOut i) : SV_Target {
    // The direction this pixel looks in, on the chart's axes, as azimuth (right positive) and elevation.
    float2 uv = i.pos.xy / size.xy;
    float3 d = normalize(float3(lerp(tan.x, tan.y, uv.x), lerp(tan.z, tan.w, uv.y), -1));
    float3 w = float3(dot(m0.xyz, d), dot(m1.xyz, d), dot(m2.xyz, d));
    float az = degrees(atan2(w.x, -w.z)), el = degrees(asin(clamp(w.y, -1, 1)));

    // The nearest cell's centre, and the pixel's place around it on the plane that touches the chart there.
    float cellAz = kCell * round(az / kCell), cellEl = kCell * round(el / kCell);
    float ca = radians(cellAz), ce = radians(cellEl);
    float3 centre = float3(sin(ca) * cos(ce), sin(ce), -cos(ca) * cos(ce));
    float3 right = float3(cos(ca), 0, sin(ca)), up = cross(right, centre);
    float depth = max(dot(w, centre), 1e-3);
    float2 p = degrees(float2(atan(dot(w, right) / depth), atan(dot(w, up) / depth)));

    // A pixel's footprint, taken before any branch (the derivatives need every pixel of a quad).
    float2 fp = fwidth(p), dpx = ddx(p), dpy = ddy(p);
    float fAz = fwidth(az), fEl = fwidth(el);

    if (abs(cellEl) > kMaxCellElevation) return float4(kBackground.xxx, 1);

    // The cell's border: the meridians and parallels halfway between cell centres.
    float cosEl = cos(radians(el));
    float border = max(Line((0.5 * kCell - abs(az - cellAz)) * cosEl, kBorderWidth, fAz * cosEl),
                       Line(0.5 * kCell - abs(el - cellEl), kBorderWidth, fEl));

    float value = lerp(CellPatterns(p, fp, dpx, dpy, cellAz, cellEl), kBorder, border);
    return float4(value.xxx, 1);
}
)";

    struct GridConstants {
        float chartFromEye[3][4];
        float tan[4];
        float size[4];
    };

    bool Compile(const char* entry, const char* target, ComPtr<ID3DBlob>* code, std::string* error) {
        ComPtr<ID3DBlob> errors;

        const HRESULT hr = D3DCompile(
            kShader,
            strlen(kShader),
            "game_grid",
            nullptr,
            nullptr,
            entry,
            target,
            D3DCOMPILE_OPTIMIZATION_LEVEL3,
            0,
            code->GetAddressOf(),
            &errors
        );

        if (FAILED(hr)) {
            *error = std::string("the grid's shader didn't compile: ")
                     + (errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no message");
            return false;
        }

        return true;
    }

}

bool GameGrid::Init(ID3D11Device* device, std::string* error) {
    ComPtr<ID3DBlob> vertexCode;
    ComPtr<ID3DBlob> pixelCode;

    if (!Compile("VS", "vs_5_0", &vertexCode, error) || !Compile("PS", "ps_5_0", &pixelCode, error)) {
        return false;
    }

    HRESULT hr = device->CreateVertexShader(
        vertexCode->GetBufferPointer(),
        vertexCode->GetBufferSize(),
        nullptr,
        &vertexShader_
    );

    if (SUCCEEDED(hr)) {
        hr = device->CreatePixelShader(
            pixelCode->GetBufferPointer(),
            pixelCode->GetBufferSize(),
            nullptr,
            &pixelShader_
        );
    }

    if (SUCCEEDED(hr)) {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(GridConstants);
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        hr = device->CreateBuffer(&desc, nullptr, &constants_);
    }

    if (FAILED(hr)) {
        *error = "the grid's shaders or constants couldn't be made";
        return false;
    }

    return true;
}

void GameGrid::Draw(
    ID3D11DeviceContext* context,
    ID3D11RenderTargetView* target,
    uint32_t width,
    uint32_t height,
    const GridView& view
) const {
    GridConstants constants = {};
    memcpy(constants.chartFromEye, view.chartFromEye, sizeof(constants.chartFromEye));
    constants.tan[0] = view.tanLeft;
    constants.tan[1] = view.tanRight;
    constants.tan[2] = view.tanUp;
    constants.tan[3] = view.tanDown;
    constants.size[0] = static_cast<float>(width);
    constants.size[1] = static_cast<float>(height);
    context->UpdateSubresource(constants_.Get(), 0, nullptr, &constants, 0, 0);

    const D3D11_VIEWPORT viewport = {0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
    context->OMSetRenderTargets(1, &target, nullptr);
    context->RSSetViewports(1, &viewport);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertexShader_.Get(), nullptr, 0);
    context->PSSetShader(pixelShader_.Get(), nullptr, 0);
    context->PSSetConstantBuffers(0, 1, constants_.GetAddressOf());
    context->Draw(3, 0);
}

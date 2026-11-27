// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "fake_openxr_recording.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

using Microsoft::WRL::ComPtr;

namespace {

    constexpr char kShader[] = R"(
cbuffer Params : register(b0) {
    float4 tan;  // the eye's field of view: left, right, up, down
    float4 cut;  // x, y: the tangents of half the frame's width and height
};
Texture2D<float> frameTex : register(t0);
SamplerState linearClamp : register(s0);
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut VS(uint id : SV_VertexID) {
    VSOut o; float2 uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); o.uv = uv; return o;
}
float4 PS(VSOut i) : SV_Target {
    float x = -tan.x + i.uv.x * (tan.x + tan.y), y = tan.z - i.uv.y * (tan.z + tan.w);
    float2 at = float2((x / cut.x + 1) * 0.5, (1 - y / cut.y) * 0.5);
    if (any(at < 0) || any(at > 1)) return float4(0, 0, 0, 1);
    // The recording's grey as the video stored it: written to an sRGB target, it is stored the same.
    float g = frameTex.SampleLevel(linearClamp, at, 0);
    float l = g <= 0.04045 ? g / 12.92 : pow((g + 0.055) / 1.055, 2.4);
    return float4(l, l, l, 1);
})";

    std::vector<std::string> Split(const std::string& line) {
        std::vector<std::string> parts;
        std::stringstream stream(line);
        std::string part;

        while (std::getline(stream, part, ',')) {
            parts.push_back(part);
        }

        return parts;
    }

}

// menu_cut.csv's "layout,<width>,<height>,<left>,<right>,<up>,<down>" (tangents) and "frames,<count>", the
// frames' times and cameras in menu.csv ("<time>,<qx>,<qy>,<qz>,<qw>" lines), their images in menu.gray.
bool RecordingPlayback::LoadCut(const std::string& folder) {
    std::ifstream index(folder + "\\menu_cut.csv"), poses(folder + "\\menu.csv");
    std::string line;
    size_t count = 0;

    while (std::getline(index, line)) {
        const std::vector<std::string> v = Split(line);

        if (v.size() >= 7 && v[0] == "layout") {
            cutWidth_ = std::stoi(v[1]);
            cutHeight_ = std::stoi(v[2]);
            tanW_ = std::stof(v[3]);
            tanH_ = std::stof(v[5]);
        } else if (v.size() >= 2 && v[0] == "frames") {
            count = std::stoul(v[1]);
        }
    }

    for (size_t k = 0; k < count; k++) {
        const std::vector<std::string> v = std::getline(poses, line) ? Split(line)
                                                                     : std::vector<std::string>();

        if (v.size() < 5) {
            return false;
        }

        frames_.push_back(
            {std::stod(v[0]), {std::stof(v[1]), std::stof(v[2]), std::stof(v[3]), std::stof(v[4])}}
        );
    }

    gray_ = folder + "\\menu.gray";
    const size_t bytes = static_cast<size_t>(cutWidth_) * cutHeight_;
    std::ifstream gray(gray_, std::ios::binary | std::ios::ate);
    return !frames_.empty() && bytes > 0 && gray && static_cast<size_t>(gray.tellg()) >= count * bytes;
}

bool RecordingPlayback::LoadPlan(const std::string& path) {
    std::ifstream file(path);
    std::string line;

    while (std::getline(file, line)) {
        const std::vector<std::string> v = Split(line);

        if (v.size() >= 4 && v[0] == "menu") {
            plan_.push_back({false, std::stod(v[1]), std::stod(v[2]), std::stod(v[3])});
        }
    }

    return !plan_.empty();
}

int RecordingPlayback::FrameAt(double moment) const {
    const auto after = std::upper_bound(frames_.begin(), frames_.end(), moment, [](double t, const Frame& f) {
        return t < f.time;
    });
    return after == frames_.begin() ? 0 : static_cast<int>(after - frames_.begin()) - 1;
}

bool RecordingPlayback::Init(const Settings& settings, ID3D11Device* device, int width, int height) {
    settings_ = settings;
    width_ = width, height_ = height;

    if (!LoadCut(settings.menuCut)) {
        fprintf(stderr, "fake_openxr: no menu cut in %s\n", settings.menuCut.c_str());
        return false;
    }

    if (!LoadPlan(settings.plan)) {
        fprintf(stderr, "fake_openxr: no plan in %s\n", settings.plan.c_str());
        return false;
    }

    if (!InitDrawing(device)) {
        fprintf(stderr, "fake_openxr: can't make what draws the recording\n");
        return false;
    }

    return true;
}

bool RecordingPlayback::InitDrawing(ID3D11Device* device) {
    ComPtr<ID3DBlob> vs, ps, err;

    if (FAILED(D3DCompile(
            kShader,
            sizeof(kShader) - 1,
            "recording",
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
            "recording",
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
    bd.ByteWidth = 32;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device->CreateBuffer(&bd, nullptr, &cbuffer_);

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    device->CreateSamplerState(&sd, &sampler_);

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = cutWidth_;
    td.Height = cutHeight_;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8_UNORM;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    if (FAILED(device->CreateTexture2D(&td, nullptr, &texture_))
        || FAILED(device->CreateShaderResourceView(texture_.Get(), nullptr, &view_))) {
        return false;
    }

    return vs_ && ps_ && cbuffer_ && sampler_;
}

void RecordingPlayback::Restart(double at, double pauseS) {
    const double script = at - start_;
    const double cutFrom = plan_.front().cutFrom;

    for (Stretch& s : plan_) {
        s.to = std::min(s.to, script);
    }

    plan_.push_back({true, script, script + pauseS, 0});
    plan_.push_back({false, script + pauseS, script + pauseS + (frames_.back().time - cutFrom), cutFrom});
}

bool RecordingPlayback::Paused(double target) const {
    const double script = target - start_;

    for (const Stretch& s : plan_) {
        if (s.pause && script >= s.from && script < s.to) {
            return true;
        }
    }

    return false;
}

Quat RecordingPlayback::Head(double target, const Quat& eyeTurn) {
    const double script = target - start_;

    for (const Stretch& s : plan_) {
        if (!s.pause && script >= s.from && script < s.to) {
            shownFrame_ = FrameAt(s.cutFrom + (script - s.from));
            break;
        }
    }

    return Normalize(Mul(frames_[shownFrame_].camera, Conj(eyeTurn)));
}

void RecordingPlayback::Draw(
    ID3D11DeviceContext* context,
    ID3D11RenderTargetView* target,
    float leftTan,
    float rightTan,
    float upTan,
    float downTan
) {
    if (loadedFrame_ != shownFrame_) {
        const size_t bytes = static_cast<size_t>(cutWidth_) * cutHeight_;
        image_.resize(bytes);
        std::ifstream gray(gray_, std::ios::binary);
        gray.seekg(static_cast<std::streamoff>(shownFrame_ * bytes));
        gray.read(reinterpret_cast<char*>(image_.data()), static_cast<std::streamsize>(bytes));
        context->UpdateSubresource(texture_.Get(), 0, nullptr, image_.data(), cutWidth_, 0);
        loadedFrame_ = shownFrame_;
    }

    const float params[8] = {leftTan, rightTan, upTan, downTan, tanW_, tanH_, 0, 0};
    D3D11_MAPPED_SUBRESOURCE mapped;
    context->Map(cbuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, params, sizeof(params));
    context->Unmap(cbuffer_.Get(), 0);

    D3D11_VIEWPORT vp = {0, 0, float(width_), float(height_), 0, 1};
    context->RSSetViewports(1, &vp);
    context->OMSetRenderTargets(1, &target, nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vs_.Get(), nullptr, 0);
    context->PSSetShader(ps_.Get(), nullptr, 0);
    context->PSSetConstantBuffers(0, 1, cbuffer_.GetAddressOf());
    ID3D11ShaderResourceView* view = view_.Get();
    context->PSSetShaderResources(0, 1, &view);
    context->PSSetSamplers(0, 1, sampler_.GetAddressOf());
    context->Draw(3, 0);
}

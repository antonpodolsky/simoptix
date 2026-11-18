// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lap_video.h"

#include <d3d10.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <mfapi.h>
#include <mferror.h>

#include <algorithm>
#include <cmath>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace {

    // A full-screen triangle that copies each pixel as it is (the image's bytes, in the target's channel
    // order), or, for a smaller video, averages four bilinear taps spread over the video pixel's area of the
    // image (each tap blends four image pixels, so up to 2x smaller every image pixel counts).
    constexpr char kShader[] = R"(
Texture2D image : register(t0);
SamplerState linearClamp : register(s0);

cbuffer Scale : register(b0) {
    float2 pixel;  // one video pixel, in the image's uv
    float2 unused;
};

float4 FullScreen(uint id : SV_VertexID) : SV_Position {
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

float4 CopyPixel(float4 position : SV_Position) : SV_Target {
    return image.Load(int3(position.xy, 0));
}

float4 ScalePixel(float4 position : SV_Position) : SV_Target {
    float2 uv = position.xy * pixel;
    float2 q = 0.25 * pixel;
    return 0.25 * (image.Sample(linearClamp, uv + float2(-q.x, -q.y)) + image.Sample(linearClamp, uv + float2(q.x, -q.y))
                   + image.Sample(linearClamp, uv + float2(-q.x, q.y)) + image.Sample(linearClamp, uv + float2(q.x, q.y)));
}
)";

    // About 0.1 bit per pixel: 48 Mbit/s at 2560x2098 and 90 fps, at most 150 Mbit/s.
    UINT32 BitRate(int width, int height, double period) {
        const double bits = 0.1 * width * height / period;
        return static_cast<UINT32>(std::min(bits, 150e6));
    }

    bool SetVideoType(IMFMediaType* type, const GUID& subtype, int width, int height, UINT32 fps) {
        return SUCCEEDED(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video))
               && SUCCEEDED(type->SetGUID(MF_MT_SUBTYPE, subtype))
               && SUCCEEDED(type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive))
               && SUCCEEDED(MFSetAttributeSize(
                   type,
                   MF_MT_FRAME_SIZE,
                   static_cast<UINT32>(width),
                   static_cast<UINT32>(height)
               ))
               && SUCCEEDED(MFSetAttributeRatio(type, MF_MT_FRAME_RATE, fps, 1))
               && SUCCEEDED(MFSetAttributeRatio(type, MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
    }

    ComPtr<IDXGIAdapter> FindAdapter(LUID luid) {
        ComPtr<IDXGIFactory1> factory;

        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
            return nullptr;
        }

        ComPtr<IDXGIAdapter1> adapter;

        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++) {
            DXGI_ADAPTER_DESC1 desc;

            if (SUCCEEDED(adapter->GetDesc1(&desc)) && memcmp(&desc.AdapterLuid, &luid, sizeof(LUID)) == 0) {
                return adapter;
            }
        }

        return nullptr;
    }

}

LapVideo::~LapVideo() {
    Abort();
    manager_.Reset();
    device_.Reset();
    if (started_) {
        MFShutdown();
    }
}

bool LapVideo::Init(LUID adapter, std::string* error) {
    if (device_ && manager_ && scale_) {
        return true;  // made for an earlier lap
    }

    if (!started_ && FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {
        *error = "Windows' video encoder (Media Foundation) could not start";
        return false;
    }

    started_ = true;
    const ComPtr<IDXGIAdapter> found = FindAdapter(adapter);

    const HRESULT hr = D3D11CreateDevice(
        found.Get(),
        found ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &device_,
        nullptr,
        &context_
    );

    if (FAILED(hr)) {
        *error = "could not create the lap recorder's device";
        return false;
    }

    // Media Foundation uses the device from its own threads.
    ComPtr<ID3D10Multithread> multithread;

    if (SUCCEEDED(device_.As(&multithread))) {
        multithread->SetMultithreadProtected(TRUE);
    }

    UINT token = 0;

    if (FAILED(MFCreateDXGIDeviceManager(&token, &manager_))
        || FAILED(manager_->ResetDevice(device_.Get(), token))) {
        *error = "could not hand the device to Windows' video encoder";
        return false;
    }

    if (!MakeShaders()) {
        *error = "could not make the lap recorder's shaders";
        return false;
    }

    return true;
}

ComPtr<ID3DBlob> LapVideo::Compile(const char* entry, const char* profile) {
    ComPtr<ID3DBlob> code;

    const HRESULT hr = D3DCompile(
        kShader,
        sizeof(kShader) - 1,
        "lap_video",
        nullptr,
        nullptr,
        entry,
        profile,
        0,
        0,
        &code,
        nullptr
    );

    return SUCCEEDED(hr) ? code : nullptr;
}

bool LapVideo::MakeShaders() {
    const ComPtr<ID3DBlob> vs = Compile("FullScreen", "vs_5_0");
    const ComPtr<ID3DBlob> copy = Compile("CopyPixel", "ps_5_0");
    const ComPtr<ID3DBlob> scale = Compile("ScalePixel", "ps_5_0");

    if (!vs || !copy || !scale) {
        return false;
    }

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = 16;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    return SUCCEEDED(device_->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vs_))
           && SUCCEEDED(
               device_->CreatePixelShader(copy->GetBufferPointer(), copy->GetBufferSize(), nullptr, &copy_)
           )
           && SUCCEEDED(
               device_->CreatePixelShader(scale->GetBufferPointer(), scale->GetBufferSize(), nullptr, &scale_)
           )
           && SUCCEEDED(device_->CreateSamplerState(&sd, &sampler_))
           && SUCCEEDED(device_->CreateBuffer(&bd, nullptr, &constants_));
}

bool LapVideo::Open(const wchar_t* path, int width, int height, double period, std::string* error) {
    Abort();
    width_ = width, height_ = height;
    duration_ = static_cast<LONGLONG>(std::llround(period * 1e7));
    const UINT32 fps = static_cast<UINT32>(std::lround(1 / period));

    ComPtr<IMFAttributes> attributes;
    ComPtr<IMFMediaType> encoded, input;
    ComPtr<IMFAttributes> poolAttributes;

    const bool made = SUCCEEDED(MFCreateAttributes(&attributes, 3)) && SUCCEEDED(MFCreateMediaType(&encoded))
                      && SUCCEEDED(MFCreateMediaType(&input))
                      && SUCCEEDED(MFCreateAttributes(&poolAttributes, 2));

    // A full pool says the encoder is behind, so the writer never makes this thread wait (throttling off).
    const bool described = made
                           && SUCCEEDED(attributes->SetUnknown(MF_SINK_WRITER_D3D_MANAGER, manager_.Get()))
                           && SUCCEEDED(attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE))
                           && SUCCEEDED(attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE))
                           && SetVideoType(encoded.Get(), MFVideoFormat_HEVC, width, height, fps)
                           && SUCCEEDED(encoded->SetUINT32(MF_MT_AVG_BITRATE, BitRate(width, height, period)))
                           && SetVideoType(input.Get(), MFVideoFormat_ARGB32, width, height, fps)
                           && SUCCEEDED(
                               input->SetUINT32(MF_MT_DEFAULT_STRIDE, static_cast<UINT32>(width) * 4)
                           )
                           && SUCCEEDED(poolAttributes->SetUINT32(
                               MF_SA_D3D11_BINDFLAGS,
                               D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE
                           ))
                           && SUCCEEDED(poolAttributes->SetUINT32(MF_SA_D3D11_USAGE, D3D11_USAGE_DEFAULT));

    if (!described) {
        *error = "out of memory for the video encoder's settings";
        return false;
    }

    if (FAILED(MFCreateSinkWriterFromURL(path, nullptr, attributes.Get(), &writer_))) {
        *error = "could not create the recorded lap's video file";
        return false;
    }

    if (FAILED(writer_->AddStream(encoded.Get(), &stream_))) {
        Abort();
        *error = "Windows has no HEVC encoder for this size on this graphics card";
        return false;
    }

    if (FAILED(writer_->SetInputMediaType(stream_, input.Get(), nullptr))
        || FAILED(writer_->BeginWriting())) {
        Abort();
        *error = "Windows' HEVC encoder doesn't take these images";
        return false;
    }

    const bool pooled = SUCCEEDED(MFCreateVideoSampleAllocatorEx(IID_PPV_ARGS(&pool_)))
                        && SUCCEEDED(pool_->SetDirectXManager(manager_.Get()))
                        && SUCCEEDED(pool_->InitializeSampleAllocatorEx(
                            kPool,
                            kPool,
                            poolAttributes.Get(),
                            input.Get()
                        ));

    if (!pooled) {
        Abort();
        *error = "not enough video memory for the encoder's images";
        return false;
    }

    return true;
}

bool LapVideo::Take(ID3D11Texture2D* image, ComPtr<IMFSample>* out) {
    ComPtr<IMFSample> sample;

    if (!pool_ || pool_->AllocateSample(&sample) != S_OK) {
        return false;  // MF_E_SAMPLEALLOCATOR_EMPTY: every image is held or still in the encoder
    }

    ComPtr<IMFMediaBuffer> buffer;
    ComPtr<IMFDXGIBuffer> dxgi;
    ComPtr<ID3D11Texture2D> target;
    UINT subresource = 0;

    const bool found = SUCCEEDED(sample->GetBufferByIndex(0, &buffer)) && SUCCEEDED(buffer.As(&dxgi))
                       && SUCCEEDED(dxgi->GetResource(IID_PPV_ARGS(&target)))
                       && SUCCEEDED(dxgi->GetSubresourceIndex(&subresource));

    if (!found || !Draw(image, target.Get(), subresource)) {
        return false;
    }

    // Without its length the encoder refuses the sample (E_INVALIDARG): the buffer's whole size is the image.
    DWORD length = 0;

    if (FAILED(buffer->GetMaxLength(&length)) || FAILED(buffer->SetCurrentLength(length))) {
        return false;
    }

    *out = std::move(sample);
    return true;
}

ID3D11ShaderResourceView* LapVideo::ImageView(ID3D11Texture2D* image) {
    for (const ImageViewOf& v : imageViews_) {
        if (v.texture == image) {
            return v.view.Get();
        }
    }

    ImageViewOf made;
    made.texture = image;

    if (FAILED(device_->CreateShaderResourceView(image, nullptr, &made.view))) {
        return nullptr;
    }

    imageViews_.push_back(std::move(made));
    return imageViews_.back().view.Get();
}

ID3D11RenderTargetView* LapVideo::TargetView(ID3D11Texture2D* target, UINT subresource) {
    for (const TargetViewOf& v : targetViews_) {
        if (v.texture == target && v.subresource == subresource) {
            return v.view.Get();
        }
    }

    D3D11_RENDER_TARGET_VIEW_DESC td = {};
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

    D3D11_TEXTURE2D_DESC targetDesc;
    target->GetDesc(&targetDesc);

    if (targetDesc.ArraySize > 1) {
        td.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
        td.Texture2DArray.FirstArraySlice = subresource;
        td.Texture2DArray.ArraySize = 1;
    }

    TargetViewOf made;
    made.texture = target;
    made.subresource = subresource;

    if (FAILED(device_->CreateRenderTargetView(target, &td, &made.view))) {
        return nullptr;
    }

    targetViews_.push_back(std::move(made));
    return targetViews_.back().view.Get();
}

bool LapVideo::Draw(ID3D11Texture2D* image, ID3D11Texture2D* target, UINT subresource) {
    ID3D11ShaderResourceView* view = ImageView(image);
    ID3D11RenderTargetView* rtv = TargetView(target, subresource);

    if (!view || !rtv) {
        return false;
    }

    // The image as it is when it is the video's size, else scaled to it.
    D3D11_TEXTURE2D_DESC imageDesc;
    image->GetDesc(&imageDesc);
    const bool asIs = imageDesc.Width == static_cast<UINT>(width_)
                      && imageDesc.Height == static_cast<UINT>(height_);

    if (!asIs) {
        D3D11_MAPPED_SUBRESOURCE m;

        if (FAILED(context_->Map(constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            return false;
        }

        const float pixel[4] = {1.0f / static_cast<float>(width_), 1.0f / static_cast<float>(height_), 0, 0};
        memcpy(m.pData, pixel, sizeof(pixel));
        context_->Unmap(constants_.Get(), 0);
    }

    const D3D11_VIEWPORT viewport = {0, 0, static_cast<float>(width_), static_cast<float>(height_), 0, 1};

    context_->OMSetRenderTargets(1, &rtv, nullptr);
    context_->RSSetViewports(1, &viewport);
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(vs_.Get(), nullptr, 0);
    context_->PSSetShader(asIs ? copy_.Get() : scale_.Get(), nullptr, 0);
    context_->PSSetShaderResources(0, 1, &view);
    context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
    context_->PSSetConstantBuffers(0, 1, constants_.GetAddressOf());
    context_->Draw(3, 0);

    ID3D11ShaderResourceView* none = nullptr;
    context_->PSSetShaderResources(0, 1, &none);
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    context_->Flush();  // on this device of its own: the copy starts now, beside the game's work
    return true;
}

bool LapVideo::Write(IMFSample* sample, double t, std::string* error) {
    const bool timed = SUCCEEDED(sample->SetSampleTime(static_cast<LONGLONG>(std::llround(t * 1e7))))
                       && SUCCEEDED(sample->SetSampleDuration(duration_));

    if (!timed || FAILED(writer_->WriteSample(stream_, sample))) {
        *error = "the video encoder refused an image";
        return false;
    }

    return true;
}

bool LapVideo::Finish(std::string* error) {
    const HRESULT hr = writer_ ? writer_->Finalize() : E_FAIL;
    writer_.Reset();
    pool_.Reset();
    ForgetViews();

    if (FAILED(hr)) {
        *error = "the recorded lap's video could not be completed";
        return false;
    }

    return true;
}

void LapVideo::Abort() {
    writer_.Reset();  // unfinished: the file stays incomplete, and the caller deletes it
    pool_.Reset();
    ForgetViews();
}

// The views hold their textures: let go of the ring's and the pool's when a lap is done.
void LapVideo::ForgetViews() {
    imageViews_.clear();
    targetViews_.clear();
}

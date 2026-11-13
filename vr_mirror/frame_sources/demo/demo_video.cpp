// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "vr_mirror/frame_sources/demo/demo_video.h"

#include <d3d10.h>
#include <dxgi.h>
#include <mfapi.h>
#include <mferror.h>
#include <propvarutil.h>

using Microsoft::WRL::ComPtr;

bool DemoVideo::Open(const std::wstring& path, int width, int height, std::string* error) {
    Close();

    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {
        *error = "Windows' video decoder (Media Foundation) could not start";
        return false;
    }

    started_ = true;

    const HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
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
        *error = "could not create the demo's device";
        return false;
    }

    // Media Foundation uses the device from its own threads.
    ComPtr<ID3D10Multithread> multithread;

    if (SUCCEEDED(device_.As(&multithread))) {
        multithread->SetMultithreadProtected(TRUE);
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC desc = {};

    if (SUCCEEDED(device_.As(&dxgiDevice)) && SUCCEEDED(dxgiDevice->GetAdapter(&adapter))
        && SUCCEEDED(adapter->GetDesc(&desc))) {
        adapter_ = desc.AdapterLuid;
    }

    UINT token = 0;
    ComPtr<IMFAttributes> attributes;
    ComPtr<IMFMediaType> wanted;

    const bool made = SUCCEEDED(MFCreateDXGIDeviceManager(&token, &manager_))
                      && SUCCEEDED(manager_->ResetDevice(device_.Get(), token))
                      && SUCCEEDED(MFCreateAttributes(&attributes, 3))
                      && SUCCEEDED(MFCreateMediaType(&wanted));

    if (!made) {
        *error = "could not hand the device to Windows' video decoder";
        return false;
    }

    attributes->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, manager_.Get());
    attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    attributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);  // to BGRA on the GPU

    if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), attributes.Get(), &reader_))) {
        *error = "could not open the recorded lap's video";
        return false;
    }

    wanted->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    wanted->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_ARGB32);

    if (FAILED(reader_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, wanted.Get()))) {
        *error = "Windows can't decode the recorded lap's HEVC video (is the HEVC Video Extension "
                 "installed?)";
        return false;
    }

    if (!MakeTextures(width, height)) {
        *error = "not enough video memory for the demo's images";
        return false;
    }

    return true;
}

bool DemoVideo::MakeTextures(int width, int height) {
    width_ = width, height_ = height;
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = static_cast<UINT>(width);
    td.Height = static_cast<UINT>(height);
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    td.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    const D3D11_QUERY_DESC qd = {D3D11_QUERY_EVENT, 0};

    for (int i = 0; i < kTextures; i++) {
        ComPtr<IDXGIResource> resource;

        const bool made = SUCCEEDED(device_->CreateTexture2D(&td, nullptr, &textures_[i]))
                          && SUCCEEDED(device_->CreateQuery(&qd, &done_[i]))
                          && SUCCEEDED(textures_[i].As(&resource))
                          && SUCCEEDED(resource->GetSharedHandle(&shared_[i]));

        if (!made) {
            return false;
        }
    }

    return true;
}

void DemoVideo::Close() {
    reader_.Reset();

    for (int i = 0; i < kTextures; i++) {
        textures_[i].Reset();
        done_[i].Reset();
        shared_[i] = nullptr;
    }

    manager_.Reset();
    context_.Reset();
    device_.Reset();

    if (started_) {
        MFShutdown();
        started_ = false;
    }
}

bool DemoVideo::DecodeNext(int texture, bool* end) {
    *end = false;

    for (;;) {
        DWORD flags = 0;
        LONGLONG time = 0;
        ComPtr<IMFSample> sample;

        if (FAILED(
                reader_->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &time, &sample)
            )) {
            return false;
        }

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            *end = true;
            return false;
        }

        if (!sample) {
            continue;  // a format change or a gap: the next read has the image
        }

        ComPtr<IMFMediaBuffer> buffer;
        ComPtr<IMFDXGIBuffer> dxgi;
        ComPtr<ID3D11Texture2D> decoded;
        UINT subresource = 0;

        const bool found = SUCCEEDED(sample->GetBufferByIndex(0, &buffer)) && SUCCEEDED(buffer.As(&dxgi))
                           && SUCCEEDED(dxgi->GetResource(IID_PPV_ARGS(&decoded)))
                           && SUCCEEDED(dxgi->GetSubresourceIndex(&subresource));

        if (!found) {
            return false;
        }

        const D3D11_BOX box = {0, 0, 0, static_cast<UINT>(width_), static_cast<UINT>(height_), 1};
        context_->CopySubresourceRegion(
            textures_[texture].Get(),
            0,
            0,
            0,
            0,
            decoded.Get(),
            subresource,
            &box
        );
        context_->End(done_[texture].Get());
        context_->Flush();  // on this device of its own: the copy runs now
        return true;
    }
}

bool DemoVideo::Rewind() {
    PROPVARIANT start;
    InitPropVariantFromInt64(0, &start);
    const HRESULT hr = reader_->SetCurrentPosition(GUID_NULL, start);
    PropVariantClear(&start);
    return SUCCEEDED(hr);
}

bool DemoVideo::Done(int texture) {
    return context_->GetData(done_[texture].Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
}

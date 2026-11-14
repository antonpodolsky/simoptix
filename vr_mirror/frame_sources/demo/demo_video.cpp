// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "vr_mirror/frame_sources/demo/demo_video.h"

#include <d3d10.h>
#include <dxgi.h>
#include <mfapi.h>
#include <mferror.h>
#include <propvarutil.h>

using Microsoft::WRL::ComPtr;

namespace {

    // Media Foundation for as long as an object of this lives.
    struct MediaFoundation {
        bool started = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
        ~MediaFoundation() {
            if (started) {
                MFShutdown();
            }
        }
    };

    bool ManagerFor(ID3D11Device* device, ComPtr<IMFDXGIDeviceManager>* manager) {
        // Media Foundation uses the device from its own threads.
        ComPtr<ID3D10Multithread> multithread;

        if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&multithread)))) {
            multithread->SetMultithreadProtected(TRUE);
        }

        UINT token = 0;
        return SUCCEEDED(MFCreateDXGIDeviceManager(&token, manager->ReleaseAndGetAddressOf()))
               && SUCCEEDED((*manager)->ResetDevice(device, token));
    }

}

bool DemoVideo::OpenReader(
    const std::wstring& path,
    IMFDXGIDeviceManager* manager,
    ComPtr<IMFSourceReader>* reader,
    std::string* error
) {
    ComPtr<IMFAttributes> attributes;
    ComPtr<IMFMediaType> wanted;

    const bool made = SUCCEEDED(MFCreateAttributes(&attributes, 3)) && SUCCEEDED(MFCreateMediaType(&wanted))
                      && SUCCEEDED(attributes->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, manager))
                      && SUCCEEDED(attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE))
                      // to BGRA on the GPU
                      && SUCCEEDED(
                          attributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE)
                      )
                      && SUCCEEDED(wanted->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video))
                      && SUCCEEDED(wanted->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_ARGB32));

    if (!made) {
        *error = "out of memory for the video decoder's settings";
        return false;
    }

    if (FAILED(
            MFCreateSourceReaderFromURL(path.c_str(), attributes.Get(), reader->ReleaseAndGetAddressOf())
        )) {
        *error = "could not open the recorded lap's video";
        return false;
    }

    if (FAILED((*reader)->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, wanted.Get()))) {
        reader->Reset();
        *error = "Windows can't decode the recorded lap's HEVC video (is the HEVC Video Extension "
                 "installed?)";
        return false;
    }

    return true;
}

bool DemoVideo::Check(const std::wstring& path, std::string* error) {
    const MediaFoundation mf;

    if (!mf.started) {
        *error = "Windows' video decoder (Media Foundation) could not start";
        return false;
    }

    ComPtr<ID3D11Device> device;

    const HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &device,
        nullptr,
        nullptr
    );

    ComPtr<IMFDXGIDeviceManager> manager;

    if (FAILED(hr) || !ManagerFor(device.Get(), &manager)) {
        *error = "could not make a device for Windows' video decoder";
        return false;
    }

    ComPtr<IMFSourceReader> reader;
    return OpenReader(path, manager.Get(), &reader, error);
}

LUID DemoVideo::DefaultAdapter() {
    ComPtr<IDXGIFactory1> factory;
    ComPtr<IDXGIAdapter1> adapter;
    DXGI_ADAPTER_DESC1 desc = {};

    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))
        && SUCCEEDED(factory->EnumAdapters1(0, &adapter))) {
        adapter->GetDesc1(&desc);
    }

    return desc.AdapterLuid;
}

bool DemoVideo::Open(
    const std::wstring& path,
    int width,
    int height,
    ID3D11Device* device,
    std::string* error
) {
    Close();

    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {
        *error = "Windows' video decoder (Media Foundation) could not start";
        return false;
    }

    started_ = true;

    if (!ManagerFor(device, &manager_)) {
        *error = "could not hand the device to Windows' video decoder";
        return false;
    }

    if (!OpenReader(path, manager_.Get(), &reader_, error)) {
        return false;
    }

    device_ = device;
    width_ = width, height_ = height;
    return true;
}

void DemoVideo::Close() {
    reader_.Reset();
    manager_.Reset();
    device_.Reset();

    if (started_) {
        MFShutdown();
        started_ = false;
    }
}

bool DemoVideo::Next(ComPtr<IMFSample>* sample, bool* end) {
    *end = false;

    for (;;) {
        DWORD flags = 0;
        LONGLONG time = 0;
        ComPtr<IMFSample> s;

        if (FAILED(reader_->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &time, &s))) {
            return false;
        }

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            *end = true;
            return false;
        }

        if (s) {
            *sample = std::move(s);
            return true;
        }
        // No sample (a format change or a gap): the next read has the image.
    }
}

bool DemoVideo::CopyInto(IMFSample* sample, ID3D11DeviceContext* context, ID3D11Texture2D* target) const {
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
    context->CopySubresourceRegion(target, 0, 0, 0, 0, decoded.Get(), subresource, &box);
    return true;
}

bool DemoVideo::Rewind() {
    PROPVARIANT start;
    InitPropVariantFromInt64(0, &start);
    const HRESULT hr = reader_->SetCurrentPosition(GUID_NULL, start);
    PropVariantClear(&start);
    return SUCCEEDED(hr);
}

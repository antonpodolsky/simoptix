// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "adapters/d3d/frame_copy.h"

#include <dxgi.h>

#include "shared/log.h"

namespace {

    // The channel order's family: the typeless format, and the UNORM one that reads the bytes as they are.
    void Family(DXGI_FORMAT f, DXGI_FORMAT* typeless, DXGI_FORMAT* unorm) {
        switch (f) {
            case DXGI_FORMAT_R8G8B8A8_TYPELESS:
            case DXGI_FORMAT_R8G8B8A8_UNORM:
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
                *typeless = DXGI_FORMAT_R8G8B8A8_TYPELESS, *unorm = DXGI_FORMAT_R8G8B8A8_UNORM;
                return;
            case DXGI_FORMAT_B8G8R8A8_TYPELESS:
            case DXGI_FORMAT_B8G8R8A8_UNORM:
            case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
                *typeless = DXGI_FORMAT_B8G8R8A8_TYPELESS, *unorm = DXGI_FORMAT_B8G8R8A8_UNORM;
                return;
            default:
                *typeless = *unorm = f;
        }
    }

}

bool FrameCopy::Fit(ID3D11Device* device, const FrameCopyShape& shape) {
    const bool same = shape.width == shape_.width && shape.height == shape_.height
                      && shape.format == shape_.format && shape.shared == shape_.shared
                      && shape.drawn == shape_.drawn;

    if (copy_ && same) {
        return false;
    }

    Reset();

    DXGI_FORMAT typeless, unorm;
    Family(shape.format, &typeless, &unorm);

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = shape.width;
    td.Height = shape.height;
    td.MipLevels = td.ArraySize = 1;
    td.Format = shape.shared ? unorm : typeless;  // another device opens a shared one with its own typed view
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | (shape.drawn ? D3D11_BIND_RENDER_TARGET : 0);
    td.MiscFlags = shape.shared ? D3D11_RESOURCE_MISC_SHARED : 0;

    if (FAILED(device->CreateTexture2D(&td, nullptr, &copy_))) {
        CoreLog("picture copy %ux%u (format %d) not made", shape.width, shape.height, int(shape.format));
        return true;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = unorm;
    srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(copy_.Get(), &srv, &view_);
    if (shape.drawn) {
        D3D11_RENDER_TARGET_VIEW_DESC rtv = {};
        rtv.Format = unorm;
        rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(copy_.Get(), &rtv, &target_);
    }

    if (shape.shared) {
        Microsoft::WRL::ComPtr<IDXGIResource> res;
        if (SUCCEEDED(copy_.As(&res))) {
            res->GetSharedHandle(&shared_);
        }
    }

    shape_ = shape;
    return true;
}

bool FrameCopy::Update(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    ID3D11Texture2D* src,
    const D3D11_BOX* region
) {
    D3D11_TEXTURE2D_DESC sd;
    src->GetDesc(&sd);
    if (region) {
        sd.Width = region->right - region->left;
        sd.Height = region->bottom - region->top;
    }

    FrameCopyShape shape;
    shape.width = sd.Width, shape.height = sd.Height, shape.format = sd.Format;
    const bool remade = Fit(device, shape);
    if (remade) {
        CoreLog("mirror: %ux%u format %d", sd.Width, sd.Height, sd.Format);
    }
    if (!copy_) {
        return remade;
    }
    if (region) {
        context->CopySubresourceRegion(copy_.Get(), 0, 0, 0, 0, src, 0, region);
    } else {
        context->CopyResource(copy_.Get(), src);
    }

    return remade;
}

void FrameCopy::Reset() {
    copy_.Reset();
    view_.Reset();
    target_.Reset();
    shared_ = nullptr;
    shape_ = {};
}

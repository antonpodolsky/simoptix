// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// FrameCopy: a host's own copy of a game frame's picture, on the host's device: the texture, a view
// that reads its bytes as they are (a UNORM view: an _SRGB one would decode them; the warp knows the
// picture's encoding, EyeInfo::encodedTwice), and, for a frame source that draws its picture in (a
// scaled one), a render target. A shared copy (another device opens it: the OBS plugin's store) has a
// legacy shared handle and a plain UNORM format; an unshared one is typeless with a UNORM view.
// The frame source fills it (FrameSource::CopyPicture); this only makes and keeps the texture.
#pragma once

#include <d3d11.h>
#include <wrl/client.h>

struct FrameCopyShape {
    UINT width = 0, height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;  // the picture's channel order
    bool shared = false;                       // another device opens it
    bool drawn = false;                        // with a render target
};

class FrameCopy {
public:
    // Makes the texture of this shape, unless it already is one. True if it was (re)made, so callers
    // can reset size-dependent state (and a shared copy's handle changed).
    bool Fit(ID3D11Device* device, const FrameCopyShape& shape);
    // Copies `src`, or only `region` of it, after fitting itself to it (unshared, not drawn).
    bool Update(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        ID3D11Texture2D* src,
        const D3D11_BOX* region = nullptr
    );

    void Reset();

    ID3D11Texture2D* Texture() const { return copy_.Get(); }
    ID3D11ShaderResourceView* View() const { return view_.Get(); }
    ID3D11RenderTargetView* Target() const { return target_.Get(); }
    HANDLE SharedHandle() const { return shared_; }
    UINT Width() const { return shape_.width; }
    UINT Height() const { return shape_.height; }

private:
    Microsoft::WRL::ComPtr<ID3D11Texture2D> copy_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target_;
    HANDLE shared_ = nullptr;
    FrameCopyShape shape_;  // as asked for (Fit)
};

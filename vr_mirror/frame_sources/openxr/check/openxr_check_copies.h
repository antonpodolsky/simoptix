// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The check program's reader: what VR Mirror would see of the layer, checked against what the check
// program submitted. On a D3D11 device of its own (as a reader in another process has), it signs as a
// reader, reads every frame record the layer publishes and every finished copy of the left eye's
// image, and compares them with the frames it was told about: the display time and poses exactly, and
// the image by the frame number the check program cleared it to (CheckColour). It also compares the
// layer's clock reading with the runtime's own conversion.
//
// It waits on the GPU when it reads an image back: it is the check program, not the game.
// It doesn't know how the check program drives OpenXR.
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

#include "openxr_d3d11.h"
#include "openxr_share.h"
#include "shared/unique_handle.h"

// The linear colour the check program clears the left eye to in frame `index`: 4 bits of the index in
// each of red, green and blue (16 levels, so the sRGB encoding of the swapchain can't blur them).
void CheckColour(int index, float rgba[4]);

class OpenXrCheckCopies {
public:
    // A device of its own on the headset's adapter. `images`: the left eye's swapchain holds
    // R8G8B8A8 (sRGB or not, `srgb`), so images can be read back and compared.
    bool Start(LUID adapter, bool images, bool srgb);
    // The runtime's own conversion, to compare the layer's clock reading with (nullptr: not compared).
    void SetClock(XrInstance instance, PFN_XrCounterToTime counterToTime) {
        instance_ = instance;
        counterToTime_ = counterToTime;
    }

    // A frame the check program submitted (in order): its index (the colour), display time and views.
    void Submitted(int index, XrTime displayTime, const XrPosef pose[2], const XrFovf fov[2]);
    // Signs, and reads what is new; call after every frame.
    void Poll();
    // Prints the results; returns the number of failures.
    int Report() const;

private:
    struct Sent {
        int index;
        XrTime displayTime;
        XrPosef pose[2];
        XrFovf fov[2];
    };

    struct Waiting {
        uint64_t frame;
        int texture;
        int index;
    };

    void Open();
    void CheckClock();
    void OpenTextures();
    void ReadRecords();
    void ReadWaiting();
    void ReadImage(const Waiting& w);

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> textures_[openxr_share::kTextures], staging_;
    uint32_t generation_ = 0;
    bool images_ = false, srgb_ = false;
    UniqueHandle mapping_;
    MappedView<openxr_share::Header> header_;
    XrInstance instance_ = XR_NULL_HANDLE;
    PFN_XrCounterToTime counterToTime_ = nullptr;
    std::vector<Sent> sent_;
    std::vector<Waiting> waiting_;
    uint64_t nextFrame_ = 1;

    int records_ = 0, recordsWrong_ = 0, recordsLost_ = 0, withImage_ = 0;
    int imagesRight_ = 0, imagesWrong_ = 0, imagesMissed_ = 0;
    int clockReadings_ = 0;
    int64_t clockWorstNs_ = 0;
    uint64_t firstCopied_ = 0;  // the first frame with an image
};

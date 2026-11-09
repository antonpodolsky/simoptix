// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "adapters/d3d/drawn_pose_feed.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>

#include "shared/log.h"

namespace {

    // Performance-counter seconds, for the footprint stats.
    double Seconds() {
        static const double perSecond = [] {
            LARGE_INTEGER f;
            QueryPerformanceFrequency(&f);
            return double(f.QuadPart);
        }();

        LARGE_INTEGER c;
        QueryPerformanceCounter(&c);
        return double(c.QuadPart) / perSecond;
    }

}

void DrawnPoseFeed::SetDrawnPose(std::shared_ptr<DrawnPose> drawnPose) {
    drawnPose_ = std::move(drawnPose);
    needs_ = drawnPose_ ? drawnPose_->Needs() : PictureNeeds();
    if (!device_) {
        return;  // before Init: Init sets it up
    }
    InitPictures();
    Reset();
}

bool DrawnPoseFeed::Init(ID3D11Device* device) {
    // Another device (the OBS plugin makes a new one each time its source is shown): the thumbnails
    // and patches are made afresh on it. The old device's shaders, targets, staging textures and
    // queries can't be used with the new one's context; kept, they left stage 2 without a frame's
    // picture until 1-2 s after it arrived, for the rest of a session after a hide and show.
    if (device != device_.Get()) {
        Release();
    }
    device_ = device;
    Reset();
    return InitPictures();
}

void DrawnPoseFeed::Release() {
    thumbs_ = Thumbnailer();
    patches_ = Thumbnailer();
    pendingHead_ = pendingCount_ = 0;
    device_.Reset();
}

bool DrawnPoseFeed::InitPictures() {
    if (needs_.thumbnail && !thumbs_.Ready()) {
        if (!thumbs_.Init(device_.Get(), true)) {
            CoreLog("drawn pose: no thumbnails, frames keep their arrival slot's pose");
            return false;
        }

        // read into without reallocating
        cur_.reserve(size_t(Thumbnailer::kWidth) * Thumbnailer::kHeight * 3);
    }

    const PatchLayout& l = needs_.patches;
    areas_.assign(size_t(std::max(1, l.count)) * 4, 0.0f);  // written into per frame without reallocating
    if (l.count <= 0) {
        return true;
    }
    const int w = l.columns * l.size, h = l.rows * l.size;
    if (patches_.Ready() && patches_.Width() == w && patches_.Height() == h) {
        return true;
    }
    if (!patches_.Init(device_.Get(), false, w, h, l.samples)) {
        CoreLog("drawn pose: no patches, frames keep the slot check's pose");
        return false;
    }

    patchesRead_.reserve(size_t(w) * h);  // read into without reallocating
    return true;
}

void DrawnPoseFeed::Reset() {
    pendingHead_ = pendingCount_ = 0;
    lastSubmitted_ = 0;
    if (drawnPose_) {
        drawnPose_->Reset();
    }
}

bool DrawnPoseFeed::Measuring(uint64_t serial) const {
    for (int k = 0; k < pendingCount_; k++) {
        const Pending& p = pending_[(pendingHead_ + k) % kPending];
        if (p.frame.serial == serial) {
            return p.patchTicket >= 0;
        }
    }

    return false;
}

void DrawnPoseFeed::ReportLookTime(const CallStats& time, StatsReport& r) {
    if (time.count == 0) {
        return;
    }
    r.Line("looking at pictures with patches");
    r.Value("frames", double(time.count));
    r.Value("us avg", time.total / time.count * 1e6);
    r.Value("us max", time.longest * 1e6);
}

void DrawnPoseFeed::Submit(
    ID3D11DeviceContext* context,
    ID3D11ShaderResourceView* image,
    const float eyeUv[4],
    const GameFrame& f,
    const PoseLookup& predictions,
    const EyeInfo& eye,
    double period
) {
    const bool decode = eye.encodedTwice;  // looked at as displayed: one encoding
    if (!drawnPose_ || !needs_.thumbnail || !thumbs_.Ready() || !f.synced || f.slotTime <= 0
        || f.serial == lastSubmitted_) {
        return;
    }

    lastSubmitted_ = f.serial;

    // The patches' areas, only for the frames the drawn pose wants them for.
    const PatchLayout& l = needs_.patches;

    // each patch's area of the eye, then in the image
    float (*areas)[4] = reinterpret_cast<float (*)[4]>(areas_.data());
    const int n = std::min(drawnPose_->Arrived(f, predictions, eye, period, areas), l.count);
    const bool makePatches = n > 0 && patches_.Ready();
    const float* area = kThumbArea;
    const float du = eyeUv[2] - eyeUv[0], dv = eyeUv[3] - eyeUv[1];
    const float uv[4] = {
        eyeUv[0] + area[0] * du,
        eyeUv[1] + area[1] * dv,
        eyeUv[0] + area[2] * du,
        eyeUv[1] + area[3] * dv,
    };

    if (pendingCount_ == kPending) {  // the GPU is far behind: drop the oldest
        pendingHead_ = (pendingHead_ + 1) % kPending;
        pendingCount_--;
    }

    Pending& p = pending_[(pendingHead_ + pendingCount_) % kPending];
    p.ticket = thumbs_.Make(context, image, uv, decode);
    p.patchTicket = -1;
    if (makePatches) {
        for (int i = 0; i < n; i++) {
            float* a = areas[i];
            a[0] = eyeUv[0] + a[0] * du, a[1] = eyeUv[1] + a[1] * dv;
            a[2] = eyeUv[0] + a[2] * du, a[3] = eyeUv[1] + a[3] * dv;
        }

        p.patchTicket = patches_.MakeTiles(context, image, areas, n, l.columns, l.size, decode);
    }

    p.frame = f;
    pendingCount_++;
}

void DrawnPoseFeed::Poll(
    ID3D11DeviceContext* context,
    const PoseLookup& predictions,
    const EyeInfo& eye,
    double period
) {
    while (pendingCount_ > 0) {
        Pending& p = pending_[pendingHead_];
        if (p.ticket < 0) {  // never made
            pendingHead_ = (pendingHead_ + 1) % kPending;
            pendingCount_--;
            continue;
        }

        // Not finished yet: frames are read in order. (Its patches were drawn after its thumbnail.)
        const bool patches = p.patchTicket >= 0;
        if (patches && !patches_.Read(context, p.patchTicket, &patchesRead_)) {
            return;
        }
        if (!thumbs_.Read(context, p.ticket, &cur_)) {
            return;
        }
        const GameFrame f = p.frame;
        pendingHead_ = (pendingHead_ + 1) % kPending;
        pendingCount_--;
        const double start = Seconds();
        drawnPose_->Look(f, cur_, patches ? patchesRead_ : none_, predictions, eye, period);
        if (patches) {
            lookTime_.Add(Seconds() - start);
        }
    }
}

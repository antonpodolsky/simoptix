// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "openxr_check_copies.h"

#include <dxgi.h>

#include <cmath>
#include <cstdio>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace {

    int64_t Counter() {
        LARGE_INTEGER c;
        QueryPerformanceCounter(&c);
        return c.QuadPart;
    }

    float Linear(uint8_t byte, bool srgb) {
        const float c = byte / 255.0f;
        if (!srgb) {
            return c;
        }
        return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }

    bool SamePose(const XrPosef& a, const openxr_share::Pose& b) {
        return a.orientation.x == b.qx && a.orientation.y == b.qy && a.orientation.z == b.qz
               && a.orientation.w == b.qw && a.position.x == b.px && a.position.y == b.py
               && a.position.z == b.pz;
    }

    bool SameFov(const XrFovf& a, const openxr_share::Fov& b) {
        return a.angleLeft == b.left && a.angleRight == b.right && a.angleUp == b.up && a.angleDown == b.down;
    }

}

void CheckColour(int index, float rgba[4]) {
    rgba[0] = float(index & 15) / 15;
    rgba[1] = float((index >> 4) & 15) / 15;
    rgba[2] = float((index >> 8) & 15) / 15;
    rgba[3] = 1;
}

bool OpenXrCheckCopies::Start(LUID adapter, bool pictures, bool srgb) {
    pictures_ = pictures;
    srgb_ = srgb;

    ComPtr<IDXGIFactory1> factory;
    ComPtr<IDXGIAdapter1> a, found;

    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        for (UINT i = 0; factory->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; i++) {
            DXGI_ADAPTER_DESC1 d;
            a->GetDesc1(&d);
            if (memcmp(&d.AdapterLuid, &adapter, sizeof(LUID)) == 0) {
                found = a;
                break;
            }
        }
    }

    return SUCCEEDED(D3D11CreateDevice(
        found.Get(),
        found ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &device_,
        nullptr,
        &context_
    ));
}

void OpenXrCheckCopies::Submitted(int index, XrTime displayTime, const XrPosef pose[2], const XrFovf fov[2]) {
    sent_.push_back({index, displayTime, {pose[0], pose[1]}, {fov[0], fov[1]}});
}

void OpenXrCheckCopies::Open() {
    mapping_.Reset(OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, openxr_share::kMappingName));
    if (!mapping_) {
        return;
    }
    header_.Reset(
        static_cast<openxr_share::Header*>(MapViewOfFile(mapping_.Get(), FILE_MAP_ALL_ACCESS, 0, 0, 0))
    );

    if (header_
        && (header_->magic != openxr_share::kMagic || header_->version != openxr_share::kVersion
            || header_->size != sizeof(openxr_share::Header))) {
        printf("  the layer's shared memory has another layout (version %u)\n", header_->version);
        header_.Reset();
    }

    if (!header_) {
        mapping_.Reset();
        return;
    }

    printf("  reading the layer's frames (session %u)\n", header_->session.load());
}

void OpenXrCheckCopies::Poll() {
    if (!header_) {
        Open();
    }
    if (!header_ || !device_) {
        return;
    }
    header_->readerSigned.store(Counter());

    CheckClock();
    OpenTextures();
    ReadRecords();
    ReadWaiting();
}

void OpenXrCheckCopies::CheckClock() {
    openxr_share::Header& h = *header_;
    int64_t counter = 0, time = 0;
    if (openxr_share::ReadClock(h, &counter, &time) && counterToTime_) {
        LARGE_INTEGER c;
        c.QuadPart = counter;
        XrTime own = 0;
        if (XR_SUCCEEDED(counterToTime_(instance_, &c, &own))) {
            clockReadings_++;
            clockWorstNs_ = (std::max)(clockWorstNs_, std::llabs(own - time));
        }
    }
}

void OpenXrCheckCopies::OpenTextures() {
    openxr_share::Header& h = *header_;
    const uint32_t generation = h.textureGeneration.load();
    if (generation != generation_) {
        generation_ = generation;
        for (auto& t : textures_) {
            t.Reset();
        }
        staging_.Reset();
        if (generation) {
            for (int k = 0; k < openxr_share::kTextures; k++) {
                device_->OpenSharedResource(
                    reinterpret_cast<HANDLE>(h.textureHandle[k]),
                    IID_PPV_ARGS(&textures_[k])
                );
            }

            D3D11_TEXTURE2D_DESC sd = {};
            sd.Width = sd.Height = 1;
            sd.MipLevels = sd.ArraySize = 1;
            sd.Format = DXGI_FORMAT(h.textureFormat);
            sd.SampleDesc.Count = 1;
            sd.Usage = D3D11_USAGE_STAGING;
            sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            device_->CreateTexture2D(&sd, nullptr, &staging_);
            printf(
                "  the layer's textures: %ux%u, format %u%s\n",
                h.textureWidth,
                h.textureHeight,
                h.textureFormat,
                textures_[0] && staging_ ? "" : " (not opened here)"
            );
        }
    }
}

void OpenXrCheckCopies::ReadRecords() {
    openxr_share::Header& h = *header_;
    const uint64_t frames = h.frames.load();
    if (frames >= nextFrame_ + openxr_share::kRecords) {
        recordsLost_ += int(frames - openxr_share::kRecords + 1 - nextFrame_);
        nextFrame_ = frames - openxr_share::kRecords + 1;
    }

    for (; nextFrame_ <= frames; nextFrame_++) {
        openxr_share::Record r;
        if (!openxr_share::ReadRecord(h, nextFrame_, &r)) {
            recordsLost_++;
            continue;
        }

        records_++;
        const Sent* s = nullptr;
        for (auto it = sent_.rbegin(); it != sent_.rend() && !s; ++it) {
            if (it->displayTime == r.displayTime) {
                s = &*it;
            }
        }

        if (!s || !SamePose(s->pose[0], r.eye[0]) || !SamePose(s->pose[1], r.eye[1])
            || !SameFov(s->fov[0], r.fov[0]) || !SameFov(s->fov[1], r.fov[1])) {
            if (recordsWrong_++ < 5) {
                printf(
                    "  frame %llu: its record doesn't match what was submitted\n",
                    static_cast<unsigned long long>(r.frame)
                );
            }

            continue;
        }

        if (r.texture >= 0 && r.texture < openxr_share::kTextures) {
            withPicture_++;
            if (!firstCopied_) {
                firstCopied_ = r.frame;
            }
            waiting_.push_back({r.frame, r.texture, s->index});
        }
    }
}

void OpenXrCheckCopies::ReadWaiting() {
    openxr_share::Header& h = *header_;
    for (size_t i = 0; i < waiting_.size();) {
        const Waiting w = waiting_[i];
        const uint64_t held = h.textureFrame[w.texture].load();
        if (held == w.frame) {
            ReadPicture(w);
        } else if (held < w.frame && waiting_.size() < 8) {
            i++;  // its copy isn't finished yet
            continue;
        } else {
            picturesMissed_++;  // written over before it was read
        }

        waiting_.erase(waiting_.begin() + i);
    }
}

void OpenXrCheckCopies::ReadPicture(const Waiting& w) {
    openxr_share::Header& h = *header_;
    if (!pictures_ || !textures_[w.texture] || !staging_
        || !openxr_share::ClaimTexture(h, w.texture, w.frame)) {
        picturesMissed_++;
        return;
    }

    D3D11_BOX box = {
        h.textureWidth / 2,
        h.textureHeight / 2,
        0,
        h.textureWidth / 2 + 1,
        h.textureHeight / 2 + 1,
        1,
    };

    context_->CopySubresourceRegion(staging_.Get(), 0, 0, 0, 0, textures_[w.texture].Get(), 0, &box);
    D3D11_MAPPED_SUBRESOURCE m;

    // waits: the check, not the game
    const HRESULT hr = context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &m);
    openxr_share::ReleaseTexture(h, w.texture);
    if (FAILED(hr)) {
        picturesMissed_++;
        return;
    }

    const uint8_t* p = static_cast<const uint8_t*>(m.pData);
    int levels[3];
    for (int c = 0; c < 3; c++) {
        levels[c] = int(std::lround(Linear(p[c], srgb_) * 15));
    }
    context_->Unmap(staging_.Get(), 0);
    const int shown = levels[0] | levels[1] << 4 | levels[2] << 8;
    if (shown == (w.index & 0xFFF)) {
        picturesRight_++;
    } else if (picturesWrong_++ < 5) {
        printf(
            "  frame %llu: the copy shows frame %d's picture, not frame %d's\n",
            static_cast<unsigned long long>(w.frame),
            shown,
            w.index & 0xFFF
        );
    }
}

int OpenXrCheckCopies::Report() const {
    int failures = 0;
    printf(
        "frame records: %d read, %d not matching what was submitted, %d lost\n",
        records_,
        recordsWrong_,
        recordsLost_
    );

    failures += recordsWrong_ + recordsLost_ + (records_ == 0);
    const uint64_t since = firstCopied_ ? nextFrame_ - firstCopied_ : 0;
    printf(
        "pictures: %d copied of %llu frames since the first copy; read back %d right, %d wrong, %d "
        "missed\n",
        withPicture_,
        static_cast<unsigned long long>(since),
        picturesRight_,
        picturesWrong_,
        picturesMissed_
    );

    if (pictures_) {
        failures += picturesWrong_;
        if (picturesRight_ == 0) {
            printf("  no picture was read back right\n");
            failures++;
        }

        if (since > 0 && withPicture_ < 0.9 * double(since)) {
            printf("  fewer than 90%% of the frames were copied\n");
            failures++;
        }
    } else {
        printf("  (the swapchain isn't R8G8B8A8: pictures not compared)\n");
    }

    if (counterToTime_) {
        printf(
            "clock: %d readings, worst %lld ns from the runtime's own conversion\n",
            clockReadings_,
            static_cast<long long>(clockWorstNs_)
        );

        if (clockReadings_ == 0 || clockWorstNs_ > 100000) {
            failures++;
        }
    } else {
        printf(
            "clock: the instance has no %s (the layer should have added it)\n",
            kXrTimeConversionExtension
        );

        failures++;
    }

    return failures;
}

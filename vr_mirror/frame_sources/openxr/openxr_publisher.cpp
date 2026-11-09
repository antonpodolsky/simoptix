// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "openxr_publisher.h"

#include <d3d11_4.h>
#include <dxgi.h>

#include <algorithm>
#include <cstring>

#include "shared/log.h"

using Microsoft::WRL::ComPtr;

namespace {

    int64_t Counter() {
        LARGE_INTEGER c;
        QueryPerformanceCounter(&c);
        return c.QuadPart;
    }

    // The format family of a game's image, as a shared texture can hold it: typeless, so the reader can
    // view it as the game's colour format (an _SRGB view decodes what an _SRGB swapchain holds).
    DXGI_FORMAT Typeless(DXGI_FORMAT f) {
        switch (f) {
            case DXGI_FORMAT_R8G8B8A8_TYPELESS:
            case DXGI_FORMAT_R8G8B8A8_UNORM:
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
                return DXGI_FORMAT_R8G8B8A8_TYPELESS;
            case DXGI_FORMAT_B8G8R8A8_TYPELESS:
            case DXGI_FORMAT_B8G8R8A8_UNORM:
            case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
                return DXGI_FORMAT_B8G8R8A8_TYPELESS;
            case DXGI_FORMAT_R10G10B10A2_TYPELESS:
            case DXGI_FORMAT_R10G10B10A2_UNORM:
                return DXGI_FORMAT_R10G10B10A2_TYPELESS;
            case DXGI_FORMAT_R16G16B16A16_TYPELESS:
            case DXGI_FORMAT_R16G16B16A16_FLOAT:
                return DXGI_FORMAT_R16G16B16A16_TYPELESS;
            default:
                return f;
        }
    }

    // Whether the game turned on D3D11's multithread protection for its device context. Only read: the
    // layer copies on that context from the thread that ends the frame.
    openxr_share::ContextProtection ProtectionOf(ID3D11DeviceContext* context) {
        ComPtr<ID3D11Multithread> multithread;

        if (!context || FAILED(context->QueryInterface(IID_PPV_ARGS(&multithread)))) {
            return openxr_share::kProtectionUnknown;
        }

        return multithread->GetMultithreadProtected() ? openxr_share::kProtected : openxr_share::kUnprotected;
    }

    // The same family's plain format, for a device that won't share a typeless texture.
    DXGI_FORMAT Typed(DXGI_FORMAT f) {
        switch (Typeless(f)) {
            case DXGI_FORMAT_R8G8B8A8_TYPELESS:
                return DXGI_FORMAT_R8G8B8A8_UNORM;
            case DXGI_FORMAT_B8G8R8A8_TYPELESS:
                return DXGI_FORMAT_B8G8R8A8_UNORM;
            case DXGI_FORMAT_R10G10B10A2_TYPELESS:
                return DXGI_FORMAT_R10G10B10A2_UNORM;
            case DXGI_FORMAT_R16G16B16A16_TYPELESS:
                return DXGI_FORMAT_R16G16B16A16_FLOAT;
            default:
                return f;
        }
    }

}

bool OpenXrPublisher::Start(ID3D11Device* device, ClockReading clock, void* clockContext) {
    Stop();
    if (!device || !clock) {
        return false;
    }
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    frequency_ = f.QuadPart;

    bool existed = false;

    if (!OpenSharedMemory(&existed)) {
        return false;
    }

    device_ = device;
    device_->GetImmediateContext(&context_);
    multithreaded_ = (device_->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) == 0;
    clock_ = clock;
    clockContext_ = clockContext;

    const uint32_t session = ResetHeader();

    ResetCounters();

    // A reader waiting for the game opens the memory now.
    if (UniqueHandle waiting(OpenEventW(EVENT_MODIFY_STATE, FALSE, openxr_share::kSessionEvent)); waiting) {
        SetEvent(waiting.Get());
    }

    CoreLog(
        "frame source: shared memory ready (session %u%s; the game's device is %s)",
        session,
        existed ? ", kept by a reader since the last one" : "",
        multithreaded_ ? "multithreaded" : "single-threaded"
    );

    thread_ = std::thread([this] {
        try {
            Run();
        } catch (...) {
            CoreLog("frame source: its thread stopped by an unexpected error");
        }
    });

    return true;
}

bool OpenXrPublisher::OpenSharedMemory(bool* existed) {
    mapping_.Reset(CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        sizeof(openxr_share::Header),
        openxr_share::kMappingName
    ));

    if (!mapping_) {
        CoreLog("frame source: shared memory not made (%lu): no frames for VR Mirror", GetLastError());
        return false;
    }

    // A reader still holding the last session's memory keeps it alive: it is the same memory again.
    *existed = GetLastError() == ERROR_ALREADY_EXISTS;
    header_.Reset(
        static_cast<openxr_share::Header*>(MapViewOfFile(mapping_.Get(), FILE_MAP_ALL_ACCESS, 0, 0, 0))
    );

    event_.Reset(CreateEventW(nullptr, FALSE, FALSE, openxr_share::kNewImageEvent));
    reader_.Reset(CreateEventW(nullptr, FALSE, FALSE, openxr_share::kReaderEvent));
    stop_.Reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));

    if (!header_ || !event_ || !reader_ || !stop_) {
        CoreLog("frame source: shared memory not mapped (%lu): no frames for VR Mirror", GetLastError());
        Stop();
        return false;
    }

    return true;
}

uint32_t OpenXrPublisher::ResetHeader() {
    openxr_share::Header& h = *header_;
    const uint32_t session = h.session.load() + 1;

    h.state.store(0);
    h.frames.store(0);
    h.textureGeneration.store(0);
    for (auto& t : h.textureFrame) {
        t.store(0);
    }
    for (openxr_share::Record& r : h.records) {
        r.seq.store(0);
    }
    h.displayPeriod.store(0);
    h.clockSeq.store(0);

    h.magic = openxr_share::kMagic;
    h.version = openxr_share::kVersion;
    h.size = sizeof(openxr_share::Header);
    h.processId = GetCurrentProcessId();
    h.counterFrequency = frequency_;
    h.contextProtection = ProtectionOf(context_.Get());

    ComPtr<IDXGIDevice> dxgi;
    ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC desc = {};
    if (SUCCEEDED(device_.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(&adapter))) {
        adapter->GetDesc(&desc);
    }
    h.adapterLow = desc.AdapterLuid.LowPart;
    h.adapterHigh = desc.AdapterLuid.HighPart;

    h.layerSigned.store(Counter());
    h.session.store(session);
    h.state.store(openxr_share::kRunning);
    return session;
}

void OpenXrPublisher::ResetCounters() {
    frame_ = 0;
    next_ = 0;
    std::fill(pending_, pending_ + openxr_share::kTextures, 0);
    std::fill(held_, held_ + openxr_share::kTextures, 0);
    stats_ = {};
    timeCount_ = 0;
    loggedCopying_ = false;
    state_ = kNone;
}

void OpenXrPublisher::Stop() {
    if (stop_) {
        SetEvent(stop_.Get());
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    if (header_) {
        header_->state.store(openxr_share::kEnded);
        header_->textureGeneration.store(0);
        for (auto& t : header_->textureFrame) {
            t.store(0);
        }
        if (event_) {
            SetEvent(event_.Get());  // the reader looks at once
        }
    }

    ReleaseTextures();
    state_ = kNone;
    context_.Reset();
    device_.Reset();
    header_.Reset();
    for (UniqueHandle* h : {&mapping_, &event_, &reader_, &stop_}) {
        h->Reset();
    }
}

bool OpenXrPublisher::ReaderThere(int64_t now) const {
    const int64_t signed_ = header_->readerSigned.load();
    return signed_ != 0 && now - signed_ < static_cast<int64_t>(openxr_share::kReaderTimeoutS * frequency_);
}

// The publisher's thread: twice a second, and at once when a reader starts, it signs, reads the clock for
// a reader that is there, and makes or lets go of the textures.
void OpenXrPublisher::Run() {
    int64_t readerSeen = 0;
    bool readerWas = false;
    const HANDLE wake[2] = {stop_.Get(), reader_.Get()};
    while (WaitForMultipleObjects(2, wake, FALSE, 500) != WAIT_OBJECT_0) {
        const int64_t now = Counter();
        header_->layerSigned.store(now);
        const bool there = ReaderThere(now);
        if (there != readerWas) {
            CoreLog("frame source: VR Mirror %s", there ? "reads the frames" : "stopped reading");
            readerWas = there;
        }

        if (!there) {
            if (state_ == kReady && readerSeen
                && now - readerSeen > openxr_share::kReleaseAfterS * frequency_) {
                state_ = kRetiring;  // the game's thread lets go of them at its next frame
            }
        } else {
            readerSeen = now;
            ReadClock();
        }

        const uint32_t w = wantedWidth_, h = wantedHeight_, f = wantedFormat_;
        if (state_ == kReady
            && (w != textures_.width || h != textures_.height
                || Typed(DXGI_FORMAT(f)) != Typed(DXGI_FORMAT(textures_.format)))) {
            state_ = kRetiring;  // the game's image changed size: made again once let go
        }

        if (!multithreaded_) {
            continue;  // the game's thread does the rest itself
        }
        if (state_ == kRetired) {
            ReleaseTextures();
            state_ = kNone;
        }

        if (state_ == kNone && there && w && h) {
            if (MakeTextures()) {
                state_ = kReady;
            }
        }
    }
}

void OpenXrPublisher::ReadClock() {
    // The runtime converts a given counter moment exactly, so the reading has no gap around it.
    const int64_t counter = Counter();
    int64_t time = 0;
    if (!clock_(clockContext_, counter, &time)) {
        return;
    }
    openxr_share::Header& h = *header_;
    const uint32_t s = h.clockSeq.load();
    h.clockSeq.store(s + 1);  // odd: being written
    h.clockCounterBefore = h.clockCounterAfter = counter;
    h.clockTime = time;
    h.clockSeq.store(s + 2);
}

bool OpenXrPublisher::MakeTextures() {
    const uint32_t w = wantedWidth_, h = wantedHeight_;
    const DXGI_FORMAT game = DXGI_FORMAT(wantedFormat_.load());
    const int64_t t0 = Counter();
    Textures t;

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w;
    td.Height = h;
    td.MipLevels = td.ArraySize = 1;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    td.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

    for (DXGI_FORMAT format : {Typeless(game), Typed(game)}) {
        td.Format = format;
        bool ok = true;
        for (auto& tex : t.texture) {
            ok = ok && SUCCEEDED(device_->CreateTexture2D(&td, nullptr, &tex));
        }
        if (ok) {
            break;
        }
        for (auto& tex : t.texture) {
            tex.Reset();
        }
    }

    if (!t.texture[0]) {
        CoreLog("frame source: shared textures %ux%u (format %u) not made", w, h, unsigned(game));
        return false;
    }

    t.width = w, t.height = h, t.format = td.Format;
    D3D11_QUERY_DESC event = {D3D11_QUERY_EVENT, 0}, disjoint = {D3D11_QUERY_TIMESTAMP_DISJOINT, 0},
                     stamp = {D3D11_QUERY_TIMESTAMP, 0};

    uint64_t handles[openxr_share::kTextures] = {};
    for (int k = 0; k < openxr_share::kTextures; k++) {
        if (FAILED(device_->CreateQuery(&event, &t.done[k]))) {
            return false;
        }
        device_->CreateQuery(&disjoint, &t.disjoint[k]);  // timing is optional
        device_->CreateQuery(&stamp, &t.before[k]);
        device_->CreateQuery(&stamp, &t.after[k]);
        ComPtr<IDXGIResource> res;
        HANDLE shared = nullptr;
        if (FAILED(t.texture[k].As(&res)) || FAILED(res->GetSharedHandle(&shared))) {
            return false;
        }
        handles[k] = reinterpret_cast<uint64_t>(shared);
    }

    textures_ = t;
    openxr_share::Header& hd = *header_;
    hd.textureWidth = w;
    hd.textureHeight = h;
    hd.textureFormat = td.Format;
    hd.gameFormat = gameFormat_;
    for (int k = 0; k < openxr_share::kTextures; k++) {
        hd.textureHandle[k] = handles[k];
        hd.textureFrame[k].store(0);
    }

    if (++generation_ == 0) {
        generation_ = 1;  // 0 means none
    }
    hd.textureGeneration.store(generation_);
    CoreLog(
        "frame source: 3 shared textures %ux%u, format %u, %.0f MB, made in %.1f ms",
        w,
        h,
        unsigned(td.Format),
        3.0 * w * h * 4 / 1e6,
        double(Counter() - t0) * 1000 / frequency_
    );

    return true;
}

void OpenXrPublisher::ReleaseTextures() {
    if (!textures_.texture[0]) {
        return;
    }
    if (header_) {
        header_->textureGeneration.store(0);
        for (auto& t : header_->textureFrame) {
            t.store(0);
        }
    }

    textures_ = {};
    CoreLog("frame source: shared textures let go");
}

void OpenXrPublisher::CheckCopies() {
    bool finished = false;
    for (int k = 0; k < openxr_share::kTextures; k++) {
        if (!pending_[k]) {
            continue;
        }
        if (context_->GetData(textures_.done[k].Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) {
            continue;
        }

        header_->textureFrame[k].store(pending_[k]);
        held_[k] = pending_[k];
        pending_[k] = 0;
        finished = true;

        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj;
        UINT64 b = 0, a = 0;
        if (textures_.disjoint[k] && textures_.before[k] && textures_.after[k]
            && context_->GetData(textures_.disjoint[k].Get(), &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH)
                   == S_OK
            && context_->GetData(textures_.before[k].Get(), &b, sizeof(b), D3D11_ASYNC_GETDATA_DONOTFLUSH)
                   == S_OK
            && context_->GetData(textures_.after[k].Get(), &a, sizeof(a), D3D11_ASYNC_GETDATA_DONOTFLUSH)
                   == S_OK
            && !dj.Disjoint && dj.Frequency && a >= b && timeCount_ < kTimes) {
            times_[timeCount_++] = double(a - b) * 1000 / double(dj.Frequency);
        }
    }

    if (finished) {
        SetEvent(event_.Get());
    }
}

void OpenXrPublisher::Publish(
    const OpenXrPublishedFrame& f,
    ID3D11Texture2D* src,
    UINT subresource,
    const D3D11_BOX& box,
    uint32_t gameFormat
) {
    if (!header_) {
        return;
    }

    const int64_t now = Counter();
    stats_.frames++;

    int state = UpdateTextureState();

    const bool reader = ReaderThere(now);
    const uint32_t w = box.right - box.left, ht = box.bottom - box.top;

    NoteSource(src, w, ht, gameFormat, reader);

    if (!multithreaded_ && state == kNone && reader && src && MakeTextures()) {
        state_ = state = kReady;
    }
    if (state == kReady) {
        CheckCopies();
    }

    const uint64_t n = ++frame_;
    const int k = ClaimTexture(state, reader, src, w, ht);

    WriteRecord(n, f, k, box);

    if (k < 0) {
        return;
    }

    if (!loggedCopying_) {
        CoreLog(
            "frame source: copying the left eye's image, %ux%u from %d,%d of format %u",
            w,
            ht,
            int(box.left),
            int(box.top),
            unsigned(gameFormat)
        );

        loggedCopying_ = true;
    }

    Copy(k, n, src, subresource, box);
}

int OpenXrPublisher::UpdateTextureState() {
    // The textures' state: what the publisher's thread asked for, or (single-threaded device) all of it.
    int state = state_;

    if (state == kRetiring) {
        std::fill(pending_, pending_ + openxr_share::kTextures, 0);  // their copies are no longer looked at
        std::fill(held_, held_ + openxr_share::kTextures, 0);
        if (multithreaded_) {
            state_ = state = kRetired;
        } else {
            ReleaseTextures();
            state_ = state = kNone;
        }
    }

    return state;
}

void OpenXrPublisher::NoteSource(
    ID3D11Texture2D* src,
    uint32_t width,
    uint32_t height,
    uint32_t gameFormat,
    bool reader
) {
    if (!src) {
        return;
    }

    D3D11_TEXTURE2D_DESC sd;
    src->GetDesc(&sd);
    const bool changed = width != wantedWidth_ || height != wantedHeight_ || sd.Format != wantedFormat_;
    wantedWidth_ = width, wantedHeight_ = height, wantedFormat_ = sd.Format, gameFormat_ = gameFormat;

    // The image's size is known now (the first frame, or a new size): the publisher's thread makes
    // the textures at once for a reader that is there, not at its next tick.
    if (changed && reader) {
        SetEvent(reader_.Get());
    }
}

int OpenXrPublisher::ClaimTexture(
    int state,
    bool reader,
    ID3D11Texture2D* src,
    uint32_t width,
    uint32_t height
) {
    if (state != kReady || !reader || !src || width != textures_.width || height != textures_.height) {
        return -1;
    }

    openxr_share::Header& h = *header_;
    const int candidate = next_;

    if (pending_[candidate]) {
        stats_.copyPending++;
        return -1;
    }

    // This side's mark first, then the reader's (openxr_share.h): one of the two backs off.
    h.textureFrame[candidate].store(0);
    if (openxr_share::ReaderCount(h.readerBusy.load(), candidate) != 0) {
        h.textureFrame[candidate].store(held_[candidate]);
        stats_.readerBusy++;
        return -1;
    }

    held_[candidate] = 0;
    next_ = (next_ + 1) % openxr_share::kTextures;
    return candidate;
}

void OpenXrPublisher::WriteRecord(uint64_t n, const OpenXrPublishedFrame& f, int k, const D3D11_BOX& box) {
    openxr_share::Header& h = *header_;
    const uint32_t w = box.right - box.left, ht = box.bottom - box.top;

    openxr_share::Record& r = h.records[(n - 1) % openxr_share::kRecords];
    r.seq.store(2 * n - 1);
    r.frame = n;
    r.displayTime = f.displayTime;
    r.endFrameCounter = f.endFrameCounter;
    std::memcpy(r.eye, f.eye, sizeof(r.eye));
    std::memcpy(r.fov, f.fov, sizeof(r.fov));
    r.texture = k;
    r.x = int32_t(box.left), r.y = int32_t(box.top), r.width = int32_t(w), r.height = int32_t(ht);
    r.gameImage = f.gameImage, r.imagesHeld = f.imagesHeld, r.releasedDuringEnd = f.releasedDuringEnd;
    r.endThread = f.endThread, r.releaseThread = f.releaseThread;
    r.seq.store(2 * n);
    h.frames.store(n);
}

void OpenXrPublisher::Copy(int k, uint64_t n, ID3D11Texture2D* src, UINT subresource, const D3D11_BOX& box) {
    const bool timed = textures_.disjoint[k] && textures_.before[k] && textures_.after[k];
    if (timed) {
        context_->Begin(textures_.disjoint[k].Get());
        context_->End(textures_.before[k].Get());
    }

    context_->CopySubresourceRegion(textures_.texture[k].Get(), 0, 0, 0, 0, src, subresource, &box);
    if (timed) {
        context_->End(textures_.after[k].Get());
        context_->End(textures_.disjoint[k].Get());
    }

    context_->End(textures_.done[k].Get());
    pending_[k] = n;
    stats_.copied++;
}

OpenXrPublisherStats OpenXrPublisher::TakeStats() {
    OpenXrPublisherStats s = stats_;
    s.timed = uint32_t(timeCount_);
    if (timeCount_ > 0) {
        std::sort(times_, times_ + timeCount_);
        s.gpuP50 = times_[timeCount_ / 2];
        s.gpuP99 = times_[std::min(timeCount_ - 1, int(timeCount_ * 0.99))];
        s.gpuMax = times_[timeCount_ - 1];
    }

    stats_ = {};
    timeCount_ = 0;
    return s;
}

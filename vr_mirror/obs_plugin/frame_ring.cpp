// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "frame_ring.h"

#include <dxgi1_2.h>
#include <timeapi.h>

#include <cstring>

#include "shared/log.h"
#include "shared/thread_cpu.h"

using Microsoft::WRL::ComPtr;

FrameRing::~FrameRing() {
    Stop();
}

bool FrameRing::Start(FrameSource* source) {
    source_ = source;
    LUID luid = source->AdapterLuid();

    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return false;
    }

    ComPtr<IDXGIAdapter1> adapter, found;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        if (memcmp(&desc.AdapterLuid, &luid, sizeof(LUID)) == 0) {
            found = adapter;
            break;
        }
    }

    if (FAILED(D3D11CreateDevice(
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
        ))) {
        return false;
    }

    // Normal GPU priority on purpose: at the lowest one, copies waited tens of milliseconds
    // behind the game, so OBS ticks found no finished frame (and a game-eye copy that late
    // can catch the next frame already written over it).
    D3D11_QUERY_DESC qd = {D3D11_QUERY_EVENT, 0};
    for (auto& q : done_) {
        device_->CreateQuery(&qd, &q);
    }

    TakeNewDrawnPose();
    feed_.Init(device_.Get());

    if (!wake_) {
        wake_.Reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    }
    slotCount_ = wantSlots_;  // as the graphics thread asked before starting (SetSlotCount)
    next_ = 0;

    running_ = true;
    thread_ = std::thread([this] {
        // An exception escaping a thread would end OBS on the spot; log it and stop copying.
        try {
            Run();
        } catch (...) {
            CoreLog("copy thread stopped by an unexpected error");
        }
    });

    return true;
}

void FrameRing::Stop() {
    running_ = false;
    if (wake_) {
        SetEvent(wake_.Get());
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < kMaxSlots; i++) {
        slots_[i].Reset();
        info_[i] = {};
        inFlight_[i] = false;
    }

    feed_.Reset();
    feed_.Release();  // its pictures were made on this device, released below
    if (source_) {
        source_->ReleasePictures();  // ...and the source's, opened on it
    }
    loggedWidth_ = loggedHeight_ = 0;
    context_.Reset();
    device_.Reset();
}

uint64_t FrameRing::ThreadCycles() {
    return ::ThreadCycles(thread_);
}

void FrameRing::Snapshot(SlotInfo out[kMaxSlots]) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::copy(info_, info_ + kMaxSlots, out);
}

// Copies each new picture as soon as it exists and otherwise sleeps: it wakes for a new game
// frame (FrameSource::FrameEvent), Stop, or, while copies or thumbnails are still on the GPU,
// every millisecond.
void FrameRing::Run() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    timeBeginPeriod(1);  // 1 ms waits while GPU work is in flight
    double lastOpen = -10;
    uint64_t lastSerial = 0;
    while (running_) {
        wakeups_++;
        const double now = source_->Now();

        // The source may re-create its pictures (Pimax when a VR app starts or stops, the OpenXR layer
        // for a new game session); pick up new handles.
        if (now - lastOpen > 2.0) {
            lastOpen = now;
            source_->OpenPictures(device_.Get());
        }

        TakeNewDrawnPose();
        copied_ = false;
        GameFrame f;
        while (source_->NextFrame(lastSerial, &f)) {
            lastSerial = f.serial;

            // Also the pictures the drawn pose looks at (a thumbnail, to tell if the game has
            // frames queued).
            const int slot = Copy(f);
            const float whole[4] = {0, 0, 1, 1};
            if (slot >= 0 && slots_[slot].View()) {
                feed_.Submit(
                    context_.Get(),
                    slots_[slot].View(),
                    whole,
                    f,
                    source_->RecordedPredictions(),
                    source_->Eye(),
                    source_->Period()
                );
            }
        }

        if (copied_) {
            context_->Flush();  // one submission for this pass's copies (and thumbnails)
        }
        const bool copying = CheckCopies();
        source_->PollPictures(context_.Get());
        Look();

        HANDLE wait[2];
        DWORD n = 0;
        wait[n++] = wake_.Get();
        if (source_->FrameEvent()) {
            wait[n++] = source_->FrameEvent();
        }
        WaitForMultipleObjects(n, wait, FALSE, copying || feed_.Busy() ? 1 : 50);
    }

    timeEndPeriod(1);
}

// Queues one copy of frame `f`'s picture into the next slot (the slot's shared texture is made or
// re-made to fit it: UNORM, so OBS reads the stored bytes unchanged and the warp decodes what the
// picture's encoding asks for); CheckCopies hands it to OBS once the GPU has done it. Returns the
// slot, or -1.
int FrameRing::Copy(const GameFrame& f) {
    const int want = wantSlots_;
    if (want != slotCount_) {
        // Fewer slots: free the textures of the others (OBS's handles to them are closed by the
        // graphics thread, which asked for this). More: made as they are first used.
        std::lock_guard<std::mutex> lock(mutex_);
        for (int i = want; i < slotCount_; i++) {
            slots_[i].Reset();
            info_[i] = {};
            inFlight_[i] = false;
        }

        slotCount_ = want;
        next_ %= slotCount_;
        CoreLog(
            "frame ring: %d slots, %.0f MB",
            slotCount_,
            slotCount_ * double(loggedWidth_) * loggedHeight_ * 4 / 1e6
        );
    }

    const int slot = next_;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        info_[slot].valid = false;  // being rewritten
    }

    if (!source_->CopyPicture(device_.Get(), context_.Get(), f, &slots_[slot], true)) {
        return -1;
    }
    next_ = (next_ + 1) % slotCount_;
    const FrameCopy& c = slots_[slot];
    if (c.SharedHandle() != info_[slot].shared) {
        // A slot made or re-made: OBS opens its texture again.
        std::lock_guard<std::mutex> lock(mutex_);
        info_[slot].shared = c.SharedHandle();
        info_[slot].generation = ++generation_;
    }

    if (c.Width() != loggedWidth_ || c.Height() != loggedHeight_) {
        loggedWidth_ = c.Width(), loggedHeight_ = c.Height();
        CoreLog(
            "frame ring: %ux%u, %d slots, %.0f MB",
            loggedWidth_,
            loggedHeight_,
            slotCount_,
            slotCount_ * double(loggedWidth_) * loggedHeight_ * 4 / 1e6
        );
    }

    context_->End(done_[slot].Get());
    inFlight_[slot] = true;
    inFlightFrame_[slot] = f;
    copied_ = true;
    return slot;
}

// Hands the finished pictures to the drawn pose. With Instant a frame is drawn ~4 ms after arrival
// (its own thumbnail and patches are ready ~1 ms after its copy), with Smooth and Smoothest up to a
// second later, by when the frames after it have been looked at too. Copy thread.
void FrameRing::Look() {
    feed_.Poll(context_.Get(), source_->RecordedPredictions(), source_->Eye(), source_->Period());
    const CallStats t = feed_.TakeLookTime();
    if (t.count == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    statLookTime_.Merge(t);
}

// Switches to the drawn pose SetDrawnPose left, if any. Copy thread, and Start before the thread
// runs.
void FrameRing::TakeNewDrawnPose() {
    std::shared_ptr<DrawnPose> next;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!hasNext_) {
            return;
        }
        next = std::move(nextDrawnPose_);
        hasNext_ = false;
    }

    feed_.SetDrawnPose(std::move(next));
}

// Hands finished copies to OBS (never waits, never submits work). True while any is pending.
bool FrameRing::CheckCopies() {
    bool pending = false;
    for (int i = 0; i < kMaxSlots; i++) {
        if (!inFlight_[i]) {
            continue;
        }
        if (context_->GetData(done_[i].Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) {
            pending = true;
            continue;
        }

        inFlight_[i] = false;
        std::lock_guard<std::mutex> lock(mutex_);
        info_[i].frame = inFlightFrame_[i];
        info_[i].ready = source_->Now();
        info_[i].valid = true;
    }

    return pending;
}

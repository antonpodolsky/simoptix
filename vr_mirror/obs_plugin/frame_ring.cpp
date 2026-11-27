// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "frame_ring.h"

#include <dxgi1_2.h>
#include <timeapi.h>

#include <cmath>
#include <cstring>

#include "shared/log.h"
#include "shared/thread_cpu.h"

using Microsoft::WRL::ComPtr;

namespace {

    // A frame this long after the one before starts the game's frames again (FrameStarts).
    constexpr double kStartGapS = 2.0;

    // A frame as the forecast sees it: it is asked before any copy.
    StoredFrame Stored(const GameFrame& f) {
        return {true, f.serial, f.time, f.smoothTime, f.slotTime, 0, f.synced};
    }

}

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

    // With video support: the demo decodes its images on this device (DemoSource). Without it if the adapter
    // has none, for the game's sources.
    bool made = false;

    for (const UINT flags : {static_cast<UINT>(D3D11_CREATE_DEVICE_VIDEO_SUPPORT), 0u}) {
        const HRESULT hr = D3D11CreateDevice(
            found.Get(),
            found ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            flags,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            &device_,
            nullptr,
            &context_
        );

        if (SUCCEEDED(hr)) {
            made = true;
            break;
        }
    }

    if (!made) {
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
    greys_.Init(device_.Get(), false, kGreyWidth, kGreyHeight, 4);
    greysPendingCount_ = 0;

    if (!wake_) {
        wake_.Reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    }
    slotCount_ = wantSlots_;  // as the graphics thread asked before starting (SetSlotCount)
    next_ = 0;
    hasPrevious_ = held_ = false;

    running_ = true;
    const std::atomic<bool>* logGate = CurrentLogGate();  // it logs as the thread starting it does
    thread_ = std::thread([this, logGate] {
        const ScopedLogGate gate(logGate);

        // An exception escaping a thread would end OBS on the spot; log it and stop copying.
        try {
            Run();
        } catch (...) {
            CoreLogError("copy thread stopped by an unexpected error");
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
    feed_.Release();  // its images were made on this device, released below
    greys_ = Thumbnailer();
    if (source_) {
        source_->ReleaseImages();  // ...and the source's, opened on it
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

void FrameRing::SetForecast(std::unique_ptr<ShowForecast> forecast, const TickRecord& record) {
    std::lock_guard<std::mutex> lock(recordMutex_);
    nextForecast_ = std::move(forecast);
    hasNextForecast_ = true;
    nextRecord_ = record;
}

void FrameRing::SetTickRecord(const TickRecord& record) {
    std::unique_lock<std::mutex> lock(recordMutex_, std::try_to_lock);

    if (!lock.owns_lock()) {
        return;
    }

    nextRecord_ = record;
}

// Copies each new image as soon as it exists, or once the next one has, and otherwise sleeps: it
// wakes for a new game frame (FrameSource::FrameEvent), Stop, when a held frame has waited two display
// periods, or, while copies or thumbnails are still on the GPU, every millisecond.
void FrameRing::Run() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    timeBeginPeriod(1);  // 1 ms waits while GPU work is in flight
    double lastOpen = -10;
    uint64_t lastSerial = 0;
    while (running_) {
        wakeups_++;
        const double now = source_->Now();

        // The source may re-create its images (Pimax when a VR app starts or stops, the OpenXR layer
        // for a new game session); pick up new handles.
        if (now - lastOpen > 2.0) {
            lastOpen = now;
            source_->OpenImages(device_.Get());
        }

        TakeNewDrawnPose();
        copied_ = false;
        GameFrame f;
        while (source_->NextFrame(lastSerial, &f)) {
            lastSerial = f.serial;
            Arrived(f);
        }

        CopyHeldIfLate();

        if (copied_) {
            context_->Flush();  // one submission for this pass's copies (and thumbnails)
        }
        const bool copying = CheckCopies();
        source_->PollImages(context_.Get());
        Look();

        HANDLE wait[2];
        DWORD n = 0;
        wait[n++] = wake_.Get();
        if (source_->FrameEvent()) {
            wait[n++] = source_->FrameEvent();
        }
        WaitForMultipleObjects(n, wait, FALSE, WaitMs(copying || feed_.Busy() || greysPendingCount_ > 0));
    }

    timeEndPeriod(1);
}

// A new frame: asks the forecast about the frame before it, which is copied now if it was held and a
// tick will show it. Then copies this one, unless the picker asks for the frames shown only, the
// source keeps its image that long, and the forecast can't tell yet that a tick shows it: then it is
// held until the next one. Copying at once whatever is sure keeps the source's whole margin before it
// reuses an image (findings\obs_plugin.md). Without the newest record (the graphics thread was
// writing it) the forecast is still told of the frame, and nothing is held or skipped. Copy thread.
void FrameRing::Arrived(const GameFrame& f) {
    const bool fresh = TakeTickRecord();
    const bool show = !forecast_ || !hasPrevious_ || forecast_->WillShow(record_, Stored(f));

    if (held_ && (show || !fresh)) {
        CopyAndFeed(previous_);
    } else if (held_) {
        skipped_++;
    }

    const bool sure = !forecast_ || forecast_->Arrived(record_, Stored(f));
    held_ = !sure && fresh && record_.copiesShownOnly && source_->CanCopyAfterNextFrame();

    if (!held_) {
        CopyAndFeed(f);
    }

    if (hasPrevious_ && f.time - previous_.time >= kStartGapS) {
        frameStarts_++;
    }

    previous_ = f;
    hasPrevious_ = true;
}

// A held frame with no next one within two display periods (the game paused) is copied then: its
// image may be needed, and the source still has it. Copy thread.
void FrameRing::CopyHeldIfLate() {
    if (held_ && source_->Now() >= previous_.time + 2 * source_->Period()) {
        CopyAndFeed(previous_);
        held_ = false;
    }
}

// How long the copy thread sleeps when nothing wakes it: 1 ms while GPU work is in flight (`busy`),
// until a held frame has waited two display periods, else 50 ms. Copy thread.
DWORD FrameRing::WaitMs(bool busy) {
    if (busy) {
        return 1;
    }

    if (held_) {
        const double leftS = previous_.time + 2 * source_->Period() - source_->Now();
        return static_cast<DWORD>(std::clamp(std::ceil(leftS * 1000), 1.0, 50.0));
    }

    return 50;
}

// Takes the forecast and the record SetForecast and SetTickRecord left; false while the graphics
// thread is writing them (record_ stays as it was). Never waits. Copy thread.
bool FrameRing::TakeTickRecord() {
    std::unique_lock<std::mutex> lock(recordMutex_, std::try_to_lock);

    if (!lock.owns_lock()) {
        return false;
    }

    if (hasNextForecast_) {
        forecast_ = std::move(nextForecast_);
        hasNextForecast_ = false;
    }

    record_ = nextRecord_;
    return true;
}

// Copies frame `f`'s image, and hands it to the drawn pose with the images it looks at (a thumbnail,
// to tell if the game has frames queued). Copy thread.
void FrameRing::CopyAndFeed(const GameFrame& f) {
    const int slot = Copy(f);
    const float whole[4] = {0, 0, 1, 1};

    if (slot >= 0 && slots_[slot].View() && greysOn_) {
        MakeGrey(slot, f);
    }

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

// Queues one copy of frame `f`'s image into the next slot (the slot's shared texture is made or
// re-made to fit it: UNORM, so OBS reads the stored bytes unchanged and the warp decodes what the
// image's encoding asks for); CheckCopies hands it to OBS once the GPU has done it. Returns the
// slot, or -1.
int FrameRing::Copy(const GameFrame& f) {
    const int want = wantSlots_;
    if (want > slotCount_) {
        // More slots: made as they are first used.
        slotCount_ = want;
        CoreLog(
            "frame ring: %d slots, %.0f MB",
            slotCount_,
            slotCount_ * double(loggedWidth_) * loggedHeight_ * 4 / 1e6
        );
    } else if (want < slotCount_) {
        ShrinkTo(want);
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

    if (!source_->CopyImage(device_.Get(), context_.Get(), f, &slots_[slot], true)) {
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

// Keeps the `want` most recently copied entries, compacted to indices [0, want), oldest kept at 0,
// newest at the end, and frees the rest, wherever they physically were: the ring overwrites by
// index, so once it has wrapped, the newest entry's index can be anywhere. Copy thread.
void FrameRing::ShrinkTo(int want) {
    FrameCopy keptSlots[kMaxSlots];
    Microsoft::WRL::ComPtr<ID3D11Query> keptDone[kMaxSlots];
    bool keptInFlight[kMaxSlots] = {};
    GameFrame keptInFlightFrame[kMaxSlots];
    SlotInfo keptInfo[kMaxSlots];

    for (int k = 0; k < want; k++) {
        const int oldIndex = (next_ + slotCount_ - want + k) % slotCount_;
        keptSlots[k] = slots_[oldIndex];
        keptDone[k] = done_[oldIndex];
        keptInFlight[k] = inFlight_[oldIndex];
        keptInFlightFrame[k] = inFlightFrame_[oldIndex];
        keptInfo[k] = info_[oldIndex];
    }

    for (int k = 0; k < want; k++) {
        slots_[k] = keptSlots[k];
        done_[k] = keptDone[k];
        inFlight_[k] = keptInFlight[k];
        inFlightFrame_[k] = keptInFlightFrame[k];
    }

    for (int i = want; i < kMaxSlots; i++) {
        slots_[i].Reset();
        inFlight_[i] = false;
    }

    {
        // info_ is read by the graphics thread (Snapshot): move it as one update.
        std::lock_guard<std::mutex> lock(mutex_);
        for (int k = 0; k < want; k++) {
            info_[k] = keptInfo[k];
        }
        for (int i = want; i < kMaxSlots; i++) {
            info_[i] = {};
        }
    }

    slotCount_ = want;
    next_ = 0;  // the oldest kept, next to be overwritten
}

// Hands the finished images to the drawn pose. With Instant a frame is drawn ~4 ms after arrival
// (its own thumbnail and patches are ready ~1 ms after its copy), with Look-ahead about half a
// second later, by when the frames after it have been looked at too. Copy thread.
void FrameRing::Look() {
    ReadGreys();
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
    bool pending = false, finished = false;
    for (int i = 0; i < kMaxSlots; i++) {
        if (!inFlight_[i]) {
            continue;
        }
        if (context_->GetData(done_[i].Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) {
            pending = true;
            continue;
        }

        inFlight_[i] = false;
        finished = true;
        std::lock_guard<std::mutex> lock(mutex_);
        info_[i].frame = inFlightFrame_[i];
        info_[i].ready = source_->Now();
        info_[i].valid = true;
    }

    if (finished) {
        SetEvent(copiedEvent_.Get());
    }

    return pending;
}

// Queues a small grey copy of slot `slot`'s image (frame `f`), with the head pose the frame was drawn
// with: the one the frame source says, else its arrival slot's. Copy thread.
void FrameRing::MakeGrey(int slot, const GameFrame& f) {
    Quat pose;
    const double drawnFor = f.drawnSlot > 0 ? f.drawnSlot : f.slotTime;

    if (greysPendingCount_ == kGreysPending
        || !source_->RecordedPredictions().PredictedHeadAt(drawnFor, &pose)) {
        return;
    }

    const int64_t
        ticket = greys_.Make(context_.Get(), slots_[slot].View(), kGreyArea, source_->Eye().encodedTwice);

    if (ticket < 0) {
        return;
    }

    greysPending_[greysPendingCount_++] = {ticket, f.smoothTime, pose};
}

// Keeps the grey copies the GPU has finished, in order. Skipped while auto sync copies them out: the
// next pass reads them. Copy thread.
void FrameRing::ReadGreys() {
    while (greysPendingCount_ > 0) {
        const GreyPending& first = greysPending_[0];
        std::vector<float>& values = greysKept_[greyNext_].values;

        {
            std::unique_lock<std::mutex> lock(greyMutex_, std::try_to_lock);

            if (!lock.owns_lock() || !greys_.Read(context_.Get(), first.ticket, &values)) {
                return;
            }

            greysKept_[greyNext_].time = first.time;
            greysKept_[greyNext_].pose = first.pose;
            greyNext_ = (greyNext_ + 1) % kGreyKept;
        }

        std::move(greysPending_ + 1, greysPending_ + greysPendingCount_, greysPending_);
        greysPendingCount_--;
    }
}

void FrameRing::CopyGreys(double from, double to, std::vector<GreyFrame>* out) {
    std::lock_guard<std::mutex> lock(greyMutex_);
    out->clear();

    for (int i = 0; i < kGreyKept; i++) {
        const GreyFrame& grey = greysKept_[(greyNext_ + i) % kGreyKept];

        if (grey.time >= from && grey.time <= to && !grey.values.empty()) {
            out->push_back(grey);
        }
    }
}

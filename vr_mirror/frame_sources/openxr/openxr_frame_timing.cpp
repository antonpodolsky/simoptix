// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "openxr_frame_timing.h"

#include <d3d11_4.h>

#include "shared/log.h"

using Microsoft::WRL::ComPtr;

namespace {

    int64_t Counter() {
        LARGE_INTEGER c;
        QueryPerformanceCounter(&c);
        return c.QuadPart;
    }

    bool IsProtected(ID3D11DeviceContext* context) {
        ComPtr<ID3D11Multithread> multithread;

        if (FAILED(context->QueryInterface(IID_PPV_ARGS(&multithread)))) {
            return false;
        }

        return multithread->GetMultithreadProtected() != FALSE;
    }

}

bool OpenXrFrameTiming::Start(ID3D11Device* device) {
    Stop();

    if (!device) {
        return false;
    }

    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    frequency_ = f.QuadPart;

    mapping_.Reset(CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        sizeof(performance_share::Header),
        performance_share::kMappingName
    ));

    if (mapping_) {
        header_.Reset(
            static_cast<performance_share::Header*>(
                MapViewOfFile(mapping_.Get(), FILE_MAP_ALL_ACCESS, 0, 0, 0)
            )
        );
    }

    if (!header_) {
        CoreLog("frame timing: shared memory not made (%lu): no performance overlay", GetLastError());
        mapping_.Reset();
        return false;
    }

    device_ = device;
    device_->GetImmediateContext(&context_);
    protected_ = IsProtected(context_.Get());
    const D3D11_QUERY_DESC disjoint = {D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
    const D3D11_QUERY_DESC stamp = {D3D11_QUERY_TIMESTAMP, 0};
    bool queries = true;

    for (Slot& s : slots_) {
        queries = queries && SUCCEEDED(device_->CreateQuery(&disjoint, &s.disjoint))
                  && SUCCEEDED(device_->CreateQuery(&stamp, &s.begin))
                  && SUCCEEDED(device_->CreateQuery(&stamp, &s.end));
    }

    if (!queries) {
        for (Slot& s : slots_) {
            s = Slot();
        }
    }

    ResetHeader();
    CoreLog(
        "frame timing: shared memory ready; GPU times %s",
        !queries     ? "unknown (no timestamp queries)"
        : protected_ ? "on any thread (the game's context is protected)"
                     : "where the game begins and ends its frames on one thread"
    );

    return true;
}

void OpenXrFrameTiming::ResetHeader() {
    performance_share::Header& h = *header_;
    h.written.store(0);

    for (performance_share::Record& r : h.records) {
        r.seq.store(0);
    }

    h.magic = performance_share::kMagic;
    h.version = performance_share::kVersion;
    h.size = sizeof(performance_share::Header);
    h.processId = GetCurrentProcessId();
    h.counterFrequency = frequency_;

    active_ = false;
    endThread_ = 0;
    inFrame_ = false;
    waits_ = 0;

    for (std::atomic<uint64_t>& f : cpuFrame_) {
        f.store(0);
    }

    for (Slot& s : slots_) {
        s.begun = s.ended = s.timed = false;
    }

    current_ = -1;
    next_ = 0;
    written_ = 0;
    stats_ = {};
    cpuSumMs_ = gpuSumMs_ = 0;
}

void OpenXrFrameTiming::Stop() {
    if (active_) {
        CoreLog("frame timing: stopped with the session");
    }

    active_ = false;

    for (Slot& s : slots_) {
        s = Slot();
    }

    current_ = -1;
    context_.Reset();
    device_.Reset();
    header_.Reset();
    mapping_.Reset();
}

bool OpenXrFrameTiming::ReaderThere(int64_t now) const {
    const int64_t signedAt = header_->readerSigned.load();
    return signedAt != 0
           && now - signedAt < static_cast<int64_t>(performance_share::kReaderTimeoutS * frequency_);
}

void OpenXrFrameTiming::WaitCalled() {
    if (!active_) {
        return;
    }

    const int64_t now = Counter();
    const uint64_t frame = waits_.load();

    if (frame == 0) {
        return;
    }

    const int k = static_cast<int>(frame % kWaits);
    cpuTicks_[k].store(now - waitReturned_[k].load());
    pipelined_[k].store(inFrame_.load());
    cpuFrame_[k].store(frame, std::memory_order_release);
}

void OpenXrFrameTiming::WaitReturned(int64_t periodNs) {
    if (!active_) {
        return;
    }

    const uint64_t frame = waits_.load() + 1;
    const int k = static_cast<int>(frame % kWaits);
    waitReturned_[k].store(Counter());
    periodNs_[k].store(periodNs);
    waits_.store(frame);
}

void OpenXrFrameTiming::FrameBegun() {
    inFrame_ = true;

    if (!header_) {
        return;
    }

    // A frame begun and never ended (the game began another, which OpenXR allows): its queries end here.
    if (current_ >= 0) {
        EndQueries(slots_[current_]);
        slots_[current_].begun = false;
        current_ = -1;
    }

    if (!active_ || waits_.load() == 0) {
        return;
    }

    // The slot is needed again: whatever its frame still lacks stays unknown.
    Slot& s = slots_[next_];

    if (s.ended) {
        int64_t cpuUs = performance_share::kUnknown;
        bool pipelined = false;
        CpuTime(s.frame, &cpuUs, &pipelined);
        int64_t gpuUs = performance_share::kUnknown;
        bool resolved = false;
        GpuTime(s, &gpuUs, &resolved);
        Write(s, cpuUs, gpuUs, pipelined);
    }

    current_ = next_;
    next_ = (next_ + 1) % kSlots;
    s.frame = waits_.load();
    s.begun = true;
    s.ended = false;
    s.timed = false;

    const bool sameThread = GetCurrentThreadId() == endThread_.load();

    if (s.disjoint && (sameThread || protected_)) {
        context_->Begin(s.disjoint.Get());
        context_->End(s.begin.Get());
        s.timed = true;
    }
}

void OpenXrFrameTiming::EndQueries(Slot& s) {
    if (s.timed && s.begun) {
        context_->End(s.end.Get());
        context_->End(s.disjoint.Get());
    }
}

void OpenXrFrameTiming::FrameEnding() {
    inFrame_ = false;

    if (current_ < 0) {
        return;
    }

    Slot& s = slots_[current_];
    s.endCounter = Counter();
    EndQueries(s);
}

void OpenXrFrameTiming::FrameEnded(bool succeeded) {
    if (!header_) {
        return;
    }

    endThread_ = GetCurrentThreadId();

    if (current_ >= 0) {
        Slot& s = slots_[current_];
        s.begun = false;
        s.ended = succeeded;
        current_ = -1;
    }

    FinishSlots();

    const bool there = ReaderThere(Counter());

    if (there != active_.load()) {
        CoreLog(
            "frame timing: %s",
            there ? "SimOptix reads the game's frame times" : "SimOptix stopped reading"
        );
        active_ = there;
    }
}

bool OpenXrFrameTiming::CpuTime(uint64_t frame, int64_t* cpuUs, bool* pipelined) const {
    const int k = static_cast<int>(frame % kWaits);

    if (cpuFrame_[k].load(std::memory_order_acquire) != frame) {
        return false;
    }

    *cpuUs = cpuTicks_[k].load() * 1000000 / frequency_;
    *pipelined = pipelined_[k].load();
    return true;
}

// *resolved: the GPU time is known, or will never be. Reads the queries without waiting or flushing.
bool OpenXrFrameTiming::GpuTime(Slot& s, int64_t* gpuUs, bool* resolved) {
    if (!s.timed) {
        *resolved = true;
        return false;
    }

    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj = {};
    UINT64 begin = 0, end = 0;
    const UINT flags = D3D11_ASYNC_GETDATA_DONOTFLUSH;

    if (context_->GetData(s.disjoint.Get(), &dj, sizeof(dj), flags) != S_OK
        || context_->GetData(s.begin.Get(), &begin, sizeof(begin), flags) != S_OK
        || context_->GetData(s.end.Get(), &end, sizeof(end), flags) != S_OK) {
        *resolved = false;
        return false;
    }

    *resolved = true;

    if (dj.Disjoint || dj.Frequency == 0 || end < begin) {
        return false;
    }

    *gpuUs = static_cast<int64_t>((end - begin) * 1000000 / dj.Frequency);
    return true;
}

// The frames ended whose times are all in, oldest first: written for the reader. A frame whose CPU time
// will never come (the timing started or stopped in between) is written once its GPU time is in.
void OpenXrFrameTiming::FinishSlots() {
    for (int i = 0; i < kSlots; i++) {
        Slot& s = slots_[(next_ + i) % kSlots];

        if (!s.ended) {
            continue;
        }

        int64_t cpuUs = performance_share::kUnknown, gpuUs = performance_share::kUnknown;
        bool pipelined = false, resolved = false;
        const bool cpu = CpuTime(s.frame, &cpuUs, &pipelined);
        GpuTime(s, &gpuUs, &resolved);
        const bool cpuGone = !active_ || waits_.load() > s.frame + 1;

        if (!resolved || (!cpu && !cpuGone)) {
            break;  // the frames after it are written after it
        }

        Write(s, cpuUs, gpuUs, pipelined);
    }
}

void OpenXrFrameTiming::Write(Slot& s, int64_t cpuUs, int64_t gpuUs, bool pipelined) {
    s.ended = false;
    performance_share::Record r;
    r.frame = s.frame;
    r.endCounter = s.endCounter;
    r.periodNs = periodNs_[s.frame % kWaits].load();
    r.cpuUs = cpuUs;
    r.gpuUs = gpuUs;
    r.pipelined = pipelined ? 1 : 0;
    performance_share::WriteRecord(*header_, ++written_, r);

    stats_.frames++;

    if (cpuUs != performance_share::kUnknown && !pipelined) {
        stats_.cpuFrames++;
        cpuSumMs_ += cpuUs / 1000.0;
    }

    if (gpuUs != performance_share::kUnknown) {
        stats_.gpuFrames++;
        gpuSumMs_ += gpuUs / 1000.0;
    }
}

OpenXrFrameTimingStats OpenXrFrameTiming::TakeStats() {
    OpenXrFrameTimingStats s = stats_;
    s.cpuMs = s.cpuFrames ? cpuSumMs_ / s.cpuFrames : 0;
    s.gpuMs = s.gpuFrames ? gpuSumMs_ / s.gpuFrames : 0;
    stats_ = {};
    cpuSumMs_ = gpuSumMs_ = 0;
    return s;
}

// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// OpenXrFrameTiming: the OpenXR layer's part that times the game's frames for the performance overlay,
// and writes each frame's times into the memory of
// shared\performance\performance_share.h:
// - the CPU time: from the moment xrWaitFrame returned to the game to its next call of xrWaitFrame
// - the GPU time: between a timestamp put in the game's immediate context when xrBeginFrame returned and
//   one put there before xrEndFrame goes on, read a few frames later without waiting
// - the display period xrWaitFrame gave the frame, and whether the game called xrWaitFrame for its next
//   frame before it ended this one (pipelining: its CPU time is then cut short)
//
// Only while a reader has signed within performance_share::kReaderTimeoutS: otherwise each call returns at
// once and nothing goes into the game's context. The GPU timestamps go in only where the layer's copies go
// (the thread that ends the frames), so xrBeginFrame gets one only when the game begins its frames on that
// thread too, or its context is protected for several threads; otherwise the GPU time is unknown.
//
// Nothing here makes the game wait: no locks, no flushes, no waits on the GPU, nothing allocated per
// frame. Plain D3D11 and Windows calls: it doesn't know OpenXR (the layer passes plain values).
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>

#include "shared/performance/performance_share.h"
#include "shared/unique_handle.h"

// The timing's numbers since the last TakeStats, for the layer's log (the thread that ends frames).
struct OpenXrFrameTimingStats {
    uint32_t frames = 0;  // frames timed (written for the reader)
    uint32_t cpuFrames = 0, gpuFrames = 0;
    double cpuMs = 0, gpuMs = 0;  // their average times
};

class OpenXrFrameTiming {
public:
    // Whether a reader reads the frame times (`there`), and with its Debug setting on (`debug`): told once
    // at each change, from the thread that ends frames, and as gone when the session ends.
    using ReaderNews = void (*)(bool there, bool debug);

    ~OpenXrFrameTiming() { Stop(); }

    // Who hears the reader's news (the layer's log). Before Start.
    void SetReaderNews(ReaderNews news) { news_ = news; }

    // The game's D3D11 session started (the game's thread): makes the shared memory and the GPU queries.
    // False, with nothing made, if the memory can't be made.
    bool Start(ID3D11Device* device);
    // The session ends (the game's thread): lets go of everything.
    void Stop();

    // The game's thread that waits for frames: as xrWaitFrame is called, and once it returned with the
    // frame's display period (ns).
    void WaitCalled();
    void WaitReturned(int64_t periodNs);
    // The game's thread that begins frames, after xrBeginFrame succeeded.
    void FrameBegun();
    // The game's thread that ends frames: before xrEndFrame goes on, and after it returned.
    void FrameEnding();
    void FrameEnded(bool succeeded);

    // The thread that ends frames: the numbers since the last call.
    OpenXrFrameTimingStats TakeStats();

private:
    // A frame whose times are awaited: its GPU queries, and what the reader is told.
    struct Slot {
        Microsoft::WRL::ComPtr<ID3D11Query> disjoint, begin, end;
        uint64_t frame = 0;
        int64_t endCounter = 0;
        bool begun = false;  // between FrameBegun and FrameEnded
        bool ended = false;  // ended, its times not yet written
        bool timed = false;  // its GPU timestamps went in
    };

    static constexpr int kSlots = 4;  // frames whose GPU time is awaited
    static constexpr int kWaits = 8;  // frames whose CPU time is kept

    bool ReaderThere(int64_t now) const;
    void ResetHeader();
    void EndQueries(Slot& s);
    bool CpuTime(uint64_t frame, int64_t* cpuUs, bool* pipelined) const;
    bool GpuTime(Slot& s, int64_t* gpuUs, bool* resolved);
    void FinishSlots();
    void Write(Slot& s, int64_t cpuUs, int64_t gpuUs, bool pipelined);

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;  // the game's immediate context
    bool protected_ = false;  // the game's context is protected for several threads (ID3D11Multithread)
    UniqueHandle mapping_;
    MappedView<performance_share::Header> header_;
    int64_t frequency_ = 1;  // performance-counter ticks per second
    ReaderNews news_ = nullptr;
    bool debugWas_ = false;  // the thread that ends frames: the reader's Debug as last told

    // Set by the thread that ends frames, read by the others.
    std::atomic<bool> active_{false};     // a reader is there: time the frames
    std::atomic<uint32_t> endThread_{0};  // the thread that ended the last frame (Windows thread id)
    std::atomic<bool> inFrame_{false};    // between xrBeginFrame and xrEndFrame

    // The thread that waits for frames: frame n is the n-th xrWaitFrame return of the session. Its CPU
    // time is kept in place n % kWaits once the next xrWaitFrame call gives it.
    std::atomic<uint64_t> waits_{0};
    std::atomic<int64_t> waitReturned_[kWaits] = {};  // performance counter
    std::atomic<int64_t> periodNs_[kWaits] = {};
    std::atomic<int64_t> cpuTicks_[kWaits] = {};
    std::atomic<bool> pipelined_[kWaits] = {};
    std::atomic<uint64_t> cpuFrame_[kWaits] = {};  // the frame whose CPU time is in place k; set last

    // The threads that begin and end frames (one after the other, as OpenXR orders the game's calls).
    Slot slots_[kSlots];
    int current_ = -1;  // the slot of the frame begun; -1 none
    int next_ = 0;
    uint64_t written_ = 0;
    OpenXrFrameTimingStats stats_;
    double cpuSumMs_ = 0, gpuSumMs_ = 0;
};

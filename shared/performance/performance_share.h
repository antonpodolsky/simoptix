// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The memory the OpenXR layer shares with the performance overlay: how long each of the game's frames
// took on the CPU and on the GPU, and the frame time the headset's runtime gives the game. The layer
// (vr_mirror\frame_sources\openxr\openxr_frame_timing.*) writes it; a reader in either plugin
// (performance_reader.h) reads it and signs that it is there. The layer times the game's frames only
// while a reader has signed within kReaderTimeoutS.
//
// The layer makes it when the game's D3D11 session starts, as a named file mapping in this Windows
// session (kMappingName). Only plain data and fixed sizes, so both sides (64-bit) agree by
// construction; kVersion changes with every change of layout or meaning.
//
// The frames, in the order the layer finishes timing them (a frame's GPU time is known a few frames
// after it ended): record k holds the frame written k-th, under a sequence count (odd while it is
// written), so a reader never takes half of one.
//
// Times: the CPU time runs from the moment xrWaitFrame returned to the game to its next call of
// xrWaitFrame. The GPU time is the time on the GPU between two timestamps the layer puts in the game's
// immediate context, at xrBeginFrame and at xrEndFrame. Microseconds; kUnknown when the layer couldn't time
// it.
#pragma once

#include <atomic>
#include <cstdint>

namespace performance_share {

    constexpr const wchar_t* kMappingName = L"Local\\SimOptix.Performance";
    constexpr uint32_t kMagic = 0x46524550;  // "PERF"
    constexpr uint32_t kVersion = 1;
    constexpr int kRecords = 512;  // 2.1 s of frames at 240 Hz, for a reader's 1 s window

    // The reader counts as there while it signs within this long (performance-counter seconds).
    constexpr double kReaderTimeoutS = 1.5;
    constexpr int64_t kUnknown = -1;

    struct Record {
        std::atomic<uint64_t>
            seq;             // 2 * (its place in the order written, from 1) once written; odd meanwhile
        uint64_t frame;      // the layer's count of the game's frames in this session
        int64_t endCounter;  // performance counter when the game called xrEndFrame
        int64_t periodNs;    // the display period xrWaitFrame gave the game for this frame
        int64_t cpuUs;       // the game's CPU time for this frame; kUnknown
        int64_t gpuUs;       // its GPU time; kUnknown
        // The game called xrWaitFrame for its next frame before it ended this one, so cpuUs doesn't hold its
        // whole CPU time (pipelining).
        uint32_t pipelined;
    };

    struct Header {
        uint32_t magic;            // kMagic
        uint32_t version;          // kVersion
        uint32_t size;             // sizeof(Header)
        uint32_t processId;        // the game's (for the log only)
        int64_t counterFrequency;  // performance-counter ticks per second, as the layer read it

        std::atomic<int64_t> readerSigned;  // performance counter of a reader's last sign; 0: no reader
        std::atomic<uint64_t> written;  // records written: the newest is records[(written - 1) % kRecords]
        Record records[kRecords];
    };

    static_assert(std::atomic<uint64_t>::is_always_lock_free, "the shared memory needs lock-free atomics");
    static_assert(std::atomic<int64_t>::is_always_lock_free, "the shared memory needs lock-free atomics");

    // The layer's side: writes the n-th record (from 1). Only the game's thread that ends frames writes.
    inline void WriteRecord(Header& h, uint64_t n, const Record& values) {
        Record& r = h.records[(n - 1) % kRecords];
        r.seq.store(2 * n - 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        r.frame = values.frame;
        r.endCounter = values.endCounter;
        r.periodNs = values.periodNs;
        r.cpuUs = values.cpuUs;
        r.gpuUs = values.gpuUs;
        r.pipelined = values.pipelined;
        r.seq.store(2 * n, std::memory_order_release);
        h.written.store(n, std::memory_order_release);
    }

    // The reader's side: the n-th record, if it is still there whole (not yet written over).
    inline bool ReadRecord(const Header& h, uint64_t n, Record* out) {
        const Record& r = h.records[(n - 1) % kRecords];
        const uint64_t before = r.seq.load(std::memory_order_acquire);

        if (before != 2 * n) {
            return false;
        }

        out->frame = r.frame;
        out->endCounter = r.endCounter;
        out->periodNs = r.periodNs;
        out->cpuUs = r.cpuUs;
        out->gpuUs = r.gpuUs;
        out->pipelined = r.pipelined;
        std::atomic_thread_fence(std::memory_order_acquire);
        return r.seq.load(std::memory_order_relaxed) == before;
    }

}

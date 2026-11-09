// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// OpenXrPublisher: the OpenXR layer's part that hands the game's frames to VR Mirror through the
// shared memory of openxr_share.h. Per frame, on the game's thread, after the frame went on to the
// runtime: a record of it (its display time, the eyes' poses and fields of view), and, while a reader is
// there, one GPU copy of the left eye's picture into one of three shared textures, with a marker after it
// that a later frame checks without waiting or flushing; a finished copy is announced by an event. A
// thread of its own signs the layer's presence once a second, reads the runtime's clock for the reader,
// and makes the textures when a reader comes (lets them go 10 s after it left).
//
// Nothing here ever makes the game wait: no locks, no CPU waits on the GPU, no flushes, nothing
// allocated per frame. A frame whose texture the reader still reads is simply not copied.
//
// Plain D3D11 and Windows calls only: it doesn't know OpenXR (the layer passes plain values) and is also
// what the e2e tests' fake game publishes with, so the tests run the layer's own code.
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <thread>

#include "openxr_share.h"
#include "shared/unique_handle.h"

// One frame as the game handed it to the headset.
struct OpenXrPublishedFrame {
    int64_t displayTime = 0;      // XrTime (ns) it was drawn for
    int64_t endFrameCounter = 0;  // performance counter when xrEndFrame returned
    openxr_share::Pose eye[2] = {};
    openxr_share::Fov fov[2] = {};

    // How the game handed the image over (openxr_share::Record has their meaning).
    int32_t gameImage = -1;
    uint32_t imagesHeld = 0, releasedDuringEnd = 0;
    uint32_t endThread = 0, releaseThread = 0;
};

// The publisher's numbers since the last TakeStats (the game's thread).
struct OpenXrPublisherStats {
    uint32_t frames = 0;                        // frames published
    uint32_t copied = 0;                        // ...whose picture was copied
    uint32_t readerBusy = 0;                    // ...not copied: the reader was reading the next texture
    uint32_t copyPending = 0;                   // ...not copied: the next texture's copy wasn't finished yet
    uint32_t timed = 0;                         // copies whose GPU time was read
    double gpuP50 = 0, gpuP99 = 0, gpuMax = 0;  // their GPU time (ms)
};

class OpenXrPublisher {
public:
    // The runtime's time (XrTime, ns) for performance-counter moment `counter`; false if it can't say.
    using ClockReading = bool (*)(void* context, int64_t counter, int64_t* time);

    ~OpenXrPublisher() { Stop(); }

    // The game's D3D11 session started (the game's thread): makes the shared memory and starts the
    // publisher's own thread. `clock` reads the runtime's clock (any thread). False, with nothing made,
    // if the memory can't be made.
    bool Start(ID3D11Device* device, ClockReading clock, void* clockContext);
    // The session ends (the game's thread): tells the reader, stops the thread, lets go of everything.
    void Stop();
    bool Started() const { return static_cast<bool>(header_); }

    // The display period from xrWaitFrame (ns). Any thread.
    void SetDisplayPeriod(int64_t ns) {
        if (header_) {
            header_->displayPeriod.store(ns, std::memory_order_relaxed);
        }
    }

    // The game's thread, after the frame went on to the runtime: records it, and copies `box` of
    // subresource `subresource` of `src` (the left eye's picture, the game's swapchain format
    // `gameFormat`) if a reader is there and the next texture is free.
    void Publish(
        const OpenXrPublishedFrame& f,
        ID3D11Texture2D* src,
        UINT subresource,
        const D3D11_BOX& box,
        uint32_t gameFormat
    );

    // The game's thread: the numbers since the last call.
    OpenXrPublisherStats TakeStats();

private:
    enum TextureState : int { kNone, kReady, kRetiring, kRetired };

    struct Textures {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture[openxr_share::kTextures];
        Microsoft::WRL::ComPtr<ID3D11Query> done[openxr_share::kTextures];
        Microsoft::WRL::ComPtr<ID3D11Query> disjoint[openxr_share::kTextures];
        Microsoft::WRL::ComPtr<ID3D11Query> before[openxr_share::kTextures];
        Microsoft::WRL::ComPtr<ID3D11Query> after[openxr_share::kTextures];
        uint32_t width = 0, height = 0, format = 0;
    };

    void Run();
    bool OpenSharedMemory(bool* existed);
    uint32_t ResetHeader();
    void ResetCounters();
    int UpdateTextureState();
    void NoteSource(ID3D11Texture2D* src, uint32_t width, uint32_t height, uint32_t gameFormat, bool reader);
    int ClaimTexture(int state, bool reader, ID3D11Texture2D* src, uint32_t width, uint32_t height);
    void WriteRecord(uint64_t n, const OpenXrPublishedFrame& f, int k, const D3D11_BOX& box);
    void Copy(int k, uint64_t n, ID3D11Texture2D* src, UINT subresource, const D3D11_BOX& box);
    bool ReaderThere(int64_t now) const;
    bool MakeTextures();  // from wanted*_; the publisher's thread, or the game's for a single-threaded device
    void ReleaseTextures();
    void CheckCopies();  // the game's thread: marks finished copies, reads their GPU time
    void ReadClock();    // the publisher's thread

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;  // the game's immediate context: the game's thread
    bool multithreaded_ = true;  // the device may be used from the publisher's thread
    ClockReading clock_ = nullptr;
    void* clockContext_ = nullptr;
    UniqueHandle mapping_, event_, stop_, reader_;
    MappedView<openxr_share::Header> header_;
    int64_t frequency_ = 1;  // performance-counter ticks per second
    std::thread thread_;

    // The textures: made and let go by the publisher's thread (or the game's for a single-threaded
    // device), used by the game's thread only in kReady; the game's thread confirms kRetiring with
    // kRetired, after which it no longer touches them.
    Textures textures_;
    std::atomic<int> state_{kNone};
    // game's thread
    std::atomic<uint32_t> wantedWidth_{0}, wantedHeight_{0}, wantedFormat_{0}, gameFormat_{0};
    uint32_t generation_ = 0;

    // The game's thread.
    uint64_t frame_ = 0;
    int next_ = 0;
    uint64_t pending_[openxr_share::kTextures] = {};  // the frame whose copy is on the GPU; 0 none
    uint64_t held_[openxr_share::kTextures] = {};     // the frame whose finished copy the texture holds
    OpenXrPublisherStats stats_;
    static constexpr int kTimes = 1024;
    double times_[kTimes] = {};  // GPU copy times since TakeStats (ms)
    int timeCount_ = 0;
    bool loggedCopying_ = false;
};

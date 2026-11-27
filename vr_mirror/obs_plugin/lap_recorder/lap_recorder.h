// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// LapRecorder: records the recorded lap (Record a lap, in a VR Mirror source's properties). While it runs,
// its thread wakes for each game frame the source's ring has copied (FrameRing) and asks the telemetry where
// the car is (LapFinder). Before the lap it copies nothing: the ring keeps the frames since a crossing
// (kRingFrames) until the crossing reaches the telemetry. From the lap's start it draws each frame's image
// into Windows' HEVC encoder (LapVideo), at most kWidth wide, holds the newest few until the closing crossing
// is known, and saves the video with each frame's moment and head pose (RecordedLap), where the demo source
// plays it from. A lap the player leaves (the pits, the menus) is thrown away, and it waits for the next.
//
// Its thread ends by itself once the lap is saved, or when it fails. Each frame's pose is the one its
// frame source gives for the moment it was drawn for: exact from the OpenXR layer, the arrival slot's from
// Pimax (right while the game keeps up, as it does on a clean lap).
//
// Threads: Start, Stop and Get on OBS's graphics thread; the work on its own. Knows nothing about OBS.
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <mfidl.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "lap_finder.h"
#include "lap_video.h"
#include "shared/unique_handle.h"
#include "vr_mirror/frame_sources/demo/recorded_lap.h"
#include "vr_mirror/obs_plugin/frame_ring.h"

class LapRecorder {
public:
    enum class State { kOff, kWaiting, kRecording, kSaving, kSaved, kFailed };

    // The widest video a lap is recorded at (pixels); a wider image is scaled down to it. At the game's own
    // 5084x4168 from the OpenXR layer, a lap held 4.4 GB of stored images and 2 GB in the encoder, and the
    // game dropped 4.7 frames per 10 s against about 1 without it. The video took 150 Mbit/s, which a video
    // player struggled to play. At this width it is a quarter of the pixels, and the demo's view still has
    // about one stored pixel per pixel of a 1920x1080 canvas at Zoom 1.25.
    static constexpr int kWidth = 2560;
    // The frames the source's ring keeps while a lap is asked for (MirrorView), at least: the lap's frames
    // since a crossing are still there when the crossing reaches the telemetry (kHeld says how late).
    static constexpr int kRingFrames = 24;

    ~LapRecorder() { Stop(); }

    // Starts the thread on `ring`'s frames from `source` (both outlive Stop). The telemetry must be held
    // (Telemetry::Acquire) meanwhile.
    void Start(FrameRing* ring, FrameSource* source);
    // Gives up a lap not yet saved, and waits for the thread (a lap being saved finishes first).
    void Stop();
    State Get() const { return static_cast<State>(state_.load()); }
    // The thread runs: false once it has saved, failed or been stopped.
    bool Running() const { return running_.load(); }

private:
    // A frame held or written: its image in the encoder's pool, its moment and pose.
    struct Held {
        Microsoft::WRL::ComPtr<IMFSample> image;
        double t = 0;  // the source's clock: the moment it was drawn for
        Quat pose;
    };

    // Frames of the lap held until its closing crossing is known: a crossing reaches the telemetry some 10 ms
    // after it happened, and more if the game's updates bunch. Measured on a real lap: three frames had
    // arrived after the closing crossing before it was known.
    static constexpr size_t kHeld = 12;

    void Run();
    void Record();
    bool Prepare(std::string* error);
    void Step();
    void TakeFrames();
    bool OpenVideo();
    bool TakeFrame(const FrameRing::SlotInfo& slot, int index, double moment);
    void WriteLap();
    void Discard(const char* why);
    void Save();
    void Fail(const char* why);
    ID3D11Texture2D* Opened(const FrameRing::SlotInfo& slot, int index);

    // Set by Start, read by the thread.
    FrameRing* ring_ = nullptr;
    FrameSource* source_ = nullptr;
    std::thread thread_;
    UniqueHandle stop_{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<bool> running_{false};
    std::atomic<int> state_{static_cast<int>(State::kOff)};

    // The thread's.
    LapVideo video_;
    LapFinder finder_;
    std::wstring videoPath_;      // the video while it is recorded (renamed when saved)
    std::vector<Held> held_;      // oldest first, at most LapVideo::kPool
    RecordedLap lap_;             // the eye and size from the first frame; the frames as written
    double start_ = 0, end_ = 0;  // the lap's crossings, on the source's clock (0: not yet)
    uint64_t lastSerial_ = 0;
    int behind_ = 0;           // frames the encoder had no room for
    bool firstTaken_ = false;  // the lap's first frame was taken (its gap after the line is logged)
    FrameRing::SlotInfo slots_[FrameRing::kMaxSlots];
    Microsoft::WRL::ComPtr<ID3D11Texture2D> opened_[FrameRing::kMaxSlots];  // the ring's, on this device
    uint32_t openedGeneration_[FrameRing::kMaxSlots] = {};
};

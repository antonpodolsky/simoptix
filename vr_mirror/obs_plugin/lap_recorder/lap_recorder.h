// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// LapRecorder: records the recorded lap (Record a lap, in a VR Mirror source's properties). While it runs,
// its thread wakes for each game frame the source's ring has copied (FrameRing), draws the frame's image
// into Windows' HEVC encoder (LapVideo), and asks the telemetry where the car is (LapFinder). It holds
// the last few frames until the car crosses the line, writes every frame of the lap from that crossing to
// the next, and saves the video with each frame's moment and head pose (RecordedLap), where the demo source
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

    // Frames held until the lap's start is known: a crossing reaches the telemetry some 10 ms after it
    // happened, and more if the game's updates bunch.
    static constexpr size_t kHeld = 12;

    void Run();
    void Record();
    bool Prepare(std::string* error);
    void Step();
    void TakeFrames();
    bool TakeFrame(const FrameRing::SlotInfo& slot, int index);
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
    int behind_ = 0;  // frames the encoder had no room for
    FrameRing::SlotInfo slots_[FrameRing::kMaxSlots];
    Microsoft::WRL::ComPtr<ID3D11Texture2D> opened_[FrameRing::kMaxSlots];  // the ring's, on this device
    uint32_t openedGeneration_[FrameRing::kMaxSlots] = {};
};

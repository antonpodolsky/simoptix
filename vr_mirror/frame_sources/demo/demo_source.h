// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// DemoSource: the newest recorded lap (recorded_lap.h), played in a loop as if a game were drawing it,
// for a VR Mirror source's demo (a FrameSource: core\adapters\d3d\frame_source.h). The hosts can't tell it
// from a game: the pipeline stabilizes its frames with the host's settings, so changing Smoothing or Zoom
// shows at once.
//
// A thread of its own hands out each frame a little before its moment in the lap, on the performance counter,
// with the moment it was drawn for, the head pose it was drawn with, exactly (FrameReport::kSlotAndPose, as
// the OpenXR layer's frames), and its index in the lap. The head path between frames is interpolated between
// their poses. A frame's image is decoded when the host copies it (CopyImage), on the host's own device, by
// its index in the lap: no texture passes between devices, and no image can belong to another frame. The lap
// ends where it starts, on the line, so it plays on into its start with the clock running on.
//
// Source time is the performance counter, in seconds. Knows nothing about OBS, the game or video files
// beyond DemoVideo.
#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "adapters/d3d/frame_source.h"
#include "common/arrival_stats.h"
#include "common/pose_history.h"
#include "shared/unique_handle.h"
#include "vr_mirror/frame_sources/demo/demo_video.h"
#include "vr_mirror/frame_sources/demo/recorded_lap.h"

class DemoSource : public FrameSource {
public:
    DemoSource();
    ~DemoSource() override;

    const char* Name() const override { return "recorded lap"; }
    FrameReport Report() const override { return FrameReport::kSlotAndPose; }
    void SetArrivalTiming(std::unique_ptr<ArrivalTiming> timing) override { arrival_ = std::move(timing); }
    // Loads the newest recorded lap and starts playing it.
    bool Start(std::string* error) override;
    void Stop() override;

    LUID AdapterLuid() const override { return adapter_; }
    EyeInfo Eye() const override;
    double Now() const override;
    double Period() const override { return period_.load(); }

    GameFrame LatestFrame() override;
    bool NextFrame(uint64_t after, GameFrame* f) override;
    HANDLE FrameEvent() const override { return frameEvent_.Get(); }
    bool Idle() const override { return false; }
    double SourceFps() const override { return fps_.load(); }
    uint64_t FramesArrived() const override { return framesArrived_.load(); }
    const PoseLookup& RecordedPredictions() const override { return recorded_; }

    Quat HeadAt(double t) override;
    void HeadsAt(const double* t, int n, Quat* out) override;
    bool PredictedHeadAt(double slot, Quat* q) override;
    void PredictedHeadsAt(const double* slots, int n, Quat* out) override;
    void PredictedHeadsBetween(const double* t, int n, Quat* out) override;

    void SetImageNeed(float, double) override {}  // its images are the lap's own size
    void OpenImages(ID3D11Device* device) override;
    bool CopyImage(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        const GameFrame& f,
        FrameCopy* into,
        bool shared
    ) override;

    void PollImages(ID3D11DeviceContext*) override {}
    void ReleaseImages() override;

    const char* ThreadName() const override { return "demo thread"; }
    uint64_t ThreadCycles() override;
    // The 10 s line: frames handed out, and laps played.
    uint64_t TakeStats(double seconds, void (*line)(const char*)) override;

private:
    // A frame handed out: its record, and its index in the lap.
    struct Playing {
        GameFrame frame;
        int index = -1;
    };

    // A frame decoded, waiting to be copied.
    struct Decoded {
        int index = -1;
        Microsoft::WRL::ComPtr<IMFSample> sample;
    };

    static constexpr int kFinished = 16;
    // Frames decoded after a frame before its image is copied: the decoder finishes writing a surface after
    // it hands it out.
    static constexpr int kAhead = 2;
    static constexpr int kQueue = kAhead + 2;

    void Run();
    bool HandOut(double now);
    bool FindFinished(uint64_t serial, Playing* out);
    bool OpenVideo(ID3D11Device* device);
    bool Ready(int index);
    void Forget();

    struct Recorded : PoseLookup {
        explicit Recorded(DemoSource* s) : source(s) {}
        bool PredictedHeadAt(double slot, Quat* q) const override { return source->PredictedHeadAt(slot, q); }
        DemoSource* source;
    } recorded_{this};

    // Set by Start, then read by the demo thread and the host's device thread (and the lap's eye by any
    // thread, under the lock).
    RecordedLap lap_;
    std::wstring videoPath_;
    LUID adapter_ = {};

    // The demo thread's.
    std::unique_ptr<ArrivalTiming> arrival_;
    ArrivalStats stats_;
    double lapStart_ = 0;  // when the lap now playing started (source seconds)
    size_t next_ = 0;      // the lap's next frame to hand out
    uint64_t serial_ = 0;
    std::atomic<uint64_t> wakeups_{0}, laps_{0};
    uint64_t statArrived_ = 0;  // the hosts' control thread: FramesArrived at the last TakeStats

    // Shared with the hosts: the lock (held only to copy values in or out) guards these.
    mutable std::mutex mutex_;
    PoseHistory poses_;
    EyeInfo eye_;
    Playing finished_[kFinished];  // a ring of the newest, oldest at finishedHead_
    int finishedHead_ = 0, finishedCount_ = 0;

    // The host's device thread: the video, decoded on its device, and the frames decoded and not yet copied,
    // in the lap's order.
    DemoVideo video_;
    ID3D11Device* failedDevice_ = nullptr;  // not held: the video can't be decoded on it (logged once)
    Decoded queue_[kQueue];
    int queued_ = 0;
    int decodeNext_ = 0;  // the lap index of the frame the decoder hands out next

    std::atomic<double> period_{1.0 / 90};
    std::atomic<double> fps_{0};
    std::atomic<uint64_t> framesArrived_{0};
    std::atomic<bool> running_{false};
    double frequency_ = 1;  // performance-counter ticks per second
    UniqueHandle frameEvent_, stop_;
    std::thread thread_;
};

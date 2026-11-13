// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// DemoSource: the newest recorded lap (recorded_lap.h), played in a loop as if a game were drawing it,
// for a VR Mirror source's demo (a FrameSource: core\adapters\d3d\frame_source.h). The hosts can't tell it
// from a game: the pipeline stabilizes its frames with the host's settings, so changing Smoothing or Zoom
// shows at once.
//
// A thread of its own hands out each frame at its moment in the lap, on the performance counter: it decodes
// the frame's image (DemoVideo) a moment before, and hands the frame over once the image is ready, with the
// moment it was drawn for and the head pose it was drawn with, exactly (FrameReport::kSlotAndPose, as the
// OpenXR layer's frames). The head path between frames is interpolated between their poses. The lap ends
// where it starts, on the line, so it plays on into its start with the clock running on.
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

    LUID AdapterLuid() const override { return video_.Adapter(); }
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
    // A frame being decoded, then handed out.
    struct Playing {
        GameFrame frame;
        int texture = -1;
    };

    static constexpr int kFinished = 16;

    void Run();
    bool Decode(double now);
    void Publish(double now);
    bool FindFinished(uint64_t serial, Playing* out);

    struct Recorded : PoseLookup {
        explicit Recorded(DemoSource* s) : source(s) {}
        bool PredictedHeadAt(double slot, Quat* q) const override { return source->PredictedHeadAt(slot, q); }
        DemoSource* source;
    } recorded_{this};

    // Set by Start, then read by the demo thread (and the lap's eye by any thread, under the lock).
    RecordedLap lap_;
    DemoVideo video_;

    // The demo thread's.
    std::unique_ptr<ArrivalTiming> arrival_;
    ArrivalStats stats_;
    double lapStart_ = 0;  // when the lap now playing started (source seconds)
    size_t next_ = 0;      // the lap's next frame to decode
    int nextTexture_ = 0;
    bool decoding_ = false;
    Playing decoded_;  // decoding_: the frame whose image is being copied
    uint64_t serial_ = 0;
    std::atomic<uint64_t> wakeups_{0}, laps_{0};
    uint64_t statArrived_ = 0;  // the hosts' control thread: FramesArrived at the last TakeStats

    // Shared with the hosts: the lock (held only to copy values in or out) guards these.
    mutable std::mutex mutex_;
    PoseHistory poses_;
    EyeInfo eye_;
    Playing finished_[kFinished];  // a ring of the newest, oldest at finishedHead_
    int finishedHead_ = 0, finishedCount_ = 0;
    std::atomic<uint64_t> textureSerial_[DemoVideo::kTextures] = {};  // the frame each texture holds

    // The host's device thread: the textures opened on its device.
    ID3D11Device* hostDevice_ = nullptr;  // not held
    Microsoft::WRL::ComPtr<ID3D11Texture2D> hostTextures_[DemoVideo::kTextures];

    std::atomic<double> period_{1.0 / 90};
    std::atomic<double> fps_{0};
    std::atomic<uint64_t> framesArrived_{0};
    std::atomic<bool> running_{false};
    double frequency_ = 1;  // performance-counter ticks per second
    UniqueHandle frameEvent_, stop_;
    std::thread thread_;
};

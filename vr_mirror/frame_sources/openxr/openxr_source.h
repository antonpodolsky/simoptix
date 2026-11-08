// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// OpenXrSource: the game's frames as the OpenXR layer publishes them (openxr_share.h), for the mirror
// app and the OBS plugin alike (a FrameSource: core\adapters\d3d\frame_source.h). It runs outside the
// game: it only opens the memory and textures the layer shares.
//
// A reader thread of its own waits for the layer's event (it sleeps meanwhile; with no game running it
// looks for the layer's memory once a second), signs the reader's presence so the layer copies, puts the
// frames' times on the performance counter with the layer's clock reading, records each frame's head
// pose for its display time (halfway between the two eyes, OpenXR's head) in a pose history, and hands
// each frame whose picture copy is finished to the hosts, its slot the display time the game drew it
// for (stage 1, the reported slot). Every frame's pose is the one it was drawn with, exactly: the head
// path between frames is interpolated between their poses.
//
// Source time is the performance counter, in seconds. The pictures (OpenXrPictures) are opened and
// scaled on the host's device; the layer's textures are claimed while the host's GPU reads them.
// Knows nothing about OpenXR's calls, OBS or windows.
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
#include "openxr_pictures.h"
#include "openxr_share.h"
#include "shared/unique_handle.h"

class OpenXrSource : public FrameSource {
public:
    OpenXrSource();
    ~OpenXrSource() override;

    const char* Name() const override { return "OpenXR layer"; }
    // Each frame carries the display time it was drawn for and its pose.
    FrameReport Report() const override { return FrameReport::kSlotAndPose; }
    void SetArrivalTiming(std::unique_ptr<ArrivalTiming> timing) override { arrival_ = std::move(timing); }
    // Starts the reader thread (the layer needn't be there yet).
    bool Start(std::string* error) override;
    void Stop() override;

    LUID AdapterLuid() const override;
    EyeInfo Eye() const override;
    double Now() const override;
    double Period() const override { return period_.load(); }

    GameFrame LatestFrame() override;
    bool NextFrame(uint64_t after, GameFrame* f) override;
    HANDLE FrameEvent() const override { return frameEvent_.Get(); }
    bool Idle() const override { return idle_; }
    double SourceFps() const override { return fps_.load(); }
    uint64_t FramesArrived() const override { return framesArrived_.load(); }
    const PoseLookup& RecordedPredictions() const override { return recorded_; }

    // The head at source time `t`: interpolated between the frames' poses (PoseHistory::At).
    Quat HeadAt(double t) override;
    void HeadsAt(const double* t, int n, Quat* out) override;
    // The pose frame drawn for display slot `slot` was drawn with; false if no frame was.
    bool PredictedHeadAt(double slot, Quat* q) override;
    void PredictedHeadsAt(const double* slots, int n, Quat* out) override;
    void PredictedHeadsBetween(const double* t, int n, Quat* out) override;

    void SetPictureNeed(float width, double maxBytes) override { pictures_.SetNeed(width, maxBytes); }
    void OpenPictures(ID3D11Device* device) override;
    bool CopyPicture(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        const GameFrame& f,
        FrameCopy* into,
        bool shared
    ) override;

    void PollPictures(ID3D11DeviceContext* context) override;
    void ReleasePictures() override { pictures_.Release(); }

    const char* ThreadName() const override { return "reader thread"; }
    uint64_t ThreadCycles() override;
    // The 10 s line: frames read, and those whose picture was gone before a host copied it.
    uint64_t TakeStats(double seconds, void (*line)(const char*)) override;

private:
    // A frame whose picture copy is finished, as the hosts get it: its record and where its picture is.
    struct Finished {
        GameFrame frame;
        uint64_t layerFrame = 0;    // the layer's frame number (which the texture's mark names)
        int width = 0, height = 0;  // the picture's size in the texture (pixels, from 0, 0)
    };

    // A frame whose copy the layer hasn't finished yet.
    struct Waiting {
        uint64_t layerFrame = 0;
        int texture = -1;
        double slot = 0;  // the display time it was drawn for (source seconds)
        int width = 0, height = 0;
    };

    static constexpr int kFinished = 16;  // the newest finished frames (a host copies within a frame or two)
    static constexpr int kWaiting = 8;

    void Run();
    bool OpenShare();
    void CloseShare(const char* why);
    void ReadClock();
    void ReadFrames(double now);
    void FinishCopies();
    double SourceSeconds(int64_t xrTime) const;  // an XrTime on the source's clock
    bool FindFinished(uint64_t serial, Finished* out);

    struct Recorded : PoseLookup {
        explicit Recorded(OpenXrSource* s) : source(s) {}
        bool PredictedHeadAt(double slot, Quat* q) const override { return source->PredictedHeadAt(slot, q); }
        OpenXrSource* source;
    } recorded_{this};

    // The reader thread's.
    UniqueHandle mapping_, layerEvent_;
    MappedView<openxr_share::Header> header_;
    uint32_t session_ = 0;
    uint64_t nextFrame_ = 1;  // the next record to read
    Waiting waiting_[kWaiting];
    int waitingCount_ = 0;
    bool clockValid_ = false;
    double clockOffset_ = 0;   // source seconds minus XrTime seconds
    double lastFinished_ = 0;  // source time of the last finished frame
    std::unique_ptr<ArrivalTiming> arrival_;
    ArrivalStats stats_;
    uint64_t serial_ = 0;
    std::atomic<uint64_t> wakeups_{0}, lost_{0};
    uint64_t statArrived_ = 0;  // the hosts' control thread: FramesArrived at the last TakeStats

    // Shared with the hosts: the state lock (held only to copy values in or out) guards these.
    mutable std::mutex mutex_;
    PoseHistory poses_;
    EyeInfo eye_;
    LUID adapter_ = {};
    Finished finished_[kFinished];  // a ring of the newest, oldest at finishedHead_
    int finishedHead_ = 0, finishedCount_ = 0;

    std::atomic<double> period_{1.0 / 90};
    std::atomic<double> fps_{0};
    std::atomic<uint64_t> framesArrived_{0};
    std::atomic<bool> idle_{true};
    std::atomic<bool> running_{false};
    double frequency_ = 1;  // performance-counter ticks per second
    UniqueHandle frameEvent_, stop_;
    UniqueHandle session_event_;  // openxr_share::kSessionEvent: the layer wakes the reader thread
    std::thread thread_;

    OpenXrPictures pictures_;  // the host's device thread
};

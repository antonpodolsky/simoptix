// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// FrameSource: what a VR Mirror host (the OBS plugin, the mirror app) needs of a frame source
// (vr_mirror\frame_sources\): the game's frames as they become available, the head poses for the
// pipeline (PoseSource), the eye's geometry, the clock, and a copy of each frame's picture into the
// host's own store on the host's device. Implemented by the Pimax client mirror (PimaxSource) and the
// OpenXR layer's reader (OpenXrSource); the hosts don't know which runs.
//
// Interface only: knows nothing about PVR, OpenXR, OBS or windows. Threads: Start and Stop on the host's
// control thread; the frames, poses and clock from any thread; the pictures on the thread that owns the
// host's device context.
#pragma once

#include <windows.h>
#include <d3d11.h>

#include <cstdint>
#include <memory>
#include <string>

#include "adapters/d3d/frame_copy.h"
#include "common/game_frame.h"
#include "common/pose_source.h"
#include "pipeline/1_arrival_timing/arrival_timing.h"
#include "pipeline/2_drawn_pose/drawn_pose.h"
#include "pipeline/pipeline_config.h"

// The most memory one host's stored pictures may take (bytes): Look-ahead holds at most 55 frames (its
// half second, the copy delay up to 45 ms, and 3 more: FixedDelayPicker), 4.66 GB at the OpenXR layer's
// full 5084x4168.
constexpr double kPictureMemory = 4.7e9;

class FrameSource : public PoseSource {
public:
    virtual ~FrameSource() = default;

    // For the log ("Pimax session", "OpenXR layer").
    virtual const char* Name() const = 0;
    // What it tells of each frame (pipeline_config.h): it decides stages 1 and 2 (ConfigFor).
    virtual FrameReport Report() const = 0;
    // The arrival timing its thread runs on each frame (the builder makes it from ConfigFor): set
    // before Start. Start fails without one.
    virtual void SetArrivalTiming(std::unique_ptr<ArrivalTiming> timing) = 0;
    virtual bool Start(std::string* error) = 0;
    // Safe to call more than once.
    virtual void Stop() = 0;

    // The graphics adapter the game draws on; the host's device must use the same one.
    virtual LUID AdapterLuid() const = 0;
    // The left eye the pictures show.
    virtual EyeInfo Eye() const = 0;
    // The source's clock (seconds): the frames', the poses' and the arrival timing's. Cheap.
    virtual double Now() const = 0;

    // The newest frame (GameFrame::buffer < 0: none yet).
    virtual GameFrame LatestFrame() = 0;
    // The oldest frame newer than serial `after` whose picture can be copied now; false if none. A
    // host that keeps every frame walks them with this; one that shows the newest takes LatestFrame.
    virtual bool NextFrame(uint64_t after, GameFrame* f) = 0;
    // Signalled (auto-reset) on new frames, for hosts that wait instead of polling.
    virtual HANDLE FrameEvent() const = 0;
    // No game frame for 2 s: the source does next to nothing and hosts should wait on FrameEvent.
    virtual bool Idle() const = 0;
    // The game's frame rate over the last 3 s, and frames arrived so far (never reset).
    virtual double SourceFps() const = 0;
    virtual uint64_t FramesArrived() const = 0;
    // The poses the drawn pose stage's feeding looks up (not counted in any stats).
    virtual const PoseLookup& RecordedPredictions() const = 0;

    // How wide (pixels) a stored picture of the eye needs to be for the output to be shown 1:1 in its
    // middle, and the most memory one may take (bytes): a source that can scale its pictures keeps them
    // that size, or its own when that is near it, never above its own; one that can't ignores it. Any
    // thread, before Start too.
    virtual void SetPictureNeed(float width, double maxBytes) = 0;
    // The host's device and context (the thread that owns the context).
    // Opens the source's pictures on `device` (again, if the source re-created them): call every 2 s.
    virtual void OpenPictures(ID3D11Device* device) = 0;
    // Copies frame `f`'s picture into `into` (made or re-made for it), on `context`; false if it can't
    // (no picture, or it is gone). `shared`: `into` is to be opened by another device (FrameCopy).
    virtual bool CopyPicture(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        const GameFrame& f,
        FrameCopy* into,
        bool shared
    ) = 0;

    // Lets go of the source's pictures the host's GPU has finished reading; call often (never waits).
    virtual void PollPictures(ID3D11DeviceContext* context) = 0;
    // The host is about to let go of its device: the source lets go of what it opened on it.
    virtual void ReleasePictures() = 0;

    // Footprint and stats (the host's control thread): the source's own thread's timestamp-counter
    // cycles so far, and its own 10 s log lines (`line`; null: only reset), returning its thread's
    // wake-ups since the last call.
    virtual const char* ThreadName() const = 0;
    virtual uint64_t ThreadCycles() = 0;
    virtual uint64_t TakeStats(double seconds, void (*line)(const char*)) = 0;
    // A debug trace of the source's own (poses, frames), to a file named `prefix` plus its own ending
    // (Pimax: `_pimax.csv`); false if it has none.
    virtual bool StartTrace(const wchar_t* /*prefix*/) { return false; }
    virtual void StopTrace() {}
};

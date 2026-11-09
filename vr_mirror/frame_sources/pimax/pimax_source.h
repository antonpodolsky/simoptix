// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// PimaxSource: the game's frames and the head pose from a separate PVR session, for the mirror app
// and the OBS plugin alike (a FrameSource: core\adapters\d3d\frame_source.h). It never touches the game
// process. Its poll thread ties the
// pieces together: the session (PvrSession), the client mirror (PvrMirror: the game's own
// left-eye image and its frame signal), the arrival timing (the pipeline's stage 1, ArrivalTiming,
// which the host chooses) and the poses with
// the runtime's predictions (PvrPoses). Hosts read frames and poses under locks that are never
// held across a runtime call.
#pragma once

#include <windows.h>
#include <d3d11.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "adapters/d3d/frame_source.h"
#include "adapters/system/trace_writer.h"
#include "common/arrival_stats.h"
#include "common/clock_sync.h"
#include "common/footprint.h"
#include "common/game_frame.h"
#include "common/pose_source.h"
#include "common/rot.h"
#include "pipeline/1_arrival_timing/arrival_timing.h"
#include "pipeline/2_drawn_pose/drawn_pose.h"
#include "shared/unique_handle.h"
#include "vr_mirror/frame_sources/pimax/pvr_mirror.h"
#include "vr_mirror/frame_sources/pimax/pvr_poses.h"
#include "vr_mirror/frame_sources/pimax/pvr_session.h"

// The calls into the Pimax runtime, by kind, for the footprint stats. Each is a call into
// pi_server, so their rate is the load this tool puts on Pimax's compositor.
enum PvrCall : int {
    kCallClock,       // getTimeSeconds on the poll thread (clock sync, with each pose sample)
    kCallPose,        // getTrackingState: the head pose sample (poll thread)
    kCallSlot,        // getPredictedDisplayTime: the next display slot (poll thread)
    kCallPrediction,  // getTrackingState for a new display slot (poll thread)
    kCallStatus,      // getHmdStatus: should the session close (poll thread)
    kCallConfig,      // mirror handles (OpenTextures, the hosts' copy threads)
    kCallNow,         // getTimeSeconds from Now() before the clock is synced (any thread)
    kPvrCallKinds
};

const char* PvrCallName(int kind);  // short name for the log, e.g. "pose"

// The runtime calls and poll-thread passes since the last TakeFootprint (Start and the reconnect
// after another app takes over are not counted: they are rare).
struct PvrFootprint {
    CallStats calls[kPvrCallKinds];
    uint64_t wakeups = 0;  // poll-thread passes
};

class PimaxSource : public FrameSource {
public:
    ~PimaxSource() override;

    const char* Name() const override { return "Pimax session"; }
    // The mirror's frames carry only their arrival: the arrival timing finds their slots.
    FrameReport Report() const override { return FrameReport::kArrival; }
    // The arrival timing the poll thread runs on each new frame (vr_mirror\core\pipeline\pipeline_builder.h
    // makes it): set before Start, on the thread that calls Start. Start fails without one.
    void SetArrivalTiming(std::unique_ptr<ArrivalTiming> timing) override { arrival_ = std::move(timing); }
    // Opens the PVR session and enables the client mirror.
    bool Start(std::string* error) override;
    // Disables the mirror and closes the session. Safe to call more than once.
    void Stop() override;

    // Adapter LUID the headset runs on; the D3D device must use the same adapter.
    LUID AdapterLuid() const override { return session_.AdapterLuid(); }
    EyeInfo Eye(int eye) const { return session_.Eye(eye); }
    EyeInfo Eye() const override { return session_.Eye(0); }

    // Opens (or re-opens, if the server re-created it) the shared mirror texture.
    bool OpenTextures(ID3D11Device* device);
    ID3D11Texture2D* Texture() const { return mirror_.Texture(); }

    GameFrame LatestFrame() override;
    // The newest frame, if it is newer than `after` (the mirror holds only the newest).
    bool NextFrame(uint64_t after, GameFrame* f) override;
    // Signalled (auto-reset) on every new synced frame, for hosts that wait instead of polling.
    HANDLE FrameEvent() const override { return frameEvent_.Get(); }
    // No game frame for 2 s (none yet, or the game quit): the poll thread makes no runtime calls
    // but the session's status check and only waits for the frame signal, and LatestFrame doesn't
    // change. Hosts should wait on FrameEvent meanwhile instead of drawing. Ends with the next frame.
    bool Idle() const override { return idle_; }
    double Period() const override { return session_.Period(); }
    // Head orientation at source time `t`, from the pose history (PoseHistory::At: interpolated, or
    // extrapolated a little past the newest sample). Never calls the runtime.
    Quat HeadAt(double t) override { return poses_.HeadAt(t); }
    // HeadAt for `n` times at once, under one hold of the pose lock (the look-ahead smoothing asks
    // for a whole window of poses each frame).
    void HeadsAt(const double* t, int n, Quat* out) override { poses_.HeadsAt(t, n, out); }
    // The head orientation the runtime predicted for display slot `slot`, asked the
    // moment that slot became the next one: the pose a game rendered its frame for that slot
    // with. False if none was recorded for it. Counted in TakePredictionStats.
    bool PredictedHeadAt(double slot, Quat* q) override { return poses_.PredictedHeadAt(slot, q, true); }
    // ...not counted (the lookups of the drawn pose's feeding).
    bool PredictedHeadAtUncounted(double slot, Quat* q) { return poses_.PredictedHeadAt(slot, q, false); }
    // The same as the drawn pose looks them up (DrawnPoseFeed's predictions).
    const PoseLookup& RecordedPredictions() const override { return recorded_; }

    // The host's images: the mirror is copied whole, as it is (it can't be scaled on the way).
    void SetImageNeed(float, double) override {}
    void OpenImages(ID3D11Device* device) override { OpenTextures(device); }
    bool CopyImage(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        const GameFrame& f,
        FrameCopy* into,
        bool shared
    ) override;

    void PollImages(ID3D11DeviceContext*) override {}
    void ReleaseImages() override { mirror_.ReleaseTexture(); }
    // For `n` display slots in increasing order at once (one hold of the pose lock): the predicted
    // pose where one was recorded, else the head at that time (HeadAt). Not counted in the stats.
    void PredictedHeadsAt(const double* slots, int n, Quat* out) override {
        poses_.PredictedHeadsAt(slots, n, out);
    }

    // ...and for `n` times between slots: the slerp of the two slots' predictions. Not counted.
    void PredictedHeadsBetween(const double* t, int n, Quat* out) override {
        poses_.PredictedHeadsBetween(t, n, out);
    }

    // How many PredictedHeadAt calls found a prediction, and how many didn't (and reset).
    void TakePredictionStats(int* found, int* missing) { poses_.TakePredictionStats(found, missing); }
    // source time (seconds). Cheap: the performance counter plus an offset the poll thread keeps
    // measuring (asking the runtime itself is a call into the Pimax server).
    double Now() const override;
    // The game's frame rate over the last 3 s window.
    double SourceFps() const override { return fps_.load(); }
    // Game frames arrived so far (never reset): a host's stats over a stretch of its own take the
    // difference.
    uint64_t FramesArrived() const override { return framesArrived_.load(); }
    // Diagnostic trace, CSV with one tagged row per event: P = head pose sample, S = the pose
    // predicted for a display slot when it became the next one, F = frame arrival with its
    // de-jittered time and display slot (header in the file), to `prefix` + `_pimax.csv`. Any time;
    // Stop also closes it. Written to disk on its own thread (TraceWriter): the poll thread never waits
    // on it.
    bool StartTrace(const wchar_t* prefix) override;
    void StopTrace() override;

    // The runtime calls and poll-thread passes since the last call (and resets them).
    PvrFootprint TakeFootprint();
    // Timestamp-counter cycles the poll thread has run so far (thread_cpu.h); 0 while stopped.
    // Call from the thread that calls Start and Stop.
    uint64_t PollThreadCycles();
    const char* ThreadName() const override { return "poll thread"; }
    uint64_t ThreadCycles() override { return PollThreadCycles(); }
    // The 10 s lines: the predicted poses found and missing, and every call into the Pimax runtime by
    // kind (and resets them); returns the poll thread's passes.
    uint64_t TakeStats(double seconds, void (*line)(const char*)) override;

    // Last-resort cleanup from crash/exit handlers: just switch the mirror off.
    static void EmergencyDisable() { PvrMirror::EmergencyDisable(); }

private:
    struct Poll;
    struct PassCalls;

    void PollThread();
    void PublishFootprint(PvrFootprint& pending);
    bool WaitIdle(Poll& poll);
    void PollPass(Poll& poll);
    PassCalls CallRuntime(Poll& poll);
    void PublishPass(Poll& poll, const PassCalls& pass);
    void CheckRate(Poll& poll, double now, bool haveSignal);
    void CheckQuit(Poll& poll);
    void WaitForNextPass(const UniqueHandle& timer);
    void Reconnect();
    void AddFootprint(int kind, const CallStats& stats);  // under the state lock

    struct Recorded : PoseLookup {
        explicit Recorded(PimaxSource* s) : source(s) {}
        bool PredictedHeadAt(double slot, Quat* q) const override {
            return source->PredictedHeadAtUncounted(slot, q);
        }

        PimaxSource* source;
    } recorded_{this};

    // The session's call lock guards every runtime call, so the hosts' threads, which take only the
    // state lock below, never wait behind a call into pi_server. Lock order: the call lock,
    // then the state lock.
    mutable PvrSession session_;
    PvrMirror mirror_;
    std::unique_ptr<ArrivalTiming> arrival_;  // poll thread (set before Start)
    std::atomic<bool> idle_{false};           // written by the poll thread (Idle)

    TraceWriter trace_;  // StartTrace: rows from the poll thread, written to disk on its own thread
    std::thread thread_;
    std::atomic<bool> running_{false};
    PvrPoses poses_;                // head pose samples and the runtime's predictions per display slot
    double currentSlot_ = 0;        // poll thread: display slot the runtime is currently predicting
    double lastPredictedSlot_ = 0;  // poll thread
    mutable std::mutex mutex_;      // the state lock: the two below, held only to copy them in or out
    GameFrame frame_;
    mutable PvrFootprint footprint_;  // mutable: Now() counts its call too
    std::atomic<double> fps_{0};
    std::atomic<uint64_t> framesArrived_{0};  // written by the poll thread
    // source time = performance counter + offset, measured by the poll thread (see Now()).
    ClockSync clock_;
    std::atomic<double> clockOffset_{0};
    std::atomic<bool> clockValid_{false};
    UniqueHandle frameEvent_{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
};

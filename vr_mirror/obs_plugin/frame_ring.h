// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// FrameRing: each new game frame's picture, copied from the frame source (FrameSource: the Pimax
// game-eye mirror, or the OpenXR layer's copy) into a ring of shared textures on our own D3D11 device,
// for the VR Mirror sources (MirrorView, mirror_view.h).
//
// A copy thread copies each new frame as soon as it can (the mirror is a single buffer the game
// overwrites about a frame later; the OpenXR layer reuses its textures two frames later), marks a slot
// ready once the GPU has finished the copy (an event query read with DONOTFLUSH: it never waits on the
// GPU), and feeds each frame's pictures to the pipeline's drawn pose (DrawnPoseFeed), which the
// graphics thread then asks for the pose each frame was drawn with. OBS's graphics thread reads the
// slots with Snapshot and opens their shared textures on OBS's device.
//
// Knows nothing about OBS (no libobs calls, no settings, no drawing), nor which frame source runs.
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "adapters/d3d/drawn_pose_feed.h"
#include "adapters/d3d/frame_copy.h"
#include "adapters/d3d/frame_source.h"
#include "shared/unique_handle.h"

class FrameRing {
public:
    // Slots in use: as many as the output delay needs (SetSlotCount: set before Start, and again
    // once the delay is measured), up to kMaxSlots: Smoothest holds about a second of frames. Each
    // slot is a copy of the game eye: 21 MB at Render Quality 1.0, 33 MB at 1.25 from the Pimax mirror;
    // from the OpenXR layer as large as the output needs (FrameSource::SetPictureNeed).
    static constexpr int kMaxSlots = 128;
    struct SlotInfo {
        GameFrame frame;   // the game frame as it arrived (the drawn pose gives its head pose)
        double ready = 0;  // source time the copy was seen complete
        HANDLE shared = nullptr;
        uint32_t generation = 0;  // changes when the textures are re-created (size change)
        bool valid = false;
    };

    ~FrameRing();

    // The drawn pose the copy thread feeds (the pipeline's, Pipeline::SharedDrawnPose), kept
    // across Stop and Start. Any thread, any time: the copy thread takes it over at its next pass and
    // starts it with no history.
    void SetDrawnPose(std::shared_ptr<DrawnPose> drawnPose) {
        std::lock_guard<std::mutex> lock(mutex_);
        nextDrawnPose_ = std::move(drawnPose);
        hasNext_ = true;
    }

    // Creates the copy device on the game's adapter and starts the copy thread.
    bool Start(FrameSource* source);
    // Stops the copy thread and frees the textures; Start can be called again.
    void Stop();

    // Copies all kMaxSlots entries into `out` (nothing allocated); those beyond the slots in use are
    // never valid.
    void Snapshot(SlotInfo out[kMaxSlots]);
    // Graphics thread: use `count` slots (4 to kMaxSlots) from the next copy on (more: made as they are
    // first used); the copy thread frees the textures of the others. The caller should close its own
    // handles to them too, or their memory stays allocated.
    void SetSlotCount(int count) { wantSlots_ = std::clamp(count, 4, kMaxSlots); }
    int SlotCount() const { return wantSlots_; }

    // The time the drawn pose took on the copy thread looking at frames with patches, since the
    // last call.
    CallStats TakeLookTime() {
        std::lock_guard<std::mutex> lock(mutex_);
        const CallStats t = statLookTime_;
        statLookTime_ = {};
        return t;
    }

    // Copy-thread passes since the last call, and the timestamp-counter cycles the copy thread has
    // run so far (thread_cpu.h; 0 while stopped). Call from the thread that calls Start and Stop.
    int TakeWakeups() { return wakeups_.exchange(0); }
    uint64_t ThreadCycles();

private:
    void Run();
    int Copy(const GameFrame& f);
    void Look();
    void TakeNewDrawnPose();
    bool CheckCopies();

    FrameSource* source_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID3D11Query> done_[kMaxSlots];
    FrameCopy slots_[kMaxSlots];  // copy thread: the slots' textures (shared), and their views for thumbnails
    DrawnPoseFeed feed_;          // copy thread
    bool inFlight_[kMaxSlots] = {};
    GameFrame inFlightFrame_[kMaxSlots];
    bool copied_ = false;
    UINT loggedWidth_ = 0, loggedHeight_ = 0;  // the slot size last logged
    int next_ = 0;
    uint32_t generation_ = 0;
    UniqueHandle wake_;
    CallStats statLookTime_;                 // the drawn pose's time, for the 10 s stats (mutex_)
    std::atomic<int> wakeups_{0};            // copy-thread passes, for the 10 s stats
    std::atomic<int> wantSlots_{kMaxSlots};  // SetSlotCount
    int slotCount_ = kMaxSlots;              // copy thread: slots in use
    std::mutex mutex_;
    std::shared_ptr<DrawnPose> nextDrawnPose_;  // SetDrawnPose's, until the copy thread takes it
    bool hasNext_ = false;
    SlotInfo info_[kMaxSlots];
    std::thread thread_;
    std::atomic<bool> running_{false};
};

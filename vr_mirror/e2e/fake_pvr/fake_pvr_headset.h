// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The fake runtime's simulated headset and game (fake_pvr.cpp says what they mimic): the display slots,
// the head poses and their predictions, and the game thread that draws the client mirror's frames and
// sets its frame signal. Knows nothing about the PVR interface's function table.
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "common/rot.h"
#include "fake_game_motion.h"
#include "fake_game_picture.h"
#include "fake_pvr_replay.h"
#include "shared/unique_handle.h"
#include "vr_mirror/frame_sources/pimax/pvr_interface.h"

namespace fake_pvr {

    // Clock: QueryPerformanceCounter seconds, the same base the capture timestamps use.
    double Now();

    // Scenario configuration.
    struct Config {
        std::string motion = "still";
        double refreshHz = 90;
        double slotStepLeadMs = 1.0;      // the predicted slot steps this long before each vsync
        double arrivalPhaseMs = 0.2;      // frames arrive this long after a vsync, on average
        double arrivalJitterMs = 0.5;     // standard deviation of the arrival time
        double dropRate = 0;              // fraction of frames the game misses
        int width = 2508, height = 2084;  // client mirror size
        std::string log;
        std::string replay;  // replay file (fake_pvr_replay.h); replaces motion and frame timing
        // a replay's real thumbnails (fake_pvr_thumbs.h), shown where the tool makes its own
        std::string thumbs;
        double drawnFov = 1;    // fraction of each reported FOV tangent drawn; black beyond it
        double gameStartS = 0;  // seconds after the mirror is switched on before the game's first frame
        bool cockpit = false;   // draw a cockpit instead of the direction codes
        int queueLag = 0;       // the game falls up to this many slots behind (0: never)

        void Load();
    };

    // LMU on the Dream Air, as reported by the real runtime.
    inline const PvrFov kFov[2] = {{0.984f, 0.984f, 1.195f, 1.173f}, {0.984f, 0.984f, 1.173f, 1.195f}};
    inline const Quat kEyeCant[2] = {{0, 0.0175f, 0, 0.9998f}, {0, -0.0175f, 0, 0.9998f}};

    // The simulated headset + game.
    class Headset {
    public:
        bool Init();
        void Shutdown();

        LUID AdapterLuid() const { return luid_; }
        double Period() const { return period_; }

        // True head orientation at time t (no knowledge of the future).
        Quat TrueHead(double t) const;

        // What a runtime would report for absTime: the measured pose for the past/present,
        // a constant-angular-velocity extrapolation for the future. A display slot's prediction is made
        // once, from when the slot became the next one (when the game asks for it): asked again later
        // (the tool's poll thread, a fraction of a millisecond after), it is the same pose, as a runtime
        // that predicts per frame gives it and the replays show. Extrapolated afresh at each asking, the
        // pose a frame was drawn with and the tool's record of it differed in fast moves.
        Quat Tracked(double absTime) const;

        // Display slot two vsyncs ahead; it steps slotStepLead before each vsync.
        double PredictedDisplayTime() const;

        void StartGame();
        void StopGame();

        int64_t TextureHandle() const { return gameRunning_ ? int64_t(sharedHandle_) : 0; }
        int64_t EventHandle() const { return gameRunning_ ? int64_t(event_.Get()) : 0; }

        // Every call the tool makes into this runtime (the interface functions in fake_pvr.cpp count them).
        void CountCall() { calls_++; }

    private:
        // No game yet (game_start_s): waits that long after the mirror was switched on, and logs the
        // calls made from 2.5 s on. False if the mirror was switched off meanwhile.
        bool WaitForGame();

        bool CreateDevice();

        // Draws the frame seen from head orientation `head` (left eye), with the real thumbnail `tile`
        // of a replay if it has them, and waits for the GPU. `sceneTime`: the moment the game drew the
        // frame for (seconds from the start), for the cockpit's moving parts.
        void Render(const Quat& head, int tile = -1, double sceneTime = 0);

        // One frame per display slot: begin when the predicted slot steps (render with the pose
        // predicted for that slot), arrive about one frame later near the next vsync.
        void GameLoop();

        // The recorded game frames at their recorded arrival times, each rendered with the runtime's
        // recorded prediction for the slot it was really rendered for (assigned slot + lag).
        void ReplayLoop();

        // True head orientation at 1 kHz, for the runner's comparisons.
        void PoseLogLoop();

        Config config_;
        motion::Motion motion_;
        replay::Replay replay_;
        bool replaying_ = false;
        double period_ = 1.0 / 90, start_ = 0;
        LUID luid_ = {};
        FILE* log_ = nullptr;
        std::mutex logMutex_;
        std::atomic<bool> running_{true}, gameRunning_{false};
        std::atomic<long long> calls_{0};
        double mirrorOnAt_ = 0;  // when the tool switched the mirror on
        std::thread poseThread_, gameThread_;

        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv_;
        FakeGamePicture picture_;  // the game thread's
        Microsoft::WRL::ComPtr<ID3D11Query> done_;
        std::vector<uint8_t> thumbPixels_;  // a replay's real thumbnails, one 64x32 RGBA tile per frame
        int thumbTiles_ = 0;
        HANDLE sharedHandle_ = nullptr;
        UniqueHandle event_;
    };

}

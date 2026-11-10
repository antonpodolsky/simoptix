// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Fake PVR runtime (libPVRClient64.dll) for end-to-end tests of simoptix_vr_mirror.exe.
//
// Placed next to a copy of simoptix_vr_mirror.exe, Windows loads this DLL instead of the real
// Pimax client, so the real tool runs unchanged against a simulated headset and game.
// Everything mimics what was measured on a Pimax headset running Le Mans Ultimate:
//
//  - Display: 90 Hz. getPredictedDisplayTime(0) returns the display slot two vsyncs
//    ahead, stepping about 1 ms before each vsync.
//  - Game ("client mirror"): one RGBA8 sRGB texture of 2508x2084 (LMU's left eye at the
//    LMU render FOV and eye cant), shared by legacy D3D handle, plus a manual-reset sync
//    event. A frame begins when the display slot steps, renders with the head pose
//    predicted for that slot, and arrives (event set) about one frame later, near the next
//    vsync, with ~0.5 ms of timing noise.
//  - Image: each pixel's color encodes the world direction it shows (0.5 + 0.5 * dir),
//    stored sRGB-encoded so that the tool's gamma fix turns it back into the plain code.
//    The e2e runner reads these colors from the captured output to know exactly where the
//    stabilized view points. A replay can bring the game's real thumbnails (key `thumbs`):
//    shown where the tool makes its own, so its slot check sees the real images.
//    Key `drawn_fov` (below 1) draws black at the image's edges, inside the FOV it reports:
//    a game image smaller than the tool assumes, which shows as black borders.
//  - Head pose: from a motion spec (fake_game_motion.h). Queries for the future are extrapolated
//    with the current angular velocity, like a runtime's pose prediction.
//  - Or a replay of a real session (key `replay`, fake_pvr_replay.h): the recorded head poses, the
//    runtime's recorded predictions and display slots, and the game frames at their recorded
//    arrival times, each rendered with the prediction for the slot it was really rendered for.
//  - A cockpit (key `cockpit`): instead of the direction codes, a car's interior fixed in the
//    tracking space (a sim rig): a detailed roof, pillars and dashboard, a plain dark panel, a wheel
//    that turns with the steering, and the world outside the windows turning as the car corners.
// What the tool's cockpit correction measures
// (vr_mirror\core\pipeline\2_drawn_pose\cockpit_drawn_pose.h).
//  - The game behind (key `queue_lag`, with `drop_rate`): as LMU below 90 fps, the game misses
//    slots and queues frames: after a dropped slot it often stays a slot behind (up to `queue_lag`),
//    drawing each frame with the prediction for an earlier slot than the one it arrives for, and
//    catches up a slot now and then; its frames arrive ~4 ms later while behind (the shift of the slot
//    lead the tool's timing gate looks for).
//  - No game yet (key `game_start_s`): for that long after the tool switches the mirror on, the
//    mirror's texture and frame signal exist but no frames come and the signal stays low, as
//    measured on Pimax with no game running. The calls the tool makes into this runtime meanwhile
//    (from 2.5 s on, once the tool could have noticed) are logged when the game starts.
//
// Configuration comes from the file named by SIMOPTIX_FAKE_PVR_SCENARIO (key=value lines); a log of
// the true head motion and every rendered frame is written to the file named by key `log`.

#include <cstdint>
#include <cstring>
#include <memory>

#include "fake_pvr_headset.h"
#include "vr_mirror/frame_sources/pimax/pvr_interface.h"

using namespace fake_pvr;

namespace {

    std::unique_ptr<Headset> g_headset;
    int g_hmdToken = 0;

    // ---------------------------------------------------------------------------------------------
    // The PVR interface functions the tool uses.

    PvrResult Initialise() {
        if (g_headset) {
            return kPvrSuccess;
        }

        g_headset = std::make_unique<Headset>();
        if (!g_headset->Init()) {
            return kPvrFailed;
        }

        return kPvrSuccess;
    }

    void Shutdown() {
        if (g_headset) {
            g_headset->Shutdown();
        }

        g_headset.reset();
    }

    PvrResult CreateHmd(PvrHmd* hmd) {
        *hmd = &g_hmdToken;
        return kPvrSuccess;
    }

    void DestroyHmd(PvrHmd) {}

    double TimeSeconds() {
        if (g_headset) {
            g_headset->CountCall();
        }

        return Now();
    }

    PvrResult EyeDisplayInfo(PvrHmd, PvrEye, PvrDisplayInfo* out) {
        *out = {};
        out->adapterLuid = g_headset->AdapterLuid();
        out->refreshRate = float(1.0 / g_headset->Period());
        return kPvrSuccess;
    }

    PvrResult EyeRenderInfo(PvrHmd, PvrEye eye, PvrEyeRenderInfo* out) {
        const Quat& cant = kEyeCant[eye];

        *out = {};
        out->fov = kFov[eye];
        out->hmdToEye.orientation = {cant.x, cant.y, cant.z, cant.w};
        return kPvrSuccess;
    }

    PvrResult HmdStatus(PvrHmd, PvrHmdStatus* out) {
        g_headset->CountCall();

        *out = {};
        return kPvrSuccess;
    }

    PvrResult TrackingState(PvrHmd, double absTime, PvrTrackingState* out) {
        g_headset->CountCall();

        const Quat q = g_headset->Tracked(absTime);

        *out = {};
        out->head.pose.orientation = {q.x, q.y, q.z, q.w};
        out->head.statusFlags = kPvrOrientationTracked;
        return kPvrSuccess;
    }

    double PredictedDisplayTime(PvrHmd, int64_t) {
        g_headset->CountCall();
        return g_headset->PredictedDisplayTime();
    }

    PvrResult SetIntConfig(PvrHmd, const char* key, int value) {
        if (strcmp(key, "enable_client_mirror") != 0) {
            return kPvrSuccess;  // other keys: accepted and ignored
        }

        if (value) {
            g_headset->StartGame();
        } else {
            g_headset->StopGame();
        }

        return kPvrSuccess;
    }

    int64_t GetInt64Config(PvrHmd, const char* key, int64_t fallback) {
        g_headset->CountCall();

        if (strcmp(key, "client_mirror_handle") == 0) {
            return g_headset->TextureHandle();
        }

        if (strcmp(key, "client_mirror_sync_handle") == 0) {
            return g_headset->EventHandle();
        }

        return fallback;
    }

    // The tool calls no others: their entries stay null.
    PvrInterface* BuildInterface() {
        static PvrInterface functions = {};
        functions.initialise = Initialise;
        functions.shutdown = Shutdown;
        functions.createHmd = CreateHmd;
        functions.destroyHmd = DestroyHmd;
        functions.getTimeSeconds = TimeSeconds;
        functions.getEyeDisplayInfo = EyeDisplayInfo;
        functions.getEyeRenderInfo = EyeRenderInfo;
        functions.getHmdStatus = HmdStatus;
        functions.getTrackingState = TrackingState;
        functions.getPredictedDisplayTime = PredictedDisplayTime;
        functions.setIntConfig = SetIntConfig;
        functions.getInt64Config = GetInt64Config;
        return &functions;
    }

}

extern "C" __declspec(dllexport) PvrInterface* getPvrInterface(uint32_t, uint32_t) {
    return BuildInterface();
}

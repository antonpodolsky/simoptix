// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// DrawnPoseFeed: feeds the pipeline's drawn pose (stage 2, DrawnPose) the pictures it looks at,
// from the GPU. For every new Pimax game-eye frame it tells the drawn pose the frame arrived, makes
// what it needs of the frame's copy on the host's device (a small colour thumbnail, and patches of
// the eye for the frames it asks them for), reads them back once the GPU is done (never waiting,
// never flushing), and hands them over with the frame source's pose predictions. Thin glue: what the
// pictures mean is the drawn pose's business; a drawn pose that looks at no pictures gets
// nothing made. Knows nothing about windows, OBS, swap chains or where the frames come from.
//
// Use, on one thread (the one with the host's device context): SetDrawnPose, Init, then Submit
// after copying each new game frame, and Poll often (before drawing, or whenever Busy). Init again
// with a new device makes the pictures afresh on it; Release lets go of the device's. The output
// thread asks the drawn pose itself (the pipeline holds it too). A frame's own pictures are ready
// about a millisecond after its copy: a host that draws a few milliseconds after arrival (the OBS
// plugin) gets each frame's own answer; one that draws at once (the mirror app) gets the one from the
// frames before, unless it waits while Measuring.
#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "adapters/d3d/thumbnail.h"
#include "common/footprint.h"
#include "common/game_frame.h"
#include "pipeline/2_drawn_pose/drawn_pose.h"
#include "pipeline/stage_stats.h"

class DrawnPoseFeed {
public:
    // The drawn pose to feed (the pipeline's: Pipeline::SharedDrawnPose), kept across Init and
    // Reset; set again after Init, it starts with no history. Without one nothing is made.
    void SetDrawnPose(std::shared_ptr<DrawnPose> drawnPose);
    // On `device` (another one than before: the thumbnails and patches are made afresh on it).
    bool Init(ID3D11Device* device);
    // Forget the history (new session, other image source).
    void Reset();
    // Let go of everything made on the device (the host is about to release it); Init sets up again.
    void Release();

    // A new game frame `f` was copied to `image`; `eyeUv` is the eye's area in it (u0, v0, u1,
    // v1), encoded as `eye` says (EyeInfo::encodedTwice). The frame source's recorded pose
    // predictions, its eye and the display's period (s) say where a frame's patches are made.
    void Submit(
        ID3D11DeviceContext* context,
        ID3D11ShaderResourceView* image,
        const float eyeUv[4],
        const GameFrame& f,
        const PoseLookup& predictions,
        const EyeInfo& eye,
        double period
    );

    // Reads finished pictures and hands them to the drawn pose.
    void Poll(ID3D11DeviceContext* context, const PoseLookup& predictions, const EyeInfo& eye, double period);

    // Whether frame `serial`'s patches are still waiting for the GPU (the drawn pose looks at them
    // once they are read; Poll).
    bool Measuring(uint64_t serial) const;
    // True while pictures are waiting for the GPU: Poll again within about a millisecond.
    bool Busy() const { return pendingCount_ > 0; }
    // The time the drawn pose took looking at frames with patches on this thread, since the last
    // call (for 10 s stats).
    CallStats TakeLookTime() {
        const CallStats t = lookTime_;
        lookTime_ = {};
        return t;
    }

    // That time as a line of the shared report ("looking at pictures with patches"), when any.
    static void ReportLookTime(const CallStats& time, StatsReport& r);

private:
    struct Pending {
        int64_t ticket = -1;       // its thumbnail (-1: none wanted)
        int64_t patchTicket = -1;  // its patches, if the drawn pose asked for them
        GameFrame frame;
    };

    static constexpr int kPending = 6;

    // Sets up the thumbnails and patches the drawn pose needs; false if they can't be made.
    bool InitPictures();

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Thumbnailer thumbs_;
    Thumbnailer patches_;  // the drawn pose's patches (PictureNeeds::patches), if it asks for any
    PictureNeeds needs_;   // the drawn pose's (SetDrawnPose)
    Pending pending_[kPending];
    int pendingHead_ = 0, pendingCount_ = 0;
    uint64_t lastSubmitted_ = 0;      // the same frame can be drawn more than once
    std::vector<float> cur_;          // the thumbnail being read
    std::vector<float> patchesRead_;  // the patches being read
    std::vector<float> areas_;        // the patches' areas for a frame (4 floats each; InitPictures)
    const std::vector<float> none_;   // no picture
    CallStats lookTime_;              // Look's time on this thread for frames with patches (10 s stats)
    std::shared_ptr<DrawnPose> drawnPose_;
};

// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// RecordingPlayback: what fake_openxr shows for auto sync's e2e test: a cut of a real recording of LMU's
// menus (vr_mirror\recording_analysis\export_menu_cut.py), played back on a plan. The cut holds every
// frame's time on the recording's clock, the camera it was drawn from, and its grey image, which is read
// from disk when shown.
//
// The plan (a file of "menu,<from>,<to>,<cut moment at from>" lines, seconds from the plan's start, which
// Begin gives) says which stretch of the cut plays when. Until then, the first frame stays. Between
// stretches, the last frame stays. A restart (Restart) stops the game's frames for a while, as when the game
// restarts, and then plays the plan's first stretch again. Each frame is drawn from its camera: the eye looks
// where the recording's camera did, so what was fixed in tracking space in the game (the menu) stays fixed
// for VR Mirror too.
//
// Knows nothing about OpenXR or the frames' timing beyond the one asked for. Begin, Restart, Paused, Head
// and Draw: one thread.
#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "common/rot.h"

class RecordingPlayback {
public:
    struct Settings {
        std::string menuCut, plan;  // the cut's folder, and the plan's file
    };

    // The cut and the plan, and what draws them into `width` x `height` eye images; false, saying why on
    // stderr, if either can't be read.
    bool Init(const Settings& settings, ID3D11Device* device, int width, int height);

    // The plan starts at `at` (performance-counter seconds).
    void Begin(double at) { start_ = at; }

    // From `at` (performance-counter seconds), no frames for `pauseS`, then the plan's first stretch from its
    // start, played to the end of the cut.
    void Restart(double at, double pauseS);

    // Whether a restart pauses the game's frames at `target` (performance-counter seconds): no frame then.
    bool Paused(double target) const;

    // The frame to show in the image drawn for `target` (performance-counter seconds), chosen for Draw, and
    // the head pose to draw it with: the camera it was recorded from, less the eye's turn `eyeTurn`.
    Quat Head(double target, const Quat& eyeTurn);

    // The frame Head chose, into `target`, for an eye whose field of view has the tangents `leftTan` to
    // `downTan`: the frame covers its own field of view around the eye's forward, black beyond.
    void Draw(
        ID3D11DeviceContext* context,
        ID3D11RenderTargetView* target,
        float leftTan,
        float rightTan,
        float upTan,
        float downTan
    );

private:
    struct Frame {
        double time = 0;  // on the recording's clock
        Quat camera;
    };

    struct Stretch {
        bool pause = false;
        double from = 0, to = 0, cutFrom = 0;
    };

    bool LoadCut(const std::string& folder);
    bool LoadPlan(const std::string& path);
    bool InitDrawing(ID3D11Device* device);
    int FrameAt(double moment) const;  // the latest frame at or before `moment`, or the first

    Settings settings_;
    double start_ = std::numeric_limits<double>::infinity();  // the plan's (performance-counter seconds)
    int width_ = 0, height_ = 0;                              // the eye images
    int cutWidth_ = 0, cutHeight_ = 0;                        // the cut's frames
    float tanW_ = 1, tanH_ = 1;  // the tangents of half the cut's frames' width and height
    std::vector<Frame> frames_;  // oldest first
    std::string gray_;           // the file of the frames' images, one after another
    std::vector<Stretch> plan_;
    int shownFrame_ = 0;    // the frame Head chose
    int loadedFrame_ = -1;  // the frame in the texture
    std::vector<uint8_t> image_;

    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> cbuffer_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view_;
};

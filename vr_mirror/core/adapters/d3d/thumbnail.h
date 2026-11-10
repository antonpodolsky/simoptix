// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Thumbnailer: tiny greyscale or colour versions of images (64x32 by default), or of several areas
// of an image side by side (tiles), read back to the CPU, so two images can be compared by content.
// DrawnPoseFeed makes the images a drawn pose looks at with it: the thumbnails the slot check
// tells from which display slot a game frame was rendered for (how its image moved), and the
// cockpit patches the cockpit correction measures.
//
// Make() draws a thumbnail on the GPU and queues its readback; Read() returns it once the GPU
// has finished (it never waits, and never submits work itself: the host's context must be
// flushed after Make, e.g. once per pass), typically a millisecond later.
#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

class Thumbnailer {
public:
    static constexpr int kWidth = 64, kHeight = 32;

    // color: thumbnails with red, green and blue per texel (Read returns 3 floats per texel,
    // row by row) instead of luminance. width, height: the image read back, in texels; samples:
    // each texel is the average of samples x samples samples across its footprint.
    bool
    Init(ID3D11Device* device, bool color = false, int width = kWidth, int height = kHeight, int samples = 8);

    int Channels() const { return color_ ? 3 : 1; }
    int Width() const { return width_; }
    int Height() const { return height_; }
    bool Ready() const { return ps_ != nullptr; }

    // Thumbnail of the area `uv` (u0, v0, u1, v1; 0..1, top-left origin) of `src`, as luminance
    // (or color, see Init).
    // decode: undo one extra sRGB encoding first (the Pimax game eye). Returns a ticket for Read,
    // or -1.
    int64_t Make(ID3D11DeviceContext* context, ID3D11ShaderResourceView* src, const float uv[4], bool decode);
    // Several areas `areas` (count of them, as above) side by side, each a tile of `tile` x `tile`
    // texels (area i in column i % columns, row i / columns). Returns a ticket for Read, or -1.
    int64_t MakeTiles(
        ID3D11DeviceContext* context,
        ID3D11ShaderResourceView* src,
        const float (*areas)[4],
        int count,
        int columns,
        int tile,
        bool decode
    );

    // The thumbnail for `ticket` once the GPU is done (false: not yet, or too old to still exist).
    bool Read(ID3D11DeviceContext* context, int64_t ticket, std::vector<float>* out);

    // Zero-mean normalized cross-correlation of two thumbnails: 1 = same image (up to
    // brightness and contrast), around 0 = unrelated.
    static float Similarity(const std::vector<float>& a, const std::vector<float>& b);

private:
    static constexpr int kSlots = 32;
    // Draws area `uv` into the texels from (x, y), `w` x `h` of them.
    void Draw(ID3D11DeviceContext* context, const float uv[4], int x, int y, int w, int h, bool decode);
    // Queues the readback of what was drawn; its ticket.
    int64_t Finish(ID3D11DeviceContext* context);

    bool color_ = false;
    int width_ = kWidth, height_ = kHeight;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> target_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> targetView_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_[kSlots];
    // One "GPU done" marker per slot: checking it costs nothing, whereas trying to map an
    // unfinished staging texture can make the runtime submit work on every attempt.
    Microsoft::WRL::ComPtr<ID3D11Query> done_[kSlots];
    int64_t next_ = 0;
};

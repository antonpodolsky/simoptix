// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// FrameHold: the mirror app's held game frames for Look-ahead smoothing (setting smoothing 2): a
// FIFO of copies. Each new frame is copied as it arrives (the game reuses its buffer about a frame
// later); the frame picker (vr_mirror\core\pipeline\3_frame_picker: HeldFramePicker) says when the
// oldest is shown. Knows nothing about windows, swap chains, timing or the pipeline.
#pragma once

#include <d3d11.h>

#include <vector>

#include "adapters/d3d/frame_copy.h"
#include "adapters/d3d/frame_source.h"

class FrameHold {
public:
    struct Held {
        FrameCopy copy;
        GameFrame frame;  // as it arrived
    };

    // Room for `frames` held frames (their copies are made as frames arrive). Clears the hold.
    void Resize(int frames);
    int Capacity() const { return int(ring_.size()); }
    // Copies frame `f`'s picture from `source`. Null (the frame is dropped) if the hold is full or the
    // picture can't be copied.
    Held* Add(ID3D11Device* device, ID3D11DeviceContext* context, FrameSource& source, const GameFrame& f);
    int Count() const { return count_; }
    // The k-th held frame, oldest first (k < Count()).
    const Held& At(int k) const { return ring_[(head_ + k) % ring_.size()]; }
    Held* Front() { return count_ ? &ring_[head_] : nullptr; }
    void Pop();

private:
    std::vector<Held> ring_;
    int head_ = 0, count_ = 0;
};

// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "frame_hold.h"

#include <algorithm>

void FrameHold::Resize(int frames) {
    ring_.resize(size_t(std::max(1, frames)));
    head_ = count_ = 0;
}

FrameHold::Held*
FrameHold::Add(ID3D11Device* device, ID3D11DeviceContext* context, FrameSource& source, const GameFrame& f) {
    if (count_ == int(ring_.size())) {
        return nullptr;
    }
    Held& h = ring_[(head_ + count_) % ring_.size()];
    if (!source.CopyImage(device, context, f, &h.copy, false)) {
        return nullptr;
    }
    h.frame = f;
    count_++;
    return &h;
}

void FrameHold::Pop() {
    if (!count_) {
        return;
    }
    head_ = (head_ + 1) % int(ring_.size());
    count_--;
}

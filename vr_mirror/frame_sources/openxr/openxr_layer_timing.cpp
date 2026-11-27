// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "openxr_layer_timing.h"

namespace {

    OpenXrFrameTiming g_timing;
    bool g_started = false;  // the game's thread that makes and destroys sessions

}

void TimingOnReader(OpenXrFrameTiming::ReaderNews news) {
    g_timing.SetReaderNews(news);
}

void TimingSessionCreated(const XrSessionCreateInfo& info) {
    ID3D11Device* device = nullptr;

    for (auto* s = static_cast<const XrBaseInStructure*>(info.next); s; s = s->next) {
        if (s->type == XR_TYPE_GRAPHICS_BINDING_D3D11_KHR) {
            device = reinterpret_cast<const XrD3d11Binding*>(s)->device;
        }
    }

    g_started = device && g_timing.Start(device);
}

void TimingSessionDestroyed() {
    g_timing.Stop();
    g_started = false;
}

void TimingWaitCalled() {
    g_timing.WaitCalled();
}

void TimingWaitReturned(XrDuration period) {
    g_timing.WaitReturned(period);
}

void TimingFrameBegun() {
    g_timing.FrameBegun();
}

void TimingFrameEnding() {
    g_timing.FrameEnding();
}

void TimingFrameEnded(bool succeeded) {
    g_timing.FrameEnded(succeeded);
}

bool TimingTakeStats(OpenXrFrameTimingStats* stats) {
    if (!g_started) {
        return false;
    }

    *stats = g_timing.TakeStats();
    return true;
}

// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// What the layer hands the performance overlay of the game's frames: the frame calls passed on to the
// frame timing (openxr_frame_timing.h), which times them only while the overlay reads. Only for a D3D11
// session. Unlike the frames for VR Mirror, it needs no time conversion.
//
// Called from openxr_layer_calls.cpp around each call; TimingWaitCalled and TimingFrameEnding before the
// call goes on, the others after it returned. It doesn't know about the loader, the log file, or which
// game this is.
#pragma once

#include "openxr_d3d11.h"
#include "openxr_frame_timing.h"

// The game's thread.
void TimingSessionCreated(const XrSessionCreateInfo& info);
void TimingSessionDestroyed();

void TimingWaitCalled();
void TimingWaitReturned(XrDuration period);
void TimingFrameBegun();
void TimingFrameEnding();
void TimingFrameEnded(bool succeeded);

// The timing's numbers since the last call; false while it isn't running.
bool TimingTakeStats(OpenXrFrameTimingStats* stats);

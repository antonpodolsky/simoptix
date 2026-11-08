// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// What the layer hands VR Mirror of the game's frames: it keeps track of the game's swapchain images
// (which texture each is, which one the game released last), and at the end of each frame finds the
// projection layer's left eye and passes it to the frame publisher (openxr_publisher.h) with the frame's
// display time and both eyes' poses and fields of view. Only for a D3D11 session, and only when the
// runtime's time conversion is there (the reader can't place the frames otherwise).
//
// Called from openxr_layer_calls.cpp after each call went on to the runtime, with what the game passed and
// the runtime returned. Nothing here locks or waits: the swapchain table is fixed in size, and OpenXR
// asks the game itself to keep one swapchain's calls on one thread at a time.
// It doesn't know about the loader, the log file, or which game this is.
#pragma once

#include <cstdint>

#include "openxr_d3d11.h"
#include "openxr_publisher.h"

// The game's thread. `counterToTime`: the runtime's time conversion, or nullptr if the instance lacks it.
void FramesSessionCreated(
    XrInstance instance,
    const XrSessionCreateInfo& info,
    PFN_XrCounterToTime counterToTime
);

void FramesSessionDestroyed();
void FramesSwapchainCreated(XrSwapchain swapchain, const XrSwapchainCreateInfo& info);
void FramesSwapchainDestroyed(XrSwapchain swapchain);
// What xrEnumerateSwapchainImages filled in (`count` images of the type in `images`).
void FramesSwapchainImages(XrSwapchain swapchain, uint32_t count, const XrSwapchainImageBaseHeader* images);
void FramesImageAcquired(XrSwapchain swapchain, uint32_t index);
void FramesImageReleased(XrSwapchain swapchain);
// From xrWaitFrame.
void FramesDisplayPeriod(XrDuration period);
// After xrEndFrame went on and succeeded; `endFrameCounter`: the performance counter then.
void FramesEnded(const XrFrameEndInfo& info, int64_t endFrameCounter);
// The publisher's numbers since the last call; false while it isn't running.
bool FramesTakeStats(OpenXrPublisherStats* stats);

// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The OpenXR calls the layer looks at inside the game. It logs the runtime and headset the game uses,
// its sessions, every swapchain it makes, and the layout of the frames it hands the headset (once, then
// on each change), with the game's frame rate and the frame source's numbers every 10 s. Every call
// first goes on unchanged to the next layer or the runtime; the layer then only reads what the game
// passed and the runtime returned, and hands it to the frame source (openxr_layer_frames.h), which copies the
// left eye's image for VR Mirror without ever making the game wait.
// It doesn't know how the layer was loaded, or whether this game is the one to look at
// (openxr_layer_entry.cpp decides that).
#pragma once

#define XR_NO_PROTOTYPES
#include "openxr/openxr.h"

// Right after the game's instance is made, on the game's thread, before the game can make any other
// call: finds the next layer's (or the runtime's) functions and logs which runtime and API this is.
void StartCalls(XrInstance instance, PFN_xrGetInstanceProcAddr next, const XrInstanceCreateInfo& info);

// The layer's own function for the OpenXR call `name`, or nullptr for a call it doesn't look at.
PFN_xrVoidFunction LayerCall(const char* name);

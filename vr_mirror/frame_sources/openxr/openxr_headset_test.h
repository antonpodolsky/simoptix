// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The headset test: the headset as OpenXR shows it to a game, read with no game running by being one for
// a moment. The runtime the registry names (HKLM\SOFTWARE\Khronos\OpenXR\1, ActiveRuntime: its manifest,
// whose library_path is the runtime) is loaded and called directly, without the OpenXR loader, so no API
// layer (the SimOptix layer among them) is involved. A D3D11 session shows a second of frames: black,
// with "Collecting headset info" straight ahead. The frames' views give the left eye's field of view and
// its turn from the head, their timing the refresh period, and the view configuration the recommended
// image size, which with Pimax OpenXR is the size the game then renders at (5084 x 4168, measured). Then
// the session ends and the runtime goes back to its own home.
//
// Without a session OpenXR tells only the image size. The session takes the headset for its second, so it
// is only run with no game in VR. The host's control thread. Knows nothing about the layer, the frames or
// the hosts.
#pragma once

#include "adapters/d3d/frame_source.h"

// Runs the test: about 1.5 s with a headset. It gives up after 5 s (the session not ready, or its views
// never read) and when the runtime refuses a frame, and is false then, as with no runtime or headset.
bool RunHeadsetTest(HeadsetReport* out);

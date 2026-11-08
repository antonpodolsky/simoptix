// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// VR Mirror's settings, shared by every host. Hosts load, save and show them by walking
// VisitSettings, so a setting added here appears everywhere with the same key.
#pragma once

struct MirrorSettings {
    // How the view follows the head (vr_mirror\core\pipeline\4_smoothing\): 0 off (the headset's
    // view as it is), 1 Instant (a filter, no delay), 2 Smooth (the picture runs half a second behind
    // the game, and the smoothing sees that far ahead), 3 Smoothest (a second).
    int smoothing = 3;
    float zoom = 1.5f;  // crop factor; the spare image around the view is how far it may trail the head
    // The drawn pose stage's method (vr_mirror\core\pipeline\2_drawn_pose\): 0 off, every frame keeps
    // the slot its arrival timing gives it; 1 the slot check: judge from small pictures whether each game
    // frame was rendered for an earlier slot than its arrival says (the game had frames queued); 2 the
    // slot check plus the cockpit correction: while frames arrive irregularly, each frame's pose is also
    // corrected by how far its fixed cockpit moved against the frame before.
    int drawnPose = 1;
};

// The view is stabilized at all (smoothing 1 to 3).
inline bool Stabilizes(const MirrorSettings& s) {
    return s.smoothing > 0;
}

// Calls f(key, value&) for every setting; value is an int& or float&.
template <class F>
void VisitSettings(MirrorSettings& s, F&& f) {
    f("smoothing", s.smoothing);
    f("zoom", s.zoom);
    f("drawn_pose", s.drawnPose);
}

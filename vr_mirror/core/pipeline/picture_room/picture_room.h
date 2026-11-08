// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Picture room: how much room a view has inside a game picture before the output would look past it
// and show black, and the stop at the picture's edge. Stateless geometry the smoothings call while they
// choose the view (Instant's catch-up and its last resort, the look-ahead's planning and its last
// resort); beside the stages, not one of them. Logic only. Knows nothing about smoothing, frames,
// timing, PVR or D3D.
#pragma once

#include "pipeline/pipeline_types.h"

// How far a view trails a picture's pose (lag) and how far it could trail in that direction before
// the output would look past the picture (room), both in radians; room 0: no room at all.
struct EdgeRoom {
    float lag = 0, room = 0;
    Vec3 axis;  // direction of the lag (head coordinates)
};

// The view trailing `picture` by `angle` (radians) about `axis` (head coordinates): where a view
// that keeps a share of its room ends up (EdgeRoom::axis).
inline Quat TrailingView(const Quat& picture, Vec3 axis, float angle) {
    const float sh = std::sin(angle / 2);
    return Normalize(Mul(picture, Quat{axis.x * sh, axis.y * sh, axis.z * sh, std::cos(angle / 2)}));
}

// What the stop at the edge did to a view: `view` is the view to show. stopped: it had to act;
// movedDeg: how far it moved the view (degrees, 0 when it didn't act); keep: the fraction (0..1; 1:
// no stop) of the view's lag behind the picture it kept, so a smoothing with hidden state can pull
// that state along.
struct EdgeStop {
    Quat view;
    bool stopped = false;
    float movedDeg = 0;
    float keep = 1;
};

// The share of its room the stop holds a view at. Not measured: it leaves 5% between the stop and the
// edge (which itself keeps 0.5% clear of the picture's last texels).
constexpr float kStopShare = 0.95f;

// How far `view` trails the picture drawn at `picture`, and its room there.
EdgeRoom RoomIn(const Quat& picture, const Quat& view, const ViewGeometry& g);

// Whether `view` stays within `fraction` (0..1) of its room in the picture drawn at `picture`; when
// not, its room there (`room`). A quick check comes first: the room is searched only near the edge.
bool Within(const Quat& picture, const Quat& view, float fraction, const ViewGeometry& g, EdgeRoom* room);

// The last resort, for a smoothing whose own means didn't keep its view inside (an extreme head flick):
// holds the view at kStopShare of its room in the picture drawn at `picture` (the one shown with it),
// so the output never shows black.
EdgeStop StopAtEdge(const Quat& picture, const Quat& view, const ViewGeometry& g);

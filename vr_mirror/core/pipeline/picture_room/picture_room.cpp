// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "pipeline/picture_room/picture_room.h"

#include <algorithm>
#include <cmath>

namespace {

    // Whether the whole output, rotated by `rm` into the eye camera, lands inside the eye image.
    // A rotation maps straight lines to straight lines in tangent space, so the output rectangle
    // maps to a quadrilateral, and that lies inside the (convex) image iff its corners do.
    bool Fits(const float rm[3][3], const ViewGeometry& g) {
        const float tw = g.outW, th = g.outH, L = g.left, R = g.right, U = g.up, D = g.down;
        const float kSafety = 0.995f;  // keep linear filtering clear of the image's last texels
        for (int corner = 0; corner < 4; corner++) {
            float x = corner & 1 ? tw : -tw, y = corner & 2 ? th : -th;
            const float sx = rm[0][0] * x + rm[0][1] * y - rm[0][2];
            const float sy = rm[1][0] * x + rm[1][1] * y - rm[1][2];
            const float sz = rm[2][0] * x + rm[2][1] * y - rm[2][2];
            if (sz > -1e-4f) {
                return false;
            }
            float tx = sx / -sz, ty = sy / -sz;
            if (tx < -L * kSafety || tx > R * kSafety || ty < -D * kSafety || ty > U * kSafety) {
                return false;
            }
        }

        return true;
    }

}

EdgeRoom RoomIn(const Quat& picture, const Quat& view, const ViewGeometry& g) {
    EdgeRoom r;
    Quat delta = Normalize(Mul(Conj(picture), view));  // picture -> view, in head coordinates
    if (delta.w < 0) {
        delta = {-delta.x, -delta.y, -delta.z, -delta.w};
    }
    r.lag = 2 * std::acos(std::min(1.0f, delta.w));
    const float sinHalf = std::sqrt(std::max(0.0f, 1 - delta.w * delta.w));
    const float kSearch = 60 * 3.14159265f / 180;
    if (r.lag < 1e-5f || sinHalf < 1e-6f) {
        r.room = kSearch;
        return r;
    }

    r.axis = {delta.x / sinHalf, delta.y / sinHalf, delta.z / sinHalf};
    auto fits = [&](float angle) {
        float rm[3][3];
        ViewToEye(picture, TrailingView(picture, r.axis, angle), g, rm);
        return Fits(rm, g);
    };

    // The largest lag in this direction that still fits (searched up to 60 degrees).
    r.room = kSearch;
    if (!fits(kSearch)) {
        if (!fits(0)) {
            r.room = 0;
            return r;
        }

        float lo = 0, hi = kSearch;
        for (int i = 0; i < 18; i++) {
            const float mid = 0.5f * (lo + hi);
            (fits(mid) ? lo : hi) = mid;
        }

        r.room = lo;
    }

    return r;
}

bool Within(const Quat& picture, const Quat& view, float fraction, const ViewGeometry& g, EdgeRoom* room) {
    Quat d = Normalize(Mul(Conj(picture), view));
    if (d.w < 0) {
        d = {-d.x, -d.y, -d.z, -d.w};
    }
    const float lag = 2 * std::acos(std::min(1.0f, d.w));
    const float sinHalf = std::sqrt(std::max(0.0f, 1 - d.w * d.w));
    if (lag < 1e-4f || sinHalf < 1e-6f) {
        return true;
    }

    // Quick check first (one fit instead of a search): still inside if trailing by lag / fraction.
    const Vec3 axis = {d.x / sinHalf, d.y / sinHalf, d.z / sinHalf};
    float rm[3][3];
    ViewToEye(picture, TrailingView(picture, axis, lag / fraction), g, rm);
    if (Fits(rm, g)) {
        return true;
    }
    *room = RoomIn(picture, view, g);
    return room->lag <= fraction * room->room;
}

EdgeStop StopAtEdge(const Quat& picture, const Quat& view, const ViewGeometry& g) {
    EdgeStop s;
    s.view = view;
    const EdgeRoom r = RoomIn(picture, view, g);
    if (!(r.lag > kStopShare * r.room)) {
        return s;
    }
    const float allowed = kStopShare * r.room;
    s.stopped = true;
    s.keep = r.lag > 0 ? allowed / r.lag : 0;
    s.view = TrailingView(picture, r.axis, allowed);

    // How far the stop moved the view (2 atan2(|v|, |w|): exact for small angles, unlike acos of the
    // dot product).
    const Quat d = Mul(Conj(view), s.view);
    s.movedDeg = float(
        2 * std::atan2(std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z), std::fabs(d.w)) * 180 / 3.14159265358979
    );

    return s;
}

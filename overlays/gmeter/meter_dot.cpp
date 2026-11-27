// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "meter_dot.h"

#include <algorithm>
#include <cmath>

namespace {

    constexpr float kMaxG = 3;

    // Colours from the design, 0..1 RGB.
    constexpr float kAccent[3] = {1.0f, 0.353f, 0.122f};  // #ff5a1f: the dot and its glow
    // #ff6b1f: the tail, a little warmer than the dot so the dot stands apart from it (in the dot's colour
    // the newest part of the tail read as part of the dot; in the design's amber, #ffb020, it stood out
    // too much, and 40 % of the way there, #ff7c1f, still a bit). 20 % of the way from the accent to that
    // amber.
    constexpr float kTail[3] = {1.0f, 0.420f, 0.122f};

    // The comet tail's 5 age bands, newest first.
    constexpr float kTailAlpha[5] = {0.55f, 0.35f, 0.20f, 0.09f, 0.02f};

    // The dot's comet tail, behind the dot of radius dotR.
    void
    AddTail(const MeterView& m, const MeterDial& d, float dotR, MeterBatch& batch, std::vector<float>& v) {
        const Trail& trail = *m.trail;

        // Comet tail: a band that narrows with age (0.9 of the dot's radius when new, 0 at the tail's
        // end), with a ball at each joint, in the design's 5 age bands.
        const float length = std::max(0.01f, m.trailS);
        for (int band = 4; band >= 0; band--) {  // oldest (faintest) first
            v.clear();
            for (size_t i = 1; i < trail.size(); i++) {
                const float age = static_cast<float>(std::min(1.0, (m.now - trail[i].t) / length));
                if (std::min(4, static_cast<int>(age * 5)) != band) {
                    continue;
                }
                const float age0 = static_cast<float>(std::min(1.0, (m.now - trail[i - 1].t) / length));

                float x0, y0, x1, y1;
                d.ToScreen(trail[i - 1].x, trail[i - 1].y, &x0, &y0);
                d.ToScreen(trail[i].x, trail[i].y, &x1, &y1);

                const float r0 = dotR * (1 - age0) * 0.9f, r1 = dotR * (1 - age) * 0.9f;
                AddTaper(v, x0, y0, r0, x1, y1, r1);
                if (r1 > 0.5f) {
                    AddDisc(v, x1, y1, r1);
                }
            }

            batch.Add(v, kTail[0], kTail[1], kTail[2], kTailAlpha[band]);
        }
    }

}

void MeterDial::ToScreen(float x, float y, float* sx, float* sy) const {
    const float len = std::sqrt(x * x + y * y);
    if (len > kMaxG) {
        x *= kMaxG / len, y *= kMaxG / len;  // pinned at the 3G ring
    }
    *sx = cx + x * scale;
    *sy = cy - y * scale;  // forward is up
}

void AddMeterDot(const MeterView& m, const MeterDial& d, MeterBatch& batch, std::vector<float>& v) {
    const bool live = m.live;

    // The glow first, so the tail crosses it in its own colour; without telemetry, the dot and glow
    // are dimmed at the centre.
    float dx = d.cx, dy = d.cy;
    if (live) {
        d.ToScreen(m.gx, m.gy, &dx, &dy);
    }
    const float dim = live ? 1.0f : 0.35f;
    v.clear();
    AddBlurredDisc(v, dx, dy, 19.2f * d.k, 6.4f * d.k);
    batch.Add(v, kAccent[0], kAccent[1], kAccent[2], 0.5f * dim);

    const float dotR = kDotR * d.k;
    if (live && m.trail->size() > 1) {
        AddTail(m, d, dotR, batch, v);
    }

    v.clear();
    AddDisc(v, dx, dy, dotR);
    batch.Add(v, kAccent[0], kAccent[1], kAccent[2], dim);
}

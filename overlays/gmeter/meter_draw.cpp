// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "meter_draw.h"

#include <graphics/vec3.h>
#include <graphics/vec4.h>
#include <obs-module.h>

#include <algorithm>
#include <cmath>

namespace {

    constexpr float kPi = 3.14159265f;
    constexpr float kFeather = 1.0f;  // soft edge width (source pixels)

    int Segments(float r, int lo, int hi) {
        return std::max(lo, std::min(hi, int(2 * kPi * r / 4)));
    }

    uint32_t Pack(float r, float g, float b, float a) {
        auto byte = [](float v) { return uint32_t(std::max(0.0f, std::min(1.0f, v)) * 255.0f + 0.5f); };
        return byte(a) << 24 | byte(b) << 16 | byte(g) << 8 | byte(r);
    }

    // An arc's centre and the radii of its edges: the inner soft edge from rad[0] to rad[1], the core to
    // rad[2], the outer soft edge to rad[3].
    struct ArcBand {
        float cx, cy;
        float rad[4];
    };

    // A point along an arc: its angle (radians, clockwise from up) and the arc's alpha there.
    struct ArcStop {
        float t, alpha;
    };

    // The stretch of `band` from one stop to the next, with alpha going linearly between theirs.
    void AddArcStretch(std::vector<float>& v, const ArcBand& band, ArcStop from, ArcStop to) {
        constexpr float kAcross[4] = {0, 1, 1, 0};
        const float span = to.t - from.t;

        // A segment every 4 px along the outer edge.
        const int n = std::max(1, static_cast<int>(std::fabs(span) * band.rad[3] / 4));

        for (int i = 0; i < n; i++) {
            const float f0 = static_cast<float>(i) / n, f1 = static_cast<float>(i + 1) / n;
            const float t0 = from.t + span * f0, t1 = from.t + span * f1;
            const float along0 = from.alpha + (to.alpha - from.alpha) * f0;
            const float along1 = from.alpha + (to.alpha - from.alpha) * f1;

            // Clockwise from up on screen (y down): angle t is at (sin t, -cos t).
            const float s0 = std::sin(t0), c0 = std::cos(t0), s1 = std::sin(t1), c1 = std::cos(t1);
            for (int b = 0; b < 3; b++) {
                const float ri = band.rad[b], ro = band.rad[b + 1];
                AddQuadA(
                    v,
                    band.cx + ri * s0,
                    band.cy - ri * c0,
                    kAcross[b] * along0,
                    band.cx + ro * s0,
                    band.cy - ro * c0,
                    kAcross[b + 1] * along0,
                    band.cx + ro * s1,
                    band.cy - ro * c1,
                    kAcross[b + 1] * along1,
                    band.cx + ri * s1,
                    band.cy - ri * c1,
                    kAcross[b] * along1
                );
            }
        }
    }

}

void AddTri(
    std::vector<float>& v,
    float x0,
    float y0,
    float a0,
    float x1,
    float y1,
    float a1,
    float x2,
    float y2,
    float a2
) {
    const float t[] = {x0, y0, a0, x1, y1, a1, x2, y2, a2};
    v.insert(v.end(), t, t + 9);
}

void AddQuadA(
    std::vector<float>& v,
    float x0,
    float y0,
    float a0,
    float x1,
    float y1,
    float a1,
    float x2,
    float y2,
    float a2,
    float x3,
    float y3,
    float a3
) {
    AddTri(v, x0, y0, a0, x1, y1, a1, x2, y2, a2);
    AddTri(v, x0, y0, a0, x2, y2, a2, x3, y3, a3);
}

void AddTaper(std::vector<float>& v, float x0, float y0, float h0, float x1, float y1, float h1) {
    float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-3f) {
        return;
    }
    float nx = -dy / len, ny = dx / len;
    AddQuadA(
        v,
        x0 + nx * h0,
        y0 + ny * h0,
        1,
        x1 + nx * h1,
        y1 + ny * h1,
        1,
        x1 - nx * h1,
        y1 - ny * h1,
        1,
        x0 - nx * h0,
        y0 - ny * h0,
        1
    );

    for (float side : {1.0f, -1.0f}) {
        float sx = nx * side, sy = ny * side;
        AddQuadA(
            v,
            x0 + sx * h0,
            y0 + sy * h0,
            1,
            x1 + sx * h1,
            y1 + sy * h1,
            1,
            x1 + sx * (h1 + kFeather),
            y1 + sy * (h1 + kFeather),
            0,
            x0 + sx * (h0 + kFeather),
            y0 + sy * (h0 + kFeather),
            0
        );
    }
}

void AddArc(std::vector<float>& v, float cx, float cy, float r, float w, float a0, float a1) {
    const float t0 = a0 * kPi / 180, t1 = a1 * kPi / 180, span = t1 - t0;
    const float half = w * 0.5f, soft = kFeather * 0.5f;
    const float core = std::max(0.0f, half - soft), edge = half + soft;
    const ArcBand band = {cx, cy, {std::max(0.0f, r - edge), r - core, r + core, r + edge}};

    // The ends' soft edges, as angles: centred on the ends, meeting in the middle of an arc shorter than
    // them.
    const float dir = span >= 0 ? 1.0f : -1.0f;
    const float endSoft = dir * std::min(soft / r, std::fabs(span) * 0.5f);
    const ArcStop stops[4] = {{t0 - endSoft, 0}, {t0 + endSoft, 1}, {t1 - endSoft, 1}, {t1 + endSoft, 0}};

    for (int i = 0; i < 3; i++) {
        AddArcStretch(v, band, stops[i], stops[i + 1]);
    }
}

void AddDisc(std::vector<float>& v, float cx, float cy, float r) {
    const int n = Segments(r, 16, 128);
    const float core = std::max(0.0f, r - kFeather * 0.5f), edge = r + kFeather * 0.5f;
    for (int i = 0; i < n; i++) {
        const float c0 = std::cos(2 * kPi * i / n), s0 = std::sin(2 * kPi * i / n);
        const float c1 = std::cos(2 * kPi * (i + 1) / n), s1 = std::sin(2 * kPi * (i + 1) / n);
        AddTri(v, cx, cy, 1, cx + core * c0, cy + core * s0, 1, cx + core * c1, cy + core * s1, 1);
        AddQuadA(
            v,
            cx + core * c0,
            cy + core * s0,
            1,
            cx + edge * c0,
            cy + edge * s0,
            0,
            cx + edge * c1,
            cy + edge * s1,
            0,
            cx + core * c1,
            cy + core * s1,
            1
        );
    }
}

void AddBlurredDisc(std::vector<float>& v, float cx, float cy, float r, float sigma) {
    // Rings of linearly interpolated alpha along the blurred edge's profile, 0.5 erfc((d - r) / (sigma
    // sqrt 2)) at distance d from the centre (the straight-edge profile; close to the round one while r
    // is over 2 sigma).
    const float outer = r + 3 * sigma;
    const int rings = 16, n = Segments(outer, 16, 128);
    auto alpha = [&](float d) { return 0.5f * std::erfc((d - r) / (std::max(1e-3f, sigma) * 1.41421356f)); };

    for (int k = 0; k < rings; k++) {
        const float ra = outer * k / rings, rb = outer * (k + 1) / rings, aa = alpha(ra), ab = alpha(rb);

        for (int i = 0; i < n; i++) {
            const float c0 = std::cos(2 * kPi * i / n), s0 = std::sin(2 * kPi * i / n);
            const float c1 = std::cos(2 * kPi * (i + 1) / n), s1 = std::sin(2 * kPi * (i + 1) / n);

            if (k == 0) {
                AddTri(v, cx, cy, aa, cx + rb * c0, cy + rb * s0, ab, cx + rb * c1, cy + rb * s1, ab);
            } else {
                AddQuadA(
                    v,
                    cx + ra * c0,
                    cy + ra * s0,
                    aa,
                    cx + rb * c0,
                    cy + rb * s0,
                    ab,
                    cx + rb * c1,
                    cy + rb * s1,
                    ab,
                    cx + ra * c1,
                    cy + ra * s1,
                    aa
                );
            }
        }
    }
}

void MeterBatch::Add(const std::vector<float>& v, float r, float g, float b, float a) {
    for (size_t i = 0; i + 2 < v.size(); i += 3) {
        frame_.push_back({v[i], v[i + 1], Pack(r, g, b, a * v[i + 2])});
    }
}

// OBS's solid effect, "SolidColored": the vertex colours. Not OBS's gs_render_start/stop: with
// per-vertex colours that path left OBS's next draw crashing (encoder colour conversion, in
// LoadVertexBufferData).
void MeterBatch::Submit() {
    const size_t n = std::min(frame_.size() / 3 * 3, kMaxVertices);
    if (n == 0) {
        return;
    }
    if (!vb_) {
        gs_vb_data* vbd = gs_vbdata_create();
        vbd->num = kMaxVertices;
        vbd->points = static_cast<vec3*>(bzalloc(sizeof(vec3) * kMaxVertices));
        vbd->colors = static_cast<uint32_t*>(bzalloc(sizeof(uint32_t) * kMaxVertices));
        vb_ = gs_vertexbuffer_create(vbd, GS_DYNAMIC);
        if (!vb_) {
            return;
        }
    }

    gs_vb_data* d = gs_vertexbuffer_get_data(vb_);
    for (size_t i = 0; i < n; i++) {
        vec3_set(&d->points[i], frame_[i].x, frame_[i].y, 0.0f);
        d->colors[i] = frame_[i].color;
    }

    for (size_t i = n; i < kMaxVertices && i < n + 3; i++) {
        d->colors[i] = 0;
    }
    gs_vertexbuffer_flush(vb_);
    gs_effect_t* solid = obs_get_base_effect(OBS_EFFECT_SOLID);
    vec4 white;
    vec4_set(&white, 1, 1, 1, 1);
    gs_effect_set_vec4(gs_effect_get_param_by_name(solid, "color"), &white);
    gs_load_vertexbuffer(vb_);
    gs_load_indexbuffer(nullptr);
    while (gs_effect_loop(solid, "SolidColored")) {
        gs_draw(GS_TRIS, 0, uint32_t(n));
    }
    gs_load_vertexbuffer(nullptr);  // leave nothing of ours bound for OBS's next draw
}

void MeterBatch::Destroy() {
    if (vb_) {
        gs_vertexbuffer_destroy(vb_);  // frees its vertex data too
    }
    vb_ = nullptr;
}

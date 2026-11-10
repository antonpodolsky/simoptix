// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "meter_chrono.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

    constexpr float kBoard = 480;  // the design's board, design pixels; the meter's centre is at (240, 240)
    constexpr float kMaxG = 3;
    constexpr float kRing3G = 112;  // the 3G ring's radius, design pixels (in the base image)

    // Colours from the design, 0..1 RGB.
    constexpr float kAccent[3] = {1.0f, 0.353f, 0.122f};  // #ff5a1f: the dot, its glow, the live G
    // #ff6b1f: the tail, a little warmer than the dot so the dot stands apart from it (in the dot's colour
    // the newest part of the tail read as part of the dot; in the design's amber, #ffb020, it stood out
    // too much, and 40 % of the way there, #ff7c1f, still a bit). 20 % of the way from the accent to that
    // amber.
    constexpr float kTail[3] = {1.0f, 0.420f, 0.122f};
    constexpr float kBrake[3] = {1.0f, 0.353f, 0.306f};         // #ff5a4e
    constexpr float kBrakeAbs[3] = {0.784f, 0.157f, 0.118f};    // #c8281e, while ABS intervenes
    constexpr float kThrottle[3] = {0.239f, 0.863f, 0.518f};    // #3ddc84
    constexpr float kThrottleTc[3] = {0.122f, 0.604f, 0.322f};  // #1f9a52, while TC intervenes
    constexpr uint32_t kAccentRgb = 0xFF5A1F;
    constexpr int kLabelOpacity = 60;  // white at 60 %: the ring labels and the highest values

    // The pedal tracks just inside the 3G ring (in the base image) and the bars filling them from their
    // lower ends, with flat ends as the tracks: degrees clockwise from up. Brake 215 -> 325 on the left,
    // throttle 145 -> 35 on the right.
    constexpr float kBarR = 103, kBarWidth = 5, kBarSpan = 110;
    constexpr float kBrakeStart = 215, kThrottleStart = 145;

    // The live G's centre, design pixels: the window's visible centre (see Draw). Horizontally 0.8 right of
    // the window's: centred by measurement (gaps of 5.6 and 5.2 px at size 300) it still looked 1 px left,
    // and 1.6 right (1 px at 300) looked shifted right.
    constexpr float kLiveGX = 240.8f, kLiveGY = 314.2f;

    // The labels' faces: the design's SemiBold (Regular for the ring labels), one weight heavier below 9 px,
    // where strokes get thinner than a pixel and read grey.
    const wchar_t* FaceFor(int px) {
        return px < 9 ? L"Bahnschrift Bold" : L"Bahnschrift SemiBold";
    }
    const wchar_t* RingFaceFor(int px) {
        return px < 9 ? L"Bahnschrift SemiBold" : L"Bahnschrift";
    }

    // The ring labels (1G, 2G, 3G): 10 px, white at 60 %, their ink from x 245.3 and their ink's bottom on
    // these lines, design pixels (measured on the design's base image with and without them).
    constexpr float kRingInkLeft = 245.3f;
    constexpr float kRingInkBottom[3] = {218.5f, 181.5f, 144.0f};

    // The comet tail's 5 age bands, newest first.
    constexpr float kTailAlpha[5] = {0.55f, 0.35f, 0.20f, 0.09f, 0.02f};

    // The meter's size for a source: k scales design pixels to screen pixels, (cx, cy) is its centre, and
    // `scale` turns G into screen pixels.
    struct Dial {
        float k, cx, cy, scale;

        // Where acceleration (x, y) in G is on screen.
        void ToScreen(float x, float y, float* sx, float* sy) const {
            const float len = std::sqrt(x * x + y * y);
            if (len > kMaxG) {
                x *= kMaxG / len, y *= kMaxG / len;  // pinned at the 3G ring
            }
            *sx = cx + x * scale;
            *sy = cy - y * scale;  // forward is up
        }
    };

    void DrawBars(const MeterView& m, const Dial& d, MeterBatch& batch, std::vector<float>& v) {
        struct Bar {
            float pedal, start, dir;
            const float* rgb;
        };

        const Bar bars[2] = {
            {m.pedals.brake, kBrakeStart, 1, m.pedals.absActive ? kBrakeAbs : kBrake},
            {m.pedals.throttle, kThrottleStart, -1, m.pedals.tcActive ? kThrottleTc : kThrottle}
        };

        for (const Bar& b : bars) {
            v.clear();
            const float fill = std::min(1.0f, std::max(0.0f, b.pedal)) * kBarSpan;
            if (fill > 0.5f) {
                AddArc(v, d.cx, d.cy, kBarR * d.k, kBarWidth * d.k, b.start, b.start + b.dir * fill);
            }

            batch.Add(v, b.rgb[0], b.rgb[1], b.rgb[2], 1);
        }
    }

    // The dot's comet tail, behind the dot of radius dotR.
    void DrawTail(const MeterView& m, const Dial& d, float dotR, MeterBatch& batch, std::vector<float>& v) {
        const Trail& trail = *m.trail;

        // Comet tail: a band that narrows with age (0.9 of the dot's radius when new, 0 at the tail's
        // end), with a ball at each joint, in the design's 5 age bands.
        const float length = std::max(0.01f, m.trailS);
        for (int band = 4; band >= 0; band--) {  // oldest (faintest) first
            v.clear();
            for (size_t i = 1; i < trail.size(); i++) {
                const float age = float(std::min(1.0, (m.now - trail[i].t) / length));
                if (std::min(4, int(age * 5)) != band) {
                    continue;
                }
                const float age0 = float(std::min(1.0, (m.now - trail[i - 1].t) / length));

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

void ChronoMeter::Tick(int size, double now) {
    base_.Build(size);
    glass_.Build(size);
    UpdateLabels(labels_, kLabels, now);
}

void ChronoMeter::Draw(const MeterView& m, MeterBatch& batch, std::vector<float>& v) {
    const float S = m.size, k = S / kBoard;
    const Dial dial = {k, S * 0.5f, S * 0.5f, kRing3G * k / kMaxG};
    const bool live = m.live;

    base_.Draw();

    batch.Clear();
    if (live) {
        DrawBars(m, dial, batch, v);
    }

    // The glow first, so the tail crosses it in its own colour; without telemetry, the dot and glow
    // are dimmed at the centre.
    float dx = dial.cx, dy = dial.cy;
    if (live) {
        dial.ToScreen(m.gx, m.gy, &dx, &dy);
    }
    const float dim = live ? 1.0f : 0.35f;
    v.clear();
    AddBlurredDisc(v, dx, dy, 19.2f * k, 6.4f * k);
    batch.Add(v, kAccent[0], kAccent[1], kAccent[2], 0.5f * dim);

    // 160 % of the first design's 7.8 (picked on the canvas); the glow and tail scale with it.
    const float dotR = 12.5f * k;
    if (live && m.trail->size() > 1) {
        DrawTail(m, dial, dotR, batch, v);
    }

    v.clear();
    AddDisc(v, dx, dy, dotR);
    batch.Add(v, kAccent[0], kAccent[1], kAccent[2], dim);
    batch.Submit();

    DrawValues(m, k);
    glass_.Draw();
}

void ChronoMeter::DrawValues(const MeterView& m, float k) {
    // The highest G reached each way, in tabular figures placed by their advance (as the design's
    // tabular-nums text boxes), so their digits stay in place as they change: braking above,
    // acceleration below, right from x 364 and left up to x 116, all with their ink's middle on the line.
    const int peakSize = int(std::round(17 * k));
    const wchar_t* face = FaceFor(peakSize);
    char text[16];
    const float peaks[4] = {m.maxForward, m.maxBack, m.maxRight, m.maxLeft};
    const float px[4] = {240, 240, 364, 116}, py[4] = {110, 371, 240, 240}, align[4] = {0.5f, 0.5f, 0, 1};

    for (int i = 0; i < 4; i++) {
        snprintf(text, sizeof(text), "%.2f", peaks[i]);
        DrawLabelCentred(
            labels_[i],
            text,
            peakSize,
            face,
            kLabelOpacity,
            px[i] * k,
            py[i] * k,
            0xFFFFFF,
            true,
            align[i]
        );
    }

    // The ring labels, along the upward axis inside each ring.
    const int ringSize = int(std::round(10 * k));
    for (int i = 0; i < 3; i++) {
        const char ring[3] = {char('1' + i), 'G', 0};
        DrawLabel(
            labels_[kRing + i],
            ring,
            ringSize,
            RingFaceFor(ringSize),
            kLabelOpacity,
            kRingInkLeft * k,
            kRingInkBottom[i] * k,
            0,
            1
        );
    }

    // The live G in its window (in the base image), two decimals, no unit, in tabular figures so its
    // digits stay in place as it changes. Centred to a fraction of a pixel, on the window as it looks:
    // its inner shadow is offset 1 px down, so it reads about 0.5 px higher than its box (centre y
    // 314.7). On the box, the gaps above and below the digits measured 7.1 and 6.2 px at size 500.
    snprintf(text, sizeof(text), "%.2f", m.live ? std::sqrt(m.gx * m.gx + m.gy * m.gy) : 0.0f);
    const int liveSize = int(std::round(16 * k));
    DrawLabelCentred(
        labels_[kLiveG],
        text,
        liveSize,
        FaceFor(liveSize),
        100,
        kLiveGX * k,
        kLiveGY * k,
        kAccentRgb,
        true
    );
}

void ChronoMeter::Destroy() {
    for (MeterLabel& l : labels_) {
        DestroyLabel(l);
    }
    base_.Destroy();
    glass_.Destroy();
}

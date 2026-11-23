// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "meter_oled.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

    constexpr float kPi = 3.14159265f;
    constexpr float kBoard = 480;  // the design's board, design pixels; the meter's centre is at (240, 240)
    constexpr float kMaxG = 3;
    constexpr float kRing3G = 136;  // the 3G ring's radius, design pixels (in the base image)

    // Colours from the design, 0..1 RGB. The tail is the accent itself (Chrono's is a little warmer).
    constexpr float kAccent[3] = {1.0f, 0.353f, 0.122f};        // #ff5a1f: the dot, its bloom, its tail
    constexpr float kBrake[3] = {1.0f, 0.353f, 0.306f};         // #ff5a4e
    constexpr float kBrakeAbs[3] = {0.784f, 0.157f, 0.118f};    // #c8281e, while ABS intervenes
    constexpr float kThrottle[3] = {0.239f, 0.863f, 0.518f};    // #3ddc84
    constexpr float kThrottleTc[3] = {0.122f, 0.604f, 0.322f};  // #1f9a52, while TC intervenes
    constexpr int kPeakOpacity = 90, kRingOpacity = 40;         // white: the highest values, the ring labels

    // The pedal bars, over their empty tracks in the base image: centre r 143, 7 wide, in degrees clockwise
    // from up. Brake 215 -> 325 on the left, throttle 145 -> 35 on the right, each lit from its start. A
    // bar is 40 segments of 4.9378 design px with gaps of 1.9751 along r 143 (the track's dashes), so the
    // 110 degrees start and end on a whole segment, and it lights whole segments only.
    constexpr float kBarR = 143, kBarWidth = 7;
    constexpr int kSegments = 40;
    constexpr float kSegmentDeg = 4.9378f / kBarR * 180 / kPi,
                    kSegmentStepDeg = (4.9378f + 1.9751f) / kBarR * 180 / kPi;
    constexpr float kBrakeStart = 215, kThrottleStart = 145;

    // The ring labels (1G, 2G, 3G): 9 px, from x 244 with baselines on y 205.7, 160.3, 115.0 in the design
    // (SVG text with tabular figures). Placed here by their ink: Bahnschrift's tabular 1, 2 and 3 start
    // 0.101, 0.054 and 0.044 em right of the pen, and the G ends 0.007 em under the baseline (measured
    // with DirectWrite at 1000 px).
    constexpr float kRingInkLeft[3] = {244.91f, 244.49f, 244.40f};
    constexpr float kRingInkBottom[3] = {205.76f, 160.36f, 115.06f};

    // The comet tail's 5 age bands, newest first.
    constexpr float kTailAlpha[5] = {0.55f, 0.35f, 0.20f, 0.09f, 0.02f};

    // The labels' faces: Medium for the highest values and Regular for the ring labels, as the design,
    // SemiBold below 9 px, where strokes get thinner than a pixel and read grey.
    const wchar_t* PeakFaceFor(int px) {
        return px < 9 ? L"Bahnschrift SemiBold" : L"Bahnschrift Medium";
    }
    const wchar_t* RingFaceFor(int px) {
        return px < 9 ? L"Bahnschrift SemiBold" : L"Bahnschrift";
    }

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
            {m.pedals.throttle, kThrottleStart, -1, m.pedals.tcActive ? kThrottleTc : kThrottle},
        };

        for (const Bar& b : bars) {
            v.clear();
            const float pedal = std::min(1.0f, std::max(0.0f, b.pedal));
            const int lit = static_cast<int>(std::lround(pedal * kSegments));

            for (int i = 0; i < lit; i++) {
                const float from = b.start + b.dir * i * kSegmentStepDeg;
                AddArc(v, d.cx, d.cy, kBarR * d.k, kBarWidth * d.k, from, from + b.dir * kSegmentDeg);
            }

            batch.Add(v, b.rgb[0], b.rgb[1], b.rgb[2], 1);
        }
    }

    // The dot's comet tail, behind the dot of radius dotR.
    void DrawTail(const MeterView& m, const Dial& d, float dotR, MeterBatch& batch, std::vector<float>& v) {
        const Trail& trail = *m.trail;

        // A band that narrows with age (0.9 of the dot's radius when new, 0 at the tail's end), with a
        // ball at each joint, in the design's 5 age bands.
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

            batch.Add(v, kAccent[0], kAccent[1], kAccent[2], kTailAlpha[band]);
        }
    }

}

void OledMeter::Tick(int size, double now) {
    base_.Build(size);
    UpdateLabels(labels_, kLabels, now);
}

void OledMeter::Draw(const MeterView& m, MeterBatch& batch, std::vector<float>& v) {
    const float S = m.size, k = S / kBoard;
    const Dial dial = {k, S * 0.5f, S * 0.5f, kRing3G * k / kMaxG};
    const bool live = m.live;

    base_.Draw();

    batch.Clear();
    if (live) {
        DrawBars(m, dial, batch, v);
    }

    // The bloom first, so the tail crosses it in its own colour; without telemetry, the dot and bloom
    // are dimmed at the centre.
    float dx = dial.cx, dy = dial.cy;
    if (live) {
        dial.ToScreen(m.gx, m.gy, &dx, &dy);
    }

    const float dim = live ? 1.0f : 0.35f;
    v.clear();
    AddBlurredDisc(v, dx, dy, 19.2f * k, 6.4f * k);
    batch.Add(v, kAccent[0], kAccent[1], kAccent[2], 0.5f * dim);

    const float dotR = 12.5f * k;
    if (live && m.trail->size() > 1) {
        DrawTail(m, dial, dotR, batch, v);
    }

    v.clear();
    AddDisc(v, dx, dy, dotR);
    batch.Add(v, kAccent[0], kAccent[1], kAccent[2], dim);
    batch.Submit();

    DrawValues(m, k);
}

void OledMeter::DrawValues(const MeterView& m, float k) {
    // The highest G reached each way, in tabular figures placed by their advance (as the design's
    // tabular-nums text boxes), so their digits stay in place as they change: braking above the display,
    // acceleration below it, right from x 400 and left up to x 80, all with their ink's middle on the line.
    const int peakSize = static_cast<int>(std::round(15 * k));
    const wchar_t* face = PeakFaceFor(peakSize);
    char text[16];
    const float peaks[4] = {m.maxForward, m.maxBack, m.maxRight, m.maxLeft};
    const float px[4] = {240, 240, 400, 80}, py[4] = {75, 406, 240, 240}, align[4] = {0.5f, 0.5f, 0, 1};

    for (int i = 0; i < 4; i++) {
        snprintf(text, sizeof(text), "%.2f", peaks[i]);
        DrawLabelCentred(
            labels_[i],
            text,
            peakSize,
            face,
            kPeakOpacity,
            px[i] * k,
            py[i] * k,
            0xFFFFFF,
            true,
            align[i]
        );
    }

    // The ring labels, along the upward axis inside each ring.
    const int ringSize = static_cast<int>(std::round(9 * k));
    for (int i = 0; i < 3; i++) {
        const char ring[3] = {static_cast<char>('1' + i), 'G', 0};
        DrawLabel(
            labels_[kRing + i],
            ring,
            ringSize,
            RingFaceFor(ringSize),
            kRingOpacity,
            kRingInkLeft[i] * k,
            kRingInkBottom[i] * k,
            0,
            1
        );
    }
}

void OledMeter::Destroy() {
    for (MeterLabel& l : labels_) {
        DestroyLabel(l);
    }
    base_.Destroy();
}

// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "meter_text.h"

#include <windows.h>
#include <obs-module.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <vector>

#include "text_raster.h"

namespace {

    // Contrast boost on the coverage, c' = c (1 + k) / (1 + k c): thin light strokes on the meter's dark face
    // read heavier, as DirectWrite's own renderers do with their "enhanced contrast" (its raw glyph analysis,
    // which this uses, leaves it out). Chosen by measuring the labels at 1:1
    // (.agents\skills\dev\findings\overlays.md, "Text").
    constexpr float kContrast = 0.5f;

    float Frac(float v) {
        return v - std::floor(v);
    }

    // A face name as GDI wrote them ("Bahnschrift SemiBold") as a family and a weight.
    TextStyle StyleOf(const MeterLabel& l, wchar_t* family, size_t n) {
        TextStyle s;
        wcsncpy(family, l.face, n - 1);
        family[n - 1] = 0;
        struct Suffix {
            const wchar_t* name;
            int weight;
        };

        const Suffix suffixes[] = {
            {L" SemiBold", 600},
            {L" Bold", 700},
            {L" SemiLight", 350},
            {L" Light", 300},
        };
        for (const Suffix& x : suffixes) {
            const size_t fl = wcslen(family), sl = wcslen(x.name);
            if (fl > sl && wcscmp(family + fl - sl, x.name) == 0) {
                family[fl - sl] = 0;
                s.weight = x.weight;
                break;
            }
        }

        s.family = family;
        s.size = float(l.size);
        s.tracking = l.tracking;
        s.tabular = l.tabular;
        return s;
    }

    // Where a raster's ink is: its box (the pixels with coverage above 8), the sums of the coverage and of
    // its squares, and the highest coverage on each edge (the box's left and right columns, top and bottom
    // rows).
    struct InkBox {
        int x0 = 0, y0 = 0, x1 = -1, y1 = -1;
        double sum = 0, sum2 = 0;
        int left = 0, right = 0, top = 0, bottom = 0;
    };

    // False for a raster with no ink.
    bool FindInk(const TextRaster& r, InkBox* k) {
        k->x0 = r.w, k->y0 = r.h;
        for (int y = 0; y < r.h; y++) {
            for (int x = 0; x < r.w; x++) {
                const int a = r.coverage[size_t(y) * r.w + x];
                k->sum += a, k->sum2 += double(a) * a;
                if (a > 8) {
                    k->x0 = std::min(k->x0, x), k->y0 = std::min(k->y0, y);
                    k->x1 = std::max(k->x1, x), k->y1 = std::max(k->y1, y);
                }
            }
        }

        if (k->x1 < 0 || k->sum <= 0) {
            return false;
        }
        for (int y = k->y0; y <= k->y1; y++) {
            k->left = std::max<int>(k->left, r.coverage[size_t(y) * r.w + k->x0]);
            k->right = std::max<int>(k->right, r.coverage[size_t(y) * r.w + k->x1]);
        }

        for (int x = k->x0; x <= k->x1; x++) {
            k->top = std::max<int>(k->top, r.coverage[size_t(k->y0) * r.w + x]);
            k->bottom = std::max<int>(k->bottom, r.coverage[size_t(k->y1) * r.w + x]);
        }

        return true;
    }

    class LabelRaster {
    public:
        LabelRaster(const MeterLabel& l, std::vector<uint8_t>* pixels, int* w, int* h, LabelInk* ink)
            : l_(l),
              pixels_(pixels),
              w_(w),
              h_(h),
              ink_(ink),
              style_(StyleOf(l, family_, LF_FACESIZE)),
              cs_(std::cos(l.angle)),
              sn_(std::sin(l.angle)),
              turned_(l.angle != 0) {
            MultiByteToWideChar(CP_UTF8, 0, l.text.c_str(), -1, text_, 63);
        }

        LabelRaster(const LabelRaster&) = delete;  // style_.family points into family_
        LabelRaster& operator=(const LabelRaster&) = delete;

        bool Render();

    private:
        double Measure(const TextRaster& r, float* cx, float* cy, bool keep);
        void Crop(const TextRaster& r, const InkBox& k, float cx, float cy);
        double Attempt(float ox, float oy, bool keep, float* cx, float* cy);
        bool MeasureUpright();

        const MeterLabel& l_;
        std::vector<uint8_t>* pixels_;
        int* w_;
        int* h_;
        LabelInk* ink_;
        wchar_t text_[64] = {}, family_[LF_FACESIZE] = {};
        const TextStyle style_;
        const float cs_, sn_;
        const bool turned_;
        const TextRaster* upright_ = nullptr;  // the upright raster of a turned label
        float ux_ = 0, uy_ = 0;  // a turned label's upright ink centre, with the pen at the origin
    };

    // The upright ink's centre from a raster: each edge pixel's coverage (its row's or column's highest)
    // tells how far into it the ink reaches. Returns the sharpness (coverage-weighted mean coverage), or
    // -1 for no ink; with `keep`, crops the raster into *pixels_.
    double LabelRaster::Measure(const TextRaster& r, float* cx, float* cy, bool keep) {
        InkBox k;
        if (!FindInk(r, &k)) {
            return -1.0;
        }

        *cx = r.x0 + 0.5f * ((k.x0 + 1 - k.left / 255.0f) + (k.x1 + k.right / 255.0f));
        if (!turned_ || &r == upright_) {
            ink_->uprightW = (k.x1 + k.right / 255.0f) - (k.x0 + 1 - k.left / 255.0f);
        }
        *cy = r.y0 + 0.5f * ((k.y0 + 1 - k.top / 255.0f) + (k.y1 + k.bottom / 255.0f));
        if (keep) {
            Crop(r, k, *cx, *cy);
        }

        return k.sum2 / k.sum / 255;
    }

    // Puts the ink's box of `r` (its centre at (cx, cy)) into the texture, in the label's colour and opacity,
    // and its place in the texture into *ink_.
    void LabelRaster::Crop(const TextRaster& r, const InkBox& k, float cx, float cy) {
        // In the texture the ink's pixels start at (1, 1).
        ink_->cx = cx - (r.x0 + k.x0) + 1;
        ink_->cy = cy - (r.y0 + k.y0) + 1;
        ink_->left = 2 - k.left / 255.0f, ink_->top = 2 - k.top / 255.0f;
        ink_->right = k.x1 - k.x0 + 1 + k.right / 255.0f, ink_->bottom = k.y1 - k.y0 + 1 + k.bottom / 255.0f;

        *w_ = k.x1 - k.x0 + 3;
        *h_ = k.y1 - k.y0 + 3;
        pixels_->assign(size_t(*w_) * *h_ * 4, 0);

        const float opacity = l_.opacity / 100.0f;
        for (int y = k.y0; y <= k.y1; y++) {
            for (int x = k.x0; x <= k.x1; x++) {
                const float c = r.coverage[size_t(y) * r.w + x] / 255.0f;
                uint8_t* p = &(*pixels_)[(size_t(y - k.y0 + 1) * *w_ + (x - k.x0 + 1)) * 4];
                p[0] = uint8_t(l_.rgb), p[1] = uint8_t(l_.rgb >> 8), p[2] = uint8_t(l_.rgb >> 16);  // BGRA
                p[3] = uint8_t(std::lround(c * (1 + kContrast) / (1 + kContrast * c) * opacity * 255));
            }
        }
    }

    // A turned label is centred on its upright ink centre, turned: measured once, upright at the origin.
    bool LabelRaster::MeasureUpright() {
        TextRaster r;
        upright_ = &r;
        if (!RasterText(text_, style_, 0, 0, 0, &r) || Measure(r, &ux_, &uy_, false) < 0) {
            return false;
        }
        upright_ = nullptr;
        return true;
    }

    // Rasterises with the pen at (ox, oy); (cx, cy) gets the label's centre (see RenderText).
    double LabelRaster::Attempt(float ox, float oy, bool keep, float* cx, float* cy) {
        TextRaster r;
        if (!RasterText(text_, style_, ox, oy, l_.angle, &r)) {
            return -1.0;
        }
        float ix, iy;
        const double sharp = Measure(r, &ix, &iy, false);
        if (sharp < 0) {
            return -1.0;
        }

        if (turned_) {
            *cx = ox + ux_ * cs_ - uy_ * sn_, *cy = oy + ux_ * sn_ + uy_ * cs_;
        } else {  // the anchor along the advance (tabular) or the ink (Measure set uprightW to its width)
            *cx = style_.tabular ? ox + r.advance * l_.anchor : ix + (l_.anchor - 0.5f) * ink_->uprightW;
            *cy = iy;
        }

        if (keep) {
            Measure(r, &ix, &iy, true);

            // The texture's centre is the label's, wherever the ink's own centre is.
            ink_->cx += *cx - ix;
            ink_->cy += *cy - iy;
        }

        return sharp;
    }

    bool LabelRaster::Render() {
        if (turned_ && !MeasureUpright()) {
            return false;
        }

        float cx, cy;
        if (l_.centreX >= 0) {
            // Where the centre falls with the pen at 0, then shifted so its fraction matches the target's.
            if (Attempt(0, 0, false, &cx, &cy) < 0) {
                return false;
            }
            return Attempt(Frac(l_.centreX - cx), Frac(l_.centreY - cy), true, &cx, &cy) >= 0;
        }

        // Horizontally at the sharpest of a few sub-pixel offsets: a straight stem (the "1") that falls
        // between two pixel columns looks grey and wide. Vertically the grid-fitting already puts strokes on
        // pixels.
        const float kSteps[] = {0, 0.25f, 0.5f, 0.75f};
        float bestX = 0;
        double best = -1;
        for (float ox : kSteps) {
            if (double s = Attempt(ox, 0, false, &cx, &cy); s > best) {
                best = s, bestX = ox;
            }
        }

        return best >= 0 && Attempt(bestX, 0, true, &cx, &cy) >= 0;
    }

    // Rasterises `l.text` in its colour and opacity and crops it to its ink. Returns false for no ink. A
    // placed label (l.centreX >= 0: centred or rotated) is rasterised at the sub-pixel offset that puts its
    // centre on the fraction of (l.centreX, l.centreY), and ink->cx, cy get that centre in the texture: the
    // ink centre, or for tabular text the middle of its advance (which doesn't move as the digits change), or
    // for rotated text its upright ink centre, turned. Other labels at the sharpest offset. *ink also gets
    // the ink's box in the texture and its upright width.
    bool RenderText(const MeterLabel& l, std::vector<uint8_t>* pixels, int* w, int* h, LabelInk* ink) {
        if (l.text.empty()) {
            return false;
        }
        LabelRaster raster(l, pixels, w, h, ink);
        return raster.Render();
    }

    void DrawTexture(const MeterLabel& l, float x, float y) {
        gs_effect_t* effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
        gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), l.tex);
        gs_matrix_push();
        gs_matrix_translate3f(x, y, 0);
        while (gs_effect_loop(effect, "Draw")) {
            gs_draw_sprite(l.tex, 0, 0, 0);
        }
        gs_matrix_pop();
    }

}

void UpdateLabels(MeterLabel* labels, int count, double now) {
    for (int i = 0; i < count; i++) {
        MeterLabel& l = labels[i];
        const bool look = l.text == l.shownText && l.size == l.shownSize && l.face == l.shownFace
                          && l.opacity == l.shownOpacity && l.rgb == l.shownRgb
                          && l.tracking == l.shownTracking && l.tabular == l.shownTabular;

        const bool place = l.angle == l.shownAngle && l.anchor == l.shownAnchor
                           && std::fabs(l.centreX - l.shownCentreX) < 0.01f
                           && std::fabs(l.centreY - l.shownCentreY) < 0.01f;

        if (look && place) {
            continue;
        }
        if (now - l.updated < 0.1) {
            continue;  // a rising highest value changes every update
        }

        std::vector<uint8_t> pixels;
        int w = 0, h = 0;
        const bool ink = RenderText(l, &pixels, &w, &h, &l.ink);

        obs_enter_graphics();
        if (l.tex) {
            gs_texture_destroy(l.tex);
        }
        const uint8_t* rows[] = {pixels.data()};
        l.tex = ink ? gs_texture_create(uint32_t(w), uint32_t(h), GS_BGRA, 1, rows, 0) : nullptr;
        obs_leave_graphics();

        l.w = w;
        l.h = h;

        l.shownText = l.text;
        l.shownSize = l.size;
        l.shownFace = l.face;
        l.shownOpacity = l.opacity;
        l.shownRgb = l.rgb;
        l.shownTracking = l.tracking;
        l.shownTabular = l.tabular;
        l.shownAngle = l.angle;
        l.shownAnchor = l.anchor;
        l.shownCentreX = l.centreX;
        l.shownCentreY = l.centreY;

        l.updated = now;
    }
}

void WantLabel(
    MeterLabel& l,
    const char* text,
    int size,
    const wchar_t* face,
    int opacity,
    uint32_t rgb,
    float tracking
) {
    l.text = text;
    l.size = std::max(6, size);
    l.face = face;
    l.opacity = opacity;
    l.rgb = rgb;
    l.tracking = tracking;
}

void PlaceLabel(MeterLabel& l, float x, float y, float angle) {
    l.centreX = x;
    l.centreY = y;
    l.angle = angle;
    l.anchor = 0.5f;
    l.tabular = false;
}

void DrawLabel(
    MeterLabel& l,
    const char* text,
    int size,
    const wchar_t* face,
    int opacity,
    float x,
    float y,
    float align,
    float valign,
    uint32_t rgb
) {
    WantLabel(l, text, size, face, opacity, rgb);
    l.centreX = l.centreY = -1;
    l.angle = 0;
    l.tabular = false;
    if (!l.tex || l.shownCentreX >= 0) {
        return;  // still built for another placement: rebuilt on the next tick
    }

    // The ink's exact edges, so a partly covered edge pixel doesn't shift the label.
    const LabelInk& k = l.ink;
    DrawTexture(
        l,
        std::round(x - (k.left + (k.right - k.left) * align)),
        std::round(y - (k.top + (k.bottom - k.top) * valign))
    );
}

void DrawLabelCentred(
    MeterLabel& l,
    const char* text,
    int size,
    const wchar_t* face,
    int opacity,
    float x,
    float y,
    uint32_t rgb,
    bool tabular,
    float align
) {
    WantLabel(l, text, size, face, opacity, rgb);
    PlaceLabel(l, x, y, 0);
    l.tabular = tabular;
    l.anchor = align;
    if (!l.tex || l.shownCentreX < 0 || l.shownAngle != 0) {
        return;
    }

    // Built so that x - ink.cx (ink.cx: the anchor) is a whole pixel, to measurement noise: drawn 1:1.
    DrawTexture(l, std::round(x - l.ink.cx), std::round(y - l.ink.cy));
}

void DrawLabelRotated(MeterLabel& l, float x, float y, float angle) {
    PlaceLabel(l, x, y, angle);
    if (!l.tex || l.shownCentreX < 0 || l.shownAngle != angle) {
        return;
    }

    // Rasterised already turned (text_raster.h): drawn 1:1, unfiltered, like any other label.
    DrawTexture(l, std::round(x - l.ink.cx), std::round(y - l.ink.cy));
}

void DestroyLabel(MeterLabel& l) {
    if (l.tex) {
        gs_texture_destroy(l.tex);
    }
    l.tex = nullptr;
}

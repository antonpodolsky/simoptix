// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "empty_state.h"

#include <graphics/vec4.h>
#include <obs-module.h>
#include <util/platform.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "shared/text_raster.h"

namespace {

    // Sizes as parts of the source's height, so the screen looks the same on any canvas.
    constexpr float kTitleSize = 0.055f, kLineSize = 0.03f;
    constexpr float kTitleGap = 0.8f;  // under the title, in the title's ems, before the next line's own em
    constexpr float kLineLeading = 1.6f;   // baseline to baseline of the lines under it, in their ems
    constexpr float kLineOpacity = 0.72f;  // the lines under the title, white over the screen
    // The facts: smaller than the lines, under a rule a line and a bit below them, labels dimmer than values.
    constexpr float kFactSize = 0.027f;
    constexpr float kRuleGap = 1.4f;      // last line's baseline to the rule, in the lines' ems
    constexpr float kFactsGap = 2.0f;     // the rule to the first fact's baseline, in the facts' ems
    constexpr float kFactLeading = 1.7f;  // baseline to baseline of the facts, in their ems
    constexpr float kColumnGap = 1.4f;    // label to value, in the facts' ems
    constexpr float kLabelOpacity = 0.5f, kValueOpacity = 0.85f, kRuleOpacity = 0.35f;
    constexpr float kScreen[4] = {0.06f, 0.06f, 0.07f, 1.0f};

    struct Colour {
        uint8_t r, g, b;
    };

    constexpr Colour kWhite = {255, 255, 255};
    constexpr Colour kGood = {120, 214, 140}, kWarning = {255, 190, 70};  // green, amber
    // The waiting bars: as wide as the fact says (about its value's width, so the centred facts barely
    // shift when the values come in) and a bit over half a line high, their white pulsing between these
    // opacities once a period, each row a little after the one above (a wave down the rows).
    constexpr float kBarHeight = 0.55f, kBarRaise = 0.62f;  // in the facts' ems
    constexpr float kBarDim = 0.06f, kBarBright = 0.22f;
    constexpr double kBarPeriodS = 1.4, kBarStaggerS = 0.15;
    constexpr double kPi = 3.14159265358979323846;

    Colour ToneColour(FactTone tone) {
        switch (tone) {
            case FactTone::kGood:
                return kGood;
            case FactTone::kWarning:
                return kWarning;
            default:
                return kWhite;
        }
    }

    // One rasterised line, its colour, and where its pen starts in the text's texture.
    struct PlacedLine {
        TextRaster raster;
        float opacity = 1;
        Colour colour = kWhite;
        int penX = 0, penY = 0;
    };

    // Adds a line's coverage into the BGRA pixels (its colour, straight alpha) of a width-wide texture.
    void Blend(const PlacedLine& line, int width, int height, std::vector<uint8_t>* pixels) {
        const TextRaster& r = line.raster;

        for (int y = 0; y < r.h; y++) {
            const int ty = line.penY + r.y0 + y;

            if (ty < 0 || ty >= height) {
                continue;
            }

            for (int x = 0; x < r.w; x++) {
                const int tx = line.penX + r.x0 + x;

                if (tx < 0 || tx >= width) {
                    continue;
                }

                uint8_t* p = &(*pixels)[(static_cast<size_t>(ty) * width + tx) * 4];
                const int alpha = static_cast<int>(
                    std::lround(r.coverage[static_cast<size_t>(y) * r.w + x] * line.opacity)
                );
                p[0] = line.colour.b, p[1] = line.colour.g, p[2] = line.colour.r;
                p[3] = static_cast<uint8_t>(std::max<int>(p[3], alpha));
            }
        }
    }

    // A line of `thickness` pixels across x0..x1 at row y, of `opacity` white.
    void Rule(
        int x0,
        int x1,
        int y,
        int thickness,
        float opacity,
        int width,
        int height,
        std::vector<uint8_t>* pixels
    ) {
        const uint8_t alpha = static_cast<uint8_t>(std::lround(255 * opacity));

        for (int ty = std::max(0, y); ty < std::min(height, y + thickness); ty++) {
            for (int tx = std::max(0, x0); tx < std::min(width, x1); tx++) {
                uint8_t* p = &(*pixels)[(static_cast<size_t>(ty) * width + tx) * 4];
                p[0] = p[1] = p[2] = 255;
                p[3] = std::max(p[3], alpha);
            }
        }
    }

    // `text` rasterised in `style`, with its pen on `baseline`; its advance, 0 for no ink.
    float
    Place(const std::wstring& text, const TextStyle& style, float opacity, float baseline, PlacedLine* out) {
        out->opacity = opacity;
        out->penY = static_cast<int>(std::lround(baseline));
        return RasterText(text.c_str(), style, 0, 0, 0, &out->raster) ? out->raster.advance : 0;
    }

    // The two-column rows under the rule: the facts, or the message's steps numbered from 1.
    std::vector<EmptyFact> RowsOf(const EmptyText& text) {
        if (text.message.heading.empty()) {
            return text.facts;
        }

        std::vector<EmptyFact> rows;

        for (size_t i = 0; i < text.message.steps.size(); i++) {
            rows.push_back({std::to_wstring(i + 1), text.message.steps[i]});
        }

        return rows;
    }

}

bool SameText(const EmptyText& a, const EmptyText& b) {
    const EmptyMessage &m = a.message, &n = b.message;

    if (a.title != b.title || a.lines != b.lines || a.facts.size() != b.facts.size() || m.heading != n.heading
        || m.tone != n.tone || m.steps != n.steps || m.note != n.note) {
        return false;
    }

    for (size_t i = 0; i < a.facts.size(); i++) {
        const EmptyFact &x = a.facts[i], &y = b.facts[i];

        if (x.label != y.label || x.value != y.value || x.placeholder != y.placeholder
            || x.waiting != y.waiting || x.barWidth != y.barWidth || x.tone != y.tone) {
            return false;
        }
    }

    return true;
}

void EmptyState::Draw(const EmptyText& text, uint32_t width, uint32_t height) {
    if (!hasBuilt_ || width != builtWidth_ || height != builtHeight_ || !SameText(text, built_)) {
        Build(text, width, height);
    }

    gs_effect_t* solid = obs_get_base_effect(OBS_EFFECT_SOLID);
    vec4 colour;
    vec4_set(&colour, kScreen[0], kScreen[1], kScreen[2], kScreen[3]);
    gs_effect_set_vec4(gs_effect_get_param_by_name(solid, "color"), &colour);

    while (gs_effect_loop(solid, "Solid")) {
        gs_draw_sprite(nullptr, 0, width, height);
    }

    if (!texture_) {
        return;
    }

    gs_effect_t* image = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    gs_effect_set_texture(gs_effect_get_param_by_name(image, "image"), texture_);

    gs_blend_state_push();
    gs_enable_blending(true);
    gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);
    gs_matrix_push();
    gs_matrix_translate3f(
        std::floor((static_cast<float>(width) - textWidth_) / 2),
        std::floor((static_cast<float>(height) - textHeight_) / 2),
        0
    );

    while (gs_effect_loop(image, "Draw")) {
        gs_draw_sprite(texture_, 0, 0, 0);
    }

    DrawBars();
    gs_matrix_pop();
    gs_blend_state_pop();
}

// The waiting placeholders' bars as of now. Inside Draw's blending and placing of the text. The colour is
// set for each bar: an effect's parameters don't outlast the pass that used them.
void EmptyState::DrawBars() {
    gs_effect_t* solid = obs_get_base_effect(OBS_EFFECT_SOLID);
    gs_eparam_t* colourParam = gs_effect_get_param_by_name(solid, "color");
    const double now = static_cast<double>(os_gettime_ns()) * 1e-9;

    for (size_t i = 0; i < bars_.size(); i++) {
        const EmptyBar& bar = bars_[i];
        const double phase = (now - static_cast<double>(i) * kBarStaggerS) / kBarPeriodS;
        const float pulse = static_cast<float>(0.5 - 0.5 * std::cos(2 * kPi * phase));  // 0 to 1
        vec4 colour;
        vec4_set(&colour, 1, 1, 1, kBarDim + (kBarBright - kBarDim) * pulse);
        gs_effect_set_vec4(colourParam, &colour);
        gs_matrix_push();
        gs_matrix_translate3f(static_cast<float>(bar.x), static_cast<float>(bar.y), 0);

        while (gs_effect_loop(solid, "Solid")) {
            gs_draw_sprite(nullptr, 0, static_cast<uint32_t>(bar.width), static_cast<uint32_t>(bar.height));
        }

        gs_matrix_pop();
    }
}

void EmptyState::Destroy() {
    if (texture_) {
        gs_texture_destroy(texture_);
    }

    texture_ = nullptr;
    hasBuilt_ = false;
}

void EmptyState::Build(const EmptyText& text, uint32_t width, uint32_t height) {
    Destroy();
    built_ = text;
    hasBuilt_ = true;
    builtWidth_ = width, builtHeight_ = height;

    const EmptyTextImage image = RasteriseEmptyText(text, height);
    textWidth_ = image.width, textHeight_ = image.height;

    if (!image.ink) {
        return;  // the screen alone
    }

    const uint8_t* rows[] = {image.bgra.data()};
    texture_ = gs_texture_create(
        static_cast<uint32_t>(image.width),
        static_cast<uint32_t>(image.height),
        GS_BGRA,
        1,
        rows,
        0
    );

    bars_ = image.bars;
}

// The title, then the lines, each centred, then under a rule the facts or a message: rows of labels
// right-aligned to a column and values left-aligned beside it, the two columns centred together, with a
// message's heading centred above its steps and its note under them. As wide as the widest part.
EmptyTextImage RasteriseEmptyText(const EmptyText& text, uint32_t height) {
    const float h = static_cast<float>(height);
    const float titleSize = std::max(8.0f, std::round(h * kTitleSize));
    const float lineSize = std::max(8.0f, std::round(h * kLineSize));
    const float factSize = std::max(8.0f, std::round(h * kFactSize));
    TextStyle style;
    style.family = L"Segoe UI";

    style.weight = 600;
    style.size = titleSize;
    std::vector<PlacedLine> centred(1 + text.lines.size());
    float baseline = titleSize;
    float widest = Place(text.title, style, 1.0f, baseline, &centred[0]);

    style.weight = 400;
    style.size = lineSize;

    for (size_t i = 0; i < text.lines.size(); i++) {
        baseline += i == 0 ? titleSize * kTitleGap + lineSize : lineSize * kLineLeading;
        widest = std::max(widest, Place(text.lines[i], style, kLineOpacity, baseline, &centred[i + 1]));
    }

    // Under the rule: the message's heading, then the rows (the facts, or the message's steps), then its
    // note.
    const EmptyMessage& message = text.message;
    const std::vector<EmptyFact> rows = RowsOf(text);
    const bool under = !rows.empty() || !message.heading.empty();
    std::vector<PlacedLine> labels(rows.size()), values(rows.size());
    PlacedLine heading, note;
    float labelWidth = 0, valueWidth = 0, headingWidth = 0, noteWidth = 0;
    int ruleY = 0;
    style.size = factSize;

    if (under) {
        ruleY = static_cast<int>(std::lround(baseline + lineSize * kRuleGap));
        baseline = static_cast<float>(ruleY);
        float step = factSize * kFactsGap;  // to the next baseline: the first one's from the rule

        if (!message.heading.empty()) {
            baseline += step;
            style.weight = 600;
            headingWidth = Place(message.heading, style, kValueOpacity, baseline, &heading);
            heading.colour = ToneColour(message.tone);
            style.weight = 400;
            step = factSize * kFactLeading;
        }

        for (size_t i = 0; i < rows.size(); i++) {
            baseline += i == 0 ? step : factSize * kFactLeading;
            labelWidth = std::max(
                labelWidth,
                Place(rows[i].label, style, kLabelOpacity, baseline, &labels[i])
            );
            valueWidth = std::max(
                valueWidth,
                Place(
                    rows[i].value,
                    style,
                    rows[i].placeholder ? kLabelOpacity : kValueOpacity,
                    baseline,
                    &values[i]
                )
            );
            values[i].colour = ToneColour(rows[i].tone);

            if (rows[i].waiting) {
                valueWidth = std::max(valueWidth, factSize * rows[i].barWidth);
            }
        }

        if (!message.note.empty()) {
            baseline += factSize * kFactLeading;
            noteWidth = Place(message.note, style, kLabelOpacity, baseline, &note);
        }
    }

    const float gap = rows.empty() ? 0 : factSize * kColumnGap;
    const float columns = labelWidth + gap + valueWidth;
    const float block = std::max({columns, headingWidth, noteWidth});
    const float last = under ? factSize : lineSize;

    EmptyTextImage image;
    image.ink = widest > 0 || block > 0;
    image.width = static_cast<int>(std::ceil(std::max(widest, block))) + 2;
    image.height = static_cast<int>(std::ceil(baseline + last * 0.5f));  // room for descenders
    image.bgra.assign(static_cast<size_t>(image.width) * image.height * 4, 0);

    if (!message.heading.empty()) {
        centred.push_back(heading);
    }

    if (!message.note.empty()) {
        centred.push_back(note);
    }

    for (PlacedLine& line : centred) {
        line.penX = static_cast<int>(std::lround((image.width - line.raster.advance) / 2));
        Blend(line, image.width, image.height, &image.bgra);
    }

    if (!under) {
        return image;
    }

    const float ruleLeft = (image.width - block) / 2, left = (image.width - columns) / 2;
    // 4 pixels at 2160: still a line where OBS shows the canvas small (the properties dialog's preview).
    const int thickness = std::max(1, static_cast<int>(std::lround(h / 540)));
    const int ruleX0 = static_cast<int>(std::lround(ruleLeft)),
              ruleX1 = static_cast<int>(std::lround(ruleLeft + block));
    Rule(ruleX0, ruleX1, ruleY, thickness, kRuleOpacity, image.width, image.height, &image.bgra);

    for (size_t i = 0; i < rows.size(); i++) {
        labels[i].penX = static_cast<int>(std::lround(left + labelWidth - labels[i].raster.advance));
        values[i].penX = static_cast<int>(std::lround(left + labelWidth + gap));
        Blend(labels[i], image.width, image.height, &image.bgra);
        Blend(values[i], image.width, image.height, &image.bgra);

        if (rows[i].waiting) {
            image.bars.push_back({
                values[i].penX,
                values[i].penY - static_cast<int>(std::lround(factSize * kBarRaise)),
                static_cast<int>(std::lround(factSize * rows[i].barWidth)),
                static_cast<int>(std::lround(factSize * kBarHeight)),
            });
        }
    }

    return image;
}

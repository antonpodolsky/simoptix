// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// EmptyState: what a VR Mirror source shows when it has no image: a dark screen with a title, a few lines
// of text, and under a rule a few facts, each a label and a value, or a message in their place. The text is
// rasterised by DirectWrite (shared\text_raster.h) into one texture at the size it is drawn, and drawn 1:1,
// centred. Rebuilt only when the text or the source's size changes: a waiting placeholder is a bar of its
// own, drawn over it, whose brightness pulses (a skeleton loader).
//
// Knows nothing about the game, the demo or what the text says. EmptyState: graphics thread only.
#pragma once

#include <graphics/graphics.h>

#include <cstdint>
#include <string>
#include <vector>

// How a fact's value reads: plainly, as good news (green), or as a warning (amber).
enum class FactTone { kPlain, kGood, kWarning };

// One fact under the rule: its label, right-aligned in a column, and its value beside it.
struct EmptyFact {
    std::wstring label, value;
    bool placeholder = false;  // the value stands for one not known yet: drawn as dim as the labels
    bool waiting = false;      // ...for one on its way: a pulsing bar where the value will be
    float barWidth = 0;        // waiting: the bar's width in the facts' ems, about the coming value's
    FactTone tone = FactTone::kPlain;
};

// A message under the rule in place of the facts: an error, or the current mode's instructions. A heading
// in its tone's colour, numbered steps in the facts' columns, and a dim note under them.
struct EmptyMessage {
    std::wstring heading;  // empty: no message, the facts are shown
    FactTone tone = FactTone::kPlain;
    std::vector<std::wstring> steps;
    std::wstring note;  // empty: none
};

// The text of an empty state.
struct EmptyText {
    std::wstring title;
    std::vector<std::wstring> lines;  // under the title, each centred
    std::vector<EmptyFact> facts;     // under a rule; none (and no message): no rule
    EmptyMessage message;             // in the facts' place when it has a heading
};

bool SameText(const EmptyText& a, const EmptyText& b);

// A waiting placeholder's bar in the image: its top left and size (pixels).
struct EmptyBar {
    int x = 0, y = 0, width = 0, height = 0;
};

// The text of an empty state as an image: white, values in their tone's colour; straight alpha, BGRA. The
// waiting placeholders' bars are apart from it, drawn over it as they pulse.
struct EmptyTextImage {
    int width = 0, height = 0;
    std::vector<uint8_t> bgra;
    bool ink = false;  // false: nothing to draw
    std::vector<EmptyBar> bars;
};

// The text laid out and rasterised for a screen `height` pixels high. Any thread.
EmptyTextImage RasteriseEmptyText(const EmptyText& text, uint32_t height);

class EmptyState {
public:
    // Draws `text` centred on a dark width x height screen, with its top left at (0, 0).
    void Draw(const EmptyText& text, uint32_t width, uint32_t height);
    void Destroy();  // inside obs_enter_graphics, or on the graphics thread

private:
    void Build(const EmptyText& text, uint32_t width, uint32_t height);
    void DrawBars();

    EmptyText built_;
    bool hasBuilt_ = false;
    uint32_t builtWidth_ = 0, builtHeight_ = 0;
    gs_texture_t* texture_ = nullptr;  // the text, or null if it has no ink
    int textWidth_ = 0, textHeight_ = 0;
    std::vector<EmptyBar> bars_;
};

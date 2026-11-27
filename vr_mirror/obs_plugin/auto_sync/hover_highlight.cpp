// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "hover_highlight.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace {

    // The grid: directions in tracking space, 0.4° apart, from 50° left to 50° right and from 35° up to
    // 35° down of straight ahead, where a menu panel floats. Blocks of 3x3 cells (1.2°): a menu item's
    // highlight covers a few.
    constexpr TrackingGrid kGrid = {0.4f, -50, 35, 249, 174};
    constexpr int kBlock = 3;
    constexpr int kBlockColumns = kGrid.columns / kBlock, kBlockRows = kGrid.rows / kBlock;
    constexpr int kBlocks = kBlockColumns * kBlockRows;
    constexpr float kUnseenBlock = -1;

    // A frame's step: the mean change of its kTop most changed blocks, if its median block changed less
    // than kQuiet and it sees at least kLeastSeen blocks; else 0.
    constexpr int kTop = 4, kLeastSeen = 200;
    constexpr float kQuiet = 1.5f;
    // A hover's frame steps at least kMinStep, and kStandOut times the median step around it.
    constexpr float kMinStep = 4, kStandOut = 6;
    // Frames further apart than kGapS don't follow on; steps are kept for kKeepS.
    constexpr double kGapS = 0.05, kKeepS = 3;

    void ToBlocks(const uint8_t* grid, std::vector<float>* means) {
        means->assign(kBlocks, kUnseenBlock);

        for (int by = 0; by < kBlockRows; by++) {
            for (int bx = 0; bx < kBlockColumns; bx++) {
                float sum = 0;
                bool seen = true;

                for (int y = by * kBlock; y < (by + 1) * kBlock && seen; y++) {
                    for (int x = bx * kBlock; x < (bx + 1) * kBlock; x++) {
                        const uint8_t value = grid[y * kGrid.columns + x];
                        seen = seen && value != kGridUnseen;
                        sum += value;
                    }
                }

                if (seen) {
                    (*means)[by * kBlockColumns + bx] = sum / (kBlock * kBlock);
                }
            }
        }
    }

}

HoverHighlight::HoverHighlight(const GreyLayout& layout)
    : layout_(layout),
      directions_(GridDirections(kGrid)),
      grid_(static_cast<size_t>(kGrid.Cells())) {}

void HoverHighlight::Reset() {
    hasPrevious_ = false;
    steps_.clear();
}

void HoverHighlight::Add(const GreyFrame& frame) {
    LayOnGrid(kGrid, directions_, layout_, frame, grid_.data());
    ToBlocks(grid_.data(), &blocks_);

    if (hasPrevious_ && frame.time - previousTime_ <= kGapS) {
        changes_.clear();

        for (int b = 0; b < kBlocks; b++) {
            if (blocks_[b] != kUnseenBlock && previousBlocks_[b] != kUnseenBlock) {
                changes_.push_back(std::fabs(blocks_[b] - previousBlocks_[b]));
            }
        }

        float step = 0;

        if (static_cast<int>(changes_.size()) >= kLeastSeen) {
            std::nth_element(changes_.begin(), changes_.begin() + changes_.size() / 2, changes_.end());
            const float median = changes_[changes_.size() / 2];
            std::partial_sort(
                changes_.begin(),
                changes_.begin() + kTop,
                changes_.end(),
                std::greater<float>()
            );

            if (median < kQuiet) {
                for (int i = 0; i < kTop; i++) {
                    step += changes_[i] / kTop;
                }
            }
        }

        steps_.push_back({frame.time, step});
    } else {
        steps_.clear();  // a gap: the frames before don't follow on
    }

    std::swap(blocks_, previousBlocks_);
    hasPrevious_ = true;
    previousTime_ = frame.time;

    while (!steps_.empty() && steps_.front().time < frame.time - kKeepS) {
        steps_.pop_front();
    }
}

HoverLook HoverHighlight::Find(double from, double to) const {
    HoverLook look;
    look.minStep = kMinStep;
    look.standOut = kStandOut;

    if (!steps_.empty()) {
        look.heldFrom = steps_.front().time;
        look.heldTo = steps_.back().time;
    }

    if (steps_.empty() || steps_.front().time > from || steps_.back().time < to) {
        return look;
    }

    std::vector<float> around;
    const Step* best = nullptr;
    const Step* second = nullptr;

    for (const Step& s : steps_) {
        if (s.time < from || s.time > to) {
            continue;
        }

        around.push_back(s.step);

        if (!best || s.step > best->step) {
            second = best;
            best = &s;
        } else if (!second || s.step > second->step) {
            second = &s;
        }
    }

    look.frames = static_cast<int>(around.size());

    if (!best || around.size() < 3) {
        look.result = HoverLook::Result::kTooFew;
        return look;
    }

    std::nth_element(around.begin(), around.begin() + around.size() / 2, around.end());
    look.median = around[around.size() / 2];
    look.best = best->time;
    look.bestStep = best->step;
    look.second = second->time;
    look.secondStep = second->step;

    if (best->step < kMinStep) {
        look.result = HoverLook::Result::kTooSmall;
    } else if (best->step < kStandOut * look.median) {
        look.result = HoverLook::Result::kNotStandingOut;
    } else {
        look.result = HoverLook::Result::kLit;
    }

    return look;
}

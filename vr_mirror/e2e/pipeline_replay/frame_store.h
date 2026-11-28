// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// FrameStore: the OBS plugin's store of copied frames (FrameRing) as far as the frame picker sees
// it: a ring of entries, each new frame written into the next one and ready once its copy
// finished, resized as the picker asks, and whether it still holds a frame. As in FrameRing, a new
// size applies at the next frame's copy: shrinking keeps the newest entries, compacted to the new
// size, and frees the rest; growing keeps every frame and adds entries as they are first used. For
// pipeline_replay's runs; knows nothing about textures.
#pragma once

#include "pipeline/pipeline_types.h"

namespace pipeline_replay {

    struct FrameStore {
        static constexpr int kMaxSlots = 128;  // FrameRing::kMaxSlots
        StoredFrame slots[kMaxSlots];
        StoredFrame pendingFrame[kMaxSlots];
        bool pending[kMaxSlots] = {};
        int count = kMaxSlots, next = 0;
        int wantCount = kMaxSlots;  // Resize's, applied at the next Add

        void Resize(int want) { wantCount = want; }

        // A frame arrived; its copy is seen complete at `ready`.
        void Add(uint64_t serial, double arrival, double smoothTime, double slotTime, double ready) {
            ApplySize();
            const int i = next;
            next = (next + 1) % count;
            slots[i].valid = false;  // being rewritten
            pending[i] = true;
            pendingFrame[i] = {true, serial, arrival, smoothTime, slotTime, ready, true};
        }

        // Copies finished by `now` become ready.
        void Finish(double now) {
            for (int i = 0; i < kMaxSlots; i++) {
                if (pending[i] && pendingFrame[i].ready <= now) {
                    slots[i] = pendingFrame[i], pending[i] = false;
                }
            }
        }

        // The frame is in an entry in use, ready or still being copied.
        bool Holds(uint64_t serial) const {
            for (int i = 0; i < count; i++) {
                if ((slots[i].valid && slots[i].serial == serial)
                    || (pending[i] && pendingFrame[i].serial == serial)) {
                    return true;
                }
            }
            return false;
        }

        // Entries in use that hold a frame, ready or still being copied.
        int Holding() const {
            int holding = 0;
            for (int i = 0; i < count; i++) {
                if (slots[i].valid || pending[i]) {
                    holding++;
                }
            }

            return holding;
        }

    private:
        // Shrinking by truncation can discard the newest entries, not the oldest: the ring overwrites
        // in index order, so after it wraps, the newest entry's index can be anywhere. This keeps the
        // `wantCount` most recently written entries (oldest kept at the new 0, newest at the end), and
        // frees the rest, wherever they physically were.
        void ApplySize() {
            if (wantCount >= count) {
                count = wantCount;  // growing: nothing moved, made as first used
                return;
            }

            StoredFrame keptSlots[kMaxSlots], keptPending[kMaxSlots];
            bool keptFlag[kMaxSlots] = {};

            for (int k = 0; k < wantCount; k++) {
                const int oldIndex = (next + count - wantCount + k) % count;
                keptSlots[k] = slots[oldIndex];
                keptPending[k] = pendingFrame[oldIndex];
                keptFlag[k] = pending[oldIndex];
            }

            for (int k = 0; k < wantCount; k++) {
                slots[k] = keptSlots[k];
                pendingFrame[k] = keptPending[k];
                pending[k] = keptFlag[k];
            }

            for (int i = wantCount; i < kMaxSlots; i++) {
                slots[i] = {}, pending[i] = false;
            }

            count = wantCount;
            next = 0;  // the oldest kept, next to be overwritten
        }
    };

}

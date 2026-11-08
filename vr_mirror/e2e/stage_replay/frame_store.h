// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// FrameStore: the OBS plugin's store of copied frames (FrameRing) as far as the frame picker sees
// it: a ring of entries, each new frame written into the next one and ready once its copy
// finished, resized as the picker asks (shrinking frees the entries past the new size, growing
// starts over). For stage_replay's runs; knows nothing about textures.
#pragma once

#include "pipeline/pipeline_types.h"

namespace stage_replay {

    struct FrameStore {
        static constexpr int kMaxSlots = 128;  // FrameRing::kMaxSlots
        StoredFrame slots[kMaxSlots];
        StoredFrame pendingFrame[kMaxSlots];
        bool pending[kMaxSlots] = {};
        int count = kMaxSlots, next = 0;

        void Resize(int want) {
            if (want < count) {
                for (int i = want; i < count; i++) {
                    slots[i] = {}, pending[i] = false;
                }
                count = want;
                next %= count;
            } else if (want > count) {
                for (int i = 0; i < kMaxSlots; i++) {
                    slots[i] = {}, pending[i] = false;
                }
                count = want;
                next = 0;
            }
        }

        // A frame arrived; its copy is seen complete at `ready`.
        void Add(uint64_t serial, double arrival, double smoothTime, double slotTime, double ready) {
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
    };

}

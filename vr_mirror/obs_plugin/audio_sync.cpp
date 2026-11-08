// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "audio_sync.h"

#include <obs-module.h>
#include <util/platform.h>

#include <atomic>
#include <cmath>

#include "shared/program_delay.h"

namespace {

    constexpr const char*
        kShiftKey = "simoptix_vr_mirror_sync_shift_ns";  // private settings: the shift applied
    constexpr uint64_t kRecheckNs = 2000000000ull;       // 2 s
    constexpr uint64_t kStartupNs = 30000000000ull;  // 30 s after loading: restore shifts left from before

    // Graphics thread (the tick callback).
    uint64_t g_loaded = 0, g_nextCheck = 0;
    int64_t g_queued = -1;  // the shift last queued (ns; -1: none yet)
    // The shift the UI task applies (ns), set when it is queued.
    std::atomic<int64_t> g_shift{0};

    // The delay as a shift, to the millisecond (so the copy delay's small changes don't touch the offsets).
    int64_t ShiftNow() {
        return int64_t(std::llround(ProgramDelay() * 1000)) * 1000000;
    }

    // OBS's UI thread: every audio source to its own offset plus the shift.
    void Apply(void*) {
        struct Ctx {
            int64_t shift;
            int changed;
        } ctx = {g_shift.load(), 0};

        obs_enum_sources(
            [](void* p, obs_source_t* source) {
                auto* c = static_cast<Ctx*>(p);
                if (!(obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO)) {
                    return true;
                }
                obs_data_t* priv = obs_source_get_private_settings(source);
                const int64_t applied = obs_data_get_int(priv, kShiftKey);
                if (applied != c->shift) {
                    obs_source_set_sync_offset(
                        source,
                        ShiftedSyncOffset(obs_source_get_sync_offset(source), applied, c->shift)
                    );
                    obs_data_set_int(priv, kShiftKey, c->shift);
                    c->changed++;
                }
                obs_data_release(priv);
                return true;
            },
            &ctx
        );

        if (ctx.changed) {
            blog(
                LOG_INFO,
                "[simoptix_vr_mirror] audio sync: %d audio source%s now %+lld ms on top of their own offsets "
                "(the picture's delay)",
                ctx.changed,
                ctx.changed == 1 ? "" : "s",
                static_cast<long long>(ctx.shift / 1000000)
            );
        }
    }

    void Tick(void*, float) {
        const uint64_t now = os_gettime_ns();
        if (!g_loaded) {
            g_loaded = now;
        }
        const int64_t shift = ShiftNow();
        const bool changed = shift != g_queued;
        const bool recheck = (shift != 0 || now - g_loaded < kStartupNs) && now >= g_nextCheck;
        if (!changed && !recheck) {
            return;
        }
        g_queued = shift;
        g_nextCheck = now + kRecheckNs;
        g_shift = shift;
        obs_queue_task(OBS_TASK_UI, Apply, nullptr, false);
    }

}

void AudioSync::Start() {
    obs_add_tick_callback(Tick, nullptr);
}

void AudioSync::Stop() {
    obs_remove_tick_callback(Tick, nullptr);
}

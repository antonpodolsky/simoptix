// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "audio_sync.h"

#include <obs-module.h>
#include <util/platform.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

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
    // The game's sound (SetGameSound): its source and its correction (ns), and a count of changes the
    // tick callback compares with what it queued.
    std::mutex g_gameMutex;
    std::string g_gameName;
    int64_t g_gameCorrection = 0;
    std::atomic<uint32_t> g_gameChanges{0};
    uint32_t g_gameQueued = 0;  // graphics thread

    // The delay as a shift, to the millisecond (so the copy delay's small changes don't touch the offsets).
    int64_t ShiftNow() {
        return int64_t(std::llround(ProgramDelay() * 1000)) * 1000000;
    }

    // OBS's UI thread: sets every audio source's sync offset to its own offset plus the shift, and the game's
    // sound's sync offset to the shift plus auto sync's correction.
    void Apply(void*) {
        struct Ctx {
            int64_t shift;
            std::string game;
            int64_t correction;
            int changed;
        } ctx = {g_shift.load(), {}, 0, 0};

        // When the delay is 0 (the VR Mirror source is hidden), the game's sound gets its own offset back
        // too.
        if (ctx.shift != 0) {
            std::lock_guard<std::mutex> lock(g_gameMutex);
            ctx.game = g_gameName;
            ctx.correction = g_gameCorrection;
        }

        obs_enum_sources(
            [](void* p, obs_source_t* source) {
                auto* c = static_cast<Ctx*>(p);

                if (!(obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO)) {
                    return true;
                }

                const bool game = !c->game.empty() && c->game == obs_source_get_name(source);
                obs_data_t* priv = obs_source_get_private_settings(source);
                const int64_t applied = obs_data_get_int(priv, kShiftKey);
                const int64_t current = obs_source_get_sync_offset(source);
                const int64_t gameOffset = c->shift + c->correction;
                const int64_t shift = game ? ShiftReplacingOwnOffset(current, applied, gameOffset) : c->shift;

                if (applied != shift) {
                    obs_source_set_sync_offset(source, ShiftedSyncOffset(current, applied, shift));
                    obs_data_set_int(priv, kShiftKey, shift);
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
                "(the view's delay)",
                ctx.changed,
                ctx.changed == 1 ? "" : "s",
                static_cast<long long>(ctx.shift / 1000000)
            );
        }

        if (ctx.changed && !ctx.game.empty()) {
            blog(
                LOG_INFO,
                "[simoptix_vr_mirror] audio sync: '%s' (the game's sound) gets the delay plus auto sync's "
                "%+lld ms, without its own offset",
                ctx.game.c_str(),
                static_cast<long long>(ctx.correction / 1000000)
            );
        }
    }

    void Tick(void*, float) {
        const uint64_t now = os_gettime_ns();
        if (!g_loaded) {
            g_loaded = now;
        }
        const int64_t shift = ShiftNow();
        const uint32_t gameChanges = g_gameChanges.load();
        const bool changed = shift != g_queued || gameChanges != g_gameQueued;
        const bool recheck = (shift != 0 || now - g_loaded < kStartupNs) && now >= g_nextCheck;
        if (!changed && !recheck) {
            return;
        }
        g_queued = shift;
        g_gameQueued = gameChanges;
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

void AudioSync::SetGameSound(const std::string& name, double correctionS) {
    {
        std::lock_guard<std::mutex> lock(g_gameMutex);
        g_gameName = name;
        g_gameCorrection = static_cast<int64_t>(std::llround(correctionS * 1000)) * 1000000;
    }

    g_gameChanges++;
}

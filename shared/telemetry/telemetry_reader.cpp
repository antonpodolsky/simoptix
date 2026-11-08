// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "shared/telemetry/telemetry_reader.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>

#include "shared/log.h"
#include "shared/telemetry/game_adapter.h"
#include "shared/telemetry/game_registry.h"
#include "shared/telemetry/qpc_clock.h"
#include "shared/telemetry/telemetry_area.h"
#include "shared/thread_cpu.h"

namespace {

    constexpr DWORD kLookAgainMs = 1000;  // while no game is connected
    constexpr int kReadWaitMs = 200;  // the longest a connected adapter waits for its game: Shutdown's delay
    constexpr double kFootprintS = 10;

    struct Reader {
        // Ensure and Shutdown, under `mutex`.
        std::mutex mutex;
        std::thread thread;
        // This plugin doesn't start the reader again: it is unloading (Shutdown), or its reader failed.
        std::atomic<bool> stopped{false};
        std::atomic<bool> done{true};  // the thread has finished (or never started): joining it doesn't wait

        // Reader thread only.
        int wakeups = 0;       // passes since the last footprint line
        double footSince = 0;  // performance-counter seconds; 0: not started
        uint64_t footClock = 0, footCycles = 0;

        // This plugin's mark in the area's `reader`: the address of its Reader, which no other DLL shares.
        int64_t Token() const { return reinterpret_cast<int64_t>(this); }
        bool Wanted(const TelemetryArea* a) const { return !stopped && a->users.load() > 0; }

        void Run(TelemetryArea* a);
        // Reads while something needs telemetry: looks for a game, follows it while it runs.
        void Serve(TelemetryArea* a);
        // Runs a connected game until it has gone or nothing needs telemetry.
        void Follow(TelemetryArea* a, GameAdapter* game);
        // A pass while connected: counts it, and every 10 s logs the thread's CPU and wake-ups.
        void CountPass(const char* game);
    };

    // Never destroyed: the thread is stopped by Shutdown, and joining a thread while the DLL unloads could
    // deadlock on the loader lock.
    Reader& TheReader() {
        static Reader* reader = new Reader();
        return *reader;
    }

    // The game has gone, or the reader stopped: overlays reading after this see no game.
    void WriteDisconnected(TelemetryArea* a) {
        SessionState s = {};
        s.connected = false;
        s.t = QpcNow();
        s.phase = SessionPhase::Unknown;
        s.session = a->sessions.session;
        RingWrite(&a->ring, s, nullptr);
    }

    void Reader::CountPass(const char* game) {
        wakeups++;
        const double now = QpcNow();
        if (footSince == 0) {
            footSince = now;
            footClock = CpuClock();
            footCycles = ThreadCycles(GetCurrentThread());
            wakeups = 0;
            return;
        }

        if (now - footSince < kFootprintS) {
            return;
        }
        const uint64_t clock = CpuClock(), cycles = ThreadCycles(GetCurrentThread());
        CoreLog(
            "footprint: telemetry thread (%s) %.2f%% CPU (of one core), %.0f wake-ups/s",
            game,
            CpuPercent(cycles, footCycles, clock - footClock),
            wakeups / (now - footSince)
        );

        footSince = now;
        footClock = clock;
        footCycles = cycles;
        wakeups = 0;
    }

    void Reader::Follow(TelemetryArea* a, GameAdapter* game) {
        footSince = 0;  // the footprint counts from here
        CoreLog("telemetry: connected to %s", game->Name());
        GameUpdate u = {};
        while (Wanted(a)) {
            const GameRead read = game->Read(kReadWaitMs, &u);
            if (read == GameRead::Gone) {
                break;
            }
            CountPass(game->Name());
            if (read != GameRead::Update) {
                continue;
            }

            u.session.connected = true;
            if (u.hasCar) {
                u.car.session = CountSession(&a->sessions, u.sessionKey, u.sessionClock);
            }
            u.session.session = a->sessions.session;
            RingWrite(&a->ring, u.session, u.hasCar ? &u.car : nullptr);
        }

        game->Close();
        WriteDisconnected(a);
        CoreLog("telemetry: %s disconnected", game->Name());
    }

    void Reader::Serve(TelemetryArea* a) {
        size_t count = 0;
        GameAdapter* const* games = RegisteredGames(&count);
        bool loggedWaiting = false;
        while (Wanted(a)) {
            GameAdapter* found = nullptr;
            for (size_t i = 0; i < count && !found; i++) {
                if (games[i]->Open()) {
                    found = games[i];
                }
            }

            if (found) {
                loggedWaiting = false;
                Follow(a, found);
                continue;
            }

            if (!loggedWaiting) {
                char names[256] = "";
                for (size_t i = 0; i < count; i++) {
                    if (i > 0) {
                        strncat_s(names, ", ", _TRUNCATE);
                    }
                    strncat_s(names, games[i]->Name(), _TRUNCATE);
                }

                if (count > 0) {
                    CoreLog("telemetry: waiting for a game (%s)", names);
                } else {
                    CoreLog("telemetry: built without any game, so no telemetry");
                }

                loggedWaiting = true;
            }

            WaitForSingleObject(TelemetryWakeEvent(), kLookAgainMs);
        }
    }

    void Reader::Run(TelemetryArea* a) {
        const int64_t token = Token();
        for (;;) {
            Serve(a);

            // Let go of the reader's place, then look once more: something may have needed telemetry since
            // Serve saw that nothing did. If no plugin has taken the place meanwhile, carry on here.
            int64_t mine = token, none = 0;
            a->reader.compare_exchange_strong(mine, 0);
            if (!Wanted(a) || !a->reader.compare_exchange_strong(none, token)) {
                break;
            }
        }
    }

}

void TelemetryReader::Ensure(bool wait) {
    TelemetryArea* a = TheTelemetryArea();
    Reader& r = TheReader();
    if (r.stopped || a->users.load() <= 0 || a->reader.load() != 0) {
        return;
    }
    std::unique_lock<std::mutex> g(r.mutex, std::defer_lock);
    if (wait) {
        g.lock();
    } else if (!g.try_lock()) {
        return;
    }

    int64_t none = 0;
    if (r.stopped || !a->reader.compare_exchange_strong(none, r.Token())) {
        return;
    }
    if (r.thread.joinable()) {
        // This plugin's previous reader: it let go of the place, so it is ending. Where waiting isn't
        // allowed and it hasn't quite finished, hand the place back and try at the next call.
        if (!wait && !r.done) {
            int64_t mine = r.Token();
            a->reader.compare_exchange_strong(mine, 0);
            return;
        }

        r.thread.join();
    }

    r.done = false;
    CoreLog("telemetry: reader started");
    r.thread = std::thread([&r, a] {
        try {
            r.Run(a);
            CoreLog("telemetry: reader stopped");
        } catch (...) {
            r.stopped = true;
            WriteDisconnected(a);

            int64_t mine = r.Token();
            a->reader.compare_exchange_strong(mine, 0);

            CoreLog("telemetry: reader stopped by an unexpected error");
        }
        r.done = true;
    });
}

void TelemetryReader::Wake() {
    SetEvent(TelemetryWakeEvent());
}

void TelemetryReader::Shutdown() {
    Reader& r = TheReader();
    std::lock_guard<std::mutex> g(r.mutex);
    r.stopped = true;
    SetEvent(TelemetryWakeEvent());
    if (r.thread.joinable()) {
        r.thread.join();
    }
}

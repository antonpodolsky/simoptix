// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// LMU fake: publishes telemetry on LMU's shared-memory interface (LMU_Data and its events), so the
// Overlays plugin can be seen and measured live through its real telemetry path without the game.
// It publishes a made-up lap: the G-meter's demo lap (shared\telemetry\demo_lap.h), 100 times a
// second like the game. (The G-meter's own demo, in its properties, draws the same lap without LMU's
// interface.)
//
// With --race it also plays a race's scoring, for race auto-record (VR Mirror): a race session (or the
// session type given), formation lap from 0 s, start lights (LMU's game phase 4) from 5 s, green from
// 10 s, the player's checkered flag at the finish time (default 30 s), then the car slows to a stop
// over 5 s and stands. Speed is 30 m/s on the formation lap, 50 when green.
//
// Only for when LMU isn't running: it refuses if LMU_Data already exists. Knows nothing about OBS or
// the plugin; it fills only the fields the plugin reads.
//
// Usage: lmu_fake.exe [seconds (default 60)] [--start <lap seconds>]
//                     [--race [--session <LMU session type, default 10>] [--finish <seconds>]]

#include <windows.h>
#include <mmsystem.h>

#include <cmath>
#include <cstdint>  // before LMU's headers, which use uint8_t without including it
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>

#include "shared/telemetry/demo_lap.h"
#include "shared/unique_handle.h"
#include "SharedMemoryInterface.hpp"  // from LMU's Support\SharedMemoryInterface (see build.bat)

namespace {

    constexpr double kG = 9.80665;

    // The race's script (--race), seconds from the start.
    constexpr double kLightsAt = 5, kGreenAt = 10, kSlowFor = 5;

    // LMU's game phase and the player's speed (m/s) at time t of the race.
    void RaceAt(double t, double finish, unsigned char* phase, double* speed) {
        *phase = t < kLightsAt ? 3 : t < kGreenAt ? 4 : 5;  // formation lap, start-light countdown, green
        *speed = t < kGreenAt ? 30 : 50;
        if (t >= finish) {
            *speed = t < finish + kSlowFor ? 50 * (finish + kSlowFor - t) / kSlowFor : 0;
        }
    }

    double Seconds(const LARGE_INTEGER& from, const LARGE_INTEGER& freq) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return double(now.QuadPart - from.QuadPart) / freq.QuadPart;
    }

    // Sleeps until `at` seconds after `from` (Sleep: no busy loop).
    void SleepUntil(double at, const LARGE_INTEGER& from, const LARGE_INTEGER& freq) {
        const double wait = at - Seconds(from, freq);
        if (wait > 0) {
            Sleep(DWORD(wait * 1000));
        }
    }

    struct Options {
        double seconds = 60, start = 0, finish = 30;
        bool race = false;
        long session = 10;
    };

    Options ParseOptions(int argc, char** argv) {
        Options o;
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "--start") && i + 1 < argc) {
                o.start = atof(argv[++i]);
            } else if (!strcmp(argv[i], "--race")) {
                o.race = true;
            } else if (!strcmp(argv[i], "--session") && i + 1 < argc) {
                o.session = atol(argv[++i]);
            } else if (!strcmp(argv[i], "--finish") && i + 1 < argc) {
                o.finish = atof(argv[++i]);
            } else {
                o.seconds = atof(argv[i]);
            }
        }

        return o;
    }

    struct FakeMemory {
        UniqueHandle map, hold, data;
        MappedView<SharedMemoryLayout> layout;
        std::optional<SharedMemoryLock> lock;
    };

    // False, with the reason printed, when LMU's shared memory is already there or this one can't be made.
    bool CreateMemory(FakeMemory* m) {
        if (UniqueHandle existing(OpenFileMappingA(FILE_MAP_READ, FALSE, LMU_SHARED_MEMORY_FILE)); existing) {
            fprintf(stderr, "LMU_Data already exists (is Le Mans Ultimate running?): not faking it.\n");
            return false;
        }

        m->map.Reset(CreateFileMappingA(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            0,
            sizeof(SharedMemoryLayout),
            LMU_SHARED_MEMORY_FILE
        ));

        // manual, always set
        m->hold.Reset(CreateEventA(nullptr, TRUE, TRUE, LMU_SHARED_MEMORY_HOLD_EVENT));

        // auto, once per update
        m->data.Reset(CreateEventA(nullptr, FALSE, FALSE, LMU_SHARED_MEMORY_DATA_EVENT));
        if (m->map) {
            m->layout.Reset(
                static_cast<SharedMemoryLayout*>(MapViewOfFile(m->map.Get(), FILE_MAP_ALL_ACCESS, 0, 0, 0))
            );
        }

        m->lock = SharedMemoryLock::MakeSharedMemoryLock();
        if (!m->layout || !m->hold || !m->data || !m->lock) {
            fprintf(stderr, "couldn't create LMU's shared memory (error %lu)\n", GetLastError());
            return false;
        }

        return true;
    }

    // Writes the made-up lap's car at time t of the run and, in a race, the scoring. Returns the game phase
    // (0 when not racing).
    unsigned char PublishStep(FakeMemory& m, const Options& o, double t) {
        const DemoSample s = DemoLapAt(o.start + t);
        m.lock->Lock();
        SharedMemoryTelemetryData& tel = m.layout->data.telemetry;
        tel.activeVehicles = 1;
        tel.playerVehicleIdx = 0;
        tel.playerHasVehicle = true;
        TelemInfoV01& v = tel.telemInfo[0];
        v.mElapsedTime = t;

        // LMU's car coordinates: +x left, +z rear. The felt force is the acceleration's direction on
        // these axes (a left turn accelerates the car left and pushes the driver right).
        v.mLocalAccel.x = s.lateralG * kG;
        v.mLocalAccel.z = s.longitudinalG * kG;
        v.mUnfilteredBrake = s.brake;
        v.mUnfilteredThrottle = s.throttle;
        v.mABSActive = s.abs;
        v.mTCActive = s.tc;

        unsigned char phase = 0;
        if (o.race) {
            double speed;
            RaceAt(t, o.finish, &phase, &speed);
            v.mLocalVel.z = -speed;  // forward is -z

            ScoringInfoV01& scoring = m.layout->data.scoring.scoringInfo;
            strcpy(scoring.mTrackName, "lmu_fake");
            scoring.mSession = o.session;
            scoring.mGamePhase = phase;
            scoring.mInRealtime = true;
            scoring.mNumVehicles = 1;

            VehicleScoringInfoV01& player = m.layout->data.scoring.vehScoringInfo[0];
            player.mIsPlayer = true;
            player.mFinishStatus = t >= o.finish ? 1 : 0;
        }

        m.lock->Unlock();
        return phase;
    }

}

int main(int argc, char** argv) {
    const Options o = ParseOptions(argc, argv);
    FakeMemory memory;
    if (!CreateMemory(&memory)) {
        return 1;
    }

    timeBeginPeriod(1);
    LARGE_INTEGER freq, t0;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    printf("publishing a made-up lap for %.0f s, from lap time %.2f s\n", o.seconds, o.start);
    if (o.race) {
        printf(
            "race (session type %ld): lights at %.0f s, green at %.0f s, finish at %.0f s, stopped at "
            "%.0f s\n",
            o.session,
            kLightsAt,
            kGreenAt,
            o.finish,
            o.finish + kSlowFor
        );
    }

    unsigned char lastPhase = 0;
    for (int step = 0;; step++) {
        const double t = Seconds(t0, freq);
        if (t >= o.seconds) {
            break;
        }
        const unsigned char phase = PublishStep(memory, o, t);
        if (o.race && phase != lastPhase) {
            printf("%5.1f s: game phase %d\n", t, phase);
            lastPhase = phase;
        }

        SetEvent(memory.data.Get());
        SleepUntil((step + 1) * 0.01, t0, freq);  // the next update on a 10 ms grid
    }

    timeEndPeriod(1);
    printf("done\n");
    return 0;
}

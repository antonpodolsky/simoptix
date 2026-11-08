// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "shared/telemetry/games/lmu/lmu_telemetry.h"

#include <windows.h>

#include <cmath>
#include <cstring>
#include <optional>

#include "shared/telemetry/game_adapter.h"
#include "shared/telemetry/games/lmu/lmu_convert.h"
#include "shared/telemetry/qpc_clock.h"
#include "shared/unique_handle.h"
#include "SharedMemoryInterface.hpp"  // from LMU's Support\SharedMemoryInterface (see build.bat)

namespace {

    constexpr int kQuitAfterQuietWaits = 25;  // 200 ms waits with no update: 5 s means the game has quit

    // LMU's driving session as a key (game_adapter.h): its session type and track, FNV-1a.
    uint64_t SessionKey(long sessionType, const char* track) {
        uint64_t h = 14695981039346656037ull;
        auto mix = [&h](unsigned char c) { h = (h ^ c) * 1099511628211ull; };
        for (size_t i = 0; i < sizeof(sessionType); i++) {
            mix(static_cast<unsigned char>(sessionType >> (8 * i)));
        }
        for (const char* c = track; *c; c++) {
            mix(static_cast<unsigned char>(*c));
        }
        return h;
    }

    // Copies the session state under LMU's lock (all but connected, t and session).
    void CopySession(const SharedMemoryLayout* layout, SessionState* r) {
        const SharedMemoryTelemetryData& tel = layout->data.telemetry;
        const ScoringInfoV01& scoring = layout->data.scoring.scoringInfo;
        r->kind = SessionKindFromLmu(scoring.mSession);
        r->phase = SessionPhaseFromLmu(scoring.mGamePhase);
        r->driving = scoring.mInRealtime;
        int finishStatus = -1;
        for (long i = 0; i < scoring.mNumVehicles && i < 104; i++) {
            if (layout->data.scoring.vehScoringInfo[i].mIsPlayer) {
                finishStatus = layout->data.scoring.vehScoringInfo[i].mFinishStatus;
                break;
            }
        }

        r->result = PlayerResultFromLmu(finishStatus);
        const int idx = tel.playerVehicleIdx;
        r->hasCar = tel.playerHasVehicle && idx >= 0 && idx < 104;
        r->speed = 0;
        if (r->hasCar) {
            const TelemVect3& v = tel.telemInfo[idx].mLocalVel;
            r->speed = float(std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z));
        }
    }

    class Lmu : public GameAdapter {
    public:
        const char* Name() const override { return "Le Mans Ultimate"; }
        bool Open() override;
        GameRead Read(int timeoutMs, GameUpdate* out) override;
        void Close() override;

    private:
        // Copies the player car's sample under LMU's lock. Returns false for no car.
        bool Copy(CarSample* s, double* elapsed, long* sessionType, char* track);

        // Reader thread only (game_adapter.h). While connected: LMU's mapping, update signal and lock.
        UniqueHandle map_;
        MappedView<const SharedMemoryLayout> layout_;
        std::optional<SharedMemoryUpdateWaiter> waiter_;
        std::optional<SharedMemoryLock> lock_;
        double lastElapsed_ = -1;  // the last physics step handed over (LMU's session seconds)
        int quiet_ = 0;            // waits in a row with no update
    };

    bool Lmu::Open() {
        // The game creates the mapping and its events while it runs.
        map_.Reset(OpenFileMappingA(FILE_MAP_READ, FALSE, "LMU_Data"));
        if (map_) {
            waiter_ = SharedMemoryUpdateWaiter::MakeSharedMemoryUpdateWaiter();
        }
        if (waiter_) {
            layout_.Reset(
                static_cast<const SharedMemoryLayout*>(
                    MapViewOfFile(map_.Get(), FILE_MAP_READ, 0, 0, sizeof(SharedMemoryLayout))
                )
            );
        }

        if (layout_) {
            lock_ = SharedMemoryLock::MakeSharedMemoryLock();
        }
        if (!lock_) {
            Close();
            return false;
        }

        lastElapsed_ = -1;
        quiet_ = 0;
        return true;
    }

    void Lmu::Close() {
        lock_.reset();
        waiter_.reset();
        layout_.Reset();
        map_.Reset();
    }

    bool Lmu::Copy(CarSample* s, double* elapsed, long* sessionType, char* track) {
        const SharedMemoryTelemetryData& tel = layout_->data.telemetry;
        const int idx = tel.playerVehicleIdx;
        if (!tel.playerHasVehicle || idx < 0 || idx >= 104) {
            return false;
        }
        const TelemInfoV01& v = tel.telemInfo[idx];
        *elapsed = v.mElapsedTime;
        s->accel = CarVectorFromLmu(v.mLocalAccel.x, v.mLocalAccel.y, v.mLocalAccel.z);
        s->throttle = float(v.mUnfilteredThrottle);
        s->brake = float(v.mUnfilteredBrake);
        s->tcActive = v.mTCActive;
        s->absActive = v.mABSActive;

        const ScoringInfoV01& scoring = layout_->data.scoring.scoringInfo;
        *sessionType = scoring.mSession;
        memcpy(track, scoring.mTrackName, 64);
        track[63] = 0;
        return true;
    }

    GameRead Lmu::Read(int timeoutMs, GameUpdate* out) {
        if (!waiter_->WaitForNextFrame(timeoutMs)) {
            return ++quiet_ <= kQuitAfterQuietWaits ? GameRead::Nothing : GameRead::Gone;
        }

        quiet_ = 0;

        // Never make the game wait for us: if it holds the lock, skip this update.
        if (!lock_->TryLockSpin(2000)) {
            return GameRead::Nothing;
        }

        double elapsed = -1;
        long sessionType = 0;
        char track[64];
        const bool has = Copy(&out->car, &elapsed, &sessionType, track);
        CopySession(layout_.Get(), &out->session);
        lock_->Unlock();

        out->hasCar = has && elapsed != lastElapsed_;  // a car, and a new physics step
        if (out->hasCar) {
            lastElapsed_ = elapsed;
            out->sessionKey = SessionKey(sessionType, track);
            out->sessionClock = elapsed;
        }

        out->session.t = out->car.t = QpcNow();
        return GameRead::Update;
    }

}

GameAdapter* LmuAdapter() {
    // Never destroyed, like the telemetry reader that uses it: the reader's thread may still run while the
    // DLL unloads.
    static Lmu* lmu = new Lmu();
    return lmu;
}

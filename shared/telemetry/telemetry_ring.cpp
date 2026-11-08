// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "shared/telemetry/telemetry_ring.h"

namespace {

    // The newest updates a reader looks at: one fewer than the ring holds, as the oldest slot may be being
    // overwritten by the next update.
    constexpr int64_t kReadable = TelemetryRing::kSize - 1;

    const TelemetryEntry& Slot(const TelemetryRing* r, int64_t n) {
        return r->entries[n % TelemetryRing::kSize];
    }

    // Copies what `copy` takes from update `n`, if update n is still there, whole. False if it was
    // overwritten (or not written yet).
    template <typename Copy>
    bool ReadUpdate(const TelemetryRing* r, int64_t n, Copy copy) {
        const TelemetryEntry& e = Slot(r, n);
        const int64_t done = 2 * n + 2;
        if (e.seq.load(std::memory_order_acquire) != done) {
            return false;
        }
        copy(e);
        std::atomic_thread_fence(std::memory_order_acquire);
        return e.seq.load(std::memory_order_relaxed) == done;
    }

    bool TimeOf(const TelemetryRing* r, int64_t n, double* t) {
        return ReadUpdate(r, n, [t](const TelemetryEntry& e) { *t = e.t; });
    }

}

void RingWrite(TelemetryRing* r, const SessionState& session, const CarSample* car) {
    const int64_t n = r->count.load(std::memory_order_relaxed);
    TelemetryEntry& e = r->entries[n % TelemetryRing::kSize];
    e.seq.store(2 * n + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    e.t = session.t;
    e.session = session;
    e.hasCar = car != nullptr;
    if (car) {
        e.car = *car;
    }
    e.seq.store(2 * n + 2, std::memory_order_release);
    r->count.store(n + 1, std::memory_order_release);
}

size_t RingSince(const TelemetryRing* r, double since, double upTo, CarSample* out, size_t max) {
    const int64_t count = r->count.load(std::memory_order_acquire);

    // Back from the newest to the first update after `since`.
    int64_t first = count;
    double t;
    while (first > 0 && count - first < kReadable && TimeOf(r, first - 1, &t) && t > since) {
        first--;
    }
    size_t n = 0;
    for (int64_t i = first; i < count && n < max; i++) {
        bool hasCar = false;
        CarSample car;
        const bool whole = ReadUpdate(r, i, [&](const TelemetryEntry& e) {
            hasCar = e.hasCar;
            if (hasCar) {
                car = e.car;
            }
        });

        if (!whole || !hasCar) {
            continue;
        }
        if (car.t > upTo) {
            break;  // not due yet, nor the newer ones after it
        }
        out[n++] = car;
    }

    return n;
}

bool RingSessionAt(const TelemetryRing* r, double at, SessionState* out) {
    const int64_t count = r->count.load(std::memory_order_acquire);
    for (int64_t i = count - 1; i >= 0 && count - i <= kReadable; i--) {
        double t;
        if (!TimeOf(r, i, &t)) {
            break;  // overwritten: the older ones are gone too
        }
        if (t > at) {
            continue;
        }
        SessionState s;
        if (!ReadUpdate(r, i, [&s](const TelemetryEntry& e) { s = e.session; })) {
            break;
        }
        *out = s;
        return true;
    }

    *out = SessionState{};
    return false;
}

// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "shared/telemetry/telemetry.h"

#include "shared/program_delay.h"
#include "shared/telemetry/qpc_clock.h"
#include "shared/telemetry/shown_moment.h"
#include "shared/telemetry/telemetry_area.h"
#include "shared/telemetry/telemetry_reader.h"

namespace {

    // Reading is also where a plugin notices that the reader has gone (the other plugin unloaded) while
    // something still needs telemetry, and takes it over.
    TelemetryArea* ForReading() {
        TelemetryReader::Ensure(false);
        return TheTelemetryArea();
    }

}

void Telemetry::Acquire() {
    TheTelemetryArea()->users.fetch_add(1);
    TelemetryReader::Ensure(true);
}

void Telemetry::Release() {
    std::atomic<int32_t>& users = TheTelemetryArea()->users;
    int32_t n = users.load();
    while (n > 0 && !users.compare_exchange_weak(n, n - 1)) {
    }

    if (n == 1) {
        TelemetryReader::Wake();  // that was the last: the reader stops
    }
}

void Telemetry::Shutdown() {
    TelemetryReader::Shutdown();
}

double Telemetry::Shown() {
    std::atomic<double>& shown = TheTelemetryArea()->shown;
    const double now = QpcNow(), delay = ProgramDelay();
    double last = shown.load();
    double moment = ShownMoment(now, delay, last);
    while (moment > last && !shown.compare_exchange_weak(last, moment)) {
        moment = ShownMoment(now, delay, last);
    }

    return moment;
}

size_t Telemetry::Since(double since, double at, CarSample* out, size_t max) {
    return RingSince(&ForReading()->ring, since, at, out, max);
}

SessionState Telemetry::SessionAt(double at) {
    SessionState s;
    RingSessionAt(&ForReading()->ring, at, &s);
    return s;
}

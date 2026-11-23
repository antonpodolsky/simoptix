// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "performance_reader.h"

#include <windows.h>

#include "shared/log.h"

namespace {

    constexpr double kOpenEveryS = 1.0;
    constexpr double kReadingEveryS = 1.0;
    // A reading covers the second that ended this long ago, so the GPU times of its last frames are in.
    constexpr double kSettleS = 0.1;
    // How far back the records are read, past the window's start (they come in the order their GPU time
    // came in, not quite in the order the frames ended).
    constexpr double kLookBackS = 0.5;

    int64_t Counter() {
        LARGE_INTEGER c;
        QueryPerformanceCounter(&c);
        return c.QuadPart;
    }

}

PerformanceReader::PerformanceReader() {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    frequency_ = f.QuadPart;
    frames_.resize(performance_share::kRecords);
}

const PerformanceReading& PerformanceReader::Read() {
    const int64_t counter = Counter();
    const double now = static_cast<double>(counter) / frequency_;

    if (!header_ && now >= nextOpen_ && !Open(now)) {
        nextOpen_ = now + kOpenEveryS;
    }

    if (header_) {
        header_->readerSigned.store(counter);
    }

    if (now >= nextReading_) {
        nextReading_ = now + kReadingEveryS;
        reading_ = header_ ? ReadWindow(now - kSettleS) : PerformanceReading();
    }

    return reading_;
}

bool PerformanceReader::Open(double now) {
    UniqueHandle mapping(
        OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, performance_share::kMappingName)
    );

    if (!mapping) {
        return false;
    }

    MappedView<performance_share::Header> header(
        static_cast<performance_share::Header*>(MapViewOfFile(
            mapping.Get(),
            FILE_MAP_READ | FILE_MAP_WRITE,
            0,
            0,
            sizeof(performance_share::Header)
        ))
    );

    if (!header) {
        return false;
    }

    if (header->magic != performance_share::kMagic || header->version != performance_share::kVersion
        || header->size != sizeof(performance_share::Header)) {
        if (!loggedLayout_) {
            CoreLog("performance: the layer's memory has another layout: install the layer from this build");
            loggedLayout_ = true;
        }

        return false;
    }

    CoreLog("performance: reading the game's frame times (game process %u)", header->processId);
    mapping_ = std::move(mapping);
    header_ = std::move(header);
    nextReading_ = now + kReadingEveryS;  // the layer starts timing at this sign
    return true;
}

PerformanceReading PerformanceReader::ReadWindow(double windowEndS) {
    const performance_share::Header& h = *header_;
    const double frequency = static_cast<double>(h.counterFrequency > 0 ? h.counterFrequency : frequency_);
    const uint64_t written = h.written.load(std::memory_order_acquire);
    const uint64_t oldest = written > performance_share::kRecords ? written - performance_share::kRecords + 1
                                                                  : 1;
    size_t count = 0;
    // A frame from before the second: the timing ran through all of it (it starts at the first sign).
    bool whole = false;

    for (uint64_t n = written; n >= oldest && n > 0; n--) {
        performance_share::Record r;

        if (!performance_share::ReadRecord(h, n, &r)) {
            continue;
        }

        PerformanceFrame& f = frames_[count++];
        f.endS = r.endCounter / frequency;
        f.periodS = r.periodNs * 1e-9;
        f.cpuS = r.cpuUs == performance_share::kUnknown ? -1 : r.cpuUs * 1e-6;
        f.gpuS = r.gpuUs == performance_share::kUnknown ? -1 : r.gpuUs * 1e-6;
        f.pipelined = r.pipelined != 0;
        whole = whole || f.endS <= windowEndS - 1;

        if (f.endS < windowEndS - 1 - kLookBackS) {
            break;
        }
    }

    if (!whole) {
        return PerformanceReading();
    }

    return ReadingOf(frames_.data(), count, windowEndS);
}

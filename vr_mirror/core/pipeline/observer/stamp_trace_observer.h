// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// StampTraceObserver: the frame-number stamp and the frame trace, for matching a recording to the
// poses frame by frame (the OBS plugin's Debug setting, the mirror app's SIMOPTIX_VR_MIRROR_TRACE). Numbers
// each output frame (Stamp: the frame's number, 15 bits, which the host draws as squares in the top-left
// corner) and, while a host gives it a sink, writes a header and one row per frame (kColumns), each row once
// the frame was presented or the next one came, and the stages' stats as "#stats," lines. Logic only: rows
// are formatted on the stack, nothing allocated. Knows nothing about files, D3D, OBS or the stages' methods.
//
// The first 19 columns are the OBS plugin's frame trace with 26 or more columns (vr_mirror\recording_analysis
// reads them by position); the mirror app's trace (e2e) is read by column name.
#pragma once

#include <cstdint>

#include "pipeline/observer/observer.h"

class StampTraceObserver : public Observer {
public:
    static const char* const kColumns;

    void SetSink(TraceSink* sink) override;
    void Frame(const FrameRecord& r) override;
    void Presented(double t) override;
    bool WantsStats() const override { return sink_ != nullptr; }
    bool WantsNotes() const override { return sink_ != nullptr; }
    void StatsLine(const char* line) override;
    uint32_t Stamp() const override { return frames_ & 0x7fff; }

private:
    // Writes the pending frame's row, if any.
    void Flush();

    TraceSink* sink_ = nullptr;
    uint32_t frames_ = 0;  // output frames recorded
    FrameRecord pending_;  // the last frame, until presented or the next comes
    uint32_t pendingStamp_ = 0;
    double presented_ = 0;
    bool hasPending_ = false;
};

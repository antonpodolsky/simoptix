// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// StatsReport: how every stage reports its numbers for the hosts' periodic log (the OBS plugin's
// 10 s lines, the mirror app's console): lines of a topic and name/value pairs, which a host prints
// without knowing the stage or its method. A stage's Report gives what it counted since its last
// Report (and starts counting afresh); a stage with nothing to say adds no line. LineStatsReport
// writes them as text lines. Knows nothing about logs, OBS or consoles.
#pragma once

#include <cstdio>

class StatsReport {
public:
    // Starts a line about `topic` (e.g. "game behind").
    virtual void Line(const char* topic) = 0;
    // A number on the current line, then what it is (e.g. 12, "frames judged by picture"), with
    // `decimals` digits after the point; the unit, if any, leads `name` ("ms copy ready p95").
    virtual void Value(const char* name, double value, int decimals = 0) = 0;

protected:
    ~StatsReport() = default;
};

// Writes each line as "topic: 12 name, 3.4 name" and hands it to `out` (a callable taking a
// const char*) when the next line starts, at Finish, or when it goes. Nothing allocated.
template <class Out>
class LineStatsReport : public StatsReport {
public:
    explicit LineStatsReport(Out out) : out_(out) {}
    ~LineStatsReport() { Finish(); }
    void Line(const char* topic) override {
        Finish();
        Append("%s:", topic);
        values_ = 0;
    }

    void Value(const char* name, double value, int decimals = 0) override {
        if (used_ == 0) {
            return;  // no line started
        }
        Append("%s %.*f %s", values_++ ? "," : "", decimals, value, name);
    }

    void Finish() {
        if (used_ > 0) {
            out_(static_cast<const char*>(text_));
        }
        used_ = 0;
    }

private:
    template <class... A>
    void Append(const char* format, A... a) {
        const int n = std::snprintf(text_ + used_, sizeof(text_) - used_, format, a...);
        if (n > 0) {
            used_ = used_ + n < int(sizeof(text_)) ? used_ + n : int(sizeof(text_)) - 1;
        }
    }

    Out out_;
    char text_[512] = {};
    int used_ = 0;    // characters in text_ (0: no line started)
    int values_ = 0;  // values on the current line
};

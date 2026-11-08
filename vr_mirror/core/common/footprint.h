// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Footprint: counters for the periodic footprint stats (how often a call runs and how long it
// takes, the spread of a per-tick duration), and their log text. Logic only: durations come in
// as parameters (seconds), and nothing here reads a clock, allocates per call or knows which
// thread it runs on; the owner guards it.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

// How often a call ran and how long it took, since the last reset.
struct CallStats {
    uint64_t count = 0;
    double total = 0, longest = 0;  // seconds

    void Add(double seconds) {
        count++;
        total += seconds;
        longest = std::max(longest, seconds);
    }

    void Merge(const CallStats& o) {
        count += o.count;
        total += o.total;
        longest = std::max(longest, o.longest);
    }
};

// A per-tick duration (e.g. the graphics thread's work per OBS tick), kept for its quantiles:
// up to kCapacity values between resets (more are counted in the maximum only). Fixed storage.
class DurationSamples {
public:
    static constexpr int kCapacity = 4096;  // 10 s at up to ~400 ticks a second

    void Add(double seconds) {
        if (count_ < kCapacity) {
            values_[count_++] = float(seconds);
        }
        longest_ = std::max(longest_, seconds);
    }

    int Count() const { return count_; }
    double Longest() const { return longest_; }
    // The q-quantile (0..1) of the values kept; reorders them. 0 when empty.
    double Quantile(double q) {
        if (count_ == 0) {
            return 0;
        }
        const int k = std::min(count_ - 1, int(q * count_));
        std::nth_element(values_, values_ + k, values_ + count_);
        return values_[k];
    }

    void Reset() {
        count_ = 0;
        longest_ = 0;
    }

private:
    float values_[kCapacity];
    int count_ = 0;
    double longest_ = 0;
};

// Appends "name N/s (avg A us, max M us)" for `c` over `seconds` of wall time, or "name 0/s".
inline void AppendCallStats(std::string* out, const char* name, const CallStats& c, double seconds) {
    char buf[96];
    if (c.count == 0) {
        snprintf(buf, sizeof(buf), "%s 0/s", name);
    } else {
        snprintf(
            buf,
            sizeof(buf),
            "%s %.0f/s (avg %.1f us, max %.0f us)",
            name,
            c.count / std::max(1e-9, seconds),
            c.total / c.count * 1e6,
            c.longest * 1e6
        );
    }

    if (!out->empty()) {
        *out += ", ";
    }
    *out += buf;
}

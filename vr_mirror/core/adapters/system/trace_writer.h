// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// TraceWriter: a diagnostic trace file written on its own thread, so the threads producing rows
// (PimaxSource's poll thread, OBS's graphics thread) never wait on the disk. A row is formatted on
// the caller's stack and copied into a preallocated buffer under a briefly held lock; the writer
// thread empties the buffer to the file every 200 ms. If the disk falls that far behind and the
// buffer fills up, rows are dropped (and counted in the log on Close) rather than waited for.
// Knows nothing about what the rows mean.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

class TraceWriter {
public:
    ~TraceWriter() { Close(); }

    // Creates the file and writes `header` first. Closes a trace already open.
    bool Open(const wchar_t* path, const char* header);
    // Writes what is buffered and closes the file. Safe to call when not open.
    void Close();
    bool IsOpen() const { return open_.load(); }

    // One row, printf-style, up to 511 characters (include the newline). Any thread; never waits
    // on the disk. Does nothing while closed.
    void Printf(const char* fmt, ...);

private:
    void Run();

    static constexpr size_t kBuffer = 4 << 20;  // ~1 min of the busiest trace
    std::mutex mutex_;                          // guards fill_, used_, dropped_, stop_
    std::condition_variable wake_;
    std::vector<char> fill_;  // rows waiting for the writer thread
    size_t used_ = 0;
    uint64_t dropped_ = 0;
    bool stop_ = false;
    std::vector<char> write_;  // writer thread: the buffer being written
    FILE* file_ = nullptr;     // writer thread while open; Open/Close otherwise
    std::thread thread_;
    std::atomic<bool> open_{false};
};

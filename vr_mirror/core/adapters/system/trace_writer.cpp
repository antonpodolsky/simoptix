// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "adapters/system/trace_writer.h"

#include <algorithm>
#include <cstdarg>
#include <cstring>

#include "shared/log.h"

bool TraceWriter::Open(const wchar_t* path, const char* header) {
    Close();

    // Binary mode: rows keep their \n line endings.
    if (_wfopen_s(&file_, path, L"wb") != 0 || !file_) {
        file_ = nullptr;
        return false;
    }

    fputs(header, file_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (fill_.size() != kBuffer) {
            fill_.resize(kBuffer);  // once: rows are never allocated
        }
        used_ = 0;
        dropped_ = 0;
        stop_ = false;
    }

    if (write_.size() != kBuffer) {
        write_.resize(kBuffer);
    }
    thread_ = std::thread([this] {
        // An exception escaping a thread would end the host on the spot; log it and stop writing.
        try {
            Run();
        } catch (...) {
            CoreLog("trace writer stopped by an unexpected error");
        }
    });

    open_ = true;
    return true;
}

void TraceWriter::Close() {
    if (!thread_.joinable()) {
        return;
    }
    open_ = false;
    uint64_t dropped = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }

    wake_.notify_one();
    thread_.join();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        dropped = dropped_;
    }

    fclose(file_);
    file_ = nullptr;
    if (dropped) {
        CoreLog("trace: %llu rows dropped (the disk fell behind)", static_cast<unsigned long long>(dropped));
    }
}

void TraceWriter::Printf(const char* fmt, ...) {
    if (!open_.load()) {
        return;
    }

    char row[512];
    va_list args;
    va_start(args, fmt);
    const int n = vsnprintf(row, sizeof(row), fmt, args);
    va_end(args);

    if (n <= 0) {
        return;
    }
    const size_t len = std::min(size_t(n), sizeof(row) - 1);

    std::lock_guard<std::mutex> lock(mutex_);
    if (fill_.size() - used_ < len) {
        dropped_++;
        return;
    }

    memcpy(fill_.data() + used_, row, len);
    used_ += len;
}

// Writer thread: swaps the filled buffer out every 200 ms and writes it outside the lock.
void TraceWriter::Run() {
    bool stop = false;
    while (!stop) {
        size_t len = 0;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait_for(lock, std::chrono::milliseconds(200), [this] { return stop_; });
            stop = stop_;
            fill_.swap(write_);
            len = used_;
            used_ = 0;
        }

        if (len) {
            fwrite(write_.data(), 1, len, file_);
        }
    }

    fflush(file_);
}

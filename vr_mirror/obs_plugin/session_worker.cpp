// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "session_worker.h"

#include <chrono>
#include <string>

#include "adapters/d3d/frame_source.h"
#include "frame_ring.h"
#include "shared/log.h"

namespace {
    double MsSince(std::chrono::steady_clock::time_point t0) {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }
}

void SessionWorker::Join() {
    if (thread_.joinable()) {
        thread_.join();  // the previous start or stop has finished (kStopped/kRunning)
    }
}

void SessionWorker::Start() {
    if (state_ != kStopped) {
        return;
    }
    Join();
    state_ = kStarting;

    thread_ = std::thread([this] {
        // An exception escaping a thread would end OBS on the spot; log it and give up the start.
        try {
            const auto t0 = std::chrono::steady_clock::now();
            std::string error;
            if (!source_->Start(&error)) {
                CoreLog("%s", error.c_str());
            } else if (!ring_->Start(source_)) {
                CoreLog("could not create the copy device");
                source_->Stop();
            } else {
                CoreLog("%s ready after %.0f ms (on its own thread)", source_->Name(), MsSince(t0));
                state_ = kRunning;
                return;
            }
        } catch (...) {
            CoreLog("starting the %s stopped by an unexpected error", source_->Name());
        }
        failed_ = true;
        state_ = kStopped;
    });
}

void SessionWorker::Stop() {
    if (state_ != kRunning) {
        return;
    }
    Join();
    state_ = kStopping;
    thread_ = std::thread([this] {
        try {
            const auto t0 = std::chrono::steady_clock::now();
            ring_->Stop();
            source_->Stop();
            CoreLog("%s and threads stopped after %.0f ms (on its own thread)", source_->Name(), MsSince(t0));
        } catch (...) {
            CoreLog("stopping the %s stopped by an unexpected error", source_->Name());
        }
        state_ = kStopped;
    });
}

void SessionWorker::StopNow() {
    Join();
    ring_->Stop();
    source_->Stop();
    state_ = kStopped;
}

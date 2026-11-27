// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lap_recorder.h"

#include <algorithm>
#include <cmath>

#include "shared/local_files.h"
#include "shared/log.h"
#include "shared/telemetry/qpc_clock.h"
#include "shared/telemetry/telemetry.h"

using Microsoft::WRL::ComPtr;

namespace {

    // How long after the closing crossing the last frames of the lap may still come (s).
    constexpr double kLastFramesS = 0.5;

    // The moment a frame was drawn for, on its source's clock: the slot it says, else the slot it arrived
    // for.
    double MomentOf(const GameFrame& f) {
        if (f.drawnSlot > 0) {
            return f.drawnSlot;
        }

        return f.slotTime > 0 ? f.slotTime : f.time;
    }

    // The video's size for images `width` x `height`: theirs, or LapRecorder::kWidth wide at their shape,
    // both even (the encoder takes even sizes). The whole image is drawn into it, so its field of view stays.
    void VideoSize(UINT width, UINT height, int* videoWidth, int* videoHeight) {
        const double scale = std::min(1.0, static_cast<double>(LapRecorder::kWidth) / width);
        *videoWidth = static_cast<int>(std::lround(width * scale / 2)) * 2;
        *videoHeight = static_cast<int>(std::lround(height * scale / 2)) * 2;
    }

    // The folder recorded laps are kept in, and the video's path while it is recorded: a name the kept laps'
    // pattern doesn't match, so a lap being recorded is never taken for a saved one.
    bool RecordingPath(std::wstring* path) {
        wchar_t file[MAX_PATH];

        if (!NewLocalFilePath(LocalFileKind::kRecordedLap, file, MAX_PATH)) {
            return false;
        }

        std::wstring folder = file;
        folder.resize(folder.find_last_of(L'\\'));
        *path = folder + L"\\recording_lap.mp4";
        return true;
    }

}

void LapRecorder::Start(FrameRing* ring, FrameSource* source) {
    Stop();
    ring_ = ring;
    source_ = source;
    ResetEvent(stop_.Get());
    state_ = static_cast<int>(State::kWaiting);
    running_ = true;

    const std::atomic<bool>* logGate = CurrentLogGate();  // it logs as the thread starting it does
    thread_ = std::thread([this, logGate] {
        const ScopedLogGate gate(logGate);

        // An exception escaping a thread would end OBS on the spot; log it and give the lap up.
        try {
            Run();
        } catch (...) {
            CoreLogError("recorded lap: the recorder stopped by an unexpected error");
            video_.Abort();
            state_ = static_cast<int>(State::kFailed);
        }

        running_ = false;
    });
}

void LapRecorder::Stop() {
    SetEvent(stop_.Get());

    if (thread_.joinable()) {
        thread_.join();
    }
}

void LapRecorder::Run() {
    Record();

    for (ComPtr<ID3D11Texture2D>& texture : opened_) {
        texture.Reset();  // the ring's textures stay allocated while opened here
    }
}

void LapRecorder::Record() {
    std::string error;

    if (!Prepare(&error)) {
        Fail(error.c_str());
        return;
    }

    CoreLog("recorded lap: waiting for the car to cross the line (leave the pits and drive a lap)");

    while (Get() == State::kWaiting || Get() == State::kRecording) {
        const HANDLE wait[2] = {stop_.Get(), ring_->CopiedEvent()};

        if (WaitForMultipleObjects(2, wait, FALSE, 50) == WAIT_OBJECT_0) {
            video_.Abort();
            DeleteFileW(videoPath_.c_str());
            state_ = static_cast<int>(State::kOff);
            CoreLog("recorded lap: recording stopped before the lap was complete");
            return;
        }

        Step();
    }

    if (Get() == State::kSaving) {
        Save();
    }
}

bool LapRecorder::Prepare(std::string* error) {
    if (!RecordingPath(&videoPath_)) {
        *error = "no folder for recorded laps";
        return false;
    }

    finder_.Reset();
    held_.clear();
    held_.reserve(LapVideo::kPool);  // nothing allocated per frame
    lap_ = RecordedLap();
    start_ = end_ = 0;
    lastSerial_ = 0;
    behind_ = 0;
    return video_.Init(source_->AdapterLuid(), error);
}

void LapRecorder::Step() {
    const double now = QpcNow();
    const double toSource = source_->Now() - now;  // the source's clock is the performance counter's, offset
    const LapDecision d = finder_.Step(Telemetry::SessionAt(now));

    if (d.step == LapStep::kStart) {
        start_ = d.at + toSource;
        end_ = 0;
        firstTaken_ = false;
        lap_.frames.clear();
        lap_.frames.reserve(static_cast<size_t>(LapFinder::kLongestLapS / lap_.period) + 1);
        state_ = static_cast<int>(State::kRecording);
        CoreLog("recorded lap: %s", d.note);
    } else if (d.step == LapStep::kEnd) {
        end_ = d.at + toSource;
        CoreLog("recorded lap: %s", d.note);
    } else if (d.step == LapStep::kDiscard) {
        Discard(d.note);
        return;
    }

    TakeFrames();
    WriteLap();

    if (end_ > 0 && now + toSource > end_ + kLastFramesS) {
        state_ = static_cast<int>(State::kSaving);
    }
}

// Takes the ring's frames of the lap, oldest first. Before the lap's start is known it copies nothing: the
// ring keeps the frames since the crossing (kRingFrames) until it is known.
void LapRecorder::TakeFrames() {
    ring_->Snapshot(slots_);

    if (!video_.IsOpen() && !OpenVideo()) {
        return;
    }

    if (start_ == 0) {
        return;
    }

    for (;;) {
        int next = -1;

        for (int i = 0; i < FrameRing::kMaxSlots; i++) {
            const FrameRing::SlotInfo& s = slots_[i];

            if (s.valid && s.frame.serial > lastSerial_
                && (next < 0 || s.frame.serial < slots_[next].frame.serial)) {
                next = i;
            }
        }

        if (next < 0) {
            return;
        }

        lastSerial_ = slots_[next].frame.serial;
        const double moment = MomentOf(slots_[next].frame);

        if (moment < start_ || (end_ > 0 && moment >= end_)) {
            continue;  // before the line, or past it
        }

        if (!firstTaken_) {
            firstTaken_ = true;

            if (moment > start_ + 1.5 * lap_.period) {
                CoreLog(
                    "recorded lap: the first %.0f ms of the lap were gone from the ring (the telemetry was "
                    "late)",
                    (moment - start_) * 1000
                );
            }
        }

        if (!TakeFrame(slots_[next], next, moment)) {
            return;  // failed, or the lap was given up
        }
    }
}

// Opens the video at the size of the ring's newest image (VideoSize). A later image of another size (the
// canvas or the zoom changed) is scaled into it.
bool LapRecorder::OpenVideo() {
    int newest = -1;

    for (int i = 0; i < FrameRing::kMaxSlots; i++) {
        if (slots_[i].valid && (newest < 0 || slots_[i].frame.serial > slots_[newest].frame.serial)) {
            newest = i;
        }
    }

    ID3D11Texture2D* texture = newest < 0 ? nullptr : Opened(slots_[newest], newest);

    if (!texture) {
        return false;
    }

    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);
    VideoSize(desc.Width, desc.Height, &lap_.width, &lap_.height);
    lap_.eye = source_->Eye();
    lap_.period = source_->Period();
    std::string error;

    if (!video_.Open(videoPath_.c_str(), lap_.width, lap_.height, lap_.period, &error)) {
        Fail(error.c_str());
        return false;
    }

    return true;
}

bool LapRecorder::TakeFrame(const FrameRing::SlotInfo& slot, int index, double moment) {
    ID3D11Texture2D* texture = Opened(slot, index);

    if (!texture) {
        return true;  // gone: the next frames carry on
    }

    Quat pose;

    if (!source_->PredictedHeadAt(moment, &pose)) {
        pose = source_->HeadAt(moment);
    }

    ComPtr<IMFSample> image;

    if (!video_.Take(texture, &image)) {
        behind_++;
        Discard("the video encoder fell behind the game");
        return false;
    }

    held_.push_back({std::move(image), moment, pose});
    return true;
}

// Writes the held frames of the lap. Until the closing crossing is known, the newest stay held: they may
// already be past the line.
void LapRecorder::WriteLap() {
    const size_t keep = end_ == 0 ? kHeld : 0;

    while (held_.size() > keep) {
        Held& h = held_.front();
        const bool inLap = h.t >= start_ && (end_ == 0 || h.t < end_);

        if (inLap) {
            std::string error;

            if (!video_.Write(h.image.Get(), h.t - start_, &error)) {
                Fail(error.c_str());
                return;
            }

            lap_.frames.push_back({h.t - start_, h.pose});
        }

        held_.erase(held_.begin());
    }
}

void LapRecorder::Discard(const char* why) {
    CoreLog("recorded lap: thrown away (%s); waiting for the next lap", why);
    video_.Abort();
    DeleteFileW(videoPath_.c_str());
    held_.clear();
    lap_.frames.clear();
    start_ = end_ = 0;
    state_ = static_cast<int>(State::kWaiting);
    finder_.Reset();
}

void LapRecorder::Save() {
    lap_.length = end_ - start_;
    std::string error;

    if (lap_.frames.size() < 2 || !video_.Finish(&error)) {
        Fail(error.empty() ? "the lap had no frames" : error.c_str());
        return;
    }

    PruneLocalFiles(LocalFileKind::kRecordedLap, 1);
    wchar_t stem[MAX_PATH];

    if (!NewLocalFilePath(LocalFileKind::kRecordedLap, stem, MAX_PATH)
        || !MoveFileExW(videoPath_.c_str(), (std::wstring(stem) + L".mp4").c_str(), MOVEFILE_REPLACE_EXISTING)
        || !WriteRecordedLap(stem, lap_, &error)) {
        Fail(error.empty() ? "the recorded lap could not be saved" : error.c_str());
        return;
    }

    WIN32_FILE_ATTRIBUTE_DATA file = {};
    GetFileAttributesExW((std::wstring(stem) + L".mp4").c_str(), GetFileExInfoStandard, &file);
    const double megabytes = (static_cast<double>(file.nFileSizeHigh) * 4294967296.0 + file.nFileSizeLow)
                             / 1e6;

    CoreLog(
        "recorded lap: saved, %.1f s, %zu frames at %dx%d, %.0f MB; frames the encoder had no room for: %d",
        lap_.length,
        lap_.frames.size(),
        lap_.width,
        lap_.height,
        megabytes,
        behind_
    );

    state_ = static_cast<int>(State::kSaved);
}

void LapRecorder::Fail(const char* why) {
    CoreLogError("recorded lap: not recorded: %s", why);  // the properties send the user here
    video_.Abort();
    DeleteFileW(videoPath_.c_str());
    state_ = static_cast<int>(State::kFailed);
}

ID3D11Texture2D* LapRecorder::Opened(const FrameRing::SlotInfo& slot, int index) {
    if (!slot.shared) {
        return nullptr;
    }

    if (opened_[index] && openedGeneration_[index] == slot.generation) {
        return opened_[index].Get();
    }

    opened_[index].Reset();
    openedGeneration_[index] = slot.generation;

    if (FAILED(video_.Device()->OpenSharedResource(slot.shared, IID_PPV_ARGS(&opened_[index])))) {
        return nullptr;
    }

    return opened_[index].Get();
}

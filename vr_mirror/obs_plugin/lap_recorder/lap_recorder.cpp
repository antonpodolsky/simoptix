// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lap_recorder.h"

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

    // The eye's field of view without the last column and row of an image `width` x `height`, when they
    // are cut for the encoder (it takes even sizes).
    EyeInfo EvenEye(EyeInfo eye, UINT width, UINT height) {
        if (width % 2) {
            eye.rightTan -= (eye.leftTan + eye.rightTan) / static_cast<float>(width);
        }
        if (height % 2) {
            eye.downTan -= (eye.upTan + eye.downTan) / static_cast<float>(height);
        }

        return eye;
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

    thread_ = std::thread([this] {
        // An exception escaping a thread would end OBS on the spot; log it and give the lap up.
        try {
            Run();
        } catch (...) {
            CoreLog("recorded lap: the recorder stopped by an unexpected error");
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

    TakeFrames();
    const LapDecision d = finder_.Step(Telemetry::SessionAt(now));

    if (d.step == LapStep::kStart) {
        start_ = d.at + toSource;
        end_ = 0;
        lap_.frames.clear();
        lap_.frames.reserve(static_cast<size_t>(LapFinder::kLongestLapS / lap_.period) + 1);
        state_ = static_cast<int>(State::kRecording);
        CoreLog("recorded lap: %s", d.note);

        if (held_.empty() || held_.front().t > start_ + lap_.period) {
            CoreLog(
                "recorded lap: the first %.0f ms of the lap came before the frames held (the telemetry was "
                "late)",
                held_.empty() ? 0.0 : (held_.front().t - start_) * 1000
            );
        }
    } else if (d.step == LapStep::kEnd) {
        end_ = d.at + toSource;
        CoreLog("recorded lap: %s", d.note);
    } else if (d.step == LapStep::kDiscard) {
        Discard(d.note);
        return;
    }

    WriteLap();

    if (end_ > 0 && now + toSource > end_ + kLastFramesS) {
        state_ = static_cast<int>(State::kSaving);
    }
}

// Takes the ring's new frames, oldest first.
void LapRecorder::TakeFrames() {
    ring_->Snapshot(slots_);

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

        if (!TakeFrame(slots_[next], next) && (Get() == State::kFailed || Get() == State::kWaiting)) {
            return;  // failed, or a lap was given up: the frames after it wait for the next pass
        }
    }
}

bool LapRecorder::TakeFrame(const FrameRing::SlotInfo& slot, int index) {
    ID3D11Texture2D* texture = Opened(slot, index);

    if (!texture) {
        return true;  // gone: the next frames carry on
    }

    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);
    const int width = static_cast<int>(desc.Width & ~1u), height = static_cast<int>(desc.Height & ~1u);

    if (!video_.IsOpen()) {
        lap_.eye = EvenEye(source_->Eye(), desc.Width, desc.Height);
        lap_.width = width, lap_.height = height;
        lap_.period = source_->Period();
        std::string error;

        if (!video_.Open(videoPath_.c_str(), width, height, lap_.period, &error)) {
            Fail(error.c_str());
            return false;
        }
    } else if (width != lap_.width || height != lap_.height) {
        Discard("the game's image changed size");
        return false;
    }

    const double moment = MomentOf(slot.frame);
    Quat pose;

    if (!source_->PredictedHeadAt(moment, &pose)) {
        pose = source_->HeadAt(moment);
    }

    ComPtr<IMFSample> image;

    // Before the lap, the oldest held frame makes room.
    if (!video_.Take(texture, &image) && start_ == 0 && !held_.empty()) {
        held_.erase(held_.begin());
        video_.Take(texture, &image);
    }

    if (!image) {
        behind_++;

        if (start_ > 0) {
            Discard("the video encoder fell behind the game");
            return false;
        }

        return true;
    }

    held_.push_back({std::move(image), moment, pose});
    return true;
}

// Writes the held frames that belong to the lap, and drops the ones before and after it; before the lap's
// start is known, holds only the newest.
void LapRecorder::WriteLap() {
    if (start_ == 0) {
        while (held_.size() > kHeld) {
            held_.erase(held_.begin());
        }

        return;
    }

    while (!held_.empty()) {
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
    CoreLog("recorded lap: not recorded: %s", why);
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

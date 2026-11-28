// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "audio_tap.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "shared/guarded.h"

namespace {

    constexpr double kKeptS = 2;

    bool IsAudioInputCapture(obs_source_t* source) {
        const char* id = obs_source_get_unversioned_id(source);
        return id && strcmp(id, "wasapi_input_capture") == 0;
    }

}

void AudioTap::Attach() {
    Detach();

    obs_audio_info info = {};

    if (!obs_get_audio_info(&info)) {
        return;
    }

    rate_ = info.samples_per_sec;
    channels_ = std::clamp(static_cast<int>(get_audio_channels(info.speakers)), 1, 2);
    obs_enum_sources(Tapped, this);
}

// obs_enum_sources: taps each source, and goes on to the next.
bool AudioTap::Tapped(void* param, obs_source_t* source) {
    Guarded("audio tap", [&] { static_cast<AudioTap*>(param)->Add(source); });
    return true;
}

// Taps `source` if it has sound and there is room.
void AudioTap::Add(obs_source_t* source) {
    if (count_ >= kMaxSources || !(obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO)) {
        return;
    }

    if (IsAudioInputCapture(source)) {
        return;
    }

    auto tap = std::make_unique<Tap>();
    tap->source = obs_source_get_ref(source);

    if (!tap->source) {
        return;
    }

    tap->name = obs_source_get_name(source);
    tap->channels = channels_;
    tap->sound.assign(static_cast<size_t>(rate_ * kKeptS), 0.0f);
    obs_source_add_audio_capture_callback(tap->source, Captured, tap.get());
    taps_[count_++] = std::move(tap);
}

void AudioTap::Detach() {
    for (int i = 0; i < count_; i++) {
        obs_source_remove_audio_capture_callback(taps_[i]->source, Captured, taps_[i].get());
        obs_source_release(taps_[i]->source);
        taps_[i].reset();
    }

    count_ = 0;
}

void AudioTap::Captured(void* param, obs_source_t*, const audio_data* audio, bool) {
    Guarded("audio tap", [&] { Write(static_cast<Tap*>(param), audio); });
}

// OBS's audio thread: the piece's samples, mixed to mono, into the ring, and where they start.
void AudioTap::Write(Tap* tap, const audio_data* audio) {
    const uint64_t written = tap->written.load(std::memory_order_relaxed);
    const size_t size = tap->sound.size();
    const auto* left = reinterpret_cast<const float*>(audio->data[0]);
    const auto* right = tap->channels > 1 && audio->data[1] ? reinterpret_cast<const float*>(audio->data[1])
                                                            : left;

    if (!left) {
        return;
    }

    for (uint32_t i = 0; i < audio->frames; i++) {
        tap->sound[(written + i) % size] = (left[i] + right[i]) / 2;
    }

    const uint64_t count = tap->pieceCount.load(std::memory_order_relaxed);
    Piece& piece = tap->pieces[count % kPieces];
    piece.timestampNs.store(audio->timestamp, std::memory_order_relaxed);
    piece.firstSample.store(written, std::memory_order_relaxed);
    tap->written.store(written + audio->frames, std::memory_order_release);
    tap->pieceCount.store(count + 1, std::memory_order_release);
}

bool AudioTap::Copy(int source, double from, double to, std::vector<float>* out, double* start) const {
    const Tap& tap = *taps_[source];
    const uint64_t pieces = tap.pieceCount.load(std::memory_order_acquire);
    const uint64_t fromNs = static_cast<uint64_t>(from * 1e9);
    const size_t size = tap.sound.size();
    uint64_t firstSample = 0, pieceNs = 0;
    bool found = false;

    // The newest piece that starts at or before `from`: its timestamp places the samples after it.
    for (uint64_t back = 1; back <= pieces && back < kPieces; back++) {
        const Piece& piece = tap.pieces[(pieces - back) % kPieces];
        pieceNs = piece.timestampNs.load(std::memory_order_relaxed);

        if (pieceNs <= fromNs) {
            firstSample = piece.firstSample.load(std::memory_order_relaxed);
            found = true;
            break;
        }
    }

    if (!found) {
        return false;
    }

    const uint64_t first = firstSample
                           + static_cast<uint64_t>(std::llround((fromNs - pieceNs) * 1e-9 * rate_));
    const uint64_t count = static_cast<uint64_t>(std::llround((to - from) * rate_));
    const uint64_t written = tap.written.load(std::memory_order_acquire);

    if (first + count > written || written - first > size) {
        return false;
    }

    out->resize(count);

    for (uint64_t i = 0; i < count; i++) {
        (*out)[i] = tap.sound[(first + i) % size];
    }

    // Written over meanwhile: the oldest samples copied may be newer ones.
    if (tap.written.load(std::memory_order_acquire) - first > size) {
        return false;
    }

    *start = (pieceNs + (first - firstSample) / rate_ * 1e9) * 1e-9;
    return true;
}

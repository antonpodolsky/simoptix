// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// AudioTap: the last two seconds of each audio source's sound in OBS, mixed to mono, with OBS's own
// timestamps, so the sound of a moment can be looked at after it. Attach hands OBS a callback for every
// audio source there is then; OBS calls it on its audio thread with each piece of sound the source
// delivers, which it copies into that source's buffer and nothing else. Detach takes the callbacks
// back; OBS returns from that only once none is running.
//
// Thin glue: what the sound means is AutoSync's business. Attach, Detach, Copy and Name: one thread
// (AutoSync's); the callbacks: OBS's audio thread. The buffers are made in Attach.
#pragma once

#include <obs-module.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class AudioTap {
public:
    static constexpr int kMaxSources = 8;

    AudioTap() = default;
    AudioTap(const AudioTap&) = delete;
    AudioTap& operator=(const AudioTap&) = delete;
    ~AudioTap() { Detach(); }

    void Attach();
    void Detach();

    int Count() const { return count_; }
    // The source's name as it was at Attach.
    const std::string& Name(int source) const { return taps_[source]->name; }
    double Rate() const { return rate_; }
    // Source `source`'s sound from `from` to `to` (performance-counter seconds) into `out`, and the time
    // of its first sample; false if the buffer doesn't hold all of it.
    bool Copy(int source, double from, double to, std::vector<float>* out, double* start) const;

private:
    struct Piece {
        std::atomic<uint64_t> timestampNs{0};  // OBS's timestamp of the piece's first sample
        std::atomic<uint64_t> firstSample{0};  // its place in the source's sound so far
    };

    static constexpr size_t kPieces = 1024;

    struct Tap {
        obs_source_t* source = nullptr;  // a reference held until Detach
        std::string name;
        int channels = 1;
        std::vector<float> sound;             // mono, a ring of two seconds
        std::atomic<uint64_t> written{0};     // samples written so far (audio thread)
        std::atomic<uint64_t> pieceCount{0};  // pieces recorded so far (audio thread)
        Piece pieces[kPieces];
    };

    static bool Tapped(void* param, obs_source_t* source);
    void Add(obs_source_t* source);
    static void Captured(void* param, obs_source_t* source, const audio_data* audio, bool muted);
    static void Write(Tap* tap, const audio_data* audio);

    std::unique_ptr<Tap> taps_[kMaxSources];
    int count_ = 0;
    double rate_ = 48000;
    int channels_ = 2;  // mixed to mono: OBS's first two
};

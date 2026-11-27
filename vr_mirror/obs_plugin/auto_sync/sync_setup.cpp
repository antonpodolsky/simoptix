// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "sync_setup.h"

#include <mmdeviceapi.h>
#include <obs-module.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdio>
#include <string>

#include "adapters/d3d/frame_source.h"
#include "vr_mirror/obs_plugin/frame_ring.h"

using Microsoft::WRL::ComPtr;

namespace {

    // A new frame rate counts once this many reads in a row had it: loading screens and short stalls don't.
    // The first counts at once, so measuring starts as soon as the game's frames run.
    constexpr int kSteadyReads = 3;

    // obs_enum_sources: each audio source's name and settings, one after another.
    bool AddAudioSource(void* param, obs_source_t* source) {
        if (!(obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO)) {
            return true;
        }

        auto* text = static_cast<std::string*>(param);
        obs_data_t* settings = obs_source_get_settings(source);
        const char* json = settings ? obs_data_get_json(settings) : nullptr;
        *text += std::string(obs_source_get_name(source)) + "=" + (json ? json : "") + ";";
        obs_data_release(settings);
        return true;
    }

    std::string AudioSources() {
        std::string text;
        obs_enum_sources(AddAudioSource, &text);
        return text;
    }

    // Windows' default output device's ID; empty if there is none or it can't be read.
    std::string OutputDevice() {
        ComPtr<IMMDeviceEnumerator> devices;
        ComPtr<IMMDevice> device;
        LPWSTR id = nullptr;

        if (FAILED(
                CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&devices))
            )
            || FAILED(devices->GetDefaultAudioEndpoint(eRender, eConsole, &device))
            || FAILED(device->GetId(&id))) {
            return {};
        }

        std::string text;

        for (const wchar_t* c = id; *c; c++) {
            text += static_cast<char>(*c < 128 ? *c : '?');
        }

        CoTaskMemFree(id);
        return text;
    }

}

std::string SetupChange(const SyncSetup& before, const SyncSetup& now) {
    char text[128];

    if (now.gameStarts != before.gameStarts) {
        return "the game's frames started again";
    }

    if (now.frameRate != before.frameRate) {
        snprintf(text, sizeof(text), "the game's frame rate, %d to %d", before.frameRate, now.frameRate);
        return text;
    }

    if (now.imageWidth != before.imageWidth || now.imageHeight != before.imageHeight) {
        snprintf(
            text,
            sizeof(text),
            "the game's image, %ux%u to %ux%u",
            before.imageWidth,
            before.imageHeight,
            now.imageWidth,
            now.imageHeight
        );
        return text;
    }

    if (now.audio != before.audio) {
        return "OBS's audio sources";
    }

    if (now.outputDevice != before.outputDevice) {
        return "Windows' output device";
    }

    return {};
}

SyncSetup SyncSetupWatch::Read(FrameSource* source, const FrameRing& ring) {
    setup_.gameStarts = ring.FrameStarts();

    if (!source->Idle()) {
        const int rate = static_cast<int>(std::lround(source->SourceFps() / 5)) * 5;
        seenReads_ = rate == seenRate_ ? seenReads_ + 1 : 1;
        seenRate_ = rate;

        if (rate > 0 && (setup_.frameRate == 0 || seenReads_ >= kSteadyReads)) {
            setup_.frameRate = rate;
        }

        // A copy as large as the image: its own size.
        uint32_t width = 0, height = 0;

        if (source->CopySize(1e9f, 1e18, &width, &height)) {
            setup_.imageWidth = width, setup_.imageHeight = height;
        }
    }

    setup_.audio = AudioSources();
    setup_.outputDevice = OutputDevice();
    return setup_;
}

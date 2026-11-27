// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "fake_openxr_sound.h"

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdio>

#include "shared/unique_handle.h"

using Microsoft::WRL::ComPtr;

namespace {

    constexpr REFERENCE_TIME kBuffer = 400000;  // 40 ms, in 100 ns units

    // The default output device in shared mode, refilled at its signal, with its clock.
    struct Output {
        ComPtr<IAudioClient> client;
        ComPtr<IAudioRenderClient> render;
        ComPtr<IAudioClock> clock;
        UniqueHandle ready;
        WAVEFORMATEX* format = nullptr;  // owned: CoTaskMemFree
        UINT32 bufferFrames = 0;
        UINT64 frequency = 0;  // the clock's units a second

        Output() = default;
        Output(const Output&) = delete;
        Output& operator=(const Output&) = delete;
        ~Output() { CoTaskMemFree(format); }
    };

    bool IsFloat(const WAVEFORMATEX* format) {
        if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
            return true;
        }

        const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        return format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format->wBitsPerSample == 32
               && extensible->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    }

    bool Open(Output* out) {
        ComPtr<IMMDeviceEnumerator> devices;
        ComPtr<IMMDevice> device;

        HRESULT
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&devices));

        if (FAILED(hr) || FAILED(devices->GetDefaultAudioEndpoint(eRender, eConsole, &device))) {
            return false;
        }

        if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &out->client))) {
            return false;
        }

        if (FAILED(out->client->GetMixFormat(&out->format)) || !IsFloat(out->format)) {
            return false;
        }

        hr = out->client->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            kBuffer,
            0,
            out->format,
            nullptr
        );

        if (FAILED(hr)) {
            return false;
        }

        out->ready.Reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));

        if (!out->ready || FAILED(out->client->SetEventHandle(out->ready.Get()))) {
            return false;
        }

        if (FAILED(out->client->GetService(IID_PPV_ARGS(&out->render)))
            || FAILED(out->client->GetService(IID_PPV_ARGS(&out->clock)))) {
            return false;
        }

        if (FAILED(out->client->GetBufferSize(&out->bufferFrames))
            || FAILED(out->clock->GetFrequency(&out->frequency))) {
            return false;
        }

        return SUCCEEDED(out->client->Start());
    }

    // The recording at `moment` seconds of it, between its samples; silence outside it.
    float SampleAt(const std::vector<float>& recording, double rate, double moment) {
        const double place = moment * rate;
        const double first = std::floor(place);

        if (first < 0 || first + 1 >= static_cast<double>(recording.size())) {
            return 0;
        }

        const size_t i = static_cast<size_t>(first);
        const double part = place - first;
        return static_cast<float>(recording[i] * (1 - part) + recording[i + 1] * part);
    }

    // Fills the device's buffer at each of its signals: each sample's play time from the device's clock, and
    // the recording at that moment less the lag: from `start` until `stop`, and from `again` if set.
    void Play(
        const Output& out,
        const std::vector<float>& recording,
        double recordingRate,
        const std::atomic<double>& start,
        double lag,
        const std::atomic<double>& stop,
        const std::atomic<double>& again,
        const std::atomic<bool>& running
    ) {
        const double rate = out.format->nSamplesPerSec;
        const int channels = out.format->nChannels;
        UINT64 written = 0;

        while (running) {
            WaitForSingleObject(out.ready.Get(), 100);
            UINT32 padding = 0;
            UINT64 position = 0, positionQpc = 0;
            BYTE* data = nullptr;

            if (FAILED(out.client->GetCurrentPadding(&padding))
                || FAILED(out.clock->GetPosition(&position, &positionQpc))) {
                return;
            }

            const UINT32 count = out.bufferFrames - padding;

            if (count == 0 || FAILED(out.render->GetBuffer(count, &data))) {
                continue;
            }

            // Sample `written` plays when the device has played everything handed to it before.
            const double playedS = static_cast<double>(position) / out.frequency;
            const double startAt = start, stopAt = stop, againAt = again;
            auto* samples = reinterpret_cast<float*>(data);

            for (UINT32 i = 0; i < count; i++) {
                const double plays = positionQpc * 1e-7 + (written + i) / rate - playedS;
                const float sample = (plays < stopAt + lag
                                          ? SampleAt(recording, recordingRate, plays - startAt - lag)
                                          : 0)
                                     + (againAt > 0
                                            ? SampleAt(recording, recordingRate, plays - againAt - lag)
                                            : 0);

                for (int c = 0; c < channels; c++) {
                    samples[i * channels + c] = sample;
                }
            }

            written += count;
            out.render->ReleaseBuffer(count, 0);
        }
    }

}

bool RecordedSound::Load(const std::string& path, double rate) {
    samples_.clear();
    rate_ = rate;
    FILE* file = nullptr;

    if (rate <= 0 || fopen_s(&file, path.c_str(), "rb") != 0 || !file) {
        return false;
    }

    float block[4096];
    size_t count = 0;

    while ((count = fread(block, sizeof(float), 4096, file)) > 0) {
        samples_.insert(samples_.end(), block, block + count);
    }

    fclose(file);
    return !samples_.empty();
}

void RecordedSound::Start(double lag) {
    lag_ = lag;
    running_ = true;
    thread_ = std::thread([this] {
        // An exception escaping a thread would end the fake game; print it and play no more.
        try {
            Run();
        } catch (...) {
            fprintf(stderr, "fake_openxr: the recorded sound stopped by an unexpected error\n");
        }
    });
}

void RecordedSound::Restart(double stop, double again) {
    stop_ = stop;
    again_ = again;
}

void RecordedSound::Stop() {
    running_ = false;

    if (thread_.joinable()) {
        thread_.join();
    }
}

void RecordedSound::Run() {
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        fprintf(stderr, "fake_openxr: no COM for the recorded sound\n");
        return;
    }

    {
        Output out;

        if (!Open(&out)) {
            fprintf(stderr, "fake_openxr: the default output device can't play the recorded sound\n");
        } else {
            Play(out, samples_, rate_, start_, lag_, stop_, again_, running_);
            out.client->Stop();
        }
    }

    CoUninitialize();
}

// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "setup_facts.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string>

#include "pipeline/pipeline_types.h"

namespace {

    constexpr float kMinZoom = 1, kMaxZoom = 3;  // the Zoom setting's range (plugin.cpp)
    // A game's fact before the game has run with the source, and when the headset couldn't be read either.
    constexpr const wchar_t* kNotRead = L"<Couldn't read the headset>";

    // A rate in its own unit: whole numbers without decimals (90), others with two (59.94).
    std::wstring Rate(double perSecond) {
        wchar_t text[32];
        const double whole = std::round(perSecond);
        swprintf(text, 32, std::fabs(perSecond - whole) < 0.01 ? L"%.0f" : L"%.2f", perSecond);
        return text;
    }

    std::wstring Size(uint32_t width, uint32_t height) {
        wchar_t text[48];
        swprintf(text, 48, L"%u \u00D7 %u", width, height);  // the multiplication sign
        return text;
    }

    // The source's left eye image and the headset's refresh rate if known, and whether the image is the
    // game's own size: "2508 x 2084 at 90 Hz (half resolution)".
    std::wstring Source(const MemoryFacts& f, const FrameSource& game) {
        const std::wstring size = Size(f.imageWidth, f.imageHeight);
        const wchar_t* resolution = game.HalvesImages() ? L" (half resolution)" : L" (full resolution)";
        return (f.period > 0 ? size + L" at " + Rate(1 / f.period) + L" Hz" : size) + resolution;
    }

    std::wstring Obs(const SetupFactsInput& in) {
        std::wstring text = Size(in.obs.canvasWidth, in.obs.canvasHeight);
        return in.obs.framePeriod > 0 ? text + L" at " + Rate(1 / in.obs.framePeriod) + L" fps" : text;
    }

    // The store's video memory with the settings now, "3.1 GB", "Up to 3.3 GB" before the game has run;
    // empty if not known.
    std::wstring Memory(const SetupFactsInput& in, const FrameSource& game) {
        int slots = 0;
        uint32_t width = 0, height = 0;

        if (!StoreSize(in.facts, in.settings, game, in.obs, &slots, &width, &height)) {
            return {};
        }

        wchar_t text[64];
        swprintf(
            text,
            64,
            in.facts.measured ? L"%.1f GB" : L"Up to %.1f GB",
            static_cast<double>(slots) * width * height * 4 / 1e9
        );
        return text;
    }

    // The largest canvas of the canvas's shape whose middle the eye image fills 1:1 at the zoom set now,
    // "2560 x 1440": the width the canvas needs of the image grows in step with its own
    // (NativeImageWidth). Widths in 16s, so a 16:9 canvas keeps whole pixels; rounded down, so it is native.
    std::wstring NativeCanvas(const SetupFactsInput& in) {
        const uint32_t canvasWidth = in.obs.canvasWidth;
        const float aspect = static_cast<float>(canvasWidth)
                             / static_cast<float>(std::max<uint32_t>(1, in.obs.canvasHeight));
        const float need = NativeImageWidth(in.facts.eye, in.settings, static_cast<int>(canvasWidth), aspect);
        const double widest = need > 0 ? static_cast<double>(canvasWidth) * in.facts.imageWidth / need : 0;
        const uint32_t width = static_cast<uint32_t>(widest / 16) * 16;
        const uint32_t height = static_cast<uint32_t>(width / aspect / 2) * 2;
        return Size(width, height);
    }

    // Whether the recording is native at the zoom and canvas set now (`*no`: it isn't), then the zoom or the
    // canvas that keeps it native: "Yes. Native up to zoom 1.45, canvas up to 4224 x 2376", "No. Lower zoom
    // to 1.31 or canvas to 2560 x 1440". Both the zoom and the canvas are rounded down, so they are native
    // too.
    std::wstring Native(const SetupFactsInput& in, bool* no) {
        const float zoom = MaxNativeZoom(in.facts, in.settings, in.obs.canvasWidth, in.obs.canvasHeight);
        const std::wstring canvas = NativeCanvas(in);
        *no = zoom <= 0 || in.settings.zoom > zoom;

        if (zoom >= kMaxZoom) {
            return L"Yes. Native at any zoom, canvas up to " + canvas;
        }

        if (zoom <= 0) {
            return L"No. Lower canvas to " + canvas;  // even zoom 1 isn't
        }

        wchar_t shown[16];
        swprintf(shown, 16, L"%.2f", std::floor(zoom * 100) / 100);

        if (!*no) {
            return L"Yes. Native up to zoom " + std::wstring(shown) + L", canvas up to " + canvas;
        }

        return L"No. Lower zoom to " + std::wstring(shown) + L" or canvas to " + canvas;
    }

    // A row of the game's: its value; or while it isn't known, a waiting bar (the headset test
    // runs, or a game in VR is about to be measured), or that the headset couldn't be read (`unread`).
    // `barWidth`: the waiting bar's, in ems, about the value's to come (measured in the empty state's font).
    EmptyFact
    GameFact(const wchar_t* label, bool known, const std::wstring& value, bool unread, float barWidth) {
        if (known) {
            return {label, value};
        }

        return unread ? EmptyFact{label, kNotRead, true} : EmptyFact{label, L"", true, true, barWidth};
    }

}

float MaxNativeZoom(
    const MemoryFacts& facts,
    const MirrorSettings& settings,
    uint32_t canvasWidth,
    uint32_t canvasHeight
) {
    const float aspect = static_cast<float>(canvasWidth)
                         / static_cast<float>(std::max<uint32_t>(1, canvasHeight));
    const auto native = [&](float zoom) {
        MirrorSettings s = settings;
        s.zoom = zoom;
        return NativeImageWidth(facts.eye, s, static_cast<int>(canvasWidth), aspect) <= facts.imageWidth;
    };

    if (!native(kMinZoom)) {
        return 0;
    }

    if (native(kMaxZoom)) {
        return kMaxZoom;
    }

    // The width the canvas needs grows with the zoom: halve the step between native and not.
    float low = kMinZoom, high = kMaxZoom;

    for (int i = 0; i < 24; i++) {
        const float mid = (low + high) / 2;
        (native(mid) ? low : high) = mid;
    }

    return low;
}

std::vector<EmptyFact> SetupFacts(const SetupFactsInput& in, const FrameSource& game) {
    const MemoryFacts& f = in.facts;
    const bool image = f.imageWidth > 0 && f.imageHeight > 0;
    const bool native = image && f.hasEye;
    const std::wstring memory = Memory(in, game);
    const bool unread = in.headsetUnread;
    // While the headset is being read every row waits, Canvas's too: the values come in together.
    const bool loading = !image && !unread;
    bool notNative = false;
    EmptyFact
        nativeRow = GameFact(L"Native recording", native, native ? Native(in, &notNative) : L"", unread, 23);

    if (native) {
        nativeRow.tone = notNative ? FactTone::kWarning : FactTone::kGood;
    }

    return {
        GameFact(L"Source", image, image ? Source(f, game) : L"", unread, 17.5f),
        GameFact(L"Canvas", !loading, Obs(in), unread, 10.5f),
        GameFact(L"VRAM usage", !memory.empty(), memory, unread, 6),
        nativeRow,
    };
}

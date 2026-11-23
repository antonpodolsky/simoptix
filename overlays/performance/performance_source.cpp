// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// SimOptix: Performance Overlay: an OBS source that shows the game's frame rate and how much of each
// frame's time is left on the GPU and on the CPU, as OpenXR Toolkit's performance overlay works them out
// (shared\performance\performance_reading.h). The OpenXR layer times the game's frames while this source
// is shown, and the source reads them once a second (shared\performance\performance_reader.h). Without the
// layer in the game, the readings are left out.
//
// Its properties hold the theme: Chrono or OLED (performance_look.h). Always drawn at the size it is shown
// on the canvas (overlay_box.h). In OBS it is 208 x 109, its size on a 1080-pixel canvas: two thirds of the
// design's 312 x 164 box, which it scales into that size. A new one starts at the top right, with its
// bezel's right edge in line with the G-meter's rim.

#include <graphics/vec2.h>
#include <obs-module.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <vector>

#include "meter_draw.h"
#include "overlay_box.h"
#include "performance_look.h"
#include "performance_view.h"
#include "shared/guarded.h"
#include "shared/log.h"
#include "shared/performance/performance_reader.h"
#include "shared/telemetry/qpc_clock.h"

namespace {

    // Its size on a 1080-pixel canvas: two thirds of the design's box (whose design pixels are canvas pixels
    // there), 208 x 109.33, its height rounded down to OBS's whole pixels.
    constexpr float kSize = 2.0f / 3;
    constexpr uint32_t kLayoutWidth = 208, kLayoutHeight = 109;

    // Its place on a 1080-pixel canvas: the bezel (at (40, 40) in the design's box, 232 wide) 47 px from the
    // right edge and 42 from the top, the G-meter's rim's distance from the right edge. Pixels.
    constexpr float kBezelRight = 272 * kSize, kBezelTop = 40 * kSize;
    constexpr float kRimFromRight = 47, kRimFromTop = 42;

    struct PerformanceOverlay {
        obs_source_t* source = nullptr;
        std::atomic<bool> wanted{false};
        std::atomic<int> theme{static_cast<int>(PerformanceTheme::kChrono)};  // set by the UI thread
        OverlayBox box{kLayoutWidth, kLayoutHeight, "Performance Overlay"};

        // Graphics thread.
        PerformanceReader reader;
        PerformanceView view;
        PerformanceLook look;
        MeterBatch batch;           // everything drawn this frame, uploaded once
        std::vector<float> shapes;  // drawing buffer, kept across frames
    };

    PerformanceTheme ThemeOf(obs_data_t* settings) {
        return strcmp(obs_data_get_string(settings, "theme"), "oled") == 0 ? PerformanceTheme::kOled
                                                                           : PerformanceTheme::kChrono;
    }

    vec2 DefaultPosition(uint32_t canvasW, uint32_t canvasH) {
        const float k = canvasH / 1080.0f;
        vec2 pos;
        vec2_set(&pos, canvasW - (kRimFromRight + kBezelRight) * k, (kRimFromTop - kBezelTop) * k);
        return pos;
    }

    // Screen pixels per design pixel, at the height it is drawn at: its 109 px in OBS hold 109 / kSize
    // design pixels.
    float DesignScale(int drawHeight) {
        return drawHeight * kSize / kLayoutHeight;
    }

    const char* GetName(void*) {
        return "SimOptix: Performance Overlay";
    }

    void GetDefaults(obs_data_t* s) {
        Guarded("defaults", [&] { obs_data_set_default_string(s, "theme", "chrono"); });
    }

    obs_properties_t* GetProperties(void*) {
        return Guarded("properties", [&] {
            obs_properties_t* p = obs_properties_create();
            obs_property_t* theme = obs_properties_add_list(
                p,
                "theme",
                "Theme",
                OBS_COMBO_TYPE_LIST,
                OBS_COMBO_FORMAT_STRING
            );

            obs_property_list_add_string(theme, "Chrono", "chrono");
            obs_property_list_add_string(theme, "OLED", "oled");
            return p;
        });
    }

    void Update(void* data, obs_data_t* settings) {
        Guarded("update", [&] {
            static_cast<PerformanceOverlay*>(data)->theme = static_cast<int>(ThemeOf(settings));
        });
    }

    void* Create(obs_data_t* settings, obs_source_t* source) {
        return Guarded("create", [&]() -> void* {
            auto o = std::make_unique<PerformanceOverlay>();
            o->source = source;
            o->theme = static_cast<int>(ThemeOf(settings));
            o->shapes.reserve(1 << 12);
            return o.release();  // OBS keeps the pointer as a void* until Destroy gives it back
        });
    }

    void Destroy(void* data) {
        Guarded("destroy", [&] {
            // the one place that frees a PerformanceOverlay
            std::unique_ptr<PerformanceOverlay> o(static_cast<PerformanceOverlay*>(data));
            obs_enter_graphics();
            o->look.Destroy();
            o->batch.Destroy();
            obs_leave_graphics();
        });
    }

    uint32_t GetWidth(void*) {
        return kLayoutWidth;
    }
    uint32_t GetHeight(void*) {
        return kLayoutHeight;
    }
    void Show(void* data) {
        static_cast<PerformanceOverlay*>(data)->wanted = true;
    }
    void Hide(void* data) {
        static_cast<PerformanceOverlay*>(data)->wanted = false;
    }

    // Reads only while shown: the reader's signing is what makes the layer time the game's frames.
    void VideoTick(void* data, float) {
        Guarded("tick", [&] {
            PerformanceOverlay* o = static_cast<PerformanceOverlay*>(data);
            const double now = QpcNow();
            o->box.Tick(now);
            o->box.Place(o->source, now, DefaultPosition);

            if (!o->wanted) {
                return;
            }

            o->view = ViewOf(o->reader.Read());

            if (o->box.DrawHeight() > 0) {
                const PerformanceTheme theme = static_cast<PerformanceTheme>(o->theme.load());
                o->look.Tick(theme, DesignScale(o->box.DrawHeight()), now);
            }
        });
    }

    void VideoRender(void* data, gs_effect_t*) {
        Guarded("render", [&] {
            PerformanceOverlay* o = static_cast<PerformanceOverlay*>(data);

            if (!o->box.Fit()) {
                return;
            }

            const PerformanceTheme theme = static_cast<PerformanceTheme>(o->theme.load());
            gs_blend_state_push();
            gs_enable_blending(true);
            gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);
            o->look.Draw(o->view, theme, DesignScale(o->box.DrawHeight()), o->batch, o->shapes);
            gs_blend_state_pop();
            gs_matrix_pop();
        });
    }

}

void RegisterPerformanceSource() {
    obs_source_info info = {};
    info.id = "simoptix_performance_overlay";
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;

    info.get_name = GetName;
    info.create = Create;
    info.destroy = Destroy;
    info.get_defaults = GetDefaults;
    info.get_properties = GetProperties;
    info.update = Update;
    info.get_width = GetWidth;
    info.get_height = GetHeight;
    info.show = Show;
    info.hide = Hide;
    info.video_tick = VideoTick;
    info.video_render = VideoRender;

    obs_register_source(&info);
}

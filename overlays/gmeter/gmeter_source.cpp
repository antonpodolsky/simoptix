// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// SimOptix: G-Meter Overlay: an OBS source that draws a friction circle (G-G meter) from the game's
// telemetry, like the in-car G-meters of Porsche's Sport Chrono or the Nissan GT-R: the dot's
// direction from the centre is the direction of the force and its distance the amount, against
// rings at every whole G. A comet tail (the dot's recent positions, shrinking and fading) shows how
// braking blends into cornering, and the highest G reached in each of the four directions stands
// outside the outer ring (reset when a new driving session starts). Two bars just inside the outer
// ring show the brake (left) and throttle (right), each darker while ABS or TC intervenes. Two themes:
// Chrono (meter_chrono.h: a black bezel around a face under domed glass) and OLED (meter_oled.h: a
// self-lit black display with dotted rings and segmented bars).
//
// Its properties hold only the theme and the demo lap's button; the rest is
// fixed here (the constants below). The dot moves the way the driver is pushed (braking: forward, a
// left turn: right), as those in-car meters do. Drawn on OBS's frame clock from the telemetry of the
// moment the view shows, lightly smoothed (raw acceleration is noisy over kerbs). Only holds
// telemetry while the source is shown. Labels are set in Bahnschrift (Windows' DIN typeface) with
// DirectWrite and placed by their ink, so gaps are exact.
//
// Always drawn at the size it is shown on the canvas, so it stays sharp however it is scaled: in OBS
// it is 300 px (its layout size) under the scene item's scale (overlay_box.h). Its Properties window is
// sized so the preview shows it at that size too (properties_preview.h).
//
// This file is the source: settings and values. The themes are drawn by meter_chrono.* and meter_oled.*
// (each a MeterTheme, meter_theme.h), from the values in meter_view.h; shapes and batching are in
// meter_draw.*, labels in meter_text.*, images in meter_image.*.

#include <graphics/vec2.h>
#include <obs-module.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "meter_chrono.h"
#include "meter_draw.h"
#include "meter_oled.h"
#include "meter_theme.h"
#include "meter_view.h"
#include "overlay_box.h"
#include "properties_preview.h"
#include "shared/guarded.h"
#include "shared/log.h"
#include "shared/source_icon.h"
#include "shared/telemetry/demo_lap.h"
#include "shared/telemetry/qpc_clock.h"
#include "shared/telemetry/telemetry.h"

namespace {

    constexpr float kG = 9.80665f;

    // The meter's layout size in OBS (its width and height before the scene item's scale), pixels.
    constexpr uint32_t kLayoutSize = 300;

    // The dot's smoothing: the time constant of its exponential filter, seconds. The tail's length, seconds.
    constexpr float kSmoothingS = 0.080f;
    constexpr float kTailS = 1;

    // The themes, as the Theme setting saves them ("chrono", "oled").
    enum class ThemeChoice { kChrono, kOled };

    ThemeChoice ThemeOf(obs_data_t* settings) {
        return strcmp(obs_data_get_string(settings, "theme"), "oled") == 0 ? ThemeChoice::kOled
                                                                           : ThemeChoice::kChrono;
    }

    std::unique_ptr<MeterTheme> MakeTheme(ThemeChoice choice) {
        if (choice == ThemeChoice::kOled) {
            return std::make_unique<OledTheme>();
        }

        return std::make_unique<ChronoTheme>();
    }

    struct GMeter {
        obs_source_t* source = nullptr;
        std::atomic<bool> wanted{false};
        bool started = false;
        OverlayBox box{kLayoutSize, kLayoutSize, "G-Meter Overlay"};

        // the demo lap instead of the game: set by the properties button, not saved
        std::atomic<bool> demo{false};
        // The Debug setting (UI thread): its log lines, as the gate of the threads working for it
        // (ScopedLogGate, shared\log.h), and the telemetry's while it holds it.
        std::atomic<bool> debug{false};
        bool heldDebug = false;  // graphics thread: Debug as told to the telemetry (Telemetry::HoldDebug)

        // Graphics thread.
        int fitTries = 0;  // UI-thread steps left for fitting the Properties preview (FitPreviewTask)
        double nextFitTry = 0;
        bool fitRestart = false;  // the next step starts a new fit

        // The Properties preview's fit: asked for by the UI thread (GetProperties), stepped there.
        std::atomic<bool> fitWanted{false}, fitStop{false};
        PreviewFit fit;  // UI thread

        double lastSample = 0,
               lastRender = 0;     // performance-counter seconds, on the view's clock (Update)
        bool connected = false;    // a game was connected at lastRender
        bool demoRunning = false;  // the demo as this thread last saw it
        double demoStart = 0;      // performance-counter seconds: the demo lap's time 0
        float gx = 0, gy = 0;
        bool hasValue = false;
        Trail trail;
        float maxRight = 0, maxLeft = 0, maxForward = 0, maxBack = 0;  // highest G reached each way
        unsigned session = 0;  // driving session of the last sample: a new one resets the maxima
        std::atomic<ThemeChoice> wantedChoice{ThemeChoice::kChrono};  // set from the settings (UI thread)
        ThemeChoice choice = ThemeChoice::kChrono;  // graphics thread: which theme `theme` is
        std::unique_ptr<MeterTheme> theme = MakeTheme(ThemeChoice::kChrono);  // graphics thread
        MeterBatch batch;  // everything drawn this frame, uploaded once
        // new telemetry each frame: sized once, so nothing is allocated per frame
        std::vector<CarSample> samples;
        PedalState pedals;          // from the newest sample
        std::vector<float> shapes;  // drawing buffer, kept across frames
    };

    // ---- Values.

    void Update(GMeter* m) {
        // The moment the view shows (the telemetry follows the Pimax view, which may run behind the game).
        // The demo lap has no view to follow.
        const bool demo = m->demo;
        const double now = demo ? QpcNow() : Telemetry::Shown();
        m->lastRender = now;

        // The demo starting or stopping starts afresh: no tail or highest values carried between the demo
        // and real telemetry, and only telemetry newer than the switch.
        if (demo != m->demoRunning) {
            m->demoRunning = demo;
            m->demoStart = m->lastSample = now;
            m->hasValue = false;
            m->gx = m->gy = 0;
            m->trail.clear();
            m->maxRight = m->maxLeft = m->maxForward = m->maxBack = 0;
            CoreLog("G-Meter Overlay: demo %s", demo ? "started" : "stopped");
        }

        const size_t
            count = demo ? DemoSamples(m->demoStart, m->lastSample, now, m->samples.data(), m->samples.size())
                         : Telemetry::Since(m->lastSample, now, m->samples.data(), m->samples.size());

        if (!demo) {
            m->connected = Telemetry::SessionAt(now).connected;
        }
        for (size_t i = 0; i < count; i++) {
            const CarSample& s = m->samples[i];

            // The meter: x right, y up (forward). It shows the force the driver feels, against the
            // acceleration: pushed right when the car accelerates left (+y, a left turn), forward when it
            // accelerates backwards (-x, braking).
            const float x = s.accel.y / kG, y = -s.accel.x / kG;
            float a = 1;
            if (m->hasValue) {
                a = 1 - std::exp(-float(s.t - m->lastSample) / kSmoothingS);
            }
            m->gx += a * (x - m->gx);
            m->gy += a * (y - m->gy);
            if (s.session != m->session) {
                m->session = s.session;
                m->maxRight = m->maxLeft = m->maxForward = m->maxBack = 0;
            }

            m->hasValue = true;
            m->lastSample = s.t;
            m->maxRight = std::max(m->maxRight, m->gx);
            m->maxLeft = std::max(m->maxLeft, -m->gx);
            m->maxForward = std::max(m->maxForward, m->gy);
            m->maxBack = std::max(m->maxBack, -m->gy);
            m->trail.push_back({s.t, m->gx, m->gy});
            m->pedals = {s.brake, s.throttle, s.absActive, s.tcActive};
        }

        while (!m->trail.empty() && m->trail.front().t < now - kTailS) {
            m->trail.pop_front();
        }
    }

    // ---- The OBS source.

    const char* GetName(void*) {
        return "SimOptix: G-Meter Overlay";
    }

    const char* GetDarkIcon(void*) {
        return Guarded("dark icon", [] { return FindSourceIcon("gmeter_dark.svg"); });
    }

    const char* GetLightIcon(void*) {
        return Guarded("light icon", [] { return FindSourceIcon("gmeter_light.svg"); });
    }

    // ---- Default size and place: the bottom-right corner, in proportion to the canvas: on 1920x1080,
    // 300 px with its box 16.125 px from the right edge and 10.875 from the bottom (on 2560x1440: 400 px,
    // gaps 21.5 and 14.5).

    vec2 DefaultPosition(uint32_t canvasW, uint32_t canvasH) {
        const float k = canvasH / 1080.0f, shown = kLayoutSize * k;
        vec2 pos;
        vec2_set(&pos, canvasW - shown - 16.125f * k, canvasH - shown - 10.875f * k);
        return pos;
    }

    void GetDefaults(obs_data_t* s) {
        Guarded("defaults", [&] { obs_data_set_default_string(s, "theme", "chrono"); });
    }

    // UI thread: starts or stops the demo lap, and names the button for what it does next.
    bool ToggleDemo(obs_properties_t*, obs_property_t* button, void* data) {
        return Guarded("demo button", [&]() -> bool {
            GMeter* m = static_cast<GMeter*>(data);
            const bool demo = !m->demo;
            m->demo = demo;
            obs_property_set_description(button, demo ? "Stop demo" : "Start demo");
            return true;  // the button's name changed
        });
    }

    obs_properties_t* GetProperties(void* data) {
        return Guarded("properties", [&] {
            // Its Properties window is opening (or refreshing): size its preview to show the meter 1:1.
            if (data) {
                static_cast<GMeter*>(data)->fitWanted = true;
            }
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
            obs_properties_add_button2(
                p,
                "demo",
                data && static_cast<GMeter*>(data)->demo ? "Stop demo" : "Start demo",
                ToggleDemo,
                data
            );

            obs_properties_add_bool(p, "debug", "Debug");
            return p;
        });
    }

    // UI thread: the theme the settings ask for, which the graphics thread makes on its next tick
    // (SwitchTheme), and Debug.
    void UpdateSettings(void* data, obs_data_t* settings) {
        Guarded("update", [&] {
            GMeter* m = static_cast<GMeter*>(data);
            m->wantedChoice = ThemeOf(settings);
            m->debug = obs_data_get_bool(settings, "debug");
        });
    }

    void* Create(obs_data_t* settings, obs_source_t* source) {
        return Guarded("create", [&]() -> void* {
            auto m = std::make_unique<GMeter>();
            m->samples.resize(Telemetry::kRing);
            m->shapes.reserve(1 << 16);
            m->source = source;
            m->wantedChoice = m->choice = ThemeOf(settings);
            m->theme = MakeTheme(m->choice);
            m->debug = obs_data_get_bool(settings, "debug");
            return m.release();  // OBS keeps the pointer as a void* until Destroy gives it back
        });
    }

    // Lets go of the telemetry if it holds it, its Debug first (Telemetry::HoldDebug).
    void ReleaseTelemetry(GMeter* m) {
        if (m->heldDebug) {
            Telemetry::HoldDebug(false);
            m->heldDebug = false;
        }

        if (m->started) {
            Telemetry::Release();
            m->started = false;
        }
    }

    // Holds the telemetry while the meter is shown, its Debug first: the reader's first pass, which starts
    // with Acquire, already logs as Debug says. Graphics thread, each tick.
    void HoldTelemetry(GMeter* m) {
        const bool debug = m->debug;

        if (debug != m->heldDebug) {
            Telemetry::HoldDebug(debug);
            m->heldDebug = debug;
        }

        if (!m->started) {
            Telemetry::Acquire();
            m->started = true;
        }
    }

    void Destroy(void* data) {
        Guarded("destroy", [&] {
            std::unique_ptr<GMeter> m(static_cast<GMeter*>(data));  // the one place that frees a GMeter
            const ScopedLogGate gate(&m->debug);                    // ends before the meter is freed
            ReleaseTelemetry(m.get());
            obs_enter_graphics();
            m->theme->Destroy();
            m->batch.Destroy();
            obs_leave_graphics();
        });
    }

    uint32_t GetWidth(void*) {
        return kLayoutSize;
    }
    uint32_t GetHeight(void*) {
        return kLayoutSize;
    }
    void Show(void* data) {
        static_cast<GMeter*>(data)->wanted = true;
    }
    void Hide(void* data) {
        static_cast<GMeter*>(data)->wanted = false;
    }

    // Graphics thread: replaces the theme when the setting changed, freeing the old one's textures. The
    // new one builds its images and labels on this tick, before it is first drawn.
    void SwitchTheme(GMeter* m) {
        const ThemeChoice wanted = m->wantedChoice;
        if (wanted == m->choice) {
            return;
        }

        obs_enter_graphics();
        m->theme->Destroy();
        obs_leave_graphics();

        m->theme = MakeTheme(wanted);
        m->choice = wanted;
        CoreLog("G-Meter Overlay: theme %s", wanted == ThemeChoice::kOled ? "OLED" : "Chrono");
    }

    // UI thread: one step of fitting the meter's Properties preview (properties_preview.h). Holds a
    // reference to the source, so the meter outlives the task.
    struct FitTask {
        obs_source_t* source;
        GMeter* m;
        bool restart;
    };

    void FitPreviewTask(void* param) {
        std::unique_ptr<FitTask> t(static_cast<FitTask*>(param));  // queued by StepPreviewFit: freed here
        Guarded("preview fit", [&] {
            GMeter* m = t->m;
            const ScopedLogGate gate(&m->debug);

            if (t->restart) {
                RestartPreviewFit(m->fit);
            }
            if (!m->fitStop) {
                const PreviewFitResult r = FitPropertiesPreview(
                    obs_source_get_name(t->source),
                    m->box.DrawHeight(),
                    m->box.PreviewPixels(),
                    m->fit
                );

                if (r == PreviewFitResult::Fitted || r == PreviewFitResult::GaveUp) {
                    m->fitStop = true;
                }
            }
        });

        obs_source_release(t->source);
    }

    // Graphics thread: while a fit is wanted, a step ten times a second, for at most 3 s.
    void StepPreviewFit(GMeter* m, double now) {
        if (m->fitWanted.exchange(false)) {
            m->fitStop = false;
            m->fitTries = 30;
            m->nextFitTry = now;
            m->fitRestart = true;
        }

        if (m->fitTries <= 0 || now < m->nextFitTry || m->box.DrawHeight() <= 0) {
            return;
        }
        if (m->fitStop) {
            m->fitTries = 0;
            return;
        }

        obs_source_t* source = obs_source_get_ref(m->source);
        if (!source) {
            return;
        }
        m->fitTries--;
        m->nextFitTry = now + 0.1;
        auto task = std::make_unique<FitTask>(FitTask{source, m, m->fitRestart});
        obs_queue_task(OBS_TASK_UI, FitPreviewTask, task.release(), false);  // FitPreviewTask frees it
        m->fitRestart = false;
    }

    // Each callback logs as the meter's Debug setting says (shared\log.h): OBS's threads work for every
    // source in turn.
    void VideoTick(void* data, float) {
        Guarded("tick", [&] {
            GMeter* m = static_cast<GMeter*>(data);
            const ScopedLogGate gate(&m->debug);

            if (m->wanted) {
                HoldTelemetry(m);
            } else {
                ReleaseTelemetry(m);
            }

            const double now = QpcNow();

            if (m->box.Tick(now)) {
                m->fitWanted = true;  // an open Properties preview follows
            }

            StepPreviewFit(m, now);
            SwitchTheme(m);
            if (m->wanted && m->box.DrawHeight() > 0) {
                m->theme->Tick(m->box.DrawHeight(), now);
            }

            m->box.Place(m->source, now, DefaultPosition);
        });
    }

    void VideoRender(void* data, gs_effect_t*) {
        Guarded("render", [&] {
            GMeter* m = static_cast<GMeter*>(data);
            const ScopedLogGate gate(&m->debug);
            Update(m);
            if (!m->box.Fit()) {
                return;
            }

            MeterView view;
            view.size = float(m->box.DrawHeight());
            view.now = m->lastRender;
            view.live = (m->demoRunning || m->connected) && m->hasValue;
            view.gx = m->gx;
            view.gy = m->gy;
            view.trail = &m->trail;
            view.trailS = kTailS;

            view.maxForward = m->maxForward;
            view.maxBack = m->maxBack;
            view.maxRight = m->maxRight;
            view.maxLeft = m->maxLeft;
            view.pedals = m->pedals;

            gs_blend_state_push();
            gs_enable_blending(true);
            gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);
            m->theme->Draw(view, m->batch, m->shapes);
            gs_blend_state_pop();
            gs_matrix_pop();
        });
    }

}

void RegisterGMeterSource() {
    obs_source_info info = {};
    info.id = "simoptix_gmeter_overlay";
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;

    info.icon_type = OBS_ICON_TYPE_CUSTOM;
    info.get_dark_icon = GetDarkIcon;
    info.get_light_icon = GetLightIcon;

    info.get_name = GetName;
    info.create = Create;
    info.destroy = Destroy;
    info.get_defaults = GetDefaults;
    info.get_properties = GetProperties;
    info.update = UpdateSettings;
    info.get_width = GetWidth;
    info.get_height = GetHeight;
    info.show = Show;
    info.hide = Hide;
    info.video_tick = VideoTick;
    info.video_render = VideoRender;

    obs_register_source(&info);
}

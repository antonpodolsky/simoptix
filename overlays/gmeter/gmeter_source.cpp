// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// SimOptix: G-Meter Overlay: an OBS source that draws a friction circle (G-G meter) from the game's
// telemetry, like the in-car G-meters of Porsche's Sport Chrono or the Nissan GT-R: the dot's
// direction from the centre is the direction of the force and its distance the amount, against
// rings at every whole G. A comet tail (the dot's recent positions, shrinking and fading) shows how
// braking blends into cornering, and the highest G reached in each of the four directions stands
// outside the outer ring (reset when a new driving session starts). Two bars show the brake (left)
// and throttle (right), with ABS and TC lighting up. One look, Chrono (meter_chrono.h: a black bezel
// under domed glass).
//
// Its properties hold only the meter type (Chrono, the one look) and the demo lap's button; the rest is
// fixed here (the constants below). The dot moves the way the driver is pushed (braking: forward, a
// left turn: right), as those in-car meters do. Drawn on OBS's frame clock from the telemetry of the
// moment the picture shows, lightly smoothed (raw acceleration is noisy over kerbs). Only holds
// telemetry while the source is shown. Labels are set in Bahnschrift (Windows' DIN typeface) with
// DirectWrite and placed by their ink, so gaps are exact.
//
// Always drawn at the size it is shown on the canvas, so it stays sharp however it is scaled: in OBS
// it is 300 px (its layout size) under the scene item's scale, which OBS keeps in proportion to the
// canvas; while drawing, the meter reads that scale and draws itself in canvas pixels (see Fit). Its
// Properties window is sized so the preview shows it at that size too (properties_preview.h).
//
// This file is the source: settings and values. The look is drawn in meter_chrono.*, from the values
// in meter_view.h; shapes and batching are in meter_draw.*, labels in meter_text.*, pictures in
// meter_image.*.

#include <graphics/matrix4.h>
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
#include "meter_view.h"
#include "properties_preview.h"
#include "shared/guarded.h"
#include "shared/log.h"
#include "shared/telemetry/demo_lap.h"
#include "shared/telemetry/qpc_clock.h"
#include "shared/telemetry/telemetry.h"

namespace {

    constexpr float kG = 9.80665f;

    // The meter's layout size in OBS (its width and height before the scene item's scale), pixels.
    constexpr uint32_t kLayoutSize = 300;
    // How long the shown size must stay the same before the meter is rebuilt for it (its pictures take
    // ~20 ms on the graphics thread): while a meter is being resized, the last build is stretched. Seconds.
    constexpr double kResizeSettle = 0.3;

    // The dot's smoothing: the time constant of its exponential filter, seconds. The tail's length, seconds.
    constexpr float kSmoothingS = 0.080f;
    constexpr float kTailS = 1;

    struct GMeter {
        obs_source_t* source = nullptr;
        std::atomic<bool> wanted{false};
        bool started = false;
        int placeTries = 0;  // first placement in the scene (see PlaceNewItem)
        double nextPlaceTry = 0;

        // the demo lap instead of the game: set by the properties button, not saved
        std::atomic<bool> demo{false};

        // Graphics thread (drawSize and previewPixels also read by the UI thread).
        float shownScale = 0;  // the largest canvas scale it was drawn at since the last tick (0: not drawn)
        std::atomic<int> drawSize{0};  // the size the meter is built and drawn at, pixels (0: not yet)
        int wantSize = 0;              // the shown size, and since when (performance-counter seconds)
        double wantSince = 0;
        std::atomic<int> previewPixels{0};  // the size it was last shown at in a preview window, pixels
        int fitTries = 0;  // UI-thread steps left for fitting the Properties preview (FitPreviewTask)
        double nextFitTry = 0;
        bool fitRestart = false;  // the next step starts a new fit

        // The Properties preview's fit: asked for by the UI thread (GetProperties), stepped there.
        std::atomic<bool> fitWanted{false}, fitStop{false};
        PreviewFit fit;  // UI thread

        double lastSample = 0,
               lastRender = 0;     // performance-counter seconds, on the picture's clock (Update)
        bool connected = false;    // a game was connected at lastRender
        bool demoRunning = false;  // the demo as this thread last saw it
        double demoStart = 0;      // performance-counter seconds: the demo lap's time 0
        float gx = 0, gy = 0;
        bool hasValue = false;
        Trail trail;
        float maxRight = 0, maxLeft = 0, maxForward = 0, maxBack = 0;  // highest G reached each way
        unsigned session = 0;  // driving session of the last sample: a new one resets the maxima
        ChronoMeter look;
        MeterBatch batch;  // everything drawn this frame, uploaded once
        // new telemetry each frame: sized once, so nothing is allocated per frame
        std::vector<CarSample> samples;
        PedalState pedals;          // from the newest sample
        std::vector<float> shapes;  // drawing buffer, kept across frames
    };

    // ---- Values.

    void Update(GMeter* m) {
        // The moment the picture shows (the telemetry follows the Pimax view, which may run behind the game).
        // The demo lap has no picture to follow.
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

    // ---- Default size and place: the bottom-right corner, in proportion to the canvas: on 1920x1080,
    // 300 px with its box 16.125 px from the right edge and 10.875 from the bottom (on 2560x1440: 400 px,
    // gaps 21.5 and 14.5).

    float DefaultScale(uint32_t canvasH) {
        return canvasH / 1080.0f;
    }

    vec2 DefaultPosition(uint32_t canvasW, uint32_t canvasH) {
        const float k = DefaultScale(canvasH), shown = kLayoutSize * k;
        vec2 pos;
        vec2_set(&pos, canvasW - shown - 16.125f * k, canvasH - shown - 10.875f * k);
        return pos;
    }

    // UI thread (OBS's place for changing scenes): moves a new meter's scene items from OBS's spot
    // for new sources (top left, unscaled) to the default place and size, once. Items the user has
    // already placed are left alone. Marks the source "placed" when it found it in a scene.
    void PlaceNewItem(void* param) {
        obs_weak_source_t* weak = static_cast<obs_weak_source_t*>(param);
        obs_source_t* source = obs_weak_source_get_source(weak);
        obs_weak_source_release(weak);
        if (!source) {
            return;
        }
        obs_data_t* settings = obs_source_get_settings(source);
        Guarded("place", [&] {
            if (obs_data_get_bool(settings, "placed")) {
                return;
            }

            struct Search {
                obs_source_t* source;
                vec2 pos, scale;
                bool found;
            } search = {source, {}, {}, false};

            obs_video_info ovi = {};
            obs_get_video_info(&ovi);
            search.pos = DefaultPosition(ovi.base_width, ovi.base_height);
            vec2_set(&search.scale, DefaultScale(ovi.base_height), DefaultScale(ovi.base_height));

            obs_enum_scenes(
                [](void* p, obs_source_t* scene) {
                    obs_scene_enum_items(
                        obs_scene_from_source(scene),
                        [](obs_scene_t*, obs_sceneitem_t* item, void* p) {
                            Search* search = static_cast<Search*>(p);
                            if (obs_sceneitem_get_source(item) != search->source) {
                                return true;
                            }
                            search->found = true;
                            vec2 pos, scale;
                            obs_sceneitem_get_pos(item, &pos);
                            obs_sceneitem_get_scale(item, &scale);
                            if (pos.x == 0 && pos.y == 0 && scale.x == 1 && scale.y == 1) {
                                obs_sceneitem_set_scale(item, &search->scale);
                                obs_sceneitem_set_pos(item, &search->pos);
                            }
                            return true;
                        },
                        p
                    );
                    return true;
                },
                &search
            );

            if (search.found) {
                obs_data_set_bool(settings, "placed", true);
            }
        });

        obs_data_release(settings);
        obs_source_release(source);
    }

    void GetDefaults(obs_data_t* s) {
        Guarded("defaults", [&] { obs_data_set_default_string(s, "meter_type", "chrono"); });
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
            obs_property_t* type = obs_properties_add_list(
                p,
                "meter_type",
                "Meter type",
                OBS_COMBO_TYPE_LIST,
                OBS_COMBO_FORMAT_STRING
            );

            obs_property_list_add_string(type, "Chrono", "chrono");
            obs_properties_add_button2(
                p,
                "demo",
                data && static_cast<GMeter*>(data)->demo ? "Stop demo" : "Start demo",
                ToggleDemo,
                data
            );

            return p;
        });
    }

    void* Create(obs_data_t*, obs_source_t* source) {
        return Guarded("create", [&]() -> void* {
            auto m = std::make_unique<GMeter>();
            m->samples.resize(Telemetry::kRing);
            m->shapes.reserve(1 << 16);
            m->source = source;
            return m.release();  // OBS keeps the pointer as a void* until Destroy gives it back
        });
    }

    void Destroy(void* data) {
        Guarded("destroy", [&] {
            std::unique_ptr<GMeter> m(static_cast<GMeter*>(data));  // the one place that frees a GMeter
            if (m->started) {
                Telemetry::Release();
            }
            obs_enter_graphics();
            m->look.Destroy();
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

    // Picks the size to build the meter at: the size it was last shown at (Fit), once that has held for
    // kResizeSettle, or at once the first time. Graphics thread.
    void FollowShownSize(GMeter* m, double now) {
        if (m->shownScale <= 0) {
            return;  // not drawn since the last tick
        }
        const int want = std::min(4096, std::max(16, int(std::lround(kLayoutSize * m->shownScale))));
        m->shownScale = 0;
        if (want != m->wantSize) {
            m->wantSize = want;
            m->wantSince = now;
        }

        if (want != m->drawSize && (m->drawSize == 0 || now - m->wantSince >= kResizeSettle)) {
            m->drawSize = want;
            m->fitWanted = true;  // an open Properties preview follows
            CoreLog("G-Meter Overlay: drawn at %d px (its size on the canvas)", want);
        }
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
            if (t->restart) {
                RestartPreviewFit(m->fit);
            }
            if (!m->fitStop) {
                const PreviewFitResult r = FitPropertiesPreview(
                    obs_source_get_name(t->source),
                    m->drawSize,
                    m->previewPixels,
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

        if (m->fitTries <= 0 || now < m->nextFitTry || m->drawSize <= 0) {
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

    void VideoTick(void* data, float) {
        Guarded("tick", [&] {
            GMeter* m = static_cast<GMeter*>(data);
            if (m->wanted && !m->started) {
                Telemetry::Acquire();
                m->started = true;
            } else if (!m->wanted && m->started) {
                Telemetry::Release();
                m->started = false;
            }

            const double now = QpcNow();
            FollowShownSize(m, now);
            StepPreviewFit(m, now);
            if (m->wanted && m->drawSize > 0) {
                m->look.Tick(m->drawSize, now);
            }

            // A new meter isn't in a scene yet when it's created: look for it twice a second, for 5 s.
            if (m->placeTries < 10 && now >= m->nextPlaceTry) {
                obs_data_t* settings = obs_source_get_settings(m->source);
                const bool placed = obs_data_get_bool(settings, "placed");
                obs_data_release(settings);
                m->placeTries = placed ? 10 : m->placeTries + 1;
                m->nextPlaceTry = now + 0.5;
                if (!placed) {
                    obs_queue_task(OBS_TASK_UI, PlaceNewItem, obs_source_get_weak_source(m->source), false);
                }
            }
        });
    }

    // Notes the canvas scale the meter is drawn at (the scene item's scale: OBS draws the scene in canvas
    // pixels), for FollowShownSize, and pushes a matrix that maps the meter's build size onto its layout
    // size. Shown at the size it was built, that is exactly 1:1 in canvas pixels, from a whole pixel, so
    // pictures and labels land unfiltered. False (nothing pushed) until the first build. Graphics thread.
    bool Fit(GMeter* m) {
        matrix4 world;
        gs_matrix_get(&world);
        const float sx = std::hypot(world.x.x, world.x.y), sy = std::hypot(world.y.x, world.y.y);
        m->shownScale = std::max(m->shownScale, std::max(sx, sy));

        // A preview window (Properties, Filters) draws the source alone, unmoved, into a viewport of the size
        // it shows it at; the canvas has a viewport of its own size.
        gs_rect viewport;
        gs_get_viewport(&viewport);
        obs_video_info ovi = {};
        if (sx == 1 && sy == 1 && world.t.x == 0 && world.t.y == 0 && obs_get_video_info(&ovi)
            && (uint32_t(viewport.cx) != ovi.base_width || uint32_t(viewport.cy) != ovi.base_height)) {
            m->previewPixels = std::min(viewport.cx, viewport.cy);
        }

        if (m->drawSize <= 0 || sx <= 0 || sy <= 0) {
            return false;
        }
        const float layout = float(kLayoutSize);
        const bool upright = std::fabs(world.x.y) < 1e-4f && std::fabs(world.y.x) < 1e-4f;
        gs_matrix_push();
        if (upright && std::fabs(sx - sy) < 1e-4f && int(std::lround(layout * sx)) == m->drawSize) {
            gs_matrix_translate3f(
                (std::round(world.t.x) - world.t.x) / sx,
                (std::round(world.t.y) - world.t.y) / sy,
                0
            );

            gs_matrix_scale3f(1 / sx, 1 / sy, 1);
        } else {
            // stretched until rebuilt (or turned)
            gs_matrix_scale3f(layout / m->drawSize, layout / m->drawSize, 1);
        }

        return true;
    }

    void VideoRender(void* data, gs_effect_t*) {
        Guarded("render", [&] {
            GMeter* m = static_cast<GMeter*>(data);
            Update(m);
            if (!Fit(m)) {
                return;
            }

            MeterView view;
            view.size = float(m->drawSize);
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
            m->look.Draw(view, m->batch, m->shapes);
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

    info.get_name = GetName;
    info.create = Create;
    info.destroy = Destroy;
    info.get_defaults = GetDefaults;
    info.get_properties = GetProperties;
    info.get_width = GetWidth;
    info.get_height = GetHeight;
    info.show = Show;
    info.hide = Hide;
    info.video_tick = VideoTick;
    info.video_render = VideoRender;

    obs_register_source(&info);
}

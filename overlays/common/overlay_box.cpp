// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "overlay_box.h"

#include <graphics/matrix4.h>

#include <algorithm>
#include <cmath>
#include <memory>

#include "shared/guarded.h"
#include "shared/log.h"

namespace {

    // How long the shown size must stay the same before the overlay is rebuilt for it. Seconds.
    constexpr double kResizeSettle = 0.3;

    float DefaultScale(uint32_t canvasH) {
        return canvasH / 1080.0f;
    }

    struct PlaceTask {
        obs_weak_source_t* weak;
        OverlayBox::DefaultPosition position;
    };

    // UI thread (OBS's place for changing scenes): moves a new overlay's scene items from OBS's spot for
    // new sources (top left, unscaled) to the default place and size, once. Items the user has already
    // placed are left alone. Marks the source "placed" when it found it in a scene.
    void PlaceNewItem(void* param) {
        const std::unique_ptr<PlaceTask> task(static_cast<PlaceTask*>(param));  // queued by Place: freed here
        obs_source_t* source = obs_weak_source_get_source(task->weak);
        obs_weak_source_release(task->weak);

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
            search.pos = task->position(ovi.base_width, ovi.base_height);
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

}

int OverlayBox::DrawWidth() const {
    const int h = drawHeight_;
    return h > 0 ? static_cast<int>(std::lround(static_cast<double>(layoutW_) * h / layoutH_)) : 0;
}

bool OverlayBox::Tick(double now) {
    if (shownScale_ <= 0) {
        return false;  // not drawn since the last tick
    }

    const int want = std::min(4096, std::max(16, static_cast<int>(std::lround(layoutH_ * shownScale_))));
    shownScale_ = 0;

    if (want != wantHeight_) {
        wantHeight_ = want;
        wantSince_ = now;
    }

    if (want == drawHeight_ || (drawHeight_ != 0 && now - wantSince_ < kResizeSettle)) {
        return false;
    }

    drawHeight_ = want;
    CoreLog("%s: drawn at %d x %d px (its size on the canvas)", name_, DrawWidth(), want);
    return true;
}

bool OverlayBox::Fit() {
    matrix4 world;
    gs_matrix_get(&world);
    const float sx = std::hypot(world.x.x, world.x.y), sy = std::hypot(world.y.x, world.y.y);
    shownScale_ = std::max(shownScale_, std::max(sx, sy));

    // A preview window (Properties, Filters) draws the source alone, unmoved, into a viewport of the size
    // it shows it at; the canvas has a viewport of its own size.
    gs_rect viewport;
    gs_get_viewport(&viewport);
    obs_video_info ovi = {};

    if (sx == 1 && sy == 1 && world.t.x == 0 && world.t.y == 0 && obs_get_video_info(&ovi)
        && (uint32_t(viewport.cx) != ovi.base_width || uint32_t(viewport.cy) != ovi.base_height)) {
        previewPixels_ = std::min(viewport.cx, viewport.cy);
    }

    const int drawHeight = drawHeight_;

    if (drawHeight <= 0 || sx <= 0 || sy <= 0) {
        return false;
    }

    const float layout = static_cast<float>(layoutH_);
    const bool upright = std::fabs(world.x.y) < 1e-4f && std::fabs(world.y.x) < 1e-4f;
    gs_matrix_push();

    if (upright && std::fabs(sx - sy) < 1e-4f && static_cast<int>(std::lround(layout * sy)) == drawHeight) {
        gs_matrix_translate3f(
            (std::round(world.t.x) - world.t.x) / sx,
            (std::round(world.t.y) - world.t.y) / sy,
            0
        );

        gs_matrix_scale3f(1 / sx, 1 / sy, 1);
    } else {
        // stretched until rebuilt (or turned)
        gs_matrix_scale3f(layout / drawHeight, layout / drawHeight, 1);
    }

    return true;
}

void OverlayBox::Place(obs_source_t* source, double now, DefaultPosition position) {
    // A new overlay isn't in a scene yet when it's created: look for it twice a second, for 5 s.
    if (placeTries_ >= 10 || now < nextPlaceTry_) {
        return;
    }

    obs_data_t* settings = obs_source_get_settings(source);
    const bool placed = obs_data_get_bool(settings, "placed");
    obs_data_release(settings);
    placeTries_ = placed ? 10 : placeTries_ + 1;
    nextPlaceTry_ = now + 0.5;

    if (!placed) {
        auto task = std::make_unique<PlaceTask>(PlaceTask{obs_source_get_weak_source(source), position});
        obs_queue_task(OBS_TASK_UI, PlaceNewItem, task.release(), false);  // PlaceNewItem frees it
    }
}

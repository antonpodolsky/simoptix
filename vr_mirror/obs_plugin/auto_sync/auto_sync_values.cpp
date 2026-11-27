// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "auto_sync_values.h"

#include <obs-module.h>
#include <util/platform.h>

namespace {

    constexpr const char* kFile = "auto_sync.json";

    // OBS's settings for VR Mirror's file, read afresh (empty if there is none). The caller releases them.
    obs_data_t* ReadFile(char** path) {
        *path = obs_module_config_path(kFile);
        obs_data_t* data = *path && os_file_exists(*path) ? obs_data_create_from_json_file_safe(*path, "bak")
                                                          : nullptr;
        return data ? data : obs_data_create();
    }

}

AutoSyncValues LoadAutoSyncValues(const char* frameSource) {
    char* path = nullptr;
    obs_data_t* data = ReadFile(&path);
    obs_data_t* entry = obs_data_get_obj(data, frameSource);
    AutoSyncValues values;

    if (entry) {
        values.gameSound = obs_data_get_string(entry, "game_sound");
        values.hasSound = !values.gameSound.empty();
        values.soundCorrection = obs_data_get_double(entry, "game_sound_correction_ms") / 1000;
        obs_data_release(entry);
    }

    obs_data_release(data);
    bfree(path);
    return values;
}

void SaveAutoSyncValues(const char* frameSource, const AutoSyncValues& values) {
    char* path = nullptr;
    obs_data_t* data = ReadFile(&path);
    obs_data_t* entry = obs_data_create();

    if (values.hasSound) {
        obs_data_set_string(entry, "game_sound", values.gameSound.c_str());
        obs_data_set_double(entry, "game_sound_correction_ms", values.soundCorrection * 1000);
    }

    obs_data_set_obj(data, frameSource, entry);

    if (path) {
        // Not saved (no folder, or the file can't be written): the next session measures again anyway.
        char* folder = obs_module_config_path("");
        (void)os_mkdirs(folder);
        bfree(folder);
        (void)obs_data_save_json_safe(data, path, "tmp", "bak");
    }

    obs_data_release(entry);
    obs_data_release(data);
    bfree(path);
}

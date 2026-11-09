// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Meter image: an image embedded in the plugin (a PNG resource, gmeter.rc), scaled once to the size
// it is drawn at with GDI+'s high-quality bicubic filter and kept as a texture, so it is drawn 1:1
// each frame (no filtering by OBS, no work per frame). Rebuilt only when the size changes.
//
// Knows nothing about what a meter shows or about OBS sources: only GDI+, the module's resources and
// libobs graphics. Graphics thread only.
#pragma once

#include <graphics/graphics.h>

class MeterImage {
public:
    explicit MeterImage(const wchar_t* resource) : resource_(resource) {}
    // Scales the image to size x size pixels if it isn't already (outside drawing; enters graphics).
    void Build(int size);
    // Draws it with its top left at (0, 0), premultiplied over what is there (blending is the caller's).
    void Draw() const;
    void Destroy();  // inside obs_enter_graphics, or on the graphics thread

private:
    const wchar_t* resource_;
    int size_ = 0;         // what the texture was built for; 0: not yet (or it failed)
    bool failed_ = false;  // the resource couldn't be read: logged once, not retried
    gs_texture_t* tex_ = nullptr;
};

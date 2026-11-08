// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The OBS effect a VR Mirror source draws with (MirrorView): OBS's default vertex shader, the
// shared warp math (vr_mirror\core\pipeline\5_warp\warp.hlsli) and a sharp (bicubic) sampling of the
// stored picture.
#pragma once

#include <string>

// The effect's text, for gs_effect_create. Its parameters: the warp's (m0, m1, m2, outTan, srcTan, rect,
// options), `image` (the stored picture) and `texSize` (its width, height, 1/width, 1/height); its
// technique is "Draw".
std::string WarpEffectText();

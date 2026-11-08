# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
r"""The slot check's thumbnails rebuilt from a recording: the plugin's warp inverted (thumbnail
texel -> eye ray -> output pixel, with the pose the game frame was drawn with and the view it was
shown at), sampled like vr_mirror\core\thumbnail (64x32 texels over kThumbArea of the eye
image, each the average of 8x8 samples). Geometry only: which frames, poses and files is up to
the caller (replica.py, export_thumbs.py).
"""

import numpy as np

from trace_common import Conj, Qmul

W, H = 960, 540  # the recording's frames as extracted (ExtractVideo)
L, R, U, D = 1.195, 1.173, 0.984, 0.984  # LMU's left eye on the Dream Air: tangents left, right, up, down
CANT = np.array([0, 0.0175, 0, 0.9998])
CANT /= np.linalg.norm(CANT)
AREA = (0.2, 0.25, 0.8, 0.85)  # kThumbArea (core drawn_pose.h): u0, v0, u1, v1 of the eye image
TWD, THD, SUB = 64, 32, 8  # thumbnail size; each texel averages SUB x SUB samples
MASKS = "0,0,135,12;790,375,960,540"  # OBS overlays not in the game image: the stamp, the G-meter


def Mat(q):
    x, y, z, w = q / np.linalg.norm(q)
    return np.array(
        [
            [1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)],
            [2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)],
            [2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)],
        ]
    )


# The thumbnail's view in the eye camera (tangents, right and up positive) and its texel samples.
vl = L - AREA[0] * (L + R)
vr = -L + AREA[2] * (L + R)
vu = U - AREA[1] * (U + D)
vd = -U + AREA[3] * (U + D)

sx, sy = (vl + vr) / TWD, (vu + vd) / THD
jj, ii = np.meshgrid(np.arange(THD), np.arange(TWD), indexing="ij")
_sub = (np.arange(SUB) + 0.5) / SUB
ex = np.broadcast_to(-vl + (ii[..., None, None] + _sub[None, None, None, :]) * sx, (THD, TWD, SUB, SUB))
ey = np.broadcast_to(vu - (jj[..., None, None] + _sub[None, None, :, None]) * sy, (THD, TWD, SUB, SUB))


def ToLinear(s):
    return np.where(s <= 0.04045, s / 12.92, ((s + 0.055) / 1.055) ** 2.4)


def ToSrgb(l):
    return np.where(l <= 0.0031308, l * 12.92, 1.055 * np.maximum(l, 0) ** (1 / 2.4) - 0.055)


def Masks(text):
    return [tuple(float(v) for v in m.split(",")) for m in text.split(";") if m]


def Thumbnail(frame, I, V, zoom=1.3, masks=Masks(MASKS), linear=False):
    """The thumbnail of the game frame shown in output frame `frame` (RGB, H x W; warped from pose
    I to view V at the source's `zoom`), and which texels the output covers. Texel values 0..1 as
    displayed; linear=True averages the samples in linear light, as the tool's thumbnails do, and
    returns the displayed value of that average (a flat block of it gives the tool that texel)."""

    tw = min(L, R) / zoom
    th = tw / (W / H)  # the output's half width and height, as tangents
    m = Mat(Qmul(Qmul(Conj(CANT), Qmul(Conj(I), V)), CANT))  # output ray -> eye ray
    d = np.stack([ex, ey, -np.ones_like(ex)], -1) @ m  # eye ray -> output ray
    px = (d[..., 0] / -d[..., 2] / tw + 1) / 2 * W - 0.5
    py = (1 - d[..., 1] / -d[..., 2] / th) / 2 * H - 0.5
    ok = (d[..., 2] < -1e-4) & (px >= 0) & (py >= 0) & (px <= W - 1.001) & (py <= H - 1.001)
    for x0, y0, x1, y1 in masks:
        ok &= ~((px >= x0) & (px < x1) & (py >= y0) & (py < y1))
    pxc, pyc = np.clip(px, 0, W - 1.001), np.clip(py, 0, H - 1.001)
    x0, y0 = pxc.astype(int), pyc.astype(int)
    ax, ay = (pxc - x0)[..., None], (pyc - y0)[..., None]
    f = frame.astype(np.float32) / 255
    s = (f[y0, x0] * (1 - ax) + f[y0, x0 + 1] * ax) * (1 - ay) + (
        f[y0 + 1, x0] * (1 - ax) + f[y0 + 1, x0 + 1] * ax
    ) * ay

    if linear:
        return ToSrgb(ToLinear(s).mean(axis=(2, 3))), ok.all(axis=(2, 3))
    return s.mean(axis=(2, 3)), ok.all(axis=(2, 3))

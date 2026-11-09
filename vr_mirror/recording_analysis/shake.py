# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
r"""Shake in a stabilized recording, from grid_shift.exe's patch shifts alone (no frame trace).

The cockpit is fixed in head-tracking space (sim rig: no car motion reaches the head), so in the
output it moves on screen only with the view, plus any error in the pose a frame was warped from.
Per frame this fits one rotation (pitch, yaw, roll) to the patch shifts; the patches that follow it
are the cockpit (and, on frames where the game frame repeated, everything), the rest is scenery,
the wheel and anything else that moves on its own. Shake is the cockpit's step away from a smooth
path: its position against a quadratic through the 7 frames either side, the frame itself left out.

usage: python -I shake.py <grid.f32> <out.npz> [--start s]   (--start: video time of the first frame)
Conventions: angles in degrees of view rotation; screen sizes in pixels of the 1920x1080 picture
the user sees (17.7 px per degree at the centre at zoom 1.25); frame i is video time (i + 1) / 90.
"""

import argparse

import numpy as np

COLS, ROWS, GX, GY, SX, SY, PW, PH = 11, 8, 32, 32, 80, 60, 48, 32  # grid_shift.cpp's grid
W, H = 960, 540
# px per unit tangent at 960 px: the Pimax eye's half-FOV seen from the image's middle, over zoom 1.25
# (vr_mirror\core\pipeline\pipeline_types.h GeometryFor; 1.173, its narrower side, in older builds)
K = (W / 2) / (1.1838 / 1.25)
TO_1080 = 2.0  # 960 px -> 1920 px
DEG = np.pi / 180
PX_PER_DEG = K * DEG * TO_1080  # px of the 1920x1080 picture per degree at the centre (17.7)
HALF = 7  # frames either side for the smooth path


def Grid():
    k = np.arange(COLS * ROWS)
    x0, y0 = GX + (k % COLS) * SX, GY + (k // COLS) * SY
    x, y = (x0 + PW / 2 - W / 2) / K, (y0 + PH / 2 - H / 2) / K  # tangent, +y down

    # Rotational flow (px per radian) for pitch, yaw, roll: u = K (xy p - (1+x^2) q + y r),
    # v = K ((1+y^2) p - xy q - x r). Rows: u of every patch, then v of every patch.
    B = np.zeros((2 * len(k), 3))
    B[: len(k)] = K * np.c_[x * y, -(1 + x * x), y]
    B[len(k) :] = K * np.c_[1 + y * y, -x * y, -x]
    gmeter = (x0 >= 770) & (y0 >= 360)  # the OBS G-meter overlay, fixed on screen
    return B, ~gmeter, x0, y0


def FitRotations(d, valid, B, rng):
    """Per frame: the rotation (radians) that most patches follow, by least median of squares over
    random patch pairs and the previous frame's fit, then least squares on the inliers.
    d: (N, 2P) shifts (u of all patches, then v); valid: (N, P)."""

    n, p = valid.shape
    pairs = rng.choice(p, size=(60, 2))
    pairs = pairs[pairs[:, 0] != pairs[:, 1]]
    best = np.full((n, 3), np.nan)
    best_score = np.full(n, np.inf)

    def Score(w):
        r = d - w @ B.T
        e = np.hypot(r[:, :p], r[:, p:])
        e = np.where(valid, e, np.nan)
        return np.nanmedian(e, axis=1), e

    for a, b in pairs:
        rows = [a, b, a + p, b + p]
        w = d[:, rows] @ np.linalg.pinv(B[rows]).T
        ok = valid[:, a] & valid[:, b]
        s, _ = Score(w)
        s = np.where(ok, s, np.inf)
        take = s < best_score
        best[take], best_score[take] = w[take], s[take]

    for _ in range(2):
        s, e = Score(best)
        inl = valid & (e < np.maximum(0.25, 3 * 1.4826 * s)[:, None])
        for i in range(n):
            m = np.r_[inl[i], inl[i]]
            if m.sum() >= 8:
                best[i] = np.linalg.lstsq(B[m], d[i, m], rcond=None)[0]

    _, e = Score(best)
    return best, e, inl


def SmoothResidual(theta):
    """theta (N, 3) -> its distance from a quadratic through the HALF frames either side (the frame
    left out), and that quadratic's slope (per frame)."""

    t = np.r_[-HALF:0, 1 : HALF + 1].astype(float)
    A = np.c_[np.ones_like(t), t, t * t]
    Pinv = np.linalg.pinv(A)  # (3, 2*HALF)
    n = len(theta)
    idx = np.arange(HALF, n - HALF)[:, None] + np.r_[-HALF:0, 1 : HALF + 1][None]
    coef = np.einsum("ck,nkd->ncd", Pinv, theta[idx])
    res = np.full_like(theta, np.nan)
    vel = np.full_like(theta, np.nan)
    res[HALF : n - HALF] = theta[HALF : n - HALF] - coef[:, 0]
    vel[HALF : n - HALF] = coef[:, 1]
    return res, vel


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("grid")
    ap.add_argument("out")
    ap.add_argument("--start", type=float, default=0.0)
    args = ap.parse_args()
    Run(args.grid, args.out, args.start)


def Run(grid, out, start=0.0):
    B, usable, x0, y0 = Grid()
    p = COLS * ROWS
    raw = np.fromfile(grid, np.float32).reshape(-1, 1 + 4 * p)
    mad = raw[:, 0]
    patch = raw[:, 1:].reshape(-1, p, 4)
    d = np.c_[patch[:, :, 0], patch[:, :, 1]].astype(float)
    var = patch[:, :, 3]
    valid = usable[None] & (var > 25)  # some texture: std above 5 grey levels
    rng = np.random.default_rng(1)
    w = np.zeros((len(d), 3))
    err = np.zeros((len(d), p))
    inl = np.zeros((len(d), p), bool)
    for c in range(0, len(d), 20000):
        w[c : c + 20000], err[c : c + 20000], inl[c : c + 20000] = FitRotations(
            d[c : c + 20000], valid[c : c + 20000], B, rng
        )

    n_in = inl.sum(1)
    in_rms = np.sqrt(np.nanmean(np.where(inl, err, np.nan) ** 2, axis=1))
    theta = np.cumsum(w, axis=0) / DEG  # degrees: pitch, yaw, roll of the cockpit on screen
    res, vel = SmoothResidual(theta)
    np.savez_compressed(
        out,
        start=start,
        mad=mad,
        omega=(w / DEG).astype(np.float32),
        theta=theta,
        res=res,
        vel=vel,
        n_valid=valid.sum(1),
        n_in=n_in,
        in_rms=in_rms,
        err=err.astype(np.float16),
        inl=inl,
        resid=patch[:, :, 2].astype(np.float16),
    )


if __name__ == "__main__":
    main()

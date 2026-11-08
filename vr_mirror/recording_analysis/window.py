# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
r"""A stretch of a recording against the OBS plugin's frame trace: which display slot's pose explains
how the cockpit moved in each frame, against the slot the plugin drew it with. The cockpit is fixed
in head-tracking space, so in the stabilized recording it moves only with the view and with the
error of the pose a frame was warped from; patch_shift measures that motion on cockpit patches.

usage: python -I window.py <OBS log> <recording .mp4> <start mm:ss> <secs> --patches "x,y,w,h;..."
  --patches  cockpit patches in a 960x540 frame of the recording: fixed parts of the car (roof,
             pillars, panels), not the wheel, screens, mirrors, the windscreen or OBS overlays,
             and at least 20 px from the edges. New ones for each car.
  --zoom     the source's zoom setting (default 1.3)

Prints how well each candidate lag (0 = the slot timing gave, -1 = one slot earlier...) explains
the cockpit's frame-to-frame motion (step error, px at 960x540), overall and on the frames the
plugin re-timed, then frame by frame around those. Lower = the video agrees.
"""

import argparse
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from trace_common import (
    HERE,
    P,
    T_IMG,
    T_SERIAL,
    T_SLOT,
    T_TICK,
    T_VIEW,
    F_SERIAL,
    F_SLOT,
    Conj,
    DrawnSlotColumn,
    ExtractVideo,
    FrameLag,
    FirstTick,
    LineUp,
    LoadTrace,
    Predicted,
    Qmul,
    ReadObsLog,
    Recording,
    Rotate,
    Seconds,
    VideoTime,
)


def Screen(T, I, V, point, scale):
    """Where `point` lands on screen per frame if each frame was rendered with pose T: the plugin
    warps from the pose it drew with (I) to the view (V). `scale`: px per unit tangent."""

    r = Rotate(Qmul(Qmul(Conj(V), I), Conj(T)), np.broadcast_to(point, V.shape[:-1] + (3,)))
    return np.stack([r[..., 0] / -r[..., 2] * scale, -r[..., 1] / -r[..., 2] * scale], -1)


def StepError(T, I, V, point, scale, moved):
    e = moved - np.diff(Screen(T, I, V, point, scale), axis=0)
    return np.hypot(e[:, 0], e[:, 1])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("video")
    ap.add_argument("start")
    ap.add_argument("secs", type=float)
    ap.add_argument("--patches", required=True)
    ap.add_argument("--zoom", type=float, default=1.3)
    args = ap.parse_args()
    W, H = 960, 540
    scale = (W / 2) / (1.173 / args.zoom)  # px per unit tangent: the eye's half-FOV over the zoom

    log = ReadObsLog(args.log)
    rec_start, _ = Recording(log, args.video)
    t0 = FirstTick(log["trace"])
    start = Seconds(args.start)
    approx = t0 + rec_start + start - log["trace_start"]  # source time of the window's first frame, roughly
    raw, stamps = ExtractVideo(args.video, start, args.secs, W, H)
    ticks, S, F = LoadTrace(log["trace"], approx - 60, approx + args.secs + 60)
    rows = LineUp(stamps, ticks, approx)
    if rows is None:
        raise SystemExit("the video's frame stamps don't match the trace here (stamp option off?)")

    seg = ticks[rows]
    print(
        f"window {args.start} +{args.secs:g} s: {len(seg)} video frames lined up by stamp; "
        f"video time offset against the log {approx - seg[0, T_TICK]:+.2f} s (episodes.py --offset)"
    )

    exe = os.path.join(HERE, "patch_shift.exe")
    if not os.path.exists(exe):
        raise SystemExit(f"build {exe} first (build.bat)")
    out = subprocess.run(
        [exe, raw, str(W), str(H)] + sum([q.split(",") for q in args.patches.split(";")], []),
        capture_output=True,
        text=True,
    )

    if out.returncode:
        raise SystemExit(out.stderr)
    shift = np.array([[float(x) for x in l.split(",")] for l in out.stdout.strip().splitlines()])
    moved = shift[:, 2:4]  # measured cockpit motion from each frame to the next (px)

    serial = seg[:, T_SERIAL].astype(np.int64)
    I, V = seg[:, T_IMG], seg[:, T_VIEW]
    fidx = {int(s): i for i, s in enumerate(F[:, F_SERIAL])}
    timing_slot = np.array(
        [F[fidx[int(s)], F_SLOT] if int(s) in fidx else d for s, d in zip(serial, seg[:, T_SLOT])]
    )

    drawn_slot = DrawnSlotColumn(log["trace"])
    drawn_lag = np.array([FrameLag(row, t, S, drawn_slot) for row, t in zip(seg, timing_slot)])
    point = Rotate(V[:1], np.array([[0.0, -0.1, -1.0]]))[0]  # a cockpit point just below the view's centre

    late = drawn_lag[1:] != 0
    names, errors = [], []
    for lag in (0, -1, -2, -3):
        T, ok = Predicted(S, timing_slot + lag * P)
        names.append(f"lag {lag:+d} for every frame")
        errors.append(StepError(np.where(ok[:, None], T, I), I, V, point, scale, moved))

    names.append("what the plugin drew")
    errors.append(StepError(I, I, V, point, scale, moved))
    new = np.r_[True, serial[1:] != serial[:-1]]
    print(
        "plugin's lags (game frames):",
        dict(zip(*[x.tolist() for x in np.unique(drawn_lag[new], return_counts=True)])),
    )

    for n, e in zip(names, errors):
        on_late = f", on frames it re-timed {np.sqrt((e[late] ** 2).mean()):.3f}" if late.any() else ""
        print(f"  {n:24s} step error rms {np.sqrt((e**2).mean()):.3f}{on_late}, worst {e.max():.2f} px")

    near = np.where(late | np.r_[late[1:], False] | np.r_[False, late[:-1]])[0]
    if len(near):
        print("  per frame around the re-timed ones: step error with lag 0/-1/-2/-3, then as drawn")
        for k in near:
            print(
                f"    video {VideoTime(start + (k + 1) / 90)} serial {serial[k + 1]} drawn lag "
                f"{drawn_lag[k + 1]:+d}: "
                + " ".join(f"{e[k]:.2f}" for e in errors[:4])
                + f" | {errors[4][k]:.2f}"
            )


if __name__ == "__main__":
    main()

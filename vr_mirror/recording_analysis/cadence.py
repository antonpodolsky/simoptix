# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
r"""Cadence of a recording, frame by frame, from its frame stamps and the frame trace.

Per video frame: which OBS tick it is (stamp), which game frame that tick showed (serial), and the
steps from the video frame before: ticks (OBS showed a tick twice or dropped one), game frames
(same again, next, skipped), the shown frame's display slot (the moment the game drew it for: the
scenery's own clock) and the view's time (the moment the view is for). The scenery moves smoothly
only if the shown frame's slot steps by one display period with every video frame.

usage: python -I cadence.py <out folder of analyse_recording.py>
(analyse_recording.py runs it; it reads that folder's lineup.npz.) Times in ms, steps in display
periods. Traces with 18 columns, the oldest, have no view time: those lines are left out.
"""

import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from trace_common import VideoTime

P = 1 / 90


def Hist(x, name):
    u, c = np.unique(x, return_counts=True)
    print(f"  {name}: " + ", ".join(f"{a:+d}: {b}" for a, b in zip(u.tolist(), c.tolist())))


def Run(cad, video_name):
    """cad: lineup.npz's arrays (rows, serial, slot, view_time per video frame)."""
    rows, serial, slot, view_time = cad["rows"], cad["serial"], cad["slot"], cad["view_time"]
    ok = rows >= 0
    n = len(rows)
    has_view = bool(np.isfinite(view_time[ok]).all())
    print(f"{video_name}: {n} frames, {ok.sum()} lined up")
    both = ok[1:] & ok[:-1]
    Hist(np.clip(np.diff(rows)[both], -2, 4), "OBS ticks per video frame (1 = every tick once)")
    Hist(
        np.clip(np.diff(serial)[both], -2, 4), "game frames per video frame (0 = same again, 2 = one skipped)"
    )

    # The scenery's own clock: the shown frame's slot step, in display periods, against the view's.
    d_slot = np.diff(slot)[both] / P
    d_view = np.diff(view_time)[both] / P if has_view else np.ones_like(d_slot)
    off = d_slot - d_view  # how far the scenery's step differs from the view's (periods)
    print(
        f"  slot step (periods): p0.1 {np.percentile(d_slot, 0.1):.3f} p1 {np.percentile(d_slot, 1):.3f} "
        f"p50 {np.median(d_slot):.3f} p99 {np.percentile(d_slot, 99):.3f} p99.9 "
        f"{np.percentile(d_slot, 99.9):.3f}"
    )

    if has_view:
        print(
            f"  view-time step (periods): p0.1 {np.percentile(d_view, 0.1):.3f} p1 "
            f"{np.percentile(d_view, 1):.3f} "
            f"p50 {np.median(d_view):.3f} p99 {np.percentile(d_view, 99):.3f} p99.9 "
            f"{np.percentile(d_view, 99.9):.3f}"
        )
    else:
        print("  no view time in this trace: the scenery's steps are compared with one period")

    for lim in (0.25, 0.5, 0.75):
        m = np.abs(off) > lim
        print(
            f"  scenery step off the view's step by over {lim} period: {m.sum()} "
            f"({m.sum() / (n / 90 / 60):.1f} per min)"
        )

    if has_view:
        # Picture age at the view's moment: the view is for view_time, the picture for its slot.
        age = (view_time - slot)[ok] * 1e3
        print(
            f"  view time - shown frame's slot (ms): p1 {np.percentile(age, 1):.1f} p50 "
            f"{np.median(age):.1f} "
            f"p99 {np.percentile(age, 99):.1f}; range {age.min():.1f} to {age.max():.1f}"
        )

        # Over the recording: how that age drifts (the beat between OBS's clock and the headset's).
        tt = np.where(ok)[0] / 90
        for a, b in ((0, 60), (60, 120), (300, 360), (600, 660), (900, 960)):
            m = (tt >= a) & (tt < b)
            if m.any():
                print(
                    f"    {VideoTime(a)}-{VideoTime(b)}: age p50 {np.median(age[m]):.1f} ms, "
                    f"p1 {np.percentile(age[m], 1):.1f}, p99 {np.percentile(age[m], 99):.1f}"
                )

    bad = np.where(both)[0][np.abs(off) > 0.5]
    first = [b for i, b in enumerate(bad) if i == 0 or b - bad[i - 1] > 3]
    print(f"  {len(first)} scenery stutters (step off by > 0.5 period), first ones:")
    for b in first[:30]:
        i = b + 1
        seg = slice(max(0, i - 3), i + 4)
        print(
            f"    {VideoTime(i / 90)}: slot steps {np.round(np.diff(slot[seg]) / P, 2).tolist()}, "
            + (f"view steps {np.round(np.diff(view_time[seg]) / P, 2).tolist()}, " if has_view else "")
            + f"serial steps {np.diff(serial[seg]).tolist()}, tick steps {np.diff(rows[seg]).tolist()}"
        )

    # 10 s windows: stutters per window.
    c = np.bincount((bad + 1) // 900, minlength=n // 900 + 1)
    print(
        f"  stutters per 10 s window: median {np.median(c):.0f}, max {c.max()}, windows with none "
        f"{np.sum(c == 0)} of {len(c)}"
    )


def main():
    import json

    folder = sys.argv[1]
    video = json.load(open(os.path.join(folder, "run.json")))["video"]
    Run(np.load(os.path.join(folder, "lineup.npz")), os.path.basename(video))


if __name__ == "__main__":
    main()

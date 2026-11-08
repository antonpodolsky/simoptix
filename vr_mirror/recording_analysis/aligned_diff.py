# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
r"""The scenery's change between video frames once the cockpit is lined up: each frame is shifted onto the
one before by the shift (a fraction of a pixel, bilinear) that best lines up the cockpit's roof and
pillars, and the road ahead is then compared. The cockpit is fixed in head-tracking space (a sim
rig), so what is left on the road is the car's own motion through the world: about 0 on a frozen
game frame (4 Nov 10:41.73: 1.05 against about 6 around it), about double on a skipped one.

usage: python -I aligned_diff.py <recording .mp4> <mm:ss.ss> <seconds> [--stamps first-last]
       [--cockpit "x0,y0,x1,y1;..."] [--road "x0,y0,x1,y1"]
The clip is decoded at the recording's full size (cached as trace_common.ExtractVideo does). Regions
are in pixels of the recording; the defaults are the McLaren's roof lettering and left door and the
road ahead at 2560x1440: pick new ones for another car (cockpit only, also in corners: not the wheel,
screens, mirrors or OBS overlays). Shifts in px of the recording; change in grey levels (mean
absolute difference).
"""

import argparse
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from trace_common import FFMPEG, ExtractVideo, Seconds

COCKPIT = "1750,110,2450,240;30,560,250,880"
ROAD = "900,760,1800,880"
R = 10  # px searched either way


def Shift(img, dx, dy):
    """img moved by (dx, dy), bilinear (np.roll at the integer part: edges wrap, regions stay clear of them)."""
    ix, iy = int(np.floor(dx)), int(np.floor(dy))
    fx, fy = dx - ix, dy - iy
    a = np.roll(np.roll(img, iy, 0), ix, 1)
    b = np.roll(a, 1, 1)
    c = np.roll(a, 1, 0)
    d = np.roll(b, 1, 0)
    return (1 - fx) * (1 - fy) * a + fx * (1 - fy) * b + (1 - fx) * fy * c + fx * fy * d


def Cost(prev, cur, dx, dy, regions):
    s = Shift(cur, dx, dy)
    return sum(np.abs(s[y0:y1, x0:x1] - prev[y0:y1, x0:x1]).mean() for x0, y0, x1, y1 in regions)


def Size(video):
    probe = os.path.join(os.path.dirname(FFMPEG), "ffprobe.exe")
    out = subprocess.run(
        [
            probe,
            "-v",
            "error",
            "-select_streams",
            "v:0",
            "-show_entries",
            "stream=width,height",
            "-of",
            "csv=p=0",
            video,
        ],
        capture_output=True,
        text=True,
        check=True,
    ).stdout

    w, h = out.strip().split(",")[:2]
    return int(w), int(h)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("video")
    ap.add_argument("at")
    ap.add_argument("secs", type=float)
    ap.add_argument("--stamps", help="first-last: only these frames")
    ap.add_argument("--cockpit", default=COCKPIT)
    ap.add_argument("--road", default=ROAD)
    args = ap.parse_args()
    cockpit = [tuple(int(v) for v in r.split(",")) for r in args.cockpit.split(";")]
    x0, y0, x1, y1 = (int(v) for v in args.road.split(","))
    w, h = Size(args.video)
    path, stamps = ExtractVideo(args.video, Seconds(args.at), args.secs, w, h)
    lo, hi = (int(v) for v in args.stamps.split("-")) if args.stamps else (0, 1 << 15)
    clip = np.memmap(path, np.uint8, "r").reshape(-1, h, w)
    print("stamp  cockpit shift (dx, dy) px  cockpit left  road unaligned  road aligned")
    for i in range(1, len(clip)):
        if not lo <= stamps[i] <= hi:
            continue

        prev, cur = clip[i - 1].astype(np.float32), clip[i].astype(np.float32)
        best = (np.inf, 0, 0)
        for dy in range(-R, R + 1, 2):  # coarse, then fine
            for dx in range(-R, R + 1, 2):
                best = min(best, (Cost(prev, cur, dx, dy, cockpit), dx, dy))

        for step in (1, 0.5, 0.25):
            _, bx, by = best
            for dy in (by - step, by, by + step):
                for dx in (bx - step, bx, bx + step):
                    best = min(best, (Cost(prev, cur, dx, dy, cockpit), dx, dy))

        c, dx, dy = best
        raw = np.abs(cur[y0:y1, x0:x1] - prev[y0:y1, x0:x1]).mean()
        al = np.abs(Shift(cur, dx, dy)[y0:y1, x0:x1] - prev[y0:y1, x0:x1]).mean()
        print(
            f"{stamps[i]:5d}  ({dx:+6.2f}, {dy:+6.2f})        {c / len(cockpit):6.2f}        {raw:6.2f}    "
            f"    {al:6.2f}"
        )


if __name__ == "__main__":
    main()

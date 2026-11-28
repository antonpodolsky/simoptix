# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
"""A cut of a recording of LMU's menus for the auto sync test (vr_mirror\\e2e\\auto_sync_e2e\\): every
game frame of a stretch, greyscale, with the camera it was drawn from and its time on the frame trace's
clock, and the recording's sound put back where OBS captured it, as auto sync's audio tap hears it.

The recorded view is an image drawn from a known camera: the view in the frame trace, turned by the eye's
cant and held level. So each video frame goes to the replay as a frame of its own, with that camera as its
pose, and the menu panel stays put on auto sync's grid as in the game's own frames. The camera's field of
view is fitted: the width (as the tangent of half the view's angle) at which the panel moves least
between frames once each is turned back by its camera. The trace doesn't hold the eye's field of view, and
the output's follows from it and the zoom. The fit needs the head to turn: where it barely does, --tan-w
gives the width, as fitted on another recording with the same headset and zoom.

The recorded sound was moved by its sync offset: the view's delay that VR Mirror added (the OBS log's
"audio sync: ... now +<ms>" line) and the source's user sync offset (--user-offset, ms). The cut's sound is put
back where OBS captured it, on the trace's clock.

  python -I export_menu_cut.py "<recording>.mp4" <out folder> [--from 0] [--seconds 40]
      [--user-offset -120] [--gmeter 3537,1872,133] [--width 384] [--cant 0] [--tan-w 0.825]

It writes menu_cut.csv (what each file holds, line by line), menu.gray (the frames, one after another),
menu.csv (each frame's time and camera) and sound.f32 (mono, 48 kHz) into the out folder. It needs the
recording's OBS log and its frame trace, with the stamp on (the Debug setting). The cut shows the game's
menus, with cars and tracks: keep it in the data folder, outside the repo.
"""

import argparse
import math
import os
import re
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sources  # noqa: E402
import sync_check  # noqa: E402
from trace_common import (  # noqa: E402
    T_SERIAL,
    T_SMOOTH,
    T_TICK,
    T_VIEW,
    ExtractVideo,
    FirstTick,
    LineUp,
    LoadTrace,
    Qmul,
    ReadObsLog,
)

CHUNK = 4.0  # s of video lined up with the trace at a time: a stamp that doesn't match loses only that


def axis_angle(axis, degrees):
    half = math.radians(degrees) / 2
    return np.array(
        [axis[0] * math.sin(half), axis[1] * math.sin(half), axis[2] * math.sin(half), math.cos(half)]
    )


def level(q):
    """The same view direction, with world +Y up (rot.h's LookRotation of the view's forward)."""
    x, y, z, w = q
    forward = np.array([-(2 * (x * z + w * y)), -(2 * (y * z - w * x)), -(1 - 2 * (x * x + y * y))])
    forward /= np.linalg.norm(forward)
    yaw = math.atan2(-forward[0], -forward[2])
    pitch = math.asin(max(-1.0, min(1.0, forward[1])))
    return Qmul(axis_angle((0, 1, 0), math.degrees(yaw)), axis_angle((1, 0, 0), math.degrees(pitch)))


def matrix(q):
    x, y, z, w = q
    return np.array(
        [
            [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
        ]
    )


def turned_back(frames, cams, ref, tan_w):
    """Each frame as seen from camera `ref` (nearest texel), and where it is seen."""
    count, height, width = frames.shape
    f = (width / 2) / tan_w
    u, v = np.meshgrid(np.arange(width) + 0.5, np.arange(height) + 0.5)
    rays = np.stack([(u - width / 2) / f, -(v - height / 2) / f, -np.ones_like(u)], axis=-1).reshape(-1, 3)
    world = rays @ matrix(ref).T
    out = np.zeros(frames.shape, np.uint8)
    seen = np.zeros(frames.shape, bool)
    for k in range(count):
        cam = world @ matrix(cams[k])
        z = -cam[:, 2]
        ok = z > 0.1
        x = np.round(width / 2 + f * cam[:, 0] / np.maximum(z, 0.1) - 0.5).astype(int)
        y = np.round(height / 2 - f * cam[:, 1] / np.maximum(z, 0.1) - 0.5).astype(int)
        ok &= (x >= 0) & (x < width) & (y >= 0) & (y < height)
        img = np.zeros(width * height, np.uint8)
        img[ok] = frames[k][y[ok], x[ok]]
        out[k], seen[k] = img.reshape(height, width), ok.reshape(height, width)
    return out, seen


def turn_deg(a, b):
    return math.degrees(2 * math.acos(min(1.0, abs(float(np.dot(a, b))))))


def fit_width(frames, cams):
    """The tangent of half the view's width at which the scene moves least, frames 9 apart whose cameras
    turned at least 1.5 degrees; None if fewer than 20 did."""
    ref = cams[len(cams) // 2]
    sample = np.array([k for k in range(0, len(frames) - 9, 3) if turn_deg(cams[k], cams[k + 9]) >= 1.5])
    if len(sample) < 20:
        return None
    best = None
    for tan_w in np.arange(0.55, 1.15, 0.0125):
        a, sa = turned_back(frames[sample], cams[sample], ref, tan_w)
        b, sb = turned_back(frames[sample + 9], cams[sample + 9], ref, tan_w)
        both = sa & sb & (a > 8) & (b > 8)
        score = np.abs(a.astype(np.int16) - b.astype(np.int16))[both].mean()
        if best is None or score < best[1]:
            best = (tan_w, score)
    return best[0]


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("video")
    parser.add_argument("out")
    parser.add_argument("--from", dest="start", type=float, default=0.0)
    parser.add_argument("--seconds", type=float, default=None)
    parser.add_argument("--user-offset", type=float, default=-120)
    parser.add_argument("--gmeter", type=lambda s: [float(v) for v in s.split(",")], default=None)
    parser.add_argument("--width", type=int, default=384)
    parser.add_argument("--cant", type=float, default=0.0)
    parser.add_argument("--tan-w", type=float, default=None)
    args = parser.parse_args()

    run = sources.Find(args.video)
    log = ReadObsLog(run["log"])
    prefix = run["trace"] or log["trace"]
    if not prefix:
        raise SystemExit("no frame trace in the recording's OBS log")
    first_tick = FirstTick(prefix)

    def approx_tick(video_time):
        return first_tick + run["start"] + video_time - log["trace_start"]

    video_w, video_h = sync_check.video_size(args.video)
    width = args.width
    height = int(round(width * video_h / video_w / 2)) * 2
    stop = (
        args.start + args.seconds
        if args.seconds
        else (run["stop"] - run["start"] if run["stop"] else args.start + 60) - 0.5
    )
    ticks, _, _ = LoadTrace(prefix, approx_tick(args.start) - 30, approx_tick(stop) + 30)

    masks = [(0, 0, 256 / video_w, 16 / video_h)]  # the stamp
    if args.gmeter:
        gx, gy, gr = args.gmeter
        masks.append(((gx - 1.8 * gr) / video_w, (gy - 1.8 * gr) / video_h, 1, 1))

    frames, cams, times, tick_of_video, seen = [], [], [], [], set()
    t = args.start
    while t < stop - 0.25:
        secs = min(CHUNK, stop - t)
        raw, stamps = ExtractVideo(args.video, t, secs, width, height)
        chunk = np.fromfile(raw, np.uint8).reshape(-1, height, width)
        rows = LineUp(stamps, ticks, approx_tick(t))
        if rows is None:
            print(f"{t:.1f}-{t + secs:.1f} s: the stamps don't match the trace, left out")
        else:
            for x0, y0, x1, y1 in masks:
                chunk[:, int(y0 * height) : int(y1 * height) + 1, int(x0 * width) : int(x1 * width) + 1] = 0
            for k, row in enumerate(rows):
                tick_of_video.append((t + k / 90, ticks[row, T_TICK]))
                serial = ticks[row, T_SERIAL]
                if serial not in seen:
                    seen.add(serial)
                    frames.append(chunk[k])
                    view = ticks[row, T_VIEW]
                    cams.append(level(Qmul(view, axis_angle((0, 1, 0), args.cant))))
                    times.append(ticks[row, T_SMOOTH])
        t += secs
    if not frames:
        raise SystemExit("no frame lined up with the trace")

    frames, cams = np.array(frames), np.array(cams)
    tan_w = args.tan_w or fit_width(frames, cams)
    if tan_w is None:
        raise SystemExit("the head turned too little to fit the view's width: give it with --tan-w")
    tan_h = tan_w * height / width
    print(f"{len(frames)} frames; the view's width: tangent {tan_w:.4f} of its half")

    os.makedirs(args.out, exist_ok=True)
    frames.tofile(os.path.join(args.out, "menu.gray"))
    with open(os.path.join(args.out, "menu.csv"), "w", encoding="utf-8") as f:
        for time, q in zip(times, cams):
            f.write(f"{time:.6f},{q[0]:.7f},{q[1]:.7f},{q[2]:.7f},{q[3]:.7f}\n")

    # The sound, on the trace's clock: the video's time maps to the ticks' (OBS's output clock), less the
    # sync offset it was played out with.
    pairs = np.array(tick_of_video)
    slope, offset = np.polyfit(pairs[:, 0], pairs[:, 1], 1)
    shifts = [
        float(re.search(r"now \+?(-?\d+) ms", text)[1])
        for when, text in log["lines"]
        if "audio sync:" in text and when <= run["start"] + args.start
    ]
    if not shifts:
        raise SystemExit("no 'audio sync:' line in the OBS log before the cut")
    sync_offset = (shifts[-1] + args.user_offset) / 1000
    print(f"the sound was played out {sync_offset * 1000:+.0f} ms from where OBS captured it")
    sound = sync_check.sound(args.video, args.start, stop - args.start)
    sound.astype(np.float32).tofile(os.path.join(args.out, "sound.f32"))

    lines = [
        f"layout,{width},{height},{tan_w:.6f},{tan_w:.6f},{tan_h:.6f},{tan_h:.6f}",
        f"frames,{len(frames)}",
        f"sound,{slope * args.start + offset - sync_offset:.6f},{sync_check.RATE},{len(sound)}",
    ]
    with open(os.path.join(args.out, "menu_cut.csv"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print(f"written to {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
r"""Which frames of an e2e replay run came out from the wrong pose, and which lag the tool used
instead: compares the tool's trace (tool_frames.csv: the pose it warped each frame from) with the
fake runtime's log (fake.log: the pose each frame was rendered with) and the replay's true lags.

usage: python -I e2e_frames.py <vr_mirror\e2e\out\replay_name folder> [--all]
  --all  list every frame the tool re-timed, not only the wrong ones
"""

import os
import sys

import numpy as np


def Angle(a, b):
    """Degrees between two orientations: 2 atan2(|v|, |w|) of the relative rotation."""
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    x = aw * bx - ax * bw - ay * bz + az * by
    y = aw * by + ax * bz - ay * bw - az * bx
    z = aw * bz - ax * by + ay * bx - az * bw
    w = aw * bw + ax * bx + ay * by + az * bz
    return np.degrees(2 * np.arctan2(np.sqrt(x * x + y * y + z * z), abs(w)))


P = 1 / 90


def main():
    d = sys.argv[1]
    show_all = "--all" in sys.argv

    frames = []  # fake.log: index, begin, target slot, arrival, q
    for line in open(os.path.join(d, "fake.log")):
        if line.startswith("frame,"):
            frames.append([float(v) for v in line.split(",")[1:]])

    frames = np.array(frames)
    replay = next(l for l in open(os.path.join(d, "scenario.cfg")) if l.startswith("replay=")).strip()[7:]
    lags = {}
    for line in open(replay):
        if line.startswith("F,"):
            v = line.split(",")
            lags[int(v[1])] = int(v[4])

    tool = np.genfromtxt(
        os.path.join(d, "tool_frames.csv"),
        delimiter=",",
        names=True,
        comments="#",
        dtype=None,
        encoding="utf-8",
    )

    wrong = total = 0
    for r in tool:
        k = np.searchsorted(frames[:, 3], r["arrival"] - 0.003)
        if k >= len(frames) or frames[k, 3] > r["arrival"] + 0.003:
            continue

        # The pose the picture was warped from: img in the observer's trace, else raw (older builds).
        p = "img" if "img_x" in tool.dtype.names else "raw"
        raw = (r[p + "_x"], r[p + "_y"], r[p + "_z"], r[p + "_w"])
        off = Angle(raw, frames[k, 4:8])
        true = lags.get(int(frames[k, 0]), 0)
        used = (
            true
            if off <= 0.02
            else next(
                (
                    true + int(round((frames[j, 2] - frames[k, 2]) / P))
                    for j in range(max(0, k - 6), min(len(frames), k + 6))
                    if j != k and Angle(raw, frames[j, 4:8]) < 0.02
                ),
                None,
            )
        )

        total += 1
        if off > 0.02 or (show_all and used not in (0, None)):
            wrong += off > 0.02
            print(
                f"  t {r['arrival'] - frames[0, 3]:7.3f} s  serial {int(frames[k, 0])}  true lag {true:+d} "
                f" "
                f"used "
                f"{'?' if used is None else f'{used:+d}'}  off {off:.3f} deg"
            )

    print(f"{total} frames, {wrong} from the wrong pose")


if __name__ == "__main__":
    main()

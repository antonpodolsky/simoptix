# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
r"""Timing events of a recording, from the frame trace lined up with it (lineup.npz) and the cockpit's
steps (shake_causes.py's _frames.npz):
- same slot twice: two consecutive game frames given the same display slot (the 7:39 kind of race F:
  one of them is lined up from a neighbouring slot's head pose, unless the game really drew both for it);
- slot gap: the next game frame given a slot two or more periods on (the game drew nothing for a slot);
- repeat: the tick showed the same game frame again (the scenery holds for a tick);
- skip: a game frame never shown.
Each with the cockpit's largest step within a frame of it.

usage: python -I events.py <out folder of analyse_recording.py>
(analyse_recording.py runs it.) Steps in px of the 1920x1080 video.
"""

import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from trace_common import VideoTime

P = 1 / 90
SAME_SLOT = "same slot twice (new game frame, slot step 0)"
SLOT_GAP = "slot gap (new game frame, slot step >= 2)"
REPEAT = "repeat (same game frame again)"
SKIP = "skip (a game frame never shown)"


def Run(frames, cad):
    """frames: shake_causes.py's _frames.npz (step, ok per shake row); cad: lineup.npz. Prints the
    table and every same-slot-twice event; returns {event: (count, with a step >= 2 px)} and the video
    times of the same-slot-twice events."""

    step, ok = frames["step"], frames["ok"]
    n = len(step)
    vf = np.arange(1, n + 1)  # video frame of each shake row
    ser, slot, rows = cad["serial"], cad["slot"], cad["rows"]
    lined = (rows[vf] >= 0) & (rows[vf - 1] >= 0)
    dser = ser[vf] - ser[vf - 1]
    dslot = np.round((slot[vf] - slot[vf - 1]) / P)
    minutes = n / 90 / 60
    kinds = {
        SAME_SLOT: lined & (dser == 1) & (dslot == 0),
        SLOT_GAP: lined & (dser == 1) & (dslot >= 2),
        REPEAT: lined & (dser == 0),
        SKIP: lined & (dser >= 2),
    }

    near = np.maximum.reduce([np.roll(np.where(ok, step, 0), s) for s in (-1, 0, 1)])
    print(f"{n} frames ({minutes:.1f} min), {lined.sum()} lined up with the trace")
    print(f"  {'event':48s} {'count':>6s} {'per min':>8s} {'cockpit step >= 2 px':>21s} {'median step':>12s}")
    counts = {}
    for name, m in kinds.items():
        counts[name] = (int(m.sum()), int(np.sum(m & (near >= 2))))
        print(
            f"  {name:48s} {m.sum():6d} {m.sum() / minutes:8.1f} {np.sum(m & (near >= 2)):21d} "
            f"{np.median(near[m]) if m.any() else 0:12.2f}"
        )

    print(f"  all frames: median step {np.median(step[ok]):.2f} px")
    same = np.where(kinds[SAME_SLOT])[0]
    print(f"  {SAME_SLOT}, each:")
    for i in same:
        print(f"    {VideoTime((i + 1) / 90)}  cockpit step {near[i]:.2f} px")

    return counts, [((i + 1) / 90, float(near[i])) for i in same]


def main():
    folder = sys.argv[1]
    Run(np.load(os.path.join(folder, "shake_frames.npz")), np.load(os.path.join(folder, "lineup.npz")))


if __name__ == "__main__":
    main()

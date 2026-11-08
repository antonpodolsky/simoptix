# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
r"""The frames shake_causes.py's detector took for frozen scenery (a repeated game frame: everything
moves with the cockpit), against the frame trace: did the tick really show the same game frame again?
In a clean race most of its finds are new game frames read near its cut-off (shake_causes.py).

usage: python -I frozen_check.py <out folder of analyse_recording.py>
"""

import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from trace_common import VideoTime


def main():
    folder = sys.argv[1]
    f, c = np.load(os.path.join(folder, "shake_frames.npz")), np.load(os.path.join(folder, "lineup.npz"))
    rep = f["repeat"] & f["ok"]
    vf = np.where(rep)[0] + 1  # shake rows: row k is video frame k + 1
    ser = c["serial"]
    ds = ser[vf] - ser[vf - 1]
    print(f"flagged {len(vf)}")
    u, n = np.unique(ds, return_counts=True)
    print("trace's game-frame step on the flagged frames:", dict(zip(u.tolist(), n.tolist())))
    print(
        f"view speed on the flagged frames: median {np.nanmedian(f['speed'][vf - 1]):.3f} deg/frame; "
        f"all frames {np.nanmedian(f['speed']):.3f}"
    )

    groups = []
    for x in vf:
        if groups and x - groups[-1][-1] <= 90:
            groups[-1].append(x)
        else:
            groups.append([x])

    for g in groups:
        g = np.array(g)
        print(
            f"  {VideoTime((g[0] + 1) / 90)}: {len(g)} frames, game-frame steps "
            f"{sorted(set((ser[g] - ser[g - 1]).tolist()))}, spread p50 "
            f"{np.median(f['spread'][g - 1]):.2f}"
        )


if __name__ == "__main__":
    main()

# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
r"""One moment of a recording, frame by frame: the trace rows each video frame showed, the frame
source's own rows for those game frames, and the cockpit's measured step.

usage: python -I moment.py <out folder of analyse_recording.py> <mm:ss.ss> [frames either side, 8]
Times in ms relative to the first row shown. Columns: the tick, its game frame (serial), the frame's
arrival and smoothed arrival, the slot it was given, the view's time against that slot, the slot
lead, the cockpit's step (px at 1080p), then the frame source's row, and the drawn pose stage's notes.
The frame source's row is the runtime's F row with the Pimax source (display slot, current slot,
offset), and the layer's F row with the OpenXR source (layer frame, when the game ended it, the slot it
was drawn for, images held, images released during the end of frame).
"""

import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace_ticks
from shake import PX_PER_DEG


def SourceRowText(columns, values, t0):
    """The frame source's row: times in ms relative to t0, spans in ms, numbers as they are."""
    texts = []
    for (_, kind), value in zip(columns, values):
        if kind == "time":
            texts.append(f"{(value - t0) * 1e3:8.2f}")
        elif kind == "span":
            texts.append(f"{value * 1e3:6.2f}")
        else:
            texts.append(f"{value:6.0f}")

    return " ".join(texts)


def main():
    folder, at = sys.argv[1:3]
    half = int(sys.argv[3]) if len(sys.argv) > 3 else 8
    run = json.load(open(os.path.join(folder, "run.json")))
    m, s = at.split(":")
    centre = int(round((int(m) * 60 + float(s)) * 90)) - 1  # video frame i is time (i + 1) / 90
    rows = np.load(os.path.join(folder, "lineup.npz"))["rows"]
    trace = trace_ticks.Load(run["trace"])
    num, notes = trace["num"], trace["notes"]
    res = np.load(os.path.join(folder, "shake.npz"))["res"]
    step = np.r_[np.nan, np.hypot(np.diff(res[:, 0]), np.diff(res[:, 1])) * PX_PER_DEG]
    frames = range(max(centre - half, 0), min(centre + half + 1, len(rows)))
    columns, source_rows = trace_ticks.Arrivals(
        run["trace"], [num[rows[i], 2] for i in frames if rows[i] >= 0]
    )
    t0 = num[rows[frames[0]], 1]
    print(
        "frame  time      tick   serial  arrival  smooth   slot  slot-smooth view-slot  lead  cockpit-step "
        " "
        f"F: {' '.join(name for name, _ in columns)} | notes"
    )

    for i in frames:
        r = rows[i]
        if r < 0:
            print(f"{i:6d} {(i + 1) / 90:8.3f}  (stamp not in the trace)")
            continue

        tick, serial, arr, smo, slot = num[r, 1], int(num[r, 2]), num[r, 3], num[r, 4], num[r, 5]
        source_row = source_rows.get(serial)
        fs = SourceRowText(columns, source_row, t0) if source_row else "(no row)"

        k = i - 1  # shake rows: row k is video frame k + 1
        st = step[k] if 0 <= k < len(step) else np.nan
        print(
            f"{i:6d} {(i + 1) / 90:8.3f} {(tick - t0) * 1e3:7.2f} {serial:7d} {(arr - t0) * 1e3:8.2f} "
            f"{(smo - t0) * 1e3:8.2f} {(slot - t0) * 1e3:7.2f} {(slot - smo) * 1e3:7.2f} "
            f"{(num[r, 24] - slot) * 1e3:8.2f} {num[r, 22] * 1e3:6.2f} {st:8.2f}     {fs} | {notes[r]}"
        )


if __name__ == "__main__":
    main()

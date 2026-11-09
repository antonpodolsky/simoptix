# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
r"""A whole frame trace in memory: every tick row in one numeric layout, whatever the trace's format,
and the runtime's last pose prediction for each display slot. Cached (reading a trace of an hour or
two takes minutes). Knows nothing about recordings or video.

Tick formats (`_ticks.csv`, one row per OBS tick; vr_mirror\core\pipeline\observer\README.md):
- 18 columns, the oldest: stamp ... view_w; `display_slot` is the slot the frame was drawn for.
- 26 columns: then picture_pose, draw, presented, zoom, edge_clamps, slot_lead,
  edge_planned, view_time; `slot` is the slot the frame arrived for.
- 31 columns, for a short time: then followed_serial, edge_repeat_clamps, edge_shift, aspect, picture_notes.
- 30 columns, the newest: as 31 without edge_repeat_clamps. In newer traces two of them are named
  drawn_pose and pose_notes (picture_pose and picture_notes in older ones), at the same places.

The numeric layout (`num`), column by column:
  0 stamp, 1 tick, 2 serial, 3 arrival, 4 smooth, 5 slot, 6-9 img, 10-13 raw, 14-17 view (x, y, z, w),
  18 draw, 19 presented, 20 zoom, 21 edge_clamps, 22 slot_lead, 23 edge_planned, 24 view_time,
  25 followed_serial, 26 edge_repeat_clamps, 27 edge_shift, 28 aspect
NaN where the trace's format has no such column (18-26 in the 18-column format), -1 for 25-28 where it has
none since. `kind` is the drawn_pose column, `notes` the drawn pose stage's notes (text). Times in PVR
seconds.
"""

import os

import numpy as np

from trace_common import CACHE, SourceTrace

N_COLUMNS = 29


def Row(v):
    """One tick row's fields -> (numbers, drawn_pose, notes) in the layout above."""
    n = len(v)
    if n < 26:  # the 18-column format
        return [float(x) for x in v[:18]] + [np.nan] * 7 + [-1.0] * 4, "", ""

    nums = [float(x) for x in v[:18]] + [float(x) for x in v[19:26]]
    if n >= 31:  # with edge_repeat_clamps
        extra, note = [float(x) for x in v[26:30]], v[30]
    elif n == 30:  # edge_repeat_clamps dropped
        extra, note = [float(v[26]), -1.0, float(v[27]), float(v[28])], v[29]
    else:
        extra, note = [-1.0] * 4, ""

    return nums + extra, v[18], note


def Load(prefix):
    """{'num', 'kind', 'notes', 'S', 'columns'} of the trace at `prefix` (without '_pimax.csv'). S rows:
    time asked, slot, q (x, y, z, w), the last prediction for each slot, sorted by slot. 'columns': the
    trace's own column count (18, 26, 30 or 31)."""

    size = os.path.getsize(prefix + "_ticks.csv")
    os.makedirs(CACHE, exist_ok=True)
    cache = os.path.join(CACHE, f"{os.path.basename(prefix)}_whole_{size}.npz")
    if os.path.exists(cache):
        d = np.load(cache)
        return {k: d[k] for k in d.files}

    num, kind, notes, columns = [], [], [], 0
    with open(prefix + "_ticks.csv") as f:
        for line in f:
            if not line[0].isdigit():
                continue
            v = line.rstrip("\r\n").split(",")
            if len(v) < 18:
                continue  # a row cut short where the trace was closed
            columns = max(columns, len(v))
            a, b, c = Row(v)
            num.append(a)
            kind.append(b)
            notes.append(c)

    S = []
    with open(SourceTrace(prefix)) as f:
        for line in f:
            if line[0] == "S":
                r = line[2:].split(",")
                if len(r) == 6:
                    S.append([float(x) for x in r])

    S = np.array(S)
    S = S[np.lexsort((S[:, 0], S[:, 1]))]
    S = S[np.r_[S[1:, 1] != S[:-1, 1], True]]  # the last prediction for each slot
    d = {
        "num": np.array(num),
        "kind": np.array(kind),
        "notes": np.array(notes),
        "S": S,
        "columns": np.array(columns),
    }

    np.savez(cache + ".part.npz", **d)
    os.replace(cache + ".part.npz", cache)
    return d


def Arrivals(prefix, serials):
    """The runtime's F rows (arrival, smooth, display_slot, current_slot, offset) for some game frames:
    {serial: [values]}."""

    want, F = set(int(s) for s in serials), {}
    with open(SourceTrace(prefix)) as f:
        for line in f:
            if line.startswith("F,"):
                v = line.split(",")
                if int(v[1]) in want:
                    F[int(v[1])] = [float(x) for x in v[2:]]

    return F


def FirstPose(prefix):
    """source time of the trace's first head pose (P row)."""
    with open(SourceTrace(prefix)) as f:
        for line in f:
            if line.startswith("P,"):
                return float(line.split(",")[1])

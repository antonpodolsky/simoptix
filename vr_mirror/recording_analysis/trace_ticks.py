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
- 30 columns: as 31 without edge_repeat_clamps.
- 31 columns: as 30 with drawn_slot before the notes.
- 33 columns, the newest: as 31 with smoothing and smoothing_strength after the notes.
In newer traces two of them are named drawn_pose and pose_notes (picture_pose and picture_notes in older
ones). The columns are read by the names in the trace's header.

The numeric layout (`num`), column by column:
  0 stamp, 1 tick, 2 serial, 3 arrival, 4 smooth, 5 slot, 6-9 img, 10-13 raw, 14-17 view (x, y, z, w),
  18 draw, 19 presented, 20 zoom, 21 edge_clamps, 22 slot_lead, 23 edge_planned, 24 view_time,
  25 followed_serial, 26 edge_repeat_clamps, 27 edge_shift, 28 aspect, 29 smoothing (the Stabilization
  Method: 0 off, 1 Instant, 2 Look-ahead), 30 smoothing_strength (the strength its level gives)
NaN where the trace's format has no such column (18-24 in the 18-column format), -1 for 25-30 where it has
none since. `kind` is the drawn_pose column, `notes` the drawn pose stage's notes (text). Times in PVR
seconds.
"""

import os

import numpy as np

from trace_common import CACHE, SourceTrace

# The numeric layout above, as the trace's header names its columns.
NUM_NAMES = [
    "stamp",
    "tick",
    "serial",
    "arrival",
    "smooth",
    "slot",
    *(f"{pose}_{axis}" for pose in ("img", "raw", "view") for axis in "xyzw"),
    "draw",
    "presented",
    "zoom",
    "edge_clamps",
    "slot_lead",
    "edge_planned",
    "view_time",
    "followed_serial",
    "edge_repeat_clamps",
    "edge_shift",
    "aspect",
    "smoothing",
    "smoothing_strength",
]
N_COLUMNS = len(NUM_NAMES)
FIRST_LATER = NUM_NAMES.index("followed_serial")  # a column missing from here on reads -1, before it NaN
OLDER_NAMES = {"display_slot": "slot", "picture_pose": "drawn_pose", "picture_notes": "pose_notes"}
# The columns of a trace that has no header before its first row: the newest
# (StampTraceObserver::kColumns, vr_mirror\core\pipeline\observer\stamp_trace_observer.cpp).
NEWEST_HEADER = (
    "stamp,tick,serial,arrival,smooth,slot,img_x,img_y,img_z,img_w,raw_x,raw_y,raw_z,raw_w,view_x,view_y,"
    "view_z,view_w,drawn_pose,draw,presented,zoom,edge_clamps,slot_lead,edge_planned,view_time,"
    "followed_serial,edge_shift,aspect,drawn_slot,pose_notes,smoothing,smoothing_strength"
)


def Columns(header):
    """A trace's header line -> (the place in a row of each numeric column, -1 where the trace has
    none; the place of drawn_pose; the place of pose_notes)."""
    names = [OLDER_NAMES.get(name, name) for name in header.rstrip("\r\n").split(",")]
    place = {name: i for i, name in enumerate(names)}
    return (
        [place.get(name, -1) for name in NUM_NAMES],
        place.get("drawn_pose", -1),
        place.get("pose_notes", -1),
    )


def Row(v, columns):
    """One tick row's fields -> (numbers, drawn_pose, notes) in the layout above. `columns`: Columns() of
    the trace's header."""
    num, kind, notes = columns
    nums = [
        float(v[c]) if 0 <= c < len(v) else (np.nan if i < FIRST_LATER else -1.0) for i, c in enumerate(num)
    ]

    return nums, v[kind] if 0 <= kind < len(v) else "", v[notes] if 0 <= notes < len(v) else ""


def Load(prefix):
    """{'num', 'kind', 'notes', 'S', 'columns'} of the trace at `prefix` (without '_pimax.csv'). S rows:
    time asked, slot, q (x, y, z, w), the last prediction for each slot, sorted by slot. 'columns': the
    trace's own column count (18, 26, 30, 31 or 33)."""

    size = os.path.getsize(prefix + "_ticks.csv")
    os.makedirs(CACHE, exist_ok=True)
    # The layout's width in the name: a trace cached in an older layout is read again.
    cache = os.path.join(CACHE, f"{os.path.basename(prefix)}_whole{N_COLUMNS}_{size}.npz")
    if os.path.exists(cache):
        d = np.load(cache)
        return {k: d[k] for k in d.files}

    num, kind, notes, columns = [], [], [], 0
    places = Columns(NEWEST_HEADER)
    with open(prefix + "_ticks.csv") as f:
        for line in f:
            if line.startswith("stamp,"):
                places = Columns(line)
                continue
            if not line[0].isdigit():
                continue
            v = line.rstrip("\r\n").split(",")
            if len(v) < 18:
                continue  # a row cut short where the trace was closed
            columns = max(columns, len(v))
            a, b, c = Row(v, places)
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

    S = np.array(S).reshape(-1, 6)  # none in an OpenXR trace

    if len(S):
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
    """source time of the trace's first head pose (P row), or of its first tick where the source's
    file has no poses (OpenXR): both start at the game's first frame."""
    with open(SourceTrace(prefix)) as f:
        for line in f:
            if line.startswith("P,"):
                return float(line.split(",")[1])

    with open(prefix + "_ticks.csv") as f:
        for line in f:
            if line[0].isdigit():
                return float(line.split(",")[1])

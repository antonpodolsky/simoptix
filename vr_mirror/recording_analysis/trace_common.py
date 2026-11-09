# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
r"""Shared helpers for the frame-trace analysis scripts: reading the OBS plugin's frame trace and the
OBS log, quaternion math, the frame-number stamp in recordings, and ffmpeg. Knows nothing about any
one session: files, times and cockpit patches come in as arguments.

Conventions: trace times are source seconds (the runtime's clock); log and recording times are wall
clock seconds since midnight; video times are seconds into a recording. Quaternions are x, y, z, w.
"""

import os
import re
import shutil
import subprocess

import numpy as np

P = 1 / 90  # display period (s): the Pimax at 90 Hz

# ffmpeg.exe, with ffprobe.exe beside it: the one FFMPEG names, else the one on PATH.
FFMPEG = os.environ.get("FFMPEG") or shutil.which("ffmpeg") or "ffmpeg"
CACHE = os.environ.get(
    "TRACE_CACHE", os.path.join(os.environ.get("TEMP", "."), "simoptix_recording_analysis")
)

HERE = os.path.dirname(os.path.abspath(__file__))

# _ticks.csv columns (vr_mirror\core\pipeline\observer\README.md). T_SLOT: the slot the frame arrived for;
# in traces with 18 columns (header `display_slot`) the slot it was drawn for (DrawnSlotColumn).
T_STAMP, T_TICK, T_SERIAL, T_ARRIVAL, T_SMOOTH, T_SLOT = range(6)
T_IMG, T_RAW, T_VIEW = slice(6, 10), slice(10, 14), slice(14, 18)
T_COLUMNS = 18  # the numeric columns read (more follow: the drawn pose's name, timing, ...)

# F rows: serial, arrival, smooth, display_slot (the slot timing assigned), current_slot, offset
F_SERIAL, F_ARRIVAL, F_SMOOTH, F_SLOT, F_CURRENT = range(5)


def SourceTrace(prefix):
    """The frame source's half of the frame trace at `prefix`."""
    return prefix + "_pimax.csv"


def Seconds(text):
    """'hh:mm:ss.f', 'mm:ss.f' or seconds -> seconds."""
    s = 0.0
    for part in str(text).split(":"):
        s = s * 60 + float(part)

    return s


def VideoTime(s):
    return f"{int(s // 60)}:{s % 60:05.2f}"


def ReadObsLog(path):
    """The OBS log's frame trace (path prefix; `trace_start`: wall time of the trace's first drawn
    tick, roughly), recordings (start, stop wall times) and the plugin's lines (wall time, text).
    Times past midnight keep counting up."""

    trace, start, first_drawn, recs, lines = None, None, None, [], []
    last, day = 0.0, 0.0
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = re.match(r"(\d\d):(\d\d):(\d\d\.\d+): (.*)", line)
            if not m:
                continue
            t = int(m[1]) * 3600 + int(m[2]) * 60 + float(m[3]) + day
            if t < last - 43200:
                day += 86400
                t += 86400

            last, text = t, m[4]
            if "frame trace:" in text:
                trace = re.search(r"frame trace: (.*)_ticks\.csv", text)[1]
                start, first_drawn = t, None  # a later trace in the same log starts afresh
            elif "output delay" in text and start is not None and not first_drawn:
                # The trace's first tick is drawn about 2 s before this line (the file itself can open
                # minutes earlier, before anything is drawn).
                first_drawn = t - 2.0
            elif "==== Recording Start" in text:
                recs.append([t, None])
            elif "==== Recording Stop" in text and recs:
                recs[-1][1] = t

            if "[simoptix_vr_mirror]" in text:
                lines.append((t, text.split("] ", 1)[1]))

    return {"trace": trace, "trace_start": first_drawn or start, "recordings": recs, "lines": lines}


def Recording(log, video, clip_of=None, clip_start=None):
    """The recording in `log` whose start matches the file name 'YYYY-MM-DD HH-MM-SS[_tag].mp4': its start
    and stop (wall s). For a clip cut from a recording (`clip_of`: the recording's file name), the
    start is moved to the clip's start (`clip_start`: seconds into the recording; a clip cut without
    re-encoding starts at the keyframe before its nominal time, which only shifts the first guess:
    the stamp decides)."""

    if clip_of:
        start, stop = Recording(log, clip_of)
        return start + clip_start, stop

    m = re.search(r"(\d\d)-(\d\d)-(\d\d)(?:_\w+)?\.\w+$", video)
    want = int(m[1]) * 3600 + int(m[2]) * 60 + int(m[3])
    start, stop = min(log["recordings"], key=lambda r: abs((r[0] % 86400) - want))
    if abs((start % 86400) - want) > 5:
        raise SystemExit(f"no recording starting at {want} s in the log")
    return start, stop


def FirstTick(prefix):
    with open(prefix + "_ticks.csv") as f:
        for line in f:
            if line[0].isdigit():
                return float(line.split(",")[1])


def LoadTrace(prefix, t_from, t_to):
    """Tick rows, S rows (time, slot, q) and F rows of the trace for tick times t_from..t_to (source s),
    cached in CACHE (reading a whole trace takes a while)."""

    os.makedirs(CACHE, exist_ok=True)
    cache = os.path.join(CACHE, f"{os.path.basename(prefix)}_{t_from:.0f}_{t_to:.0f}.npz")
    if os.path.exists(cache):
        d = np.load(cache)
        return d["ticks"], d["S"], d["F"]

    ticks = []
    with open(prefix + "_ticks.csv") as f:
        for line in f:
            if not line[0].isdigit():
                continue
            v = line.split(",")[:T_COLUMNS]
            if t_from <= float(v[1]) <= t_to:
                ticks.append([float(x) for x in v])

    S, F = [], []
    lo, hi = t_from - 2, t_to + 2
    with open(SourceTrace(prefix)) as f:
        for line in f:
            if line[0] == "S":
                r = [float(x) for x in line[2:].split(",")]
                if lo <= r[0] <= hi:
                    S.append(r)
            elif line[0] == "F":
                r = [float(x) for x in line[2:].split(",")]
                if lo <= r[1] <= hi:
                    F.append(r)

    ticks, S, F = np.array(ticks), np.array(S), np.array(F)
    np.savez(cache, ticks=ticks, S=S, F=F)
    return ticks, S, F


def TicksForSerials(prefix, first, last, margin=200):
    """Tick rows showing game frames `first`..`last` (serials), plus `margin` ticks around them
    (for lining up a video window by its stamps)."""

    rows, before, after = [], [], 0
    with open(prefix + "_ticks.csv") as f:
        for line in f:
            if not line[0].isdigit():
                continue
            v = line.split(",")[:T_COLUMNS]
            s = int(v[T_SERIAL])
            if first <= s <= last:
                if before:
                    rows.extend(before)
                    before = []

                rows.append([float(x) for x in v])
            elif s < first:
                before = (before + [[float(x) for x in v]])[-margin:]
            elif rows:
                rows.append([float(x) for x in v])
                after += 1
                if after >= margin:
                    break

    return np.array(rows)


def DrawnSlotColumn(prefix):
    """Whether the trace's T_SLOT column is the slot each frame was drawn for (traces with
    18 columns, header `display_slot`); else it is the slot the frame arrived for, and the slot it
    was drawn for is found by comparing poses (DrawnLag)."""

    with open(prefix + "_ticks.csv") as f:
        for line in f:
            if line.startswith("stamp,"):
                return "display_slot" in line.split(",")
            if line[0].isdigit():
                break

    return False


def DrawnLag(img, slot, S, lags=4):
    """The slot an image was warped from, by comparing poses: of the display slots `slot` - k periods
    (k = 0 .. lags - 1), the one whose predicted pose (S rows) is nearest to the pose the plugin warped
    it from (`img`), the nearer slot of equals (a still head), as a lag: 0, -1, -2, ... (0 where none
    is recorded). A drawn pose with a correction from the image matches no slot exactly: the
    nearest."""

    poses, ok = Predicted(S, slot - np.arange(lags) * P)
    if not ok.any():
        return 0
    deg = np.degrees(2 * np.arccos(np.clip(np.abs(np.sum(poses * img, -1)), 0, 1)))
    deg[~ok] = np.inf
    return -int(np.argmax(deg <= deg.min() + 0.001))


def FrameLag(row, slot, S, drawn_slot):
    """The lag a tick row's image was drawn with: from the trace's drawn slot (`drawn_slot`,
    DrawnSlotColumn) or by comparing poses; `slot` the slot the frame arrived for."""

    if drawn_slot:
        return int(round((row[T_SLOT] - slot) / P))
    return DrawnLag(row[T_IMG], slot, S)


def DrawnLags(ticks, F, S, drawn_slot):
    """Per game frame (first tick showing it): serial, tick time, the lag the plugin drew it with
    (display periods; 0 = the slot timing gave, -1 = one slot earlier; FrameLag), arrival gap, slot
    step and slot lead (ms)."""

    fidx = {int(s): i for i, s in enumerate(F[:, F_SERIAL])}
    serial = ticks[:, T_SERIAL].astype(np.int64)
    out = []
    for k in np.where(np.r_[True, serial[1:] != serial[:-1]])[0]:
        i = fidx.get(int(serial[k]))
        if i is None or i == 0:
            continue
        r, q = F[i], F[i - 1]
        out.append(
            (
                serial[k],
                ticks[k, T_TICK],
                FrameLag(ticks[k], r[F_SLOT], S, drawn_slot),
                (r[F_ARRIVAL] - q[F_ARRIVAL]) * 1000,
                (r[F_SLOT] - q[F_SLOT]) * 1000,
                (r[F_CURRENT] - r[F_ARRIVAL]) * 1000,
            )
        )

    return out


def Qmul(a, b):
    ax, ay, az, aw = np.moveaxis(a, -1, 0)
    bx, by, bz, bw = np.moveaxis(b, -1, 0)
    return np.stack(
        [
            aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz,
        ],
        -1,
    )


def Conj(q):
    return q * np.array([-1, -1, -1, 1])


def Rotate(q, v):
    qv = np.concatenate([v, np.zeros(v.shape[:-1] + (1,))], -1)
    return Qmul(Qmul(q, qv), Conj(q))[..., :3]


def Predicted(S, slots):
    """The runtime's predicted pose for each display slot (nearest S row), and whether one was
    found within a quarter period."""

    k = np.clip(np.searchsorted(S[:, 1], slots), 1, len(S) - 1)
    k = np.where(np.abs(S[k - 1, 1] - slots) < np.abs(S[k, 1] - slots), k - 1, k)
    return S[k, 2:6], np.abs(S[k, 1] - slots) < 0.25 * P


def ExtractVideo(video, start, secs, width=960, height=540, rgb=False):
    """Raw frames (width x height, greyscale or RGB) and the stamp strip (256x16, full scale) of a
    part of a recording, cached in CACHE. Returns the frames' path and the decoded stamps."""

    os.makedirs(CACHE, exist_ok=True)
    tag = f"{os.path.splitext(os.path.basename(video))[0]}_{start:.2f}_{secs:.1f}".replace(" ", "_")
    if (width, height) != (960, 540):
        tag += f"_{width}x{height}"
    raw, stp = (
        os.path.join(CACHE, tag + (".rgb" if rgb else ".gray")),
        os.path.join(CACHE, tag + "_stamp.gray"),
    )

    jobs = [(raw, f"scale={width}:{height},format=" + ("rgb24" if rgb else "gray"))]
    if not os.path.exists(stp):
        jobs.append((stp, "crop=256:16:0:0,format=gray"))
    if not os.path.exists(raw):
        for out, vf in jobs:
            subprocess.run(
                [
                    FFMPEG,
                    "-v",
                    "error",
                    "-y",
                    "-ss",
                    f"{start:.3f}",
                    "-t",
                    str(secs),
                    "-i",
                    video,
                    "-vf",
                    vf,
                    "-f",
                    "rawvideo",
                    out + ".part",
                ],
                check=True,
            )

            os.replace(out + ".part", out)

    return raw, DecodeStamp(np.fromfile(stp, np.uint8).reshape(-1, 16, 256))


def DecodeStamp(strip):
    """The frame number of each frame's stamp: top-left, 15 squares of 16x16 px, bit 0 first,
    white = 1 (read from the middle of each square)."""

    v = strip[:, 4:12, :240].reshape(len(strip), 8, 15, 16)[:, :, :, 4:12].mean(axis=(1, 3))
    return ((v > 128).astype(int) * (1 << np.arange(15))).sum(1)


def LineUp(stamps, ticks, approx_tick):
    """The tick row of each video frame: the stamp occurrence (it wraps every 32768 ticks) nearest
    `approx_tick` (source s) where every frame's stamp matches; frames OBS showed twice keep their own
    stamp. None if the stamps don't match the trace."""

    st = ticks[:, T_STAMP].astype(int)
    off = (stamps - stamps[0]) % 32768
    cands = [
        c
        for c in np.where(st == stamps[0])[0]
        if c + off.max() < len(ticks) and (st[c + off] == stamps).all()
    ]
    if not cands:
        return None
    c = min(cands, key=lambda c: abs(ticks[c, T_TICK] - approx_tick))
    return c + off

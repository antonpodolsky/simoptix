# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
r"""Lining a whole recording up with its frame trace: which tick row each video frame shows.

Each video frame carries the tick's frame-number stamp (top-left, 15 bits), which wraps every 32768
ticks (6 min 4 s at 90 Hz), so a run of stamps fits every 6 minutes of a long trace equally well. The
log's clock gives a first guess of the recording's first tick, but it can be minutes off: the trace's
first head pose can be written long after the log says the trace opened (session E: 481 s late). So the
place is chosen by the images. On a tick where the game frame repeats, everything in the image
moves with the cockpit, so the patches' spread around the cockpit's rotation (shake.py) dips below
that of the frames either side. At the right place most of the trace's single repeated game frames
fall on such a dip; at a wrong one only the few that chance puts there (about 4%). Long runs of
repeats (a menu, the car stopped) are left out: over a long frozen stretch they agree anywhere
nearby. Only where the trace has too few single repeats to tell is the place nearest the log's
estimate taken, and the report says so.

Knows nothing about where the files are: stamps, trace rows and shake.py's patch errors come in.
shake.py's row k is the step from video frame k to k + 1.
"""

import math
import warnings

import numpy as np

WRAP = 32768
DIP = 0.7  # a frame's spread under 0.7 times both neighbours' is a dip
SURE = 3.0  # -log10 of the chance of so many dips: needed to trust the choice ...
MARGIN = 2.0  # ... and this much more than any other place's


def Spread(err):
    """Per shake.py row: the 90th percentile of the patches' distance from the cockpit's rotation (px at
    960 px; low when everything moves with the cockpit). err: shake.py's err (NaN: no texture)."""

    with warnings.catch_warnings():
        warnings.simplefilter("ignore", RuntimeWarning)  # rows without a textured patch
        s = np.nanpercentile(err.astype(float), 90, axis=1)

    return np.nan_to_num(s, nan=np.inf)


def Dips(spread):
    """Per shake row: its spread dips under both neighbours' (DIP)."""
    d = np.zeros(len(spread), bool)
    d[1:-1] = spread[1:-1] < DIP * np.minimum(spread[:-2], spread[2:])
    return d


def Unwrapped(stamps):
    """Each video frame's tick offset from the first, through the stamp's wraps (OBS showing a tick
    twice or dropping one moves the offset by 0 or 2)."""

    return np.r_[0, np.cumsum((np.diff(stamps) + WRAP // 2) % WRAP - WRAP // 2)]


def Reference(stamps):
    """A video frame whose stamp is surely read right: the first that starts a run of 5 steps by one."""
    ones = np.diff(stamps) == 1
    for k in range(len(ones) - 5):
        if ones[k : k + 5].all():
            return k

    return 0


def TraceRepeats(serial, rows):
    """Per shake row (video frames 1 .. n): the trace shows the same game frame as on the frame before."""
    have = rows >= 0
    s = serial[np.maximum(rows, 0)]
    return have[1:] & have[:-1] & (s[1:] == s[:-1])


def Single(rep):
    """The repeats with no repeat on either side."""
    return rep & ~np.r_[False, rep[:-1]] & ~np.r_[rep[1:], False]


def Unlikely(k, n, p):
    """-log10 of the chance of k or more hits in n tries at p each (binomial tail)."""
    if k == 0 or n == 0:
        return 0.0
    p = min(max(p, 1e-6), 1 - 1e-6)
    logs = [
        math.lgamma(n + 1)
        - math.lgamma(i + 1)
        - math.lgamma(n - i + 1)
        + i * math.log(p)
        + (n - i) * math.log1p(-p)
        for i in range(k, n + 1)
    ]

    top = max(logs)
    return -(top + math.log(sum(math.exp(x - top) for x in logs))) / math.log(10)


def LineUp(stamps, num, spread, approx):
    """The tick row of each video frame (-1 where its stamp doesn't fit) and a report.
    stamps: per video frame; num: trace_ticks.Load's rows; spread: Spread() per shake row; approx: the
    log's estimate of the first frame's tick (source s)."""

    tick_stamps, ticks, serial = num[:, 0].astype(np.int64), num[:, 1], num[:, 2].astype(np.int64)
    off = Unwrapped(stamps)
    k0 = Reference(stamps)
    n = len(spread)
    dips = Dips(spread)
    p0 = dips[1:-1].mean()  # the chance that any frame dips
    cands = []
    for c in np.where(tick_stamps == stamps[k0])[0]:
        rows = c - off[k0] + off
        inside = (rows >= 0) & (rows < len(num))
        good = inside.copy()
        good[inside] = tick_stamps[rows[inside]] == stamps[inside]
        rows = np.where(good, rows, -1)
        hits = int(good.sum())
        if hits < 0.05 * len(stamps):
            continue
        rep = TraceRepeats(serial, rows)[:n]
        single = Single(rep)
        k = int((single & dips[: len(single)]).sum())
        first_tick = ticks[min(max(c - off[k0], 0), len(ticks) - 1)]  # the first video frame's tick
        cands.append(
            dict(
                rows=rows,
                hits=hits,
                repeats=int(rep.sum()),
                single=int(single.sum()),
                dips=k,
                score=Unlikely(k, int(single.sum()), p0),
                first_tick=first_tick,
                off_estimate=first_tick - approx,
            )
        )

    if not cands:
        raise SystemExit("the recording's stamps don't fit the trace anywhere")
    nearest = min(cands, key=lambda c: abs(c["off_estimate"]))
    by_images = sorted(cands, key=lambda c: (-c["score"], -c["hits"], abs(c["off_estimate"])))
    best, second = by_images[0], (by_images[1] if len(by_images) > 1 else None)
    sure = best["score"] >= SURE and (second is None or best["score"] >= second["score"] + MARGIN)
    chosen = best if sure else nearest
    lines = [
        f"{len(stamps)} video frames; {len(cands)} places in the trace where the stamps fit "
        f"(every {WRAP} ticks); the log's estimate of the first frame's tick: {approx:.2f} s",
        f"Per place: the trace's repeated game frames, the single ones (no repeat either side), how many of",
        f"those fall on a video frame whose patch spread dips (any frame does with a chance of "
        f"{p0:.1%}), and",
        f"how unlikely that many would be by chance (-log10; higher is better).",
        f"  {'first tick (s)':>14s} {'from estimate':>13s} {'frames lined up':>15s} {'repeats':>7s} "
        f"{'single':>6s} {'on a dip':>8s} {'unlikely':>8s}",
    ]

    for c in sorted(cands, key=lambda c: c["first_tick"]):
        mark = "  <- chosen" if c is chosen else ""
        lines.append(
            f"  {c['first_tick']:14.2f} {c['off_estimate']:+13.2f} {c['hits']:15d} "
            f"{c['repeats']:7d} "
            f"{c['single']:6d} {c['dips']:8d} {c['score']:8.1f}{mark}"
        )

    if sure:
        lines.append(
            f"  chosen by the images: {best['dips']} of the trace's {best['single']} single "
            f"repeated game "
            f"frames fall on a dip (chance 1 in 10^{best['score']:.1f}"
            + (f"; next best place 1 in 10^{second['score']:.1f})" if second else ")")
        )
    else:
        lines.append(
            "  NOT SURE: too few single repeated game frames to tell the places apart by the "
            "images; "
            "took the one nearest the log's estimate"
        )

    # The lineup checked frame by frame: shifted by a frame or two, the repeats should miss the dips.
    single = Single(TraceRepeats(serial, chosen["rows"])[:n])
    d = dips[: len(single)]
    lines.append(
        f"  check: single repeats on a dip {int((single & d).sum())} of {int(single.sum())}; with "
        f"the trace "
        f"shifted by "
        + ", ".join(f"{s:+d} frames: {int((np.roll(single, s) & d).sum())}" for s in (-2, -1, 1, 2))
    )

    return chosen["rows"], dict(
        sure=sure,
        hits=chosen["hits"],
        first_tick=chosen["first_tick"],
        approx=approx,
        report="\n".join(lines),
    )

# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
r"""How regular OBS's ticks are, from a frame trace's tick times alone.

A frame picker that decides ahead which game frame each coming tick shows predicts those ticks as
the last tick plus whole OBS frame periods. This measures how far the real tick lands from that
prediction, half a second and more ahead, over every tick of a trace. It also counts the ticks OBS
skipped (two frame periods or more between rows), which the prediction steps over.

The ticks are the trace's `tick` column (the frame source's clock, as the plugin's frame picker
sees them). OBS's frame period is taken from the median step between ticks, rounded to a whole
frame rate or to the NTSC rates (59.94, 29.97). A stretch ends where the source was hidden or
restarted (a step over 1 s, or the stamp starting again).

usage: python -I tick_regularity.py <trace>_ticks.csv [...]
Times in ms.
"""

import itertools
import sys

import numpy as np

HORIZONS_S = (0.1, 0.5, 0.6)
STRETCH_BREAK_S = 1.0


def read_ticks(path):
    """The stamp and tick columns of a `_ticks.csv`, as arrays."""
    stamps, ticks = [], []

    with open(path, encoding="utf-8") as f:
        for line in f:
            if not line[0].isdigit():
                continue

            fields = line.split(",", 2)
            if len(fields) < 3:
                continue  # a row cut short where the trace was closed

            stamps.append(int(fields[0]))
            ticks.append(float(fields[1]))

    return np.array(stamps), np.array(ticks)


def obs_period(steps):
    """OBS's frame period (s) from the steps between ticks: the median, at the nearest standard rate."""
    rate = 1 / np.median(steps)
    candidates = [r for r in range(10, 241)] + [30000 / 1001, 60000 / 1001]
    nearest = min(candidates, key=lambda r: abs(r - rate))
    return 1 / nearest


def stretches(stamps, ticks):
    """(first, end) index pairs of the stretches the source ran without a break."""
    breaks = np.flatnonzero((np.diff(ticks) > STRETCH_BREAK_S) | (np.diff(stamps) <= 0)) + 1
    edges = [0, *breaks.tolist(), len(ticks)]
    return [(a, b) for a, b in itertools.pairwise(edges) if b - a > 1]


def prediction_errors(ticks, period, horizon_s):
    """For each tick, the real tick about `horizon_s` later minus its prediction (this tick plus a whole
    number of periods). Ticks too near the end have none."""
    later = np.searchsorted(ticks, ticks + horizon_s - period / 2)
    has_later = later < len(ticks)
    ahead = ticks[later[has_later]] - ticks[has_later]
    return ahead - np.round(ahead / period) * period


def report(path):
    stamps, ticks = read_ticks(path)

    if len(ticks) < 2:
        print(f"{path}: no ticks")
        return

    parts = stretches(stamps, ticks)
    steps = np.concatenate([np.diff(ticks[a:b]) for a, b in parts])
    period = obs_period(steps)
    skipped = np.round(steps / period).astype(int) - 1
    seconds = steps.sum()

    print(f"{path}")
    print(
        f"  {len(ticks)} ticks in {len(parts)} stretches, {seconds / 60:.1f} min, OBS at {1 / period:.2f} fps"
    )
    print(
        f"  step between ticks: median {np.median(steps) * 1000:.3f}, "
        f"p1 {np.percentile(steps, 1) * 1000:.3f}, p99 {np.percentile(steps, 99) * 1000:.3f}"
    )
    print(
        f"  ticks skipped by OBS: {skipped[skipped > 0].sum()} "
        f"({np.count_nonzero(skipped > 0)} places, {np.count_nonzero(skipped > 0) / seconds * 60:.1f} a minute)"
    )

    for horizon_s in HORIZONS_S:
        errors = np.abs(np.concatenate([prediction_errors(ticks[a:b], period, horizon_s) for a, b in parts]))
        print(
            f"  {horizon_s * 1000:.0f} ms ahead: off by median {np.median(errors) * 1000:.3f}, "
            f"p99 {np.percentile(errors, 99) * 1000:.3f}, p99.9 {np.percentile(errors, 99.9) * 1000:.3f}, "
            f"max {errors.max() * 1000:.3f}; over 1 ms {np.mean(errors > 0.001) * 100:.2f}%, "
            f"over 2 ms {np.mean(errors > 0.002) * 100:.2f}%"
        )


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    for path in sys.argv[1:]:
        report(path)

    return 0


if __name__ == "__main__":
    sys.exit(main())

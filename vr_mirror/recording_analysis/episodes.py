# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
r"""What the OBS plugin's slot check did during one recording, from the frame trace: every
stretch of game frames it re-timed (drew from an earlier slot's pose), with video times, and the
frame timing around them. Knows nothing about the pictures: window.py checks a stretch against the
video.

usage: python -I episodes.py <OBS log> <recording .mp4> [--context N] [--offset S]
  --context N  also print the timing of N frames before and after each stretch
  --offset S   seconds to add to the video times: the trace's first tick is placed at the log line
               that announced the trace, which can be a few seconds off; window.py prints the
               exact offset once it has lined a window up by the frame stamp
"""

import argparse
import re
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from trace_common import DrawnLags, DrawnSlotColumn, FirstTick, LoadTrace, ReadObsLog, Recording, VideoTime


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("video")
    ap.add_argument("--context", type=int, default=0)
    ap.add_argument("--offset", type=float, default=0.0)
    args = ap.parse_args()

    log = ReadObsLog(args.log)
    if not log["trace"]:
        raise SystemExit("the log shows no frame trace (Debug: write a frame trace was off)")
    rec_start, rec_stop = Recording(log, args.video)
    t0 = FirstTick(log["trace"])

    def Tick(wall):
        return t0 + wall - log["trace_start"]

    def Video(tick):
        return tick - t0 + log["trace_start"] - rec_start + args.offset

    print(f"trace {log['trace']}, recording {VideoTime(rec_stop - rec_start)} long")
    for t, text in log["lines"]:
        # "game behind: X frames rendered for an earlier slot ..., Y frames judged by picture" (before
        # 2026-10-31: "game behind: X of Y frames judged by picture ...").
        m = re.match(r"game behind: (\d+)(?: of| frames rendered)", text)
        if rec_start <= t <= rec_stop and m and m[1] != "0":
            print(f"  log, 10 s up to video {VideoTime(t - rec_start)}: {m[1]} frames re-timed")

    ticks, S, F = LoadTrace(log["trace"], Tick(rec_start) - 5, Tick(rec_stop) + 5)
    rows = DrawnLags(ticks, F, S, DrawnSlotColumn(log["trace"]))
    late = [k for k, r in enumerate(rows) if r[2] != 0]
    print(
        f"{len(rows)} game frames, {len(late)} re-timed (video times from the log, "
        f"{'offset %+.1f s' % args.offset if args.offset else 'may be a few s off: see --offset'})"
    )

    stretches = []
    for k in late:
        if stretches and rows[k][1] - rows[stretches[-1][-1]][1] < 1.0:
            stretches[-1].append(k)
        else:
            stretches.append([k])

    for s in stretches:
        lags = sorted({rows[k][2] for k in s})
        print(
            f"  video {VideoTime(Video(rows[s[0]][1]))}-{VideoTime(Video(rows[s[-1]][1]))}: {len(s)} "
            f"frames, "
            f"lags {lags}, serials {rows[s[0]][0]}-{rows[s[-1]][0]}"
        )

        if args.context:
            show = range(max(0, s[0] - args.context), min(len(rows), s[-1] + args.context + 1))
            for k in show:
                serial, tick, lag, gap, step, lead = rows[k]
                print(
                    f"      {serial} video {VideoTime(Video(tick))} arrival gap {gap:5.1f} slot step "
                    f"{step:5.1f} "
                    f"lead {lead:5.1f} ms, drawn lag {lag:+d}"
                )


if __name__ == "__main__":
    main()

# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
r"""Exports a stretch of a session's frame trace as an e2e replay file (vr_mirror\e2e\replay\*.csv, format in
.agents\skills\dev\reference\e2e.md "Adding a replay of a real session"): the head poses, the runtime's predictions and every game frame
with the lag it was really rendered with, times in seconds from the file's start.

usage: python -I export_replay.py <OBS log> <recording .mp4> <start mm:ss> <secs> <out.csv>
           --about "what happened" [--lags "first-last:lag,..."] [--offset S]
  --about   the header text: what the session shows and what happened (several lines: use \n). The
            replay is public: no recording name, date, clock time, track or car.
  --lags    true lags of game frames by serial (e.g. "630873-630940:-2,630941-630973:-1"), as found
            with window.py; every other frame is lag 0 (on time)
  --offset  the video time offset window.py printed, so the stretch matches the video exactly
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from trace_common import FirstTick, ReadObsLog, Recording, Seconds, SourceTrace


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("video")
    ap.add_argument("start")
    ap.add_argument("secs", type=float)
    ap.add_argument("out")
    ap.add_argument("--about", required=True)
    ap.add_argument("--lags", default="")
    ap.add_argument("--offset", type=float, default=0.0)
    args = ap.parse_args()

    lags = []
    for part in filter(None, args.lags.split(",")):
        span, lag = part.split(":")
        a, b = (span.split("-") + [span])[:2]
        lags.append((int(a), int(b), int(lag)))

    def Lag(serial):
        return next((l for a, b, l in lags if a <= serial <= b), 0)

    log = ReadObsLog(args.log)
    rec_start, _ = Recording(log, args.video)
    t0 = FirstTick(log["trace"])
    begin = t0 + rec_start + Seconds(args.start) - log["trace_start"] + args.offset  # source s
    end = begin + args.secs

    rows = []  # (time, text)
    with open(SourceTrace(log["trace"])) as f:
        for line in f:
            kind = line[0]
            if kind not in "PSF":
                continue
            v = line[2:].strip().split(",")
            if kind == "P" and begin <= float(v[0]) <= end:
                rows.append((float(v[0]), "P,%.6f," + ",".join(v[1:5])))
            elif kind == "S" and begin <= float(v[0]) <= end:
                rows.append((float(v[0]), "S,%.6f," + f"{float(v[1]) - begin:.6f}," + ",".join(v[2:6])))
            elif kind == "F" and begin <= float(v[1]) <= end:
                serial = int(v[0])
                rows.append((float(v[1]), f"F,{serial},%.6f," + f"{float(v[3]) - begin:.6f},{Lag(serial)}"))

    rows.sort(key=lambda r: r[0])
    with open(args.out, "w", newline="\n") as f:
        for line in args.about.replace("\\n", "\n").splitlines():
            f.write(f"# {line}\n")
        f.write(
            "# Times in seconds from the start of this file.\n"
            "# P,time,qx,qy,qz,qw: head pose (1 kHz)\n"
            "# S,time,slot,qx,qy,qz,qw: the runtime's predicted pose for display slot `slot`, asked at `time`\n"
            "# F,serial,arrival,slot,lag: a game frame arriving at `arrival`, assigned to display slot `slot` by the\n"
            "#   tool's timing; it was rendered for slot + lag display periods\n"
        )

        for t, text in rows:
            f.write(text % (t - begin) + "\n")

    frames = sum(1 for r in rows if r[1][0] == "F")
    print(f"{args.out}: {len(rows)} rows, {frames} game frames, {len(lags)} spans of late frames")


if __name__ == "__main__":
    main()

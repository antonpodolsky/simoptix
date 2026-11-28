# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
r"""Exports a stretch of a session's frame trace as an e2e replay file (vr_mirror\e2e\replay\*.csv, format in
.agents\skills\dev\reference\e2e.md "Adding a replay of a real session"): the head poses, the runtime's predictions and every game frame
with the lag it was really rendered with, times in seconds from the file's start.

With the Pimax source's trace (_pimax.csv) the poses are the runtime's. With the OpenXR source's
(_openxr.csv) the head is known only through the frames, as the source knows it (openxr_source.cpp):
each frame's drawn pose, from the ticks trace, is the head pose at its drawn slot and that slot's
prediction, and the frame's slot is the one it was drawn for.

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

PIMAX_HEADER = (
    "# Times in seconds from the start of this file.\n"
    "# P,time,qx,qy,qz,qw: head pose (1 kHz)\n"
    "# S,time,slot,qx,qy,qz,qw: the runtime's predicted pose for display slot `slot`, asked at `time`\n"
    "# F,serial,arrival,slot,lag: a game frame arriving at `arrival`, assigned to display slot `slot` by the\n"
    "#   tool's timing; it was rendered for slot + lag display periods\n"
)

OPENXR_HEADER = (
    "# Times in seconds from the start of this file. From the OpenXR source, which knows the head only\n"
    "# through the frames: each frame's drawn pose is the head pose at its slot and that slot's prediction.\n"
    "# P,time,qx,qy,qz,qw: the drawn pose of the frame drawn for the slot at `time`\n"
    "# S,time,slot,qx,qy,qz,qw: the same pose for display slot `slot`, given at the frame's arrival `time`\n"
    "# F,serial,arrival,slot,lag: a game frame arriving at `arrival`, drawn for display slot `slot` (as the\n"
    "#   game reported it); it was rendered for slot + lag display periods\n"
)


def parse_lags(text):
    """--lags as (first serial, last serial, lag) spans."""
    lags = []

    for part in filter(None, text.split(",")):
        span, lag = part.split(":")
        a, b = (span.split("-") + [span])[:2]
        lags.append((int(a), int(b), int(lag)))

    return lags


def pimax_rows(path, begin, end, lag_of):
    """(source time, row with %.6f for its time) of the Pimax source's P, S and F rows in begin..end."""
    rows = []

    with open(path, encoding="utf-8") as f:
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
                rows.append(
                    (float(v[1]), f"F,{serial},%.6f," + f"{float(v[3]) - begin:.6f},{lag_of(serial)}")
                )

    return rows


def openxr_rows(ticks_path, begin, end, lag_of):
    """The same rows from the ticks trace of an OpenXR session: per game frame arriving in begin..end
    (its first tick), P at its drawn slot, S at its arrival for that slot, F with its drawn slot."""
    rows = []
    seen = set()

    with open(ticks_path, encoding="utf-8") as f:
        col = {name: i for i, name in enumerate(f.readline().strip().split(","))}
        img = [col[f"img_{c}"] for c in "xyzw"]

        for line in f:
            if not line[0].isdigit():
                continue

            v = line.split(",")
            if float(v[col["tick"]]) > end + 2:
                break  # ticks show frames up to the output delay after they arrive

            serial, arrival = int(v[col["serial"]]), float(v[col["arrival"]])
            if serial in seen or not begin <= arrival <= end:
                continue

            seen.add(serial)
            slot = float(v[col["drawn_slot"]])
            pose = ",".join(v[i] for i in img)
            rows.append((slot, "P,%.6f," + pose))
            rows.append((arrival, "S,%.6f," + f"{slot - begin:.6f}," + pose))
            rows.append((arrival, f"F,{serial},%.6f," + f"{slot - begin:.6f},{lag_of(serial)}"))

    return rows


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

    lags = parse_lags(args.lags)

    def lag_of(serial):
        return next((lag for a, b, lag in lags if a <= serial <= b), 0)

    log = ReadObsLog(args.log)
    rec_start, _ = Recording(log, args.video)
    t0 = FirstTick(log["trace"])
    begin = t0 + rec_start + Seconds(args.start) - log["trace_start"] + args.offset  # source s
    end = begin + args.secs

    source = SourceTrace(log["trace"])
    openxr = source.endswith("_openxr.csv")
    if openxr:
        rows = openxr_rows(log["trace"] + "_ticks.csv", begin, end, lag_of)
    else:
        rows = pimax_rows(source, begin, end, lag_of)

    rows.sort(key=lambda r: r[0])
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.writelines(f"# {line}\n" for line in args.about.replace("\\n", "\n").splitlines())
        f.write(OPENXR_HEADER if openxr else PIMAX_HEADER)

        f.writelines(text % (t - begin) + "\n" for t, text in rows)

    frames = sum(1 for r in rows if r[1][0] == "F")
    print(f"{args.out}: {len(rows)} rows, {frames} game frames, {len(lags)} spans of late frames")


if __name__ == "__main__":
    main()

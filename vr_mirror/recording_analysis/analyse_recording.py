# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
r"""A whole recording analysed with one command: shake, timing events and the frame trace.

usage: python -I analyse_recording.py "<recording>.mp4" [--out folder] [--log <OBS log>] [--redo]

Finds the OBS log that recorded it and the frame trace that was open (sources.py), then:
1. measures every frame's patch shifts (ffmpeg, CUDA decode, 960x540 grey into grid_shift.exe) and
   reads the frame-number stamp of every frame, in one decoding pass;
2. fits the cockpit's motion and its steps (shake.py), with the log's 10 s stats (shake_causes.py);
3. with a trace and stamps: lines the recording up with the trace (lineup.py: the stamp occurrence
   where the trace's single repeated game frames fall on frames that froze in the video), classifies each jump by the trace
   (shake_traced.py, traces with 26 or more columns), the cadence (cadence.py) and the timing events
   (events.py).
Everything goes into the output folder (default: a new folder '<recording> analysis' in the folder
SIMOPTIX_DATA names, else beside the recording): one report per step (.txt), their data (.npz, .csv), run.json (what
was found) and summary.txt (the headline numbers). Steps whose output is already there are skipped
(--redo runs all again). The trace's parsed copy is cached in trace_common.CACHE.

Knows nothing about the measures themselves: it finds the files, runs the steps and collects their
numbers. Steps in px of the 1920x1080 picture.
"""

import argparse
import contextlib
import json
import os
import subprocess
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cadence
import events
import lineup
import shake
import shake_causes
import shake_traced
import sources
import trace_ticks
from trace_common import CACHE, FFMPEG, HERE, DecodeStamp, VideoTime

DATA = os.environ.get("SIMOPTIX_DATA")  # where analyses go by default (else beside the recording)
GRID_SHIFT = os.path.join(HERE, "grid_shift.exe")
W, H = 960, 540  # the grid's frame size (shake.py's grid)
STAMP_W, STAMP_H = 256, 16  # the stamp strip, top-left of the full-size picture


def Say(text):
    print(f"[{time.strftime('%H:%M:%S')}] {text}", flush=True)


def Report(path, fn, *args, **kw):
    """fn's printed output into the file at path; returns fn's result."""
    with open(path, "w", encoding="utf-8", newline="\n") as f, contextlib.redirect_stdout(f):
        return fn(*args, **kw)


def Decode(video, grid, stamps_path):
    """One decoding pass: the patch shifts (grid_shift.exe) and the stamp of every frame."""
    if not os.path.exists(GRID_SHIFT):
        subprocess.run(["cmd", "/c", os.path.join(HERE, "build.bat")], check=True)

    os.makedirs(CACHE, exist_ok=True)
    strip = os.path.join(
        CACHE, os.path.splitext(os.path.basename(video))[0].replace(" ", "_") + "_stamps.gray"
    )

    graph = (
        f"[0:v]split=2[a][b];[a]scale={W}:{H}:flags=area,format=gray[g];"
        f"[b]crop={STAMP_W}:{STAMP_H}:0:0,format=gray[s]"
    )

    ff = subprocess.Popen(
        [
            FFMPEG,
            "-v",
            "error",
            "-y",
            "-hwaccel",
            "cuda",
            "-i",
            video,
            "-filter_complex",
            graph,
            "-map",
            "[g]",
            "-f",
            "rawvideo",
            "pipe:1",
            "-map",
            "[s]",
            "-f",
            "rawvideo",
            strip,
        ],
        stdout=subprocess.PIPE,
    )

    gs = subprocess.run(
        [GRID_SHIFT, str(W), str(H), grid + ".part"], stdin=ff.stdout, stderr=subprocess.DEVNULL
    )

    ff.stdout.close()
    if ff.wait() != 0 or gs.returncode != 0:
        raise SystemExit(f"decoding failed (ffmpeg {ff.returncode}, grid_shift {gs.returncode})")

    os.replace(grid + ".part", grid)
    np.save(stamps_path, DecodeStamp(np.fromfile(strip, np.uint8).reshape(-1, STAMP_H, STAMP_W)))
    os.remove(strip)


def HasStamps(stamps):
    """Whether the recording carries the frame-number stamp: most frames step by one tick."""
    return len(stamps) > 100 and np.mean(np.diff(stamps) == 1) > 0.5


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("video")
    ap.add_argument("--out")
    ap.add_argument("--log", help="the OBS log (found from the recording's name if not given)")
    ap.add_argument("--redo", action="store_true", help="run every step again")
    args = ap.parse_args()
    video = os.path.abspath(args.video)
    name = os.path.basename(video)
    stem = os.path.splitext(name)[0]
    rec = sources.FindInLog(args.log, video) if args.log else sources.Find(video)
    if rec is None:
        raise SystemExit(f"{args.log} has no recording starting at the time in {name}")
    out = args.out or os.path.join(DATA or os.path.dirname(video), f"{stem} analysis")
    os.makedirs(out, exist_ok=True)
    F = lambda f: os.path.join(out, f)
    Need = lambda *files: args.redo or not all(os.path.exists(F(f)) for f in files)
    if rec["trace"] and not os.path.exists(rec["trace"] + "_ticks.csv"):
        Say(f"the trace {rec['trace']} is gone: analysing without it")
        rec["trace"] = None

    json.dump(dict(rec, video=video), open(F("run.json"), "w"), indent=1)
    Wall = lambda s: time.strftime("%H:%M:%S", time.gmtime(s % 86400))
    Say(
        f"{name}: OBS log {os.path.basename(rec['log'])}, recording {Wall(rec['start'])} to "
        f"{Wall(rec['stop']) if rec['stop'] else '(the log ends first)'}; "
        f"trace {os.path.basename(rec['trace']) if rec['trace'] else 'none'}; out {out}"
    )

    if Need("grid.f32", "stamps.npy"):
        Say("decoding: patch shifts and stamps (about 4 min per 16 min of video)")
        Decode(video, F("grid.f32"), F("stamps.npy"))

    stamps = np.load(F("stamps.npy"))
    if Need("shake.npz"):
        Say("shake: the cockpit's rotation per frame")
        shake.Run(F("grid.f32"), F("shake.npz"))

    if Need("shake.txt", "shake_frames.npz"):
        Say("shake against the OBS log")
        Report(F("shake.txt"), shake_causes.Run, F("shake.npz"), rec, name, out=F("shake"))

    frames = np.load(F("shake_frames.npz"))
    summary = [
        f"{name}: {len(stamps)} frames ({len(stamps) / 90 / 60:.1f} min); OBS log {rec['log']}; "
        f"trace {rec['trace'] or 'none'}"
    ]

    step, ok = frames["step"], frames["ok"]
    n = len(step)
    jumps = int(np.sum(ok & (step > 2)))
    summary += [
        "(lower is better in every line)",
        f"cockpit jumps over 2 px: {jumps} ({jumps / (n / 90 / 60):.1f} per min); step p50 "
        f"{np.percentile(step[ok], 50):.2f}, p99 {np.percentile(step[ok], 99):.2f}, p99.9 "
        f"{np.percentile(step[ok], 99.9):.2f} px",
    ]

    if not rec["trace"] or not HasStamps(stamps):
        why = "no frame trace" if not rec["trace"] else "no frame-number stamp in the video"
        summary.append(
            f"{why}: no lineup, cadence or events. The video-only repeat detector (shake.txt) "
            f"flagged "
            f"{int(np.sum(frames['repeat'] & ok))} frames; in a clean race many are false alarms"
        )
    else:
        trace = None
        if Need("lineup.npz", "lineup.txt"):
            Say("reading the trace (cached after the first time)")
            trace = trace_ticks.Load(rec["trace"])
            Say("lining the recording up with the trace")
            num = trace["num"]
            approx = trace_ticks.FirstPose(rec["trace"]) + (rec["start"] - rec["trace_opened"])
            spread = lineup.Spread(np.load(F("shake.npz"))["err"])
            rows, info = lineup.LineUp(stamps, num, spread, approx)
            T = num[np.maximum(rows, 0)]
            np.savez_compressed(
                F("lineup.npz"),
                rows=rows,
                tick=T[:, 1],
                serial=T[:, 2].astype(np.int64),
                slot=T[:, 5],
                view_time=T[:, 24],
                sure=info["sure"],
            )

            with open(F("lineup.txt"), "w", encoding="utf-8", newline="\n") as f:
                f.write(info["report"] + "\n")

        cad = np.load(F("lineup.npz"))
        lineup_text = open(F("lineup.txt"), encoding="utf-8").read()
        summary.append(
            "lineup: "
            + next(
                l.strip()
                for l in lineup_text.splitlines()
                if "chosen by the pictures" in l or "NOT SURE" in l
            )
        )

        if Need("traced.txt", "traced_frames.npz"):
            trace = trace or trace_ticks.Load(rec["trace"])
            if int(trace["columns"]) >= 26:
                Say("each jump against the trace")
                Report(
                    F("traced.txt"),
                    shake_traced.Run,
                    F("shake.npz"),
                    cad["rows"],
                    trace,
                    rec,
                    name,
                    F("traced"),
                )
            else:
                Say("the trace has the oldest format (18 columns): no per-jump causes")

        Say("cadence and events")
        Report(F("cadence.txt"), cadence.Run, cad, name)
        counts, same = Report(F("events.txt"), events.Run, frames, cad)
        rows = cad["rows"]
        both = (rows[1:] >= 0) & (rows[:-1] >= 0)
        odd_ticks = int(np.sum(np.diff(rows)[both] != 1))
        vf = np.arange(1, n + 1)
        detector = frames["repeat"] & ok
        really = detector & (cad["serial"][vf] == cad["serial"][vf - 1])
        c = lambda k: counts[k][0]
        summary += [
            f"repeated game frames (trace): {c(events.REPEAT)}",
            f"game frames given a slot two or more on (a missed slot): {c(events.SLOT_GAP)}",
            f"two consecutive game frames given the same slot: {c(events.SAME_SLOT)}"
            + (
                " ("
                + ", ".join(f"{VideoTime(t)}" for t, _ in same[:12])
                + (", ..." if len(same) > 12 else "")
                + ")"
                if same
                else ""
            ),
            f"  ... of those with a cockpit jolt of 2 px or more: {counts[events.SAME_SLOT][1]}",
            f"game frames never shown (skips): {c(events.SKIP)}",
            f"OBS ticks shown twice or dropped: {odd_ticks}",
            f"video-only repeat detector (shake.txt): flagged {int(detector.sum())}, of them repeats in the trace "
            f"{int(really.sum())} (the rest are false alarms: use the trace's count)",
        ]

    with open(F("summary.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(summary) + "\n")

    print("\n".join(summary))


if __name__ == "__main__":
    main()

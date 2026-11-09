# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
r"""Shake events in a recording and what they coincide with, from shake.py's output and the OBS log.

Per output frame (OBS tick): the cockpit's step away from its smooth path (px of the 1920x1080
video), whether the frame shows a new game frame or the last one again (the plugin re-warps a
repeated game frame with the new view only, so its pose error can't change on that tick), whether
OBS showed the same image twice, and the plugin's 10 s stats at that time (game fps, repeats,
frames judged "game behind"). Needs no frame trace.

The repeated-frame detector here reads the video alone (everything in the image moves with the
cockpit). It finds the real repeats (they read 0.12 to 0.31 px) but also flags frames near its cut-off
(0.48 to 0.60 px) that are new game frames: in a clean race (race F, 18 real repeats) it flagged 128.
Its count is then no count of freezes. With a frame trace, take the repeats from the trace's serials
(cadence.py, events.py).

usage: python -I shake_causes.py <shake.npz> <recording .mp4> [--log <OBS log>] [--threshold px] [--out prefix]
(analyse_recording.py runs it; the OBS log is found from the recording's name unless given.)
"""

import argparse
import os
import re
import sys
import warnings

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sources
from shake import DEG, PX_PER_DEG
from trace_common import VideoTime

ROLL_RADIUS = 540  # px of the 1080p video at which a roll step is counted (half its height)
REPEAT_SPREAD = 0.6  # a repeated game frame: 90% of patches follow the rigid fit within 0.6 px (at 960 px)
OBS_SAME = 0.3  # whole-frame mean abs difference (grey levels) below which OBS showed the same image


def LogWindows(path, start, stop):
    """The plugin's 10 s stats inside a recording (start, stop: wall s, sources.LogLines' clock): rows
    of (video time at the window's end, game fps, repeats, skips, judged, judged behind)."""

    rows, pending = [], {}
    for t, text in sources.LogLines(path):
        if not text.startswith("[simoptix_vr_mirror] "):
            continue
        if not start < t < stop + 10:
            continue
        g = re.search(r"game behind: (\d+) of (\d+)", text)
        if g:
            pending = {"behind": int(g[1]), "judged": int(g[2])}
        g = re.search(r"game behind: (\d+) frames rendered for an earlier slot.*?(\d+) frames judged", text)
        if g:
            pending = {"behind": int(g[1]), "judged": int(g[2])}  # the log's newer wording
        g = re.search(
            r"ticks in 10 s: (\d+) showed the same.*?(\d+) skipped ahead.*game at ([\d.]+) fps", text
        )

        if g:
            rows.append(
                (
                    t - start,
                    float(g[3]),
                    int(g[1]),
                    int(g[2]),
                    pending.get("judged", 0),
                    pending.get("behind", 0),
                )
            )

            pending = {}

    return np.array(rows).reshape(-1, 6)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("npz")
    ap.add_argument("video")
    ap.add_argument("--log")
    ap.add_argument("--threshold", type=float, default=2.0)
    ap.add_argument("--out")
    args = ap.parse_args()
    rec = sources.FindInLog(args.log, args.video) if args.log else sources.Find(args.video)
    if rec is None:
        raise SystemExit(f"no recording starting at the file name's time in {args.log}")
    Run(args.npz, rec, os.path.basename(args.video), args.threshold, args.out)


def Run(npz, rec, video_name, threshold=2.0, out=None):
    """rec: sources.Find's entry for the recording; name: its file name (for the report)."""
    with warnings.catch_warnings(), np.errstate(invalid="ignore", divide="ignore"):
        warnings.simplefilter("ignore", RuntimeWarning)  # correlations of a window column that never changes
        _Run(npz, rec, video_name, threshold, out)


def _Run(npz, rec, video_name, threshold, out):
    z = np.load(npz)
    res, vel, mad, err, n_valid = z["res"], z["vel"], z["mad"], z["err"].astype(float), z["n_valid"]
    n = len(res)
    t = np.arange(n) / 90 + 1 / 90 + float(z["start"])  # video time of each frame

    # Step: how far the cockpit moved away from (or back to) its smooth path in this frame.
    d = np.diff(res, axis=0, prepend=np.nan)
    step = np.hypot(np.hypot(d[:, 0], d[:, 1]) * PX_PER_DEG, d[:, 2] * DEG * ROLL_RADIUS)
    speed = np.hypot(vel[:, 0], vel[:, 1])  # the smooth path's speed (deg per frame)

    # On a repeated game frame everything in the image (scenery, wheel, HUD) moves with the
    # cockpit; on a new one the fastest scenery (the near barrier, the road) moves on its own.
    spread = np.nanpercentile(err, 90, axis=1)

    # OBS showing an image twice can't be told from a repeated game frame under a still view, and
    # the OBS log reports no skipped or doubled frames: both count as repeated game frames.
    same = np.zeros(n, bool)
    repeat = (spread < REPEAT_SPREAD) | (mad < OBS_SAME)
    fit_ok = (z["n_in"] >= 15) & np.isfinite(step)

    start, stop = rec["start"], rec["stop"] or rec["start"] + n / 90 + 1
    win = LogWindows(rec["log"], start, stop)

    # Each frame's 10 s log window.
    wi = np.searchsorted(win[:, 0], t)
    inwin = wi < len(win)
    wi = np.minimum(wi, len(win) - 1)
    behind_frac = np.where(win[:, 4] > 0, win[:, 5] / np.maximum(win[:, 4], 1), 0)

    ok = fit_ok & inwin
    T = threshold
    ev = ok & (step > T)
    print(
        f"{video_name}: {n} frames ({n / 90 / 60:.1f} min), {ok.sum()} measured "
        f"({100 * ok.mean():.1f}%); step px at 1080p p50 {np.nanpercentile(step[ok], 50):.2f} "
        f"p90 {np.nanpercentile(step[ok], 90):.2f} p99 {np.nanpercentile(step[ok], 99):.2f} "
        f"p99.9 {np.nanpercentile(step[ok], 99.9):.2f}"
    )

    print(
        f"  frames with a step above {T:g} px: {ev.sum()} ({100 * ev.mean():.2f}%, "
        f"{ev.sum() / (n / 90 / 60):.0f} per min)"
    )

    # Repeat detector against the log's own repeat count, per 10 s window.
    cnt = np.bincount(wi[ok], weights=repeat[ok], minlength=len(win))
    tot = np.bincount(wi[ok], minlength=len(win))
    full = tot > 850

    # Where the car is stopped or slow (pit stops) the scenery hardly moves and new frames look
    # repeated: those windows are left unclassified.
    still_w = cnt > 2 * win[:, 2] + 50
    if full.any():
        g = full & ~still_w
        c = np.corrcoef(cnt[g], win[g, 2])[0, 1]
        print(
            f"  repeat detector vs the log's repeats per 10 s window: found {cnt[g].sum():.0f}, "
            f"log {win[g, 2].sum():.0f}, correlation {c:.2f} over {g.sum()} windows (median difference "
            f"{np.median(cnt[g] - win[g, 2]):+.0f}); {still_w[full].sum()} windows with the car stopped or slow "
            f"left unclassified: " + ", ".join(VideoTime(x - 10) for x in win[full & still_w, 0])
        )

    still = still_w[wi]

    # Where the steps happen: by kind of frame.
    prev_rep = np.r_[False, repeat[:-1]]
    kinds = {
        "repeated game frame (same image re-warped)": ok & repeat & ~still,
        "new game frame right after a repeat": ok & ~repeat & ~still & prev_rep,
        "new game frame in a steady run": ok & ~repeat & ~still & ~prev_rep,
    }

    e2 = np.where(ok, np.maximum(step - 0.3, 0) ** 2, 0)  # shake energy above the measuring noise
    print(
        f"  {'kind of frame':45s} {'frames':>8s} {'steps>T':>8s} {'rate':>7s} {'energy share':>12s} "
        f"{'p99 step':>9s}"
    )

    for name, m in kinds.items():
        print(
            f"  {name:45s} {m.sum():8d} {(m & ev).sum():8d} {100 * (m & ev).sum() / max(m.sum(), 1):6.2f}% "
            f"{100 * e2[m].sum() / e2.sum():11.1f}% {np.nanpercentile(step[m], 99) if m.any() else 0:9.2f}"
        )

    # By cause, as far as the video and the log can tell. A repeated game frame keeps its image, so a
    # jump on it is the warp pose changing; on a new frame during a fast head turn the view itself
    # accelerates (and the smooth-path measure is least exact); the rest are new frames whose warp
    # pose is off from their neighbours', split by whether the frame matching was re-timing frames
    # in that 10 s window (judged at least 10% "game behind").
    new = ok & ~repeat & ~still
    fast = speed >= 0.3
    busy = behind_frac[wi] >= 0.1
    causes = {
        "unclassified: car stopped or slow (pit stops)": ok & still,
        "repeated game frame re-warped from another pose": ok & repeat & ~still,
        "new frame, fast head turn (view >= 0.3 deg/frame)": new & fast,
        "new frame, frame matching re-timing (window >= 10%)": new & ~fast & busy,
        "new frame, frame matching idle (window < 10%)": new & ~fast & ~busy,
    }

    minutes = ok.sum() / 90 / 60
    print(f"  {'cause':55s} {'frames':>8s} {'steps>T':>8s} {'per min':>8s} {'share':>6s} {'energy':>7s}")
    for name, m in causes.items():
        print(
            f"  {name:55s} {m.sum():8d} {(m & ev).sum():8d} {(m & ev).sum() / minutes:8.1f} "
            f"{100 * (m & ev).sum() / max(ev.sum(), 1):5.1f}% {100 * e2[m].sum() / e2.sum():6.1f}%"
        )

    for name, m in (("windows >= 10%", busy), ("windows < 10%", ~busy)):
        mm = ok & m
        if mm.any():
            print(
                f"    {name}: {mm.sum() / 90 / 60:.1f} min, steps>T "
                f"{(mm & ev).sum() / (mm.sum() / 90 / 60):.1f} per min"
            )

    # Per 10 s window: shake against the plugin's stats.
    ev_w = np.bincount(wi[ok], weights=ev[ok], minlength=len(win))
    en_w = np.bincount(wi[ok], weights=e2[ok], minlength=len(win))
    rows = full
    print(f"  per 10 s window ({rows.sum()} windows): correlation of steps>T count with")
    for name, x in (
        ("game fps", win[:, 1]),
        ("repeats", win[:, 2]),
        ('share judged "game behind"', behind_frac),
        ('frames judged "game behind"', win[:, 5]),
    ):
        print(
            f"    {name:30s} {np.corrcoef(ev_w[rows], x[rows])[0, 1]:+.2f}   (energy "
            f"{np.corrcoef(en_w[rows], x[rows])[0, 1]:+.2f})"
        )

    for lo, hi in ((0, 0.1), (0.1, 0.4), (0.4, 0.7), (0.7, 1.01)):
        m = rows & (behind_frac >= lo) & (behind_frac < hi)
        if m.any():
            print(
                f"    judged behind {lo:.0%}-{hi:.0%}: {m.sum():4d} windows, steps>T "
                f"{ev_w[m].mean() / 10 * 60:6.1f} per min, "
                f"game {np.median(win[m, 1]):.1f} fps, repeats {np.median(win[m, 2]):.0f} per 10 s"
            )

    for lo, hi in ((0, 85), (85, 89), (89, 91)):
        m = rows & (win[:, 1] >= lo) & (win[:, 1] < hi)
        if m.any():
            print(
                f"    game {lo}-{hi} fps: {m.sum():4d} windows, steps>T {ev_w[m].mean() / 10 * 60:6.1f} "
                f"per min, "
                f"judged behind {np.median(behind_frac[m]):.0%}"
            )

    # Steps against the view's speed: one display slot of wrong pose moves the cockpit by the head's
    # motion in one slot, which at a slow view is small (trace_steps.py).
    print("  steps by the smooth path's speed (deg per frame):")
    for lo, hi in ((0, 0.03), (0.03, 0.1), (0.1, 0.3), (0.3, 9)):
        m = ok & (speed >= lo) & (speed < hi)
        if m.any():
            print(
                f"    {lo:.2f}-{hi:.2f}: {100 * m.sum() / ok.sum():4.1f}% of frames, steps>T "
                f"{100 * (m & ev).sum() / m.sum():5.2f}%, "
                f"p99 step {np.nanpercentile(step[m], 99):.2f} px"
            )

    # Shape: a pose error on one game frame moves the cockpit out and back (two steps in opposite
    # directions within a few frames).
    ks = np.where(ev)[0]
    back = 0
    for k in ks:
        for j in range(k + 1, min(k + 4, n)):
            if np.isfinite(d[j, 0]) and np.dot(d[k, :2], d[j, :2]) < -0.5 * np.dot(d[k, :2], d[k, :2]):
                back += 1
                break

    print(f"  steps>T followed within 3 frames by a step back of at least half its size: {back} of {len(ks)}")

    # Worst moments, grouped into 1 s bursts.
    order = np.argsort(np.where(ev, step, 0))[::-1]
    shown, lines = [], []
    for k in order[:4000]:
        if not ev[k] or any(abs(k - s) < 90 for s in shown):
            continue
        shown.append(k)
        burst = ev[max(0, k - 45) : k + 45].sum()
        lines.append(
            f"    {VideoTime(t[k])}  step {step[k]:5.1f} px, {burst:2d} steps>T in that second, "
            f"view "
            f"{speed[k]:.3f} deg/frame, {'repeat' if repeat[k] else 'new'} frame (prev "
            f"{'repeat' if prev_rep[k] else 'new'}), window: game {win[wi[k], 1]:.0f} fps, "
            f"{win[wi[k], 2]:.0f} repeats, judged behind {behind_frac[wi[k]]:.0%}"
        )

        if len(shown) >= 25:
            break

    print("  worst moments (one per second):")
    print("\n".join(lines))
    if out:
        np.savez_compressed(
            out + "_frames.npz",
            t=t,
            step=step,
            speed=speed,
            repeat=repeat,
            same=same,
            ok=ok,
            spread=spread,
            window=wi,
        )

        np.savetxt(
            out + "_windows.csv",
            np.c_[win, behind_frac, ev_w, en_w, tot],
            delimiter=",",
            fmt="%.4g",
            header="video_time_end,game_fps,repeats,skips,judged,judged_behind,behind_share,steps_over_T,"
            "energy,frames_measured",
        )


if __name__ == "__main__":
    main()

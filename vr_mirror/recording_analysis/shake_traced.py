# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
r"""Every cockpit jump of a recording with a frame trace, classified by cause from the trace.

The recording is lined up with the trace by the frame-number stamp. Per video frame the trace
gives the pose the shown image was warped from (img), the view, the slot the frame arrived for,
the edge counters, and the runtime's predicted pose for every slot. The cockpit's measured path
(shake.py) is split into the view's own path and the error of the warp pose; that error is
compared with the poses of the neighbouring slots, so each frame's true slot (the one whose pose
explains the cockpit) is known where the head moved enough to tell. A jump is then:
- the view's own motion: the smoothing, or a step of it by the hard edge clamp or the repeated-frame clip;
- a wrong-pose frame: the game was later than the plugin thought (missed lateness), the plugin
  re-timed a frame that was on time (false alarm), or it re-timed by the wrong number of slots;
- neither (the measure).

usage: python -I shake_traced.py <out folder of analyse_recording.py>
(analyse_recording.py runs it; it needs that folder's shake.npz and lineup.npz.)
Angles in degrees; jumps in px of the 1920x1080 video, as shake.py and shake_causes.py count them.
Needs a trace with 26 or more columns (zoom, edge counters, view time).
"""

import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shake import HALF, SmoothResidual
from shake_causes import LogWindows
from trace_common import Conj, Predicted, Qmul, VideoTime

P = 1 / 90
THRESHOLD = 2.0  # px: a visible jump (README.md, "How the whole-recording analysis measures")
ROLL_RADIUS = 540
EYE = dict(L=1.195, R=1.173, U=0.984, D=0.984)  # eye 0 (the OBS log)
CANT = np.array([0.0, 0.0175, 0.0, 0.9998])  # eye 0's orientation in the head
ASPECT = 16 / 9

# The view looks at the image's middle, with the mean of its half-angles on each side
# (vr_mirror\core\pipeline\pipeline_types.h ImageMiddle and GeometryFor; in older builds it
# looked along the eye's axis and was as wide as the narrower side).
_YAW = (np.arctan(EYE["R"]) - np.arctan(EYE["L"])) / 2
_PITCH = (np.arctan(EYE["U"]) - np.arctan(EYE["D"])) / 2
AIM = Qmul(
    np.array([0.0, -np.sin(_YAW / 2), 0.0, np.cos(_YAW / 2)]),
    np.array([np.sin(_PITCH / 2), 0.0, 0.0, np.cos(_PITCH / 2)]),
)
WIDE = np.tan((np.arctan(EYE["L"]) + np.arctan(EYE["R"])) / 2)
TALL = np.tan((np.arctan(EYE["U"]) + np.arctan(EYE["D"])) / 2)

# vr_mirror\core\pipeline\image_room\image_room.h kStopShare (in older builds: 5_edges kEdge)
EDGE = 0.95
LAGS = np.arange(-3, 2)  # candidate true slots, display periods from the slot the frame arrived for


# ---- quaternions (x, y, z, w): Qmul, Conj from trace_common ----
def RotVec(q):
    """Rotation vector (degrees) of unit quaternions."""
    q = q * np.where(q[..., 3:] < 0, -1, 1)
    s = np.linalg.norm(q[..., :3], axis=-1, keepdims=True)
    ang = 2 * np.arctan2(s, q[..., 3:])
    return np.degrees(np.where(s > 1e-12, q[..., :3] / np.maximum(s, 1e-12) * ang, 2 * q[..., :3]))


def Matrix(q):
    x, y, z, w = np.moveaxis(q, -1, 0)
    return np.stack(
        [
            np.stack([1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)], -1),
            np.stack([2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)], -1),
            np.stack([2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)], -1),
        ],
        -2,
    )


# ---- the plugin's edge test (vr_mirror\core\pipeline\image_room\, in older builds 5_edges\), vectorised
# ----
def Fits(image, view, zoom):
    tw = WIDE / zoom
    th = tw / ASPECT
    th, tw = (np.minimum(th, TALL / zoom), tw)
    delta = Qmul(Conj(image), view)
    rm = Matrix(Qmul(Qmul(Qmul(Conj(CANT), delta), CANT), AIM))
    ok = np.ones(image.shape[:-1], bool)
    for cx in (-1, 1):
        for cy in (-1, 1):
            v = np.stack([cx * tw, cy * th, -np.ones_like(tw)], -1)
            s = np.einsum("...ij,...j->...i", rm, v)
            tx, ty = s[..., 0] / -s[..., 2], s[..., 1] / -s[..., 2]
            k = 0.995
            ok &= (
                (s[..., 2] < -1e-4)
                & (tx >= -EYE["L"] * k)
                & (tx <= EYE["R"] * k)
                & (ty >= -EYE["D"] * k)
                & (ty <= EYE["U"] * k)
            )

    return ok


def RoomShare(raw, view, zoom):
    """How far `view` trails `raw` as a share of the room it has in that direction (1 = at the
    image's edge; the clamp holds it at 0.95)."""

    d = Qmul(Conj(raw), view)
    d = d * np.where(d[:, 3:] < 0, -1, 1)
    lag = 2 * np.arccos(np.clip(d[:, 3], -1, 1))
    sh = np.sqrt(np.maximum(0, 1 - d[:, 3] ** 2))
    axis = d[:, :3] / np.maximum(sh, 1e-9)[:, None]

    def Trail(angle):
        h = angle / 2
        return Qmul(raw, np.c_[axis * np.sin(h)[:, None], np.cos(h)])

    lo, hi = np.zeros(len(raw)), np.full(len(raw), np.radians(60))
    full = Fits(raw, Trail(hi), zoom)
    for _ in range(18):
        mid = (lo + hi) / 2
        f = Fits(raw, Trail(mid), zoom)
        lo, hi = np.where(f, mid, lo), np.where(f, hi, mid)

    room = np.where(full, np.radians(60), lo)
    return np.where(lag < 1e-5, 0, lag / np.maximum(room, 1e-9))


def Hp(x, half=15):
    """x minus its running median over +-half frames (removes the slow drift of a summed path)."""
    from numpy.lib.stride_tricks import sliding_window_view

    pad = np.pad(x, ((half, half), (0, 0)), mode="edge")
    return x - np.median(sliding_window_view(pad, 2 * half + 1, axis=0), axis=-1)


def Steps(path, px_per_deg):
    res, vel = SmoothResidual(path)
    d = np.diff(res, axis=0, prepend=np.nan)
    return np.hypot(np.hypot(d[:, 0], d[:, 1]) * px_per_deg, np.radians(d[:, 2]) * ROLL_RADIUS), vel


def main():
    import json
    import trace_ticks

    folder = sys.argv[1]
    run = json.load(open(os.path.join(folder, "run.json")))
    rows = np.load(os.path.join(folder, "lineup.npz"))["rows"]
    Run(
        os.path.join(folder, "shake.npz"),
        rows,
        trace_ticks.Load(run["trace"]),
        run,
        os.path.basename(run["video"]),
        os.path.join(folder, "traced"),
    )


def Run(shake_npz, rows, trace, rec, video_name, out):
    """rows: lineup.LineUp's tick row per video frame; trace: trace_ticks.Load's; rec: sources.Find's."""
    num, kind, S = trace["num"], trace["kind"], trace["S"]
    z = np.load(shake_npz)
    hits = int((rows >= 0).sum())
    n = len(z["res"])  # rows of shake.py: frame i -> i + 1; video frame i + 1 is row i
    vrow = rows[1 : n + 1]
    have = vrow >= 0
    print(
        f"{video_name}: {len(rows)} video frames, {hits} lined up with the trace by stamp; first "
        f"frame's tick {num[max(rows[0], 0), 1]:.2f} s (lineup.txt says how it was chosen)"
    )

    T = num[np.maximum(vrow, 0)]
    tick, serial, slot = T[:, 1], T[:, 2].astype(np.int64), T[:, 5]
    img, raw, view = T[:, 6:10], T[:, 10:14], T[:, 14:18]

    # num: 18 draw, 19 presented, 20 zoom, 21 edge_clamps, 22 slot_lead, 23 edge_planned, 24 view_time
    zoom, clamps, planned = T[:, 20], T[:, 21], T[:, 23]
    pkind = kind[np.maximum(vrow, 0)]
    prev = np.maximum(vrow - 1, 0)
    clamp_here = have & (clamps - num[prev, 21] > 0)
    planned_here = have & (planned - num[prev, 23] > 0)
    zoom0 = float(np.median(zoom[have]))
    K = (960 / 2) / (WIDE / zoom0)
    px_per_deg = K * np.pi / 180 * 2.0

    # Kind of tick, exactly: a new game frame, the same one again, or one skipped over.
    ps = np.r_[serial[0], serial[:-1]]
    repeat = have & (serial == ps)
    skip = have & (serial > ps + 1)

    # A repeated frame shown for another: the trace says which frame the view followed (traces
    # with 30 or 31 columns), except where the view followed the OBS tick's own moment: then the plugin writes the
    # tick's id there (1 << 62 | tick count, vr_mirror\obs_plugin\plugin.cpp): it followed no game frame, so
    # none is shown for another (a repeat still counts as one). Older traces only let it be guessed from the
    # two poses differing.
    by_pose = have & (np.degrees(2 * np.arccos(np.clip(np.abs(np.sum(img * raw, -1)), 0, 1))) > 0.01)
    followed = T[:, 25]
    names_frame = (followed >= 0) & (followed < 2.0**62)
    stand_in = np.where(followed < 0, by_pose, names_frame & have & (followed != T[:, 2]))

    # The view's own path on screen (T = img: the warp right) and its calibration against the video.
    hc = np.broadcast_to(Qmul(CANT, AIM), view.shape)  # the view's camera in the head
    Qv = Qmul(Qmul(Conj(hc), Conj(view)), hc)  # tracking -> screen when the warp pose is right
    Rv = Qmul(Qv[1:], Conj(Qv[:-1]))
    rv = np.r_[np.zeros((1, 3)), RotVec(Rv)]
    meas = z["omega"].astype(float)
    use = have & np.r_[False, have[:-1]] & (z["n_in"] >= 15) & np.isfinite(meas).all(1)

    # One scale per axis (the sign conventions and the px-per-degree estimate); roll barely moves
    # with the horizon kept level, so its scale is fitted on the frames where it does.
    M = np.zeros((3, 3))
    for a in range(3):
        m = use & (np.abs(rv[:, a]) > 0.02)
        for _ in range(3):
            M[a, a] = np.sum(rv[m, a] * meas[m, a]) / np.sum(rv[m, a] ** 2)
            r = np.abs(meas[:, a] - M[a, a] * rv[:, a])
            m = use & (np.abs(rv[:, a]) > 0.02) & (r < 3 * np.median(r[m]))

    print(
        "  screen rotation from the trace -> measured (pitch, yaw, roll):\n" + np.array2string(M, precision=3)
    )

    rv_view = rv @ M
    rv_view[~use] = 0
    meas_c = np.where(use[:, None], meas, 0)
    theta_meas, theta_view = np.cumsum(meas_c, 0), np.cumsum(rv_view, 0)
    step, vel = Steps(theta_meas, px_per_deg)
    step_view, _ = Steps(theta_view, px_per_deg)
    speed = np.hypot(vel[:, 0], vel[:, 1])

    # The cockpit's error on screen per frame (up to a slow drift), and what each candidate true
    # slot would make it: E = conj(h) conj(V) img conj(T) V h.
    e = Hp(theta_meas - theta_view)

    def ErrorFor(Tq):
        E = Qmul(Qmul(Qmul(Qmul(Conj(hc), Conj(view)), img), Conj(Tq)), Qmul(view, hc))
        return RotVec(E) @ M

    cands, okc = [], []
    for lag in LAGS:
        Tq, ok = Predicted(S, slot + lag * P)
        cands.append(ErrorFor(Tq))
        okc.append(ok)

    cands, okc = np.stack(cands, 1), np.stack(okc, 1)  # (n, lags, 3)

    # The plugin's own lag: the candidate nearest img.
    drawn = np.argmin(np.where(okc, np.linalg.norm(cands, axis=2), np.inf), axis=1)
    cand_hp = cands - cands[np.arange(len(cands)), drawn][:, None]  # error relative to as drawn: 0 at drawn

    # Cost per candidate, summed over the ticks that show the same game frame.
    cost = np.linalg.norm(
        (e[:, None, :] - cand_hp) * np.array([px_per_deg, px_per_deg, np.radians(1) * ROLL_RADIUS]), axis=2
    )

    cost = np.where(okc & use[:, None], cost, np.nan)
    uniq, inv = np.unique(serial, return_inverse=True)
    tot = np.zeros((len(uniq), len(LAGS)))
    np.add.at(tot, inv, np.nan_to_num(cost, nan=1e3))
    best = np.argmin(tot, axis=1)
    srt = np.sort(tot, axis=1)

    # how far the candidates differ (px)
    spread = np.linalg.norm(cands.max(1) - cands.min(1), axis=1) * px_per_deg
    sp = np.zeros(len(uniq))
    np.maximum.at(sp, inv, spread)
    cnt = np.bincount(inv, minlength=len(uniq))

    # the best slot explains the cockpit 1 px per tick better
    sure = (srt[:, 1] - srt[:, 0] > 1.0 * cnt) & (sp > 1.0)
    true_lag = np.where(sure, LAGS[best], 99)[inv]
    drawn_lag = LAGS[drawn]
    tl = np.where(true_lag == 99, drawn_lag, true_lag)
    err_now = cands[np.arange(len(cands)), np.searchsorted(LAGS, tl)] - cands[np.arange(len(cands)), drawn]

    # The cockpit's error against the true slot, and its step from the previous frame.
    err_true = -err_now  # img against truth
    d_err = np.diff(err_true, axis=0, prepend=err_true[:1])
    step_err = np.hypot(
        np.hypot(d_err[:, 0], d_err[:, 1]) * px_per_deg, np.radians(d_err[:, 2]) * ROLL_RADIUS
    )

    # Edges: the hard clamp (the trace's counter) and the repeated-frame clip (recomputed where the trace has
    # no counter).
    share_follow = np.zeros(n)
    share_shown = np.zeros(n)
    share_follow[have] = RoomShare(raw[have], view[have], zoom[have])
    share_shown[have] = RoomShare(img[have], view[have], zoom[have])
    at_edge = lambda s: np.abs(s - EDGE) < 0.002
    if num.shape[1] > 25 and (T[:, 26] >= 0).all():  # counted by traces with 30 or 31 columns
        clip_here = have & (T[:, 26] - num[prev, 26] > 0)
    else:
        clip_here = stand_in & at_edge(share_shown) & ~(at_edge(share_follow) & clamp_here)

    ok = use & np.isfinite(step)
    ev = ok & (step > THRESHOLD)
    minutes = ok.sum() / 90 / 60
    print(
        f"  {minutes:.1f} min measured; jumps over {THRESHOLD:g} px: {ev.sum()} ({ev.sum() / minutes:.1f} "
        f"per min); "
        f"step p50 {np.percentile(step[ok], 50):.2f} p99 {np.percentile(step[ok], 99):.2f} p99.9 "
        f"{np.percentile(step[ok], 99.9):.2f} px"
    )

    print(
        f"  ticks: {repeat[ok].sum()} repeated game frames, {skip[ok].sum()} after a skip, "
        f"{stand_in[ok].sum()} showing a repeated frame for the followed one"
    )

    print(
        f"  hard edge clamp acted on {clamp_here[ok].sum()} ticks ({clamp_here[ok].sum() / minutes:.2f} "
        f"per min; "
        f"its room share there: median {np.median(share_follow[clamp_here & ok]) if clamp_here[ok].any() else float('nan'):.3f}); "
        f"repeated-frame clip acted on {clip_here[ok].sum()} ticks ({clip_here[ok].sum() / minutes:.2f} per min); "
        f"the smoothing steered on {planned_here[ok].sum()} ticks"
    )

    print(
        f"  views within 1% of the clamp line without a count: "
        f"{(ok & at_edge(share_follow) & ~clamp_here).sum()}"
    )

    lags_drawn = dict(zip(*[x.tolist() for x in np.unique(drawn_lag[ok & ~repeat], return_counts=True)]))
    print(f"  game frames drawn with lag (slots from arrival): {lags_drawn}")
    newf = ok & ~repeat
    sure_f = newf & (true_lag != 99)
    print(
        f"  true slot known (head moved enough to tell) for {sure_f.sum()} of {newf.sum()} game frames; of "
        f"those "
        f"drawn right {np.mean(tl[sure_f] == drawn_lag[sure_f]) * 100:.1f}%, missed lateness "
        f"{np.sum(sure_f & (drawn_lag == 0) & (tl < 0))}, false alarms {np.sum(sure_f & (drawn_lag < 0) & (tl == 0))}, "
        f"other wrong lag "
        f"{np.sum(sure_f & (tl != drawn_lag) & ~((drawn_lag == 0) & (tl < 0)) & ~((drawn_lag < 0) & (tl == 0)))}"
    )

    # Cause of each jump.
    wrong_now = tl != drawn_lag
    wrong_prev = np.r_[False, wrong_now[:-1]]

    def WrongKind(k):
        j = k if wrong_now[k] else k - 1
        if drawn_lag[j] == 0 and tl[j] < 0:
            return "wrong pose: game late, not caught"
        if drawn_lag[j] < 0 and tl[j] == 0:
            return "wrong pose: false alarm (frame was on time)"
        return "wrong pose: re-timed by the wrong number of slots"

    cause = np.full(n, "", dtype=object)
    for k in np.where(ev)[0]:
        if step_err[k] >= max(1.0, step_view[k]) and (wrong_now[k] or wrong_prev[k]):
            cause[k] = WrongKind(k)
        elif step_view[k] >= 1.0:
            if clamp_here[k] or clamp_here[k - 1]:
                cause[k] = "view: hard edge clamp"
            elif clip_here[k] or clip_here[k - 1]:
                cause[k] = "view: repeated-frame clip"
            else:
                cause[k] = "view: smoothing (the view itself)"
        elif step_err[k] >= 1.0:
            cause[k] = (
                "pose error from a repeated frame" if (repeat[k] or stand_in[k]) else "pose error (other)"
            )
        else:
            cause[k] = "not explained by the trace"

    e2 = np.where(ok, np.maximum(step - 0.3, 0) ** 2, 0)
    names = sorted(set(cause[ev]), key=lambda c: -np.sum(ev & (cause == c)))
    print(
        f"  {'cause':52s} {'jumps':>6s} {'per min':>8s} {'share':>6s} {'energy':>7s}  on "
        f"repeat/skip/repeated-for-followed ticks"
    )

    for c in names:
        m = ev & (cause == c)
        print(
            f"  {c:52s} {m.sum():6d} {m.sum() / minutes:8.2f} {100 * m.sum() / ev.sum():5.1f}% "
            f"{100 * e2[m].sum() / e2[ev].sum():6.1f}%  "
            f"{(m & repeat).sum()}/{(m & skip).sum()}/{(m & stand_in).sum()}"
        )

    # The unexplained ones: is the trace's whole predicted step there too small?
    m = ev & (cause == "not explained by the trace")
    if m.any():
        print(
            f"    not explained: measured p50 {np.median(step[m]):.1f} px, the view's own step p50 "
            f"{np.median(step_view[m]):.2f}, "
            f"pose-error step p50 {np.median(step_err[m]):.2f}; fast view (>= 0.3 deg/frame) "
            f"{np.mean(speed[m] >= 0.3) * 100:.0f}%"
        )

    # The log's 10 s windows.
    start = rec["start"]
    win = LogWindows(rec["log"], start, rec["stop"] or start + n / 90 + 1)
    t = np.arange(n) / 90 + 1 / 90
    wi = np.minimum(np.searchsorted(win[:, 0], t), len(win) - 1)
    evw = np.bincount(wi[ok], weights=ev[ok], minlength=len(win))
    totw = np.bincount(wi[ok], minlength=len(win))
    share = np.where(win[:, 4] > 0, win[:, 5] / np.maximum(win[:, 4], 1), 0)
    print(
        f"  log windows: game fps median {np.median(win[:, 1]):.1f} (min {win[:, 1].min():.1f}), repeats "
        f"per 10 s median "
        f"{np.median(win[:, 2]):.0f}, total {win[:, 2].sum():.0f}; skips {win[:, 3].sum():.0f}; judged {win[:, 4].sum():.0f}, "
        f"rendered for an earlier slot {win[:, 5].sum():.0f}"
    )

    for lo, hi in ((0, 85), (85, 89), (89, 91)):
        mm = (totw > 850) & (win[:, 1] >= lo) & (win[:, 1] < hi)
        if mm.any():
            print(
                f"    game {lo}-{hi} fps: {mm.sum():3d} windows, jumps {evw[mm].mean() * 6:6.1f} per min, "
                f"repeats {np.median(win[mm, 2]):.0f} per 10 s, judged behind {np.median(share[mm]):.0%}"
            )

    # Worst moments, one per second.
    order = np.argsort(np.where(ev, step, 0))[::-1]
    shown = []
    print("  worst moments (one per second):")
    for k in order:
        if not ev[k] or any(abs(k - s) < 90 for s in shown):
            continue
        shown.append(k)
        print(
            f"    {VideoTime(t[k])}  {step[k]:5.1f} px ({ev[max(0, k - 45) : k + 45].sum():2d} jumps in that "
            f"second): {cause[k]}; "
            f"view's step {step_view[k]:.1f}, pose-error step {step_err[k]:.1f}; drawn lag {drawn_lag[k]:+d}, true "
            f"{'?' if true_lag[k] == 99 else f'{true_lag[k]:+d}'}; {'repeat' if repeat[k] else 'skip' if skip[k] else 'new'}"
            f"{', repeated for the followed frame' if stand_in[k] else ''}; view {speed[k]:.2f} deg/frame; "
            f"game {win[wi[k], 1]:.0f} fps"
        )

        if len(shown) >= 20:
            break

    np.savez_compressed(
        out + "_frames.npz",
        t=t,
        step=step,
        step_view=step_view,
        step_err=step_err,
        cause=cause.astype(str),
        repeat=repeat,
        skip=skip,
        stand_in=stand_in,
        clamp=clamp_here,
        clip=clip_here,
        planned=planned_here,
        drawn_lag=drawn_lag,
        true_lag=true_lag,
        share_follow=share_follow,
        share_shown=share_shown,
        ok=ok,
        speed=speed,
        kind=pkind,
    )


if __name__ == "__main__":
    main()

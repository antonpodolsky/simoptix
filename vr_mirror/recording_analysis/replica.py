# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
r"""The plugin's slot check (vr_mirror\core\pipeline\2_drawn_pose\slot_check_drawn_pose_lag) replayed offline on a recording's real pictures:
each game frame's 64x32 thumbnail is rebuilt from the recorded frame (the plugin's warp inverted:
thumbnail texel -> eye ray -> output pixel, with the pose the frame was drawn with), then the
matcher's logic runs on them (both thumbnails re-projected halfway between candidate poses, the
30% of the texels with the least detail left out, error trimmed to the better half of the rest,
normalized by rotation sensitivity, forward filter with the change penalties). Shows what the matcher would decide on every frame (without the timing gate, or
with it: --gate), and why, from the frames up to each and in hindsight (on the cheapest path through
the frames after it too, as held frames get it with Smooth and Smoothest). Python mirror of
vr_mirror\core\pipeline\2_drawn_pose\slot_check_drawn_pose_lag.cpp: keep the two in step.

usage: python -I replica.py <OBS log> <recording .mp4> <start mm:ss> <secs> [options]
  --show A-B      print the evidence frame by frame for video seconds A to B into the window
  --maps SERIAL   save pictures of that game frame's thumbnails and per-texel errors (8x, PPM) to
                  the cache folder: the previous and this thumbnail re-projected for lag 0 and for
                  the lag pair the matcher chose, and the error of each
  --change, --return  the filter's penalties (deg^2; default 0.03 and 0.004 as in core)
  --thumbs REPLAY an e2e replay (.csv): its exported thumbnails in place of rebuilt ones; --fill
                  counts the texels the recording didn't cover too, as the tool in e2e does
  --zoom          the source's zoom setting (default 1.3)
  --mask          areas of the 960x540 output to leave out of thumbnails: OBS overlays that are not
                  in the game image (default: the frame stamp and the G-meter at the bottom right)
"""

import argparse
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from trace_common import (
    CACHE,
    P,
    T_IMG,
    T_SERIAL,
    T_VIEW,
    F_ARRIVAL,
    F_CURRENT,
    F_SERIAL,
    F_SLOT,
    T_SLOT,
    Conj,
    DrawnSlotColumn,
    ExtractVideo,
    FrameLag,
    FirstTick,
    LineUp,
    LoadTrace,
    Predicted,
    Qmul,
    ReadObsLog,
    Recording,
    Seconds,
    VideoTime,
)


import thumbs
from thumbs import CANT, H, THD, TWD, W, Mat, ii, jj, sx, sy, vl, vu


def Halfway(a, b):
    b = b if np.dot(a, b) >= 0 else -b
    return (a + b) / np.linalg.norm(a + b)


def Resample(thumb, valid, pose_from, pose_to):
    """Thumbnail rendered at pose_from seen from pose_to: per texel value and validity (bilinear)."""
    m = Mat(Qmul(Conj(Qmul(pose_from, CANT)), Qmul(pose_to, CANT)))
    x = -vl + (ii + 0.5) * sx
    y = vu - (jj + 0.5) * sy
    v = np.stack([x, y, -np.ones_like(x)], -1) @ m.T
    fx = (v[..., 0] / -v[..., 2] + vl) / sx - 0.5
    fy = (vu - v[..., 1] / -v[..., 2]) / sy - 0.5
    ok = (v[..., 2] < -1e-4) & (fx >= 0) & (fy >= 0) & (fx <= TWD - 1.001) & (fy <= THD - 1.001)
    fxc, fyc = np.clip(fx, 0, TWD - 1.001), np.clip(fy, 0, THD - 1.001)
    x0, y0 = fxc.astype(int), fyc.astype(int)
    ax, ay = (fxc - x0)[..., None], (fyc - y0)[..., None]
    ok &= valid[y0, x0] & valid[y0, x0 + 1] & valid[y0 + 1, x0] & valid[y0 + 1, x0 + 1]
    s = (thumb[y0, x0] * (1 - ax) + thumb[y0, x0 + 1] * ax) * (1 - ay) + (
        thumb[y0 + 1, x0] * (1 - ax) + thumb[y0 + 1, x0 + 1] * ax
    ) * ay

    return s, ok


def Errors(prev, pvalid, prev_pose, cur, cvalid, cur_pose, texture):
    """Per-texel squared difference of both thumbnails re-projected halfway (NaN where either is
    missing) and the trimmed mean ReprojectionError takes from it (-1: too little overlap). With
    --texture F, the fraction F of the overlapping texels with the least detail (local gradient of
    both re-projected thumbnails) is left out before the trimming."""

    mid = Halfway(prev_pose, cur_pose)
    a, oka = Resample(prev, pvalid, prev_pose, mid)
    b, okb = Resample(cur, cvalid, cur_pose, mid)
    ok = oka & okb
    e = np.where(ok, ((a - b) ** 2).sum(-1), np.nan)
    if ok.sum() * 2 < TWD * THD:
        return e, -1.0
    if texture > 0:
        g = np.zeros(ok.shape)
        for img in (a, b):
            gy, gx = np.gradient(img.sum(-1))
            g = np.maximum(g, np.hypot(gx, gy))

        cut = np.quantile(g[ok], texture)
        e = np.where(g >= cut, e, np.nan)

    good = np.sort(e[~np.isnan(e)])
    return e, float(good[: max(1, len(good) // 2)].mean())


def Sensitivity(thumb, valid, pose, texture):
    h = np.radians(0.25) / 2
    a = Errors(thumb, valid, Qmul(pose, np.array([0, np.sin(h), 0, np.cos(h)])), thumb, valid, pose, texture)[
        1
    ]

    b = Errors(thumb, valid, Qmul(pose, np.array([np.sin(h), 0, 0, np.cos(h)])), thumb, valid, pose, texture)[
        1
    ]

    return 0.5 * (a + b) / 0.0625 if a >= 0 and b >= 0 else 0.0


class TimingGate:
    r"""vr_mirror\core\pipeline\2_drawn_pose\slot_check_drawn_pose_lag.h TimingGate: open while the slot lead (modulo a period) is more than 3 ms off
    its usual value over the last 5 frames, and 1 s after."""

    def __init__(self):
        self.base, self.count, self.devs, self.last_sign = 0.0, 0, [], -1e9

    def Update(self, arrival, lead, period):
        sign = False
        if lead > 0:
            dev = math.remainder(lead - self.base, period) if self.count else 0.0
            self.devs = (self.devs + [dev])[-5:]
            if self.count >= 20 and len(self.devs) == 5 and abs(sorted(self.devs)[2]) > 0.003:
                sign = True
            elif self.count < 20 or abs(dev) < 0.003:
                self.count = min(self.count + 1, 1000)
                self.base += (lead - self.base if self.count == 1 else dev) * max(0.02, 1.0 / self.count)

        if sign:
            self.last_sign = arrival
        return arrival - self.last_sign <= 1.0


def SavePpm(path, img):
    img = np.clip(np.repeat(np.repeat(img, 8, 0), 8, 1) * 255, 0, 255).astype(np.uint8)
    if img.ndim == 2:
        img = np.stack([img] * 3, -1)
    with open(path, "wb") as f:
        f.write(f"P6 {img.shape[1]} {img.shape[0]} 255\n".encode())
        f.write(img.tobytes())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("video")
    ap.add_argument("start")
    ap.add_argument("secs", type=float)
    ap.add_argument("--show", default="")
    ap.add_argument("--maps", type=int, default=0)
    ap.add_argument("--change", type=float, default=0.03)
    ap.add_argument("--return", dest="ret", type=float, default=0.004)
    ap.add_argument("--zoom", type=float, default=1.3)
    ap.add_argument("--mask", default="0,0,135,12;790,375,960,540")
    ap.add_argument(
        "--texture",
        type=float,
        default=0.3,
        help="leave out this fraction of the texels with the least detail before trimming "
        "(core: 0.3; 0 = the matching before 2026-10-24)",
    )

    ap.add_argument(
        "--gate", action="store_true", help="with the timing gate, as the plugin runs (default: without)"
    )

    ap.add_argument(
        "--truth", default="", help="true lags by serial ('first-last:lag,...'; others 0), for scoring"
    )

    ap.add_argument("--clip-of", default=None, help="the video is a clip cut from this recording (file name)")
    ap.add_argument(
        "--thumbs",
        default="",
        help="an e2e replay file (.csv): use its exported thumbnails (<replay>.thumbs.png, "
        "what the e2e fake shows) instead of rebuilding them from the "
        "video",
    )

    ap.add_argument(
        "--fill",
        action="store_true",
        help="with --thumbs: use the texels the recording didn't cover too (the "
        "export's smooth fill), as the tool in e2e does",
    )

    ap.add_argument("--clip-start", default="0", help="where the clip starts in that recording (mm:ss)")
    args = ap.parse_args()
    masks = thumbs.Masks(args.mask)

    log = ReadObsLog(args.log)
    rec_start, _ = Recording(log, args.video, args.clip_of, Seconds(args.clip_start))
    t0 = FirstTick(log["trace"])
    start = Seconds(args.start)
    approx = t0 + rec_start + start - log["trace_start"]
    raw, stamps = ExtractVideo(args.video, start, args.secs, W, H, rgb=True)
    frames = np.memmap(raw, np.uint8, "r").reshape(-1, H, W, 3)
    ticks, S, F = LoadTrace(log["trace"], approx - 60, approx + args.secs + 60)
    drawn_slot = DrawnSlotColumn(log["trace"])  # the trace's own drawn slot (old traces), else by pose
    rows = LineUp(stamps, ticks, approx)
    if rows is None:
        raise SystemExit("the video's frame stamps don't match the trace here")
    seg = ticks[rows]
    show = [float(x) for x in args.show.split("-")] if args.show else None
    exported = {}  # --thumbs: serial -> (tile as displayed 0..1, covered by the recording)
    if args.thumbs:
        import zlib

        with open(os.path.splitext(args.thumbs)[0] + ".thumbs.png", "rb") as f:
            png = f.read()

        # Only export_thumbs.py's own PNGs: RGBA 8-bit, one IDAT, filter 0 on every row.
        w, h = int.from_bytes(png[16:20], "big"), int.from_bytes(png[20:24], "big")
        n = int.from_bytes(png[33:37], "big")
        px = (
            np.frombuffer(zlib.decompress(png[41 : 41 + n]), np.uint8)
            .reshape(h, 1 + w * 4)[:, 1:]
            .reshape(h, w, 4)
        )

        order = [int(l.split(",")[1]) for l in open(args.thumbs) if l.startswith("F,")]
        for i, s in enumerate(order):
            tile = px[i * 32 : (i + 1) * 32]
            exported[s] = (
                tile[..., :3] / 255.0,
                np.ones((32, 64), bool) if args.fill else tile[..., 3] == 255,
            )

    fidx = {int(s): i for i, s in enumerate(F[:, F_SERIAL])}
    serial = seg[:, T_SERIAL].astype(np.int64)

    gate, open_at = TimingGate(), {}  # the gate runs over every frame's arrival, shown or not
    for r in F:
        open_at[int(r[F_SERIAL])] = gate.Update(r[F_ARRIVAL], r[F_CURRENT] - r[F_ARRIVAL], P)

    fresh = np.array([0, args.change, args.change, args.change])
    cost = fresh.copy()
    prev, out = None, []

    # LagFilter's hindsight: per update, the previous frame's lag on the cheapest path to each lag,
    # whether it started a run, and its cheapest lag; per frame, the update it was decided by or
    # follows (-1: none, lag 0).
    updates, started, last_update = [], False, -1
    for k in range(len(seg)):
        if k and serial[k] == serial[k - 1]:
            continue
        s = int(serial[k])
        if s not in fidx:
            continue
        tslot = F[fidx[s], F_SLOT]
        drawn = FrameLag(seg[k], tslot, S, drawn_slot)
        if args.thumbs and s not in exported:
            continue
        thumb, valid = (
            exported[s]
            if args.thumbs
            else thumbs.Thumbnail(frames[k], seg[k, T_IMG], seg[k, T_VIEW], args.zoom, masks)
        )

        poses, ok = Predicted(S, tslot - np.arange(4) * P)
        step, sens = None, 0.0
        if args.gate and not open_at.get(s, False):
            cost = fresh.copy()  # the slot check: timing undisturbed, lag 0, start over
            started, last_update = False, -1
        elif prev is not None and s == prev["serial"] + 1 and ok.all() and prev["ok"].all():
            sens = Sensitivity(thumb, valid, poses[0], args.texture)
            if sens > 0:
                step = (
                    np.array(
                        [
                            [
                                Errors(
                                    prev["thumb"],
                                    prev["valid"],
                                    prev["poses"][a],
                                    thumb,
                                    valid,
                                    poses[b],
                                    args.texture,
                                )[1]
                                for b in range(4)
                            ]
                            for a in range(4)
                        ]
                    )
                    / sens
                )

                if (step >= 0).all():
                    pen = np.array(
                        [
                            [0 if a == b else args.ret if b == 0 else args.change for b in range(4)]
                            for a in range(4)
                        ]
                    )
                    total = cost[:, None] + step + pen
                    cost = total.min(0)
                    cost -= cost.min()
                    updates.append((total.argmin(0), not started, int(np.argmin(cost))))
                    started, last_update = True, len(updates) - 1
                else:
                    step = None

        lag = -int(np.argmin(cost))
        rot = (
            np.degrees(2 * np.arccos(min(1.0, abs(np.dot(poses[0], prev["poses"][0])))))
            if prev is not None
            else 0
        )

        out.append((s, k, drawn, lag, sens, last_update))
        t = k / 90
        if show and show[0] <= t <= show[1]:
            ev = (
                (
                    " | step 00 11 22 33: "
                    + " ".join(f"{step[i, i]:.3f}" for i in range(4))
                    + f"  cheapest {np.unravel_index(np.argmin(step), step.shape)}"
                )
                if step is not None
                else ""
            )

            print(
                f"  video {VideoTime(start + t)} serial {s} head {rot:.2f} deg/frame sens {sens:.2e} drawn "
                f"{drawn:+d} replica {lag:+d}"
                f"  cost {' '.join(f'{c:.3f}' for c in cost)}{ev}"
            )

        if args.maps == s and prev is not None and step is not None:
            a, b = np.unravel_index(np.argmin(step), step.shape)
            for name, (pa, pb) in (("lag0", (0, 0)), (f"chosen{a}{b}", (a, b))):
                e, m = Errors(
                    prev["thumb"], prev["valid"], prev["poses"][pa], thumb, valid, poses[pb], args.texture
                )

                lim = np.nanpercentile(e, 50)
                SavePpm(
                    os.path.join(CACHE, f"map_{s}_{name}_error.ppm"),
                    np.nan_to_num(e / (4 * max(lim, 1e-6)), nan=1),
                )

                SavePpm(
                    os.path.join(CACHE, f"map_{s}_{name}_kept.ppm"),
                    np.nan_to_num((e <= lim).astype(float), nan=0.5),
                )

            SavePpm(os.path.join(CACHE, f"map_{s}_prev.ppm"), prev["thumb"])
            SavePpm(os.path.join(CACHE, f"map_{s}_cur.ppm"), thumb)
            print(f"  maps of serial {s} saved to {CACHE} (map_{s}_*.ppm); chosen pair {a}{b}")

        prev = dict(serial=s, thumb=thumb, valid=valid, poses=poses, ok=ok)

    # Each run traced back from its last update's cheapest lag (core re-traces the current run each
    # Update; earlier runs are final).
    hind, b = [0] * len(updates), 0
    for i in range(len(updates) - 1, -1, -1):
        b = updates[i][2] if i == len(updates) - 1 or updates[i + 1][1] else int(updates[i + 1][0][b])
        hind[i] = b

    hl = np.array([-hind[r[5]] if r[5] >= 0 else r[3] for r in out])
    o = np.array([r[:5] for r in out])
    sv = o[o[:, 4] > 0, 4]
    print(
        f"rotation sensitivity (error per deg^2): median {np.median(sv):.2e}, 10% "
        f"{np.percentile(sv, 10):.2e}, "
        f"90% {np.percentile(sv, 90):.2e}"
    )

    truth = []
    for part in filter(None, args.truth.split(",")):
        span, lag = part.split(":")
        a, b = (span.split("-") + [span])[:2]
        truth.append((int(a), int(b), int(lag)))

    true = np.array([next((l for a, b, l in truth if a <= s <= b), 0) for s in o[:, 0].astype(int)])
    late = true != 0
    print(
        f"against the true lags: on-time frames re-timed {int((o[~late, 3] != 0).sum())} of "
        f"{int((~late).sum())}, "
        f"late frames right {int((o[late, 3] == true[late]).sum())} of {int(late.sum())}"
    )

    print(
        f"  in hindsight: on-time frames re-timed {int((hl[~late] != 0).sum())} of {int((~late).sum())}, "
        f"late frames right {int((hl[late] == true[late]).sum())} of {int(late.sum())}"
    )

    print(
        f"{len(o)} game frames; plugin drew "
        f"{dict(zip(*[x.tolist() for x in np.unique(o[:, 2], return_counts=True)]))}, "
        f"replica {'with' if args.gate else 'without'} the gate "
        f"{dict(zip(*[x.tolist() for x in np.unique(o[:, 3], return_counts=True)]))}"
    )


if __name__ == "__main__":
    main()

# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
r"""The real thumbnails of an e2e replay's game frames, rebuilt from the session's recording, for the
fake runtime to show (vr_mirror\e2e\README.md, "Real pictures in replays"): one 64x32 tile per F row of the
replay file, in its order, in a PNG next to it (<replay>.thumbs.png, 64 wide, 32 rows per frame).
RGB is the displayed colour of the linear average of each texel's samples, so a flat block of it
gives the tool exactly that texel. Alpha 255 where the recording covers the texel; 128 where it
doesn't (outside the recorded crop, under OBS overlays) and a smooth extension of the covered
texels' colours was filled in. A frame the recording never showed takes its neighbour's tile
(counted).

usage: python -I export_thumbs.py <OBS log> <recording or clip .mp4> <replay.csv>
           [--clip-of "<recording>.mp4" --clip-start mm:ss] [--zoom 1.3] [--mask "x0,y0,x1,y1;..."]
"""

import argparse
import os
import struct
import sys
import zlib

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import thumbs
from trace_common import (
    T_IMG,
    T_SERIAL,
    T_TICK,
    T_VIEW,
    ExtractVideo,
    FirstTick,
    LineUp,
    ReadObsLog,
    Recording,
    Seconds,
    TicksForSerials,
)


def Chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("video")
    ap.add_argument("replay")
    ap.add_argument("--clip-of", default=None)
    ap.add_argument("--clip-start", default="0")
    ap.add_argument("--zoom", type=float, default=1.3)
    ap.add_argument("--mask", default=thumbs.MASKS)
    args = ap.parse_args()

    serials = [int(l.split(",")[1]) for l in open(args.replay) if l.startswith("F,")]
    log = ReadObsLog(args.log)
    rec_start, _ = Recording(log, args.video, args.clip_of, Seconds(args.clip_start))
    t0 = FirstTick(log["trace"])

    # ~30 s: the log's time is rough
    ticks = TicksForSerials(log["trace"], min(serials), max(serials), margin=3000)
    shown = ticks[np.isin(ticks[:, T_SERIAL].astype(np.int64), serials)]
    video_at = lambda tick: tick - t0 + log["trace_start"] - rec_start  # roughly; the stamps decide
    start = max(0.0, video_at(shown[0, T_TICK]) - 1.5)
    secs = video_at(shown[-1, T_TICK]) + 1.5 - start
    raw, stamps = ExtractVideo(args.video, start, secs, thumbs.W, thumbs.H, rgb=True)
    frames = np.memmap(raw, np.uint8, "r").reshape(-1, thumbs.H, thumbs.W, 3)
    rows = LineUp(stamps, ticks, ticks[0, T_TICK] + (start - video_at(ticks[0, T_TICK])))
    if rows is None:
        raise SystemExit("the video's frame stamps don't match the trace here")
    first_frame = {}
    for k, row in enumerate(rows):
        first_frame.setdefault(int(ticks[row, T_SERIAL]), k)

    masks = thumbs.Masks(args.mask)
    tiles, covered, missing = [], [], 0
    for s in serials:
        k = first_frame.get(s)
        if k is None:
            missing += 1
            tiles.append(None)
            covered.append(None)
            continue

        row = rows[k]
        t, ok = thumbs.Thumbnail(
            frames[k], ticks[row, T_IMG], ticks[row, T_VIEW], args.zoom, masks, linear=True
        )

        tiles.append(t)
        covered.append(ok)

    for i in range(len(tiles)):  # frames the recording never showed: the nearest shown neighbour's tile
        if tiles[i] is None:
            j = min((j for j in range(len(tiles)) if tiles[j] is not None), key=lambda j: abs(j - i))
            tiles[i], covered[i] = tiles[j], covered[j]

    out = np.zeros((len(tiles) * thumbs.THD, thumbs.TWD, 4), np.uint8)
    coverage = []
    for i, (t, ok) in enumerate(zip(tiles, covered)):
        t, filled = t.copy(), ok.copy()
        coverage.append(ok.mean())
        while not filled.all():  # copy in the nearest covered colour, one texel ring at a time
            grow = filled.copy()
            for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
                src = np.roll(filled, (dy, dx), (0, 1))
                if dy == 1:
                    src[0] = False
                if dy == -1:
                    src[-1] = False
                if dx == 1:
                    src[:, 0] = False
                if dx == -1:
                    src[:, -1] = False
                take = src & ~grow
                t[take] = np.roll(t, (dy, dx), (0, 1))[take]
                grow |= take

            if (grow == filled).all():
                break  # nothing covered at all: leave as is
            filled = grow

        # Then smooth what was copied in (covered texels fixed): copying alone dragged edge colours
        # along in straight streaks, which wouldn't move with the head like real content. Smooth and
        # nearly featureless instead, which the matching's detail rule mostly leaves out.
        for _ in range(60):
            p = np.pad(t, ((1, 1), (1, 1), (0, 0)), mode="edge")
            avg = (p[:-2, 1:-1] + p[2:, 1:-1] + p[1:-1, :-2] + p[1:-1, 2:]) / 4
            t = np.where(ok[..., None], t, avg)

        out[i * thumbs.THD : (i + 1) * thumbs.THD, :, :3] = np.clip(np.round(t * 255), 0, 255)
        out[i * thumbs.THD : (i + 1) * thumbs.THD, :, 3] = np.where(ok, 255, 128)

    path = os.path.splitext(args.replay)[0] + ".thumbs.png"
    h, w = out.shape[:2]
    data = b"".join(b"\0" + out[y].tobytes() for y in range(h))

    with open(path, "wb") as f:
        f.write(
            b"\x89PNG\r\n\x1a\n"
            + Chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + Chunk(b"IDAT", zlib.compress(data, 9))
            + Chunk(b"IEND", b"")
        )

    print(
        f"{path}: {len(tiles)} frames ({missing} not in the recording, neighbour's tile), "
        f"recorded coverage of the matching's area {np.mean(coverage) * 100:.0f}%, "
        f"{os.path.getsize(path) / 1e6:.1f} MB"
    )


if __name__ == "__main__":
    main()

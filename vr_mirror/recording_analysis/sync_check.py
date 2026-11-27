# Copyright (C) 2026 Anton Podolsky
# SPDX-License-Identifier: GPL-2.0-or-later
"""How well the G-meter and the game's sound line up with the view in a recording of LMU.

LMU's HUD dial is drawn into the game's image, so it shows what the view shows: its pedal arc (green
throttle, red brake), its gear number and its RPM arc. This script follows the dial through a stretch of
the recording and sets it against the G-meter's pedal bars and against the engine's sound:
- each throttle lift and press, and each braking, in the G-meter against the HUD
- each gear change on the HUD against the engine's pitch drop in the sound (upshifts)
"Later" is later in the video than the view shows it, and negative is earlier.

  python -I sync_check.py "<recording>.mp4" --from 85 --seconds 90 --hud 2775,1410@300 --hud-radius 173
      --gmeter 3537,1872,133 [--search 350] [--out <folder>]

--hud: the dial's centre in the video's pixels at a time when it is in view (s); --hud-radius: the radius
of its white RPM arc there. --gmeter: the G-meter's centre and the radius of the groove its pedal bars
fill, in the video's pixels. --search: how far the dial may move from where it was (pixels); the video is
read only around the dial and the G-meter. ffmpeg: the one FFMPEG names, else the one on PATH.
"""

import argparse
import json
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from trace_common import FFMPEG  # noqa: E402

RATE = 48000

# The HUD dial's parts, as fractions of its outer radius from its centre.
TEMPLATE = (
    -1.3,
    0.0,
    0.6,
    0.7,
)  # x0, y0, x1, y1: its lower band and the tyre icons beside it, which hold still
PEDAL_ARC = (-0.99, -0.78, -0.66, 0.73)
DIGIT = (-0.37, -0.92, 0.37, -0.22)
RPM_RING = (0.9, 1.12)
# The G-meter's bars: around the groove's radius.
GROOVE = (0.92, 1.08)


def colour_masks(img):
    r, g, b = (img[..., i].astype(np.int16) for i in range(3))
    green = (g > 110) & (g - r > 50) & (g - b > 30)
    red = (r > 110) & (r - g > 60) & (r - b > 40)
    return green, red


def grey(img):
    return img[..., 0] * 0.299 + img[..., 1] * 0.587 + img[..., 2] * 0.114


def half(image):
    h, w = image.shape[0] // 2 * 2, image.shape[1] // 2 * 2
    return image[:h, :w].reshape(h // 2, 2, w // 2, 2).mean(axis=(1, 3))


def ncc_best(image, template):
    """The place (x, y) in `image` where `template` fits best, and how well (normalised correlation)."""
    th, tw = template.shape
    ih, iw = image.shape
    t = template - template.mean()
    t /= np.sqrt((t**2).sum()) + 1e-9
    fh, fw = ih + th, iw + tw
    corr = np.fft.irfft2(np.fft.rfft2(image, (fh, fw)) * np.fft.rfft2(t[::-1, ::-1], (fh, fw)), (fh, fw))
    corr = corr[th - 1 : ih, tw - 1 : iw]

    def box(c):
        return c[th:, tw:] - c[:-th, tw:] - c[th:, :-tw] + c[:-th, :-tw]

    s1 = box(np.pad(np.cumsum(np.cumsum(image, 0), 1), ((1, 0), (1, 0))))
    s2 = box(np.pad(np.cumsum(np.cumsum(image**2, 0), 1), ((1, 0), (1, 0))))
    score = corr / np.sqrt(np.maximum(s2 - s1**2 / (th * tw), 1e-6))
    y, x = np.unravel_index(np.argmax(score), score.shape)
    return int(x), int(y), float(score[y, x])


def video_size(video):
    out = subprocess.run([FFMPEG, "-hide_banner", "-i", video], capture_output=True, text=True).stderr
    for line in out.splitlines():
        if "Video:" in line:
            for part in line.replace(",", " ").split():
                if "x" in part and part.split("x")[0].isdigit() and part.split("x")[1].isdigit():
                    return int(part.split("x")[0]), int(part.split("x")[1])
    raise SystemExit(f"no video size in {video}")


def frames(video, start, seconds, crop):
    """Each frame of the stretch, cropped to `crop` (x, y, width, height), as an RGB array."""
    x, y, width, height = crop
    cmd = [FFMPEG, "-v", "error", "-ss", str(start), "-t", str(seconds), "-i", video, "-an"]
    cmd += ["-vf", f"crop={width}:{height}:{x}:{y}", "-f", "rawvideo", "-pix_fmt", "rgb24", "-"]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, bufsize=width * height * 3 * 2)
    size = width * height * 3
    while True:
        buf = proc.stdout.read(size)
        if len(buf) < size:
            break
        yield np.frombuffer(buf, np.uint8).reshape(height, width, 3)
    proc.wait()


def frame_at(video, t, crop):
    return next(frames(video, t, 0.05, crop))


def crop_box(args, width, height):
    """The part of the video read: around the dial as far as it may move, and the G-meter."""
    hx, hy, _ = args.hud
    reach = args.search + 1.4 * args.hud_radius
    gx, gy, gr = args.gmeter
    x0 = max(0, int(min(hx - reach, gx - 1.2 * gr)) // 2 * 2)
    y0 = max(0, int(min(hy - reach, gy - 1.2 * gr)) // 2 * 2)
    x1 = min(width, int(max(hx + reach, gx + 1.2 * gr)) // 2 * 2)
    y1 = min(height, int(max(hy + reach, gy + 1.2 * gr)) // 2 * 2)
    return x0, y0, x1 - x0, y1 - y0


def ring_mask(radius_in, radius_out, size):
    yy, xx = np.mgrid[-size : size + 1, -size : size + 1]
    r = np.hypot(xx, yy)
    return (r >= radius_in) & (r <= radius_out)


def read_frames(args, width, height, fps):
    """Per frame: [video time, HUD found (0/1), HUD throttle, HUD brake, RPM ring, G-meter throttle,
    G-meter brake], and the HUD's gear number as a small grey patch."""
    crop = crop_box(args, width, height)
    width, height = crop[2], crop[3]
    hx, hy, at = args.hud[0] - crop[0], args.hud[1] - crop[1], args.hud[2]
    radius = args.hud_radius
    gx, gy, gsize = args.gmeter[0] - crop[0], args.gmeter[1] - crop[1], args.gmeter[2]

    def box(fractions, cx, cy):
        x0, y0, x1, y1 = fractions
        return int(cx + x0 * radius), int(cy + y0 * radius), int(cx + x1 * radius), int(cy + y1 * radius)

    reference = half(grey(frame_at(args.video, at, crop).astype(np.float32)))
    tx0, ty0, tx1, ty1 = (v // 2 for v in box(TEMPLATE, hx, hy))
    template = reference[ty0:ty1, tx0:tx1]
    offset = (hx / 2 - tx0, hy / 2 - ty0)  # the dial's centre from the template's corner, at half size

    ring_size = int(RPM_RING[1] * radius) + 1
    ring = ring_mask(RPM_RING[0] * radius, RPM_RING[1] * radius, ring_size)
    groove = ring_mask(GROOVE[0] * gsize, GROOVE[1] * gsize, int(GROOVE[1] * gsize) + 1)
    gsize_px = groove.shape[0] // 2
    columns = np.arange(-gsize_px, gsize_px + 1)[None, :]
    right, left = groove & (columns > 0), groove & (columns < 0)

    rows, digits = [], []
    for k, img in enumerate(frames(args.video, args.start, args.seconds, crop)):
        t = args.start + k / fps

        g = img[int(gy) - gsize_px : int(gy) + gsize_px + 1, int(gx) - gsize_px : int(gx) + gsize_px + 1]
        green, red = colour_masks(g)
        gm_throttle, gm_brake = int((green & right).sum()), int((red & left).sum())

        x, y, score = ncc_best(half(grey(img.astype(np.float32))), template)
        cx, cy = 2 * (x + offset[0]), 2 * (y + offset[1])
        found = score >= 0.6 and ring_size < cx < width - ring_size and ring_size < cy < height - ring_size
        hud = [0, 0, 0]
        digit = np.zeros((int(0.7 * radius), int(0.74 * radius)), np.uint8)
        if found:
            ax0, ay0, ax1, ay1 = box(PEDAL_ARC, cx, cy)
            arc_green, arc_red = colour_masks(img[ay0:ay1, ax0:ax1])
            around = img[
                int(cy) - ring_size : int(cy) + ring_size + 1, int(cx) - ring_size : int(cx) + ring_size + 1
            ]
            # The RPM arc is white, and red near the shift point.
            bright = (around.min(axis=2) > 175) & (
                around.max(axis=2).astype(np.int16) - around.min(axis=2) < 45
            )
            lit = bright | colour_masks(around)[1]
            hud = [int(arc_green.sum()), int(arc_red.sum()), int((lit & ring).sum())]
            dx0, dy0, dx1, dy1 = box(DIGIT, cx, cy)
            patch = grey(img[dy0:dy1, dx0:dx1].astype(np.float32)).astype(np.uint8)
            digit[: patch.shape[0], : patch.shape[1]] = patch[: digit.shape[0], : digit.shape[1]]
        rows.append([t, 1 if found else 0, *hud, gm_throttle, gm_brake])
        digits.append(digit)
    return np.array(rows, dtype=np.float64), np.stack(digits)


def gear_changes(rows, digits, fps):
    """The HUD's gear changes: (video time of the first frame that changed, of the first frame showing
    the next gear, +1 up / -1 down). LMU's HUD shows N for a few frames between two gears, so a change
    is the digit's changes within 12 frames of each other."""
    found = rows[:, 1] > 0
    steps = []
    for k in range(1, len(rows)):
        if found[k] and found[k - 1]:
            diff = np.abs(digits[k].astype(np.int16) - digits[k - 1].astype(np.int16)).mean()
            if diff > 35:
                steps.append(k)
    groups = []
    for k in steps:
        if groups and k - groups[-1][-1] <= 12:
            groups[-1].append(k)
        else:
            groups.append([k])
    changes = []
    for g in groups:
        first, last = g[0], g[-1]
        before = rows[max(0, first - 6) : first, 4][found[max(0, first - 6) : first]]
        after = rows[last + 3 : last + 10, 4][found[last + 3 : last + 10]]
        if len(before) and len(after):
            direction = 1 if np.median(after) < np.median(before) else -1
            changes.append((rows[first, 0], rows[last, 0], direction))
    return changes


def crossing(t, v, direction, level):
    for i in range(1, len(v)):
        p, q = v[i - 1], v[i]
        if (direction > 0 and p < level <= q) or (direction < 0 and p > level >= q):
            return t[i - 1] + (level - p) / (q - p) * (t[i] - t[i - 1])
    return None


def pedal_offsets(rows, fps):
    """For each throttle lift or press the G-meter shows: the G-meter's halfway time less the HUD's (s)."""
    t, found = rows[:, 0], rows[:, 1] > 0
    gm = rows[:, 5] / max(rows[:, 5].max(), 1)
    hud = rows[:, 2] / max(np.percentile(rows[found, 2], 99.5), 1) if found.any() else rows[:, 2]
    offsets, state, last = [], None, 0
    for i in range(len(gm)):
        if gm[i] <= 0.15 or gm[i] >= 0.85:
            now = "lo" if gm[i] <= 0.15 else "hi"
            if state and now != state and i - last <= int(0.6 * fps):
                direction = 1 if now == "hi" else -1
                w = slice(max(0, last - int(0.3 * fps)), min(len(gm), i + int(0.4 * fps)))
                if found[w].all():
                    level_gm = 0.5 * (np.median(gm[max(0, last - 10) : last + 1]) + np.median(gm[i : i + 10]))
                    level_hud = 0.5 * (hud[w][:10].mean() + hud[w][-10:].mean())
                    tg = crossing(t[w], gm[w], direction, level_gm)
                    th = crossing(t[w], hud[w], direction, level_hud)
                    if tg is not None and th is not None:
                        offsets.append(tg - th)
            state, last = now, i
    return offsets


def sound(video, start, seconds):
    raw = subprocess.run(
        [FFMPEG, "-v", "error", "-ss", str(start), "-t", str(seconds), "-i", video, "-vn"]
        + ["-ac", "1", "-ar", str(RATE), "-f", "f32le", "-"],
        capture_output=True,
        check=True,
    ).stdout
    return np.frombuffer(raw, np.float32).astype(np.float64)


def log_spectra(x, t0, t1, start):
    """Whitened log-frequency spectra (450-3500 Hz, 600 bins less 31 at each end) every 2.5 ms from t0
    to t1 (video time), and their times."""
    window, hop, fft = int(0.032 * RATE), int(0.0025 * RATE), 2048
    first = int((t0 - start) * RATE - window / 2)
    n = int((t1 - t0) * RATE / hop)
    if first < 0 or first + n * hop + window > len(x):
        return None, None
    idx = first + np.arange(n)[:, None] * hop + np.arange(window)[None, :]
    mag = np.abs(np.fft.rfft(x[idx] * np.hanning(window), fft, axis=1))
    log_f = np.exp(np.linspace(np.log(450), np.log(3500), 600))
    place = log_f * fft / RATE
    lm = np.log(mag + 1e-7)
    spec = np.stack([np.interp(place, np.arange(lm.shape[1]), row) for row in lm])
    kern = np.ones(31) / 31
    env = np.stack([np.convolve(r, kern, mode="same") for r in spec])
    times = start + (first + np.arange(n) * hop + window / 2) / RATE
    return (spec - env)[:, 31:-31], times


def fit(a, b, slide, pad=180):
    m = len(a)
    x, y = a[pad : m - pad], b[pad + slide : m - pad + slide]
    x, y = x - x.mean(), y - y.mean()
    den = np.sqrt((x * x).sum() * (y * y).sum())
    return float((x * y).sum() / den) if den > 0 else 0.0


def pitch_drop(x, start, around):
    """The moment near `around` (video time) where the engine's pitch drops the clearest: any drop of 6-46%,
    as the telemetry's RPM isn't in a recording. None if none stands out."""
    found = pitch_drop_and_size(x, start, around)
    return found[0] if found else None


def pitch_drop_and_size(x, start, around):
    """As pitch_drop, with the drop's size: (moment, log of the pitch after over before), or None."""
    spec, times = log_spectra(x, around - 0.25, around + 0.45, start)
    if spec is None:
        return None
    side = 16
    sums = np.vstack([np.zeros((1, spec.shape[1])), np.cumsum(spec, axis=0)])
    gains = np.full(len(times), -1.0)
    slides = np.zeros(len(times), dtype=int)
    for r in range(side, len(times) - side):
        if not around - 0.15 <= times[r] <= around + 0.3:
            continue
        before = (sums[r] - sums[r - side]) / side
        after = (sums[r + side] - sums[r]) / side
        fits = [fit(before, after, s) for s in range(-180, -17)]
        best = max(fits)
        if best >= 0.35:
            gains[r] = best - fit(before, after, 0)
            slides[r] = -180 + int(np.argmax(fits))
    k = int(np.argmax(gains))
    if gains[k] < 0.3:
        return None
    low, high = k, k
    while low > 0 and gains[low - 1] >= gains[k] / 2:
        low -= 1
    while high + 1 < len(gains) and gains[high + 1] >= gains[k] / 2:
        high += 1
    log_bin = np.log(3500 / 450) / 599
    return 0.5 * (times[low] + times[high]), slides[k] * log_bin


def summary(name, values_s):
    if not values_s:
        return f"{name}: none"
    ms = np.array(values_s) * 1000
    q = np.percentile(ms, [25, 50, 75])
    return f"{name}: {q[1]:+.0f} ms (middle half {q[0]:+.0f} to {q[2]:+.0f} ms, {len(ms)} events)"


def parse_point(text, count):
    parts = text.replace("@", ",").split(",")
    if len(parts) != count:
        raise argparse.ArgumentTypeError(f"expected {count} numbers")
    return [float(p) for p in parts]


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("video")
    parser.add_argument("--from", dest="start", type=float, required=True)
    parser.add_argument("--seconds", type=float, required=True)
    parser.add_argument("--hud", type=lambda s: parse_point(s, 3), required=True)
    parser.add_argument("--hud-radius", type=float, default=109)
    parser.add_argument("--gmeter", type=lambda s: parse_point(s, 3), required=True)
    parser.add_argument("--search", type=float, default=350)
    parser.add_argument("--fps", type=float, default=90)
    parser.add_argument("--out")
    args = parser.parse_args()

    width, height = video_size(args.video)
    rows, digits = read_frames(args, width, height, args.fps)
    changes = gear_changes(rows, digits, args.fps)
    x = sound(args.video, args.start - 0.5, args.seconds + 1)
    drops = []
    for _, engaged, direction in changes:
        if direction > 0:
            when = pitch_drop(x, args.start - 0.5, engaged)
            if when is not None:
                drops.append(when - engaged)

    print(f"HUD found in {100 * rows[:, 1].mean():.0f}% of {len(rows)} frames")
    print(summary("G-meter throttle against the HUD's", pedal_offsets(rows, args.fps)))
    print(summary("engine pitch drop against the HUD's next gear (upshifts)", drops))
    ups = sum(1 for *_, d in changes if d > 0)
    print(f"gear changes on the HUD: {ups} up, {len(changes) - ups} down")

    if args.out:
        os.makedirs(args.out, exist_ok=True)
        np.save(os.path.join(args.out, "hud_rows.npy"), rows)
        np.save(os.path.join(args.out, "gear_digits.npy"), digits)
        with open(os.path.join(args.out, "gear_changes.json"), "w", encoding="utf-8") as f:
            json.dump([{"start": a, "engaged": b, "direction": d} for a, b, d in changes], f, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())

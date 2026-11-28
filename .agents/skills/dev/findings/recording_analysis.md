# Findings: the recording analysis

Terms: `reference\terms.md`. What the scripts do and how they measure:
`vr_mirror\recording_analysis\README.md`. How to use them: `reference\recording_analysis.md`.

## Lining a recording up with its trace

- **Tried and dropped: counting the trace's repeats that fall on frozen frames** (frames whose patches
  spread under 0.3 px around the cockpit's motion). A long run of repeats (a menu, the car stopped)
  over a long frozen stretch agrees anywhere nearby. In race F a place 24 minutes off scored 75,
  against the right place's 8. `lineup.py` counts the trace's single repeats that fall on a dip in the
  spread instead (the README, "How the whole-recording analysis measures").

## Checked against an earlier investigation

`analyse_recording.py` gives the numbers of the data folder's investigation of the stutter with
smoothing 2 (its README). Lower is better in every row.

| Measure | Session E, Instant (`_s1`) | Race F, Smooth (`_s2`) |
|---|---|---|
| Cockpit jumps over 2 px per minute | 1.5 | 1.9 |
| Step p99 / p99.9 (px at 1080p) | 1.13 / 1.66 | 1.20 / 1.73 |
| Repeated game frames (trace) | 217 | 18 |
| Game frames given a slot two or more on | 198 | 11 |
| Two consecutive game frames given the same slot | 12, none with a jolt of 2 px | 1, at 7:39.57, with a jolt |
| OBS ticks shown twice or dropped | 0 | 0 |

- The shake report (`shake.txt`) and the cadence (`cadence.txt`) of both recordings match the old
  scripts' output line for line. For session E the old scripts needed the lineup shifted by hand
  (`--wraps -1`).
- The per-jump report (`traced.txt`) of race F matches too, apart from the wording and the count of
  images shown for another frame. The old scripts read every tick of these recordings as one, for the
  reason in the README's "Known limits" (`followed_serial`).
- The old per-jump report of session E came from the wrong place in the trace.

## OBS's encoder at 4K and 120 fps

- **A 120 fps recording that stuttered badly was OBS's encoder, not VR Mirror.** Three recordings of
  the OpenXR source with Look-ahead, 3840x2160, NVENC AV1 (preset p6, CQP 16, quality pass on):

  | OBS | Video frames | OBS ticks repeated in the video | Ticks lost | Game frames per second of video |
  |---|---|---|---|---|
  | 90 fps | 3,188 | 0 | 0 | 90 |
  | 60 fps | 1,757 | 0 | 0 | 60 (a third of the game's never shown, as at that rate) |
  | 120 fps | 3,608 | 2,515 (70%) | 2,507, in 443 places | 30.7 |

  - At 120 fps OBS's log says "number of skipped frames due to encoding lag: 2527/3621 (69.8%)": the
    encoder couldn't keep up, and OBS repeated the last frame in the place of each one it dropped.
  - Over those 30 s the plugin drew all 3,600 ticks, with every one of the game's 2,698 frames (90 a
    second). 25.1% of its ticks showed the game frame before again: 90 Hz on 120 fps.
  - So a recording's own frame steps (`analyse_recording.py`, "OBS ticks shown twice or dropped") and
    OBS's "skipped frames due to encoding lag" line come first, before reading anything of VR
    Mirror's into a stutter.
- **The stamp check took such a video for one without a stamp.** It asked for most frames to step by
  one tick. It now asks for nearly every frame to step forward by 30 ticks at most, which noise read
  as a stamp doesn't.

## The G-meter and the sound against the view (`sync_check.py`)

- **The template matching locked onto the G-meter's black disc.** The region that `sync_check.py` reads
  includes the G-meter. The normalised cross-correlation divides by the standard deviation of the window
  under the template. In the disc's flat black window that is close to 0, so the score reached 130 to
  4100 instead of at most 1, and the disc won even on the frame the template was cut from. The gear
  digit was then read from the wrong place in about 7 of 10 frames: 2 minutes of a race gave 122 gear
  changes, and upshift timings from −68 to +172 ms. Windows with a grey-level standard deviation under 2
  now score 0. On another 2 minutes, the script then read 16 upshifts and 12 downshifts, and the
  G-meter's throttle 42 ms ahead of the HUD's (middle half 37 to 44 ms, 8 events).
- **Most of the run time was the search, not the decoding.** ffmpeg decodes 10 s of a 4K AV1 recording
  in about 4 s in software, and no faster with `-hwaccel cuda` or `d3d11va`. Four changes brought 2
  minutes of video from about 15 minutes to 58 s, with identical output: searching around the previous
  frame's match first, converting only that region to greyscale, reading the RPM arc only on the arc's
  pixels, and reading the video in 16 parallel processes.
- **The engine's pitch drop at upshifts** is too scattered to use: middle half −124 to +262 ms from 11 of
  16 upshifts in 2 minutes, and over two whole races −129 to +247 ms (138 upshifts) and −131 to +267 ms
  (117).
- **The engine's sound at throttle lifts:** in 2 minutes there were 6 lifts, and on most of them the
  engine's band (450 to 3500 Hz) dropped only 3 to 6 dB, so only one was timed. Over two whole races,
  one gave 14 timed lifts with a middle half 51 ms wide, and the other only 2. The dial was found in 81%
  and 76% of the frames.

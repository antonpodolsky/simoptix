# Findings: the recording analysis

Terms: `reference\terms.md`. What the scripts do and how they measure:
`vr_mirror\recording_analysis\README.md`. How to use them: `reference\recording_analysis.md`.

## Lining a recording up with its trace

- **Tried and dropped: counting the trace's repeats that fall on frozen frames** (frames whose patches
  spread under 0.3 px around the cockpit's motion). A long run of repeats (a menu, the car stopped)
  over a long frozen stretch agrees anywhere nearby. On 4 Nov a place 24 minutes off scored 75,
  against the right place's 8. `lineup.py` counts the trace's single repeats that fall on a dip in the
  spread instead (the README, "How the whole-recording analysis measures").

## Checked against an earlier investigation

`analyse_recording.py` (2026-11-05) gives the numbers of the investigation in the data folder's
`2026-11 smoothing 2 stutter\README.md`. Lower is better in every row.

| Measure | 3 Nov 20:20, Instant (`_s1`) | 4 Nov 19:35, Smooth (`_s2`) |
|---|---|---|
| Cockpit jumps over 2 px per minute | 1.5 | 1.9 |
| Step p99 / p99.9 (px at 1080p) | 1.13 / 1.66 | 1.20 / 1.73 |
| Repeated game frames (trace) | 217 | 18 |
| Game frames given a slot two or more on | 198 | 11 |
| Two consecutive game frames given the same slot | 12, none with a jolt of 2 px | 1, at 7:39.57, with a jolt |
| OBS ticks shown twice or dropped | 0 | 0 |

- The shake report (`shake.txt`) and the cadence (`cadence.txt`) of both recordings match the old
  scripts' output line for line. For 3 Nov the old scripts needed the lineup shifted by hand
  (`--wraps -1`).
- The per-jump report (`traced.txt`) of 4 Nov matches too, apart from the wording and the count of
  images shown for another frame. The old scripts read every tick of these recordings as one, for the
  reason in the README's "Known limits" (`followed_serial`).
- The old per-jump report of 3 Nov came from the wrong place in the trace.

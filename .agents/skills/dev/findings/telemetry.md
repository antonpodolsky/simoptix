# Findings: the shared telemetry

Terms: `reference\terms.md`. How the telemetry works: `shared\telemetry\README.md`.

## Why one reader

- LMU's lock on its telemetry lets several readers take turns.
- LMU's signal for "new data" isn't documented as waking more than one waiting reader per update. The
  fake LMU (`shared\telemetry\games\lmu\fake`), written to match what was seen of LMU, wakes only one.
- So two readers might each see only part of the updates. With one reader the question doesn't arise.
- Not checked: what LMU itself does. LMU wasn't running when this was written (2026-11-06).

## One reader for both plugins (2026-11-06)

Checked with the fake LMU and no headset.

- **In OBS,** with the G-meter shown and auto-record ticked in the Pimax source:
  - One reader ran, started in VR Mirror, counting 2 users.
  - The fake's 60 s wrote 5,997 updates.
  - The thread used 0.03% of one core, at 100 wake-ups a second.
  - When OBS closed, the reader stopped as auto-record switched off.
  - The G-meter moved while the fake ran: 136 of the sampled pixels changed in 0.7 s, and none without
    the fake.
- **Matched to the view.** A delay of 0.5 s was written into OBS's shared memory, standing in for VR
  Mirror's, which publishes one only with the headset running.

  | | Start after the fake's lights | Stop after the car stood still |
  |---|---|---|
  | No delay | 0.24 s | 5.25 s |
  | 0.5 s delay | 0.53 s | 5.54 s |

  Auto-record checks four times a second, so 0.5–0.75 s and 5.5–5.75 s were expected.
- **Moving between plugins,** outside OBS. Two copies of this code, built as two DLLs in one program,
  stood in for the two plugins, with the fake running.
  - The reader started once, in the first DLL. It kept running there while only the second needed it.
  - When the first unloaded, the second started it at its next read. No two readings were more than
    0.041 s apart, and the session number stayed the same.
  - After the last release it stopped.
- **Not checked:** the real game (LMU wasn't running), and VR Mirror's real delay (it needs the
  headset).

## The games behind one interface (2026-11-06)

- A throwaway second game was built with no overlay file touched.
- The plugin was built with and without LMU's headers.
- With `lmu_fake --race` in OBS:
  - The reader connected 0.7 s after the fake appeared. Before, it looked every 2 s.
  - The recording started at the lights and stopped 5.0 s after the car stood still.
  - The thread used 0.04% of one core, at 100 wake-ups a second.
- While no game runs, the thread wakes once a second (10 times before), from the code. It logs nothing
  then.

## The overlays on the telemetry model (2026-11-06)

- The grip logic replayed both recorded sessions, dry and as wet, with output identical byte for byte
  to before. The replay tool was `tools\lmu\grip_replay`, removed on 2026-11-07 with the other one-off
  tools. The grip logic itself went the same day (`findings\overlays.md`).
- Race auto-record behaved as before with `lmu_fake --race`.
- The G-meter's axis conversion is exact: only the signs and the order of the axes change. Not
  checked: its image before and after.

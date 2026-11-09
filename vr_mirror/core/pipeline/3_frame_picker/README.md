# Stage 3: frame picker

This stage picks which stored game frame a host shows each time it can draw, and when that frame is
due. The OBS plugin draws on each of OBS's ticks. The mirror app draws whenever its loop wakes. The
stage also gives the moment on the timeline that the tick stands for. That is the tick minus the
output delay. The view follows the head at that moment, and the eye image is re-projected from its
own pose. Finally, it says how long the output runs behind the game, which the host's audio sync
follows, and how many stored frames it needs.

## Terms

| Term | Meaning |
|---|---|
| host | A program that runs VR Mirror's pipeline and shows its result: the OBS plugin or the mirror app |
| tick | One time OBS draws a frame of its output |
| output delay | How far VR Mirror's view runs behind the game: about 0.5 s with Look-ahead, none with Instant, and 0 while the source is hidden. The G-meter, race auto-record and OBS's audio follow it |
| look-ahead | The method behind the Look-ahead smoothing. The view runs half a second behind the game, and the smoothing plans a calm path from the head's movement in that time |
| mirror app | `simoptix_vr_mirror.exe`, VR Mirror in a desktop window (`vr_mirror\mirror_app\`). The e2e tests run it |

## Input and output

**In:** the tick's time, the display period and the host's store of copied frames (`StoredFrame` in
`pipeline_types.h`: serial, arrival, de-jittered arrival, slot, and when the copy was seen complete).

**Out:**
- `Pick`: the store entry to show, the kind of pick (`kNext`, `kRepeat`, `kJump`, `kHeld`, `kNone`,
  for the host's 10 s stats), the tick's moment and when the frame was due.
- The output delay (`OutDelay`). When it changes, the host logs it and gives the picker's `Report`.
- The entries in use (`SlotsInUse`).
- Through the shared report, once, the tick at which the delay was fixed (`TakeDelayFixed`, for the
  host's log line).

## Implementations

| File | Class | Config value | When to use |
|---|---|---|---|
| `newest_frame_picker.h/.cpp` | `NewestFramePicker` | `FramePickerKind::kNewestFrame` | The mirror app with Instant. It picks the newest frame as soon as it is new (at most 60 times a second without a frame signal). The frame is due the window's present delay after its de-jittered arrival. It is not held if that is over 15 ms ahead, which would be a clock glitch. The store is the newest frame alone |
| `held_frame_picker.h/.cpp` | `HeldFramePicker` | `FramePickerKind::kHeldFrame` | The mirror app with Look-ahead. It picks the oldest held frame once its view can be computed. The frame is due the smoothing's delay plus the present delay after its de-jittered arrival, and it is picked 4 ms ahead so the window can wait and present then. Without a frame signal it picks the newest frame at once, at most 60 times a second. The store is the held frames, oldest first, then the newest frame |
| `fixed_delay_picker.h/.cpp` | `FixedDelayPicker` (and `WarmupSlots`) | `FramePickerKind::kFixedDelay` | The OBS plugin. Each frame is shown a fixed delay after its de-jittered arrival. The delay is the copy delay measured over a 2 s warm-up (the 95th percentile plus 2 ms, between 4 and 45 ms, and 40 ms until then), plus the smoothing's delay. It shows one frame per tick in order: the next one unless it is clearly not due (4 ms slack). It catches up only when two frames are clearly overdue |

`ConfigFor(settings, clock)` chooses the implementation from how the host draws:
- `OutputClock::kTicks` (the OBS plugin) gives the fixed delay.
- `OutputClock::kFrames` (the mirror app's window) gives the newest frame with Instant and the held
  frames with Look-ahead.

The OBS plugin runs the stage every tick (`obs_plugin\mirror_view.cpp`, `Prepare`). It passes the
ring's snapshot as `StoredFrame`s and uses the stage's answers for the log line, the ring's size and
the program delay. The mirror app runs it every time its loop wakes (`main.cpp`). It passes the hold's
copies (`frame_hold.*`) and the newest frame as `StoredFrame`s, draws the pick and presents at its due
time.

**Thread:** the host's output thread (OBS's graphics thread, the mirror app's main thread). Allocation happens
only at `Restart`. The fixed delay's warm-up buffer is reserved at `Restart`, and its per-tick
ordering uses a fixed array.

**Uses:** plain data: times and the stored frames' records.

**Must not know:** textures, OBS, PVR, threads, smoothing or poses.

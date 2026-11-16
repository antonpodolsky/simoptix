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
`Restart` takes the display period too, as the host knows it then. With OpenXR it is known only once
the layer runs, which can be later. It also takes the host's output frame period, from OBS's video
settings, or 0 for a host without regular ticks (the mirror app), and whether the host can copy only
the frames its forecast says a tick will show. It can when the game's images stay readable until the
next frame has arrived (through the OpenXR layer, not Pimax's single mirror image).

**Out:**
- `Pick`: the store entry to show, the kind of pick (`kNext`, `kRepeat`, `kJump`, `kHeld`, `kNone`,
  for the host's 10 s stats), the tick's moment and when the frame was due.
- The output delay (`OutDelay`). When it changes, the host logs it and gives the picker's `Report`.
- The entries in use (`SlotsInUse`). They can change at any tick, and the host resizes its store when
  they do. The fixed delay changes them when it fixes its delay, and during the warm-up when the
  display period changes.
- The entries it would settle at for another smoothing (`SlotsFor`), from what it has measured: the
  OBS plugin shows them as video memory in a source's properties. The fixed delay gives 0 until it
  has fixed its copy delay. Its sizing is `FixedDelaySlots` and `FixedDelayCopiesShownOnly`, which it
  uses itself.
- A forecast of its picks (`MakeForecast`, a `ShowForecast`), for the host to run where it sees the
  frames arrive. It is told of every frame as it arrives (`Arrived`), and says whether a tick will
  show it even with the next frame on time. Once the next frame has arrived, it says whether any tick
  will show the frame before (`WillShow`). The picker's `Record` after each tick tells it where the
  picker stands: the frame shown, the tick, the tick period, the display period, the output delay,
  whether that delay is settled, whether the host is to copy only the frames shown, and a count of
  restarts. It is plain data, so the host can hand a copy to the thread that copies frames. To copy
  only the frames shown, the host copies at once a frame `Arrived` says is shown, holds the others
  until the next one arrives, and copies one only if `WillShow` says so. A picker without a forecast
  gives none, and the host copies every frame. A forecast that can't tell says yes.
- Through the shared report, once, the tick at which the delay was fixed (`TakeDelayFixed`, for the
  host's log line).

## Implementations

| File | Class | Config value | When to use |
|---|---|---|---|
| `newest_frame_picker.h/.cpp` | `NewestFramePicker` | `FramePickerKind::kNewestFrame` | The mirror app with Instant. It picks the newest frame as soon as it is new (at most 60 times a second without a frame signal). The frame is due the window's present delay after its de-jittered arrival. It is not held if that is over 15 ms ahead, which would be a clock glitch. The store is the newest frame alone |
| `held_frame_picker.h/.cpp` | `HeldFramePicker` | `FramePickerKind::kHeldFrame` | The mirror app with Look-ahead. It picks the oldest held frame once its view can be computed. The frame is due the smoothing's delay plus the present delay after its de-jittered arrival, and it is picked 4 ms ahead so the window can wait and present then. Without a frame signal it picks the newest frame at once, at most 60 times a second. The store is the held frames, oldest first, then the newest frame |
| `fixed_delay_picker.h/.cpp` | `FixedDelayPicker` (and `WarmupSlots`) | `FramePickerKind::kFixedDelay` | The OBS plugin. Each frame is shown a fixed delay after its de-jittered arrival. The delay is the copy delay measured over a 2 s warm-up (the 95th percentile of the copies made since it started, each counted once, plus 2 ms, between 4 and 45 ms, and 40 ms until then), plus the smoothing's delay. It shows one frame per tick in order: the next one unless it is clearly not due (4 ms slack). It catches up only when two frames are clearly overdue. Its helper `fixed_delay_picker_ahead.h/.cpp` holds that rule (`FixedDelayStep`) and its forecast, the rule run ahead on the host's ticks (`FixedDelayAhead`), which keep to a grid of output frame periods: whether it shows a frame depends only on the frames up to the next one, so the forecast answers once the next frame has arrived, assuming every copy is ready when due. It keeps its own list of the frames from the one shown on, and passes over a frame only once the next one's copy is certainly done (0.1 s after it arrived), so only with Look-ahead. Then it asks a host that can to copy only the frames shown, and once the frames copied before are past, the store holds those: the frames in the delay at the slower of the display's and the ticks' rate, plus one tick and 3 more |

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

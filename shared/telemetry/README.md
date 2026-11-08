# Telemetry (shared by both plugins)

This folder reads the game's telemetry: what the car is doing (G-forces, speed, pedals, ABS and TC)
and where the session is (practice or race, the start lights, the finish). The G-meter (Overlays) and
race auto-record read it. Both plugins compile this folder in.

## Terms

| Term | Meaning |
|---|---|
| output delay | How far VR Mirror's view runs behind the game: about 1 s with Smoothest, about 0.5 s with Smooth, none with Instant, and 0 while the source is hidden. The G-meter, race auto-record and OBS's audio follow it |
| telemetry model | The one description of the car and the session that every game is turned into (`telemetry_model.h`) |

## One reader for the whole of OBS

One thread in OBS reads the game, however many things need its telemetry and in whichever plugin they
are.

- **Where its data lives.** The reader writes each update from the game into a block of memory that both
  plugins open by name (`telemetry_area.*`). The block holds the last 10 s or so of updates. Reading
  holds nothing up: not the game, not the reader, not OBS's drawing.
- **When it runs.** The block also keeps a tally of what needs telemetry right now. Each G-meter on
  screen counts one, and race auto-record counts one while it is switched on. The first of these, in
  either plugin, starts the reader in its own plugin. The reader keeps running while the tally is above
  zero and stops by itself when it reaches zero.
- **When its plugin unloads first** (OBS closing), the reader stops. The other plugin starts it again
  the next time it reads, if it still needs it.
- **Driving sessions** are numbered by the reader, and the number is kept in the shared block. So a new
  session is never seen just because the reader moved from one plugin to the other.
- **Different builds.** The block starts with a version number and its size. If the two plugins come
  from different builds and disagree, each reads the game on its own and the OBS log says so:
  "telemetry: the other plugin is a different build". Install both from one build.

The OBS log says which plugin runs the reader: "telemetry: reader started" and "telemetry: reader
stopped", with that plugin's tag (`[simoptix_vr_mirror]` or `[simoptix_overlays]`).

**Why one reader.** LMU's lock on its telemetry lets several readers take turns. LMU's signal for "new
data" is not documented as waking more than one waiting reader per update, so two readers might each
see only part of the updates. With one reader the question doesn't arise.

## Matched to the view

VR Mirror's view runs behind the game by the output delay. The telemetry is handed out already matched
to it, so the G-meter and auto-record never deal with the delay.

- **One moment for everything.** The telemetry works out which moment of the game the view is showing:
  the time now, less the delay VR Mirror publishes (`shared\program_delay.h`, 0 without its view on
  screen).
  - The match is only as good as that published delay, because nothing looks at the view's image.
  - The G-meter and auto-record ask for that moment once per frame or check (`Telemetry::Shown`).
    They then read the car's readings up to it, and the session as it was then, from the last 10 s or
    so of the game's updates that the reader keeps, each stamped with the time it arrived.
- **Example.** With the view 1 s behind, the G-meter's dot shows the G-force from 1 s ago. Auto-record
  starts the recording at its first check after the moment the view shows reaches the start lights.
  That is 1 s after the game reached them, plus up to 0.25 s until that check, because it checks four
  times a second.
- **The moment only moves forward.** When the delay grows (the view starting, or switched to a smoother
  setting), the moment holds still until the clock catches up, so already shown data isn't shown again.
  When the delay shrinks (the view hidden), the moment jumps forward.
- **The G-meter's demo lap** (`demo_lap.*`) isn't game data and has no view to follow. It plays in
  real time.

## The telemetry model

Every overlay reads the same description of the car and the session, whichever game it comes from
(`telemetry_model.h`):
- times in performance-counter seconds
- the car's axes as in ISO 8855 (x forward, y left, z up)
- SI units
- the session in plain terms: practice, qualifying or race, the formation lap, the start lights, green,
  and so on, plus the player's result

Each game's own reading code turns the game's units, axes and codes into these. For LMU that is
`games\lmu\lmu_convert.h` (LMU's car axes are +x left, +y up, +z rear). The overlays are written in the model's terms. A
field is added to the model only when an overlay uses it.

## Games

Each game has its own reading code under `games\` (`game_adapter.h` says what it does). The reader runs
it.
- While no game is connected, the reader tries each listed game once a second and sleeps in between.
- Once one connects, the reader follows that game until it quits (for LMU, 5 s without an update).
- The OBS log says "telemetry: waiting for a game (Le Mans Ultimate)", "telemetry: connected to Le Mans
  Ultimate" and "telemetry: Le Mans Ultimate disconnected". Every 10 s while connected, it also logs the
  thread's footprint ("footprint: telemetry thread (Le Mans Ultimate) ...").

A game whose SDK can't be shipped (LMU) is compiled only when its SDK is found, and then defines
`HAVE_<GAME>`. LMU's headers are looked for in every Steam library (`games\lmu\find_lmu_sdk.ps1`).
`LMU_SDK` names their folder instead.

**The fake LMU** (`games\lmu\fake\lmu_fake.exe`) publishes made-up telemetry on LMU's interface while
LMU is closed, so the plugins read it exactly as they read the game. It refuses to run while LMU's
interface exists.
- It plays the demo lap for 60 s, or for the number of seconds given. `--start` picks the lap time to
  begin at.
- With `--race` it also plays a race's scoring:
  - formation lap from 0 s
  - start lights from 5 s
  - green from 10 s
  - the checkered flag at `--finish` (default 30 s)
  - then the car slowing to a stop over 5 s

  `--session` picks LMU's session type (default 10, a race).

## Files

| File | What |
|---|---|
| `telemetry.*` | What the overlays and auto-record call: hold telemetry while needed, and read it as of the view's moment |
| `shown_moment.h` | The moment the view shows |
| `telemetry_model.h` | The description of the car and the session that every game is turned into |
| `telemetry_area.*` | The block of memory both plugins share |
| `telemetry_ring.*` | The recent updates in that block, read without waiting |
| `telemetry_reader.*` | The one reading thread |
| `session_count.h` | Numbers the driving sessions |
| `game_adapter.h`, `game_registry.*` | What each game's reading code does, and the list of games |
| `games\lmu\` | Le Mans Ultimate: reads its shared memory (`lmu_telemetry.*`), converts its terms (`lmu_convert.h`) and finds its headers (`find_lmu_sdk.ps1`) |
| `games\lmu\fake\` | The fake LMU (`lmu_fake.cpp`, `build.bat`) |
| `demo_lap.*` | The G-meter's made-up 12 s lap |
| `qpc_clock.h` | The clock every time is on |
| `sources.bat` | The source list both plugin builds use, and LMU's headers |

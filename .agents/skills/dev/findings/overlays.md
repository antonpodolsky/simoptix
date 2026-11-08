# Findings: the Overlays plugin and the G-meter

Terms: `reference\terms.md`. What the plugin and the G-meter do: `overlays\README.md`. The design's
specs: `overlays\gmeter\design\README.md`.

## Text

DirectWrite against the GDI+ text it replaced, measured at 1:1 on a 300 px Chrono meter. Sharpness is
1 when every pixel is fully on or off, and lower when greyer.

| Label | GDI+ | DirectWrite |
|---|---|---|
| Highest values | 0.79–0.83 | 0.79–0.81. The same, because they were already upright and 11 px |
| Live G | 0.79 | 0.80 |
| ABS, TC (turned) | 0.50, 0.44, with strokes at 68–80 % | 0.72, 0.71, with strokes at 95 % |
| Ring labels | 0.47–0.52, shrunk with the image | 0.68–0.72, drawn at 6 px |

- At 500 px the turned and ring labels go from 0.57–0.68 to 0.76–0.81.
- Rebuilding the four highest values and the live G at once takes 0.3–0.6 ms on the graphics thread,
  as with GDI+.
- **Contrast boost:** a stronger one (k = 1 in `meter_text.cpp`) measured sharper still (ABS 0.74),
  but made the SemiBold digits look bold. k = 0.5 keeps the design's weight.
- **Tabular figures:** real tabular figures centre a number by its digit cells. So a leading "1"
  (narrow, in a full-width cell) puts the ink about 0.8 px right of the window's centre at 300 px.
  - In exchange the digits stay put as the value changes, which matters more.
  - With proportional figures, the highest values' decimal points moved by up to 7 px between values
    (the left value going from "2.05" to "1.11", at size 300). With tabular ones they stay on the same
    columns.
- **The live G's centre:** centred on its window by measurement (gaps of 5.6 and 5.2 px on a 300 px
  screenshot), it still looked 1 px left of centre. 1.6 design px right (1 px at 300) looked shifted
  right. So it sits halfway, 0.8 design px right of the window's centre.

## Telemetry checks

- **A recorded session played on LMU's telemetry interface** (2026-11-06). The fake LMU then had a
  `--replay` option, removed on 2026-11-07 with the other one-off tools. It played 30 s of the first
  recorded session (the data folder's `traction\lmu_tyres_1.csv`, from 120 s).
  - The plugin connected, and read a practice session at 96–100 updates a second.
  - The meter's dot moved: 293 of the meter's pixels changed in 0.7 s on a screenshot.
- **LMU's acceleration against its own speed change:** they agree within about 0.01 G, with the
  telemetry at 100 updates a second.
- **The dot snapping back to the centre when the car stops is real.** A braked car decelerates
  steadily right up to the stop.

## The G-meter's design

Why some of the specs are as they are:

- **Dot:** the first design had r 7.8 and read too small. It is r 12.5 (Dot Size 160 % on the canvas).
- **1G and 2G rings:** white at 8 % at first. They faded out in compressed video, so they are 16 %.
- **Comet tail colour:** in the accent it merged with the dot. In amber `#ffb020` it stood out too
  much, and `#ff7c1f` still a bit. `#ff6b1f` is the one kept.
- **ABS and TC placement:** centred on their design angles, the wider ABS came 14.5 design px from its
  groove, and looked closer than TC. At TC's 19.3 it looked further away, because the A's diagonal
  leans away from the bar. So it sits halfway, at 16.9.
- **Live G:** centred by measurement it looked 1 px left at size 300, and 1.6 design px right looked
  shifted right, so it is 0.8 right. Vertically, the window's inner shadow (offset 1 px down) makes it
  look about 0.5 px higher than its box. On the box, the gap under the digits measured 1 px smaller
  than the one above.

### Tried and dropped: glass reflections other than a crescent

Tried on the canvas's "Chrono Inner" board:
- a broad oval sheen
- a single point-light glint
- a rim of light round the glass edge
- a two-pane window
- a dome (a top highlight with darker edges)
- a watch-crystal arc along the top edge

At 3–12 % white they were too faint to tell apart. Made bolder (up to about 30 %), they looked
artificial, not like real reflections. The closest was a "feathered" crescent about twice as bright
(24 % at the top), whose lower edge fades out instead of ending in a line. It was chosen on 2026-10-24
and is the plugin's glass.

## Tried and removed: tyre grip learning and ideal pedals

The G-meter once learned how close each tyre is to its limit, and worked out the ideal brake and
throttle from it, to draw as arcs and ticks on the meter. The Chrono look draws neither. So it was
switched off, then removed on 2026-11-07, with its spec and a tool that replayed it over recorded
telemetry. How it worked:

- **The ideal window** was 3 % of each tyre's peak grip.
- **Grip from slip.** LMU gives no tyre forces or loads. So each tyre's slip was compared with the slip
  where its axle grips most. The meter learned that while driving, from the car's own G:
  - the braking limit, where braking G stops rising
  - the cornering limit, where lateral G stops rising, per range of tyre load (from LMU's tyre
    deflection), so downforce, weight transfer and fuel were covered
- **Dry and wet** were learned apart, from LMU's surface under each tyre. The wet started from the dry
  values until it had learned its own.
- **What it learned was kept** per car model and tyre compound, in `grip_limits.json` in OBS's
  `plugin_config\simoptix_overlays` folder. It was saved every 2 minutes while driving, when the car or
  compound changed, and when the meter was hidden or OBS closed. So a car driven before started tuned.
  A new one started from values measured in one car on one track.
- **The OBS log** showed what it used every 10 s while driving ("G-Meter Overlay grip"): which limits
  were learned, and which were still the starting values.
- **Slip was measured from free rolling.** LMU's slip ratio reads about −0.018 with no pedal pressed,
  which the meter learned from the front tyres and took off.
- **The ideal pedals** came from how much slip each tyre gets per unit of brake pressure (learned while
  braking) or of throttle (per gear).
- **The driving limit itself wasn't learned live.** Above third gear the car runs out of power first,
  and TC lets the rears run past their limit, so neither shows it. It stayed at the value measured on
  low-gear exits.

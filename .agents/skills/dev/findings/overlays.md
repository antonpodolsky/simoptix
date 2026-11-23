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

- **Bahnschrift Medium (500):** Bahnschrift's named weights are Light, SemiLight, Regular, SemiBold
  and Bold. Asked for 500, DirectWrite took SemiBold: "2.62" at 15 px rasterised to the same coverage
  as at 600 (sum 29294). With the weight axis set on the text format (`shared\text_raster.cpp`), 500
  gives its own outlines (sum 26799, between Regular's 25332 and SemiBold's 29294). Every other weight
  (300, 350, 400, 600, 700) at 5.6–20 px rasterised byte for byte as before, so Chrono's labels and
  VR Mirror's empty state are unchanged.

## Telemetry checks

- **A recorded session played on LMU's telemetry interface.** The fake LMU then had a
  `--replay` option, since removed with the other one-off tools. It played 30 s of the first
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
- **The look is the canvas's "Chrono Inner" board**, with its ABS and TC labels
  removed: the bars sit just inside the 3G ring, and the bezel is r 190. The maintainer chose it over
  the "Chrono" board below. Its glass is the same feathered crescent, so `gmeter_glass.png` stayed
  identical, pixel for pixel.
- **The pedal bars' edges:** the bars are drawn with their soft edges centred on their outline, so a
  5 px bar covers as much as the design's 5 px stroke. The round-ended bars of the earlier look had
  their soft edges outside the outline, 1 px wider than the design.
- **Live G:** centred by measurement it looked 1 px left at size 300, and 1.6 design px right looked
  shifted right, so it is 0.8 right. Vertically, the window's inner shadow (offset 1 px down) makes it
  look about 0.5 px higher than its box. On the box, the gap under the digits measured 1 px smaller
  than the one above.

### The OLED look

- **Its layer** is `gmeter_oled_base.png`, rendered from `meter_oled.html` with Edge at 2×. It is
  identical, byte for byte, to the layer the design was checked with. Its alpha is 0 at the image's
  edges and beyond r 200 design px from the centre, and 255 at its strongest.
- **The ring labels** are set in the design as SVG text from x 244 on their baselines, with tabular
  figures. The plugin places labels by their ink, so their ink was measured with DirectWrite at
  1000 px: the tabular 1, 2 and 3 start 0.101, 0.054 and 0.044 em right of the pen, and the G ends
  0.007 em under the baseline. At 9 px that puts the ink at x 244.91, 244.49 and 244.40, with its bottom
  0.06 under each baseline.
- **The dot, its glow and its tail are Chrono's,** from one file (`meter_dot.cpp`). The design's card
  gives OLED's tail the dot's own colour, `#ff5a1f`, which merged with the dot as it did on Chrono
  ("Comet tail colour", above).
- **The waiting dot:** without telemetry the dot waits at the centre at 35 %, and the layer's centre
  point (white at 50 %) showed through it as a white point. A black disc of the dot's size under it
  hides the point.
- **The tracks' last segment:** the design's track arcs had their ends rounded to 0.1 px, so they
  spanned 109.95°, and the browser's dashes drifted along its own measure of the arc. The top segment
  of each track measured 4.57 design px against 4.79–5.02 for the others. With the ends exact to 0.0001
  it measured 4.69. With `pathLength` set to the pattern's length as well, every segment measures
  4.77–5.02 (4.82 the last), the spread of the pixel grid.
- **In OBS at 600 px (1:1, the demo lap):** the 3G ring measured r 170.0 px (136 × 1.25). The lit
  brake segments landed within 0.02° of their places (0.06 px), and the empty top segments within
  0.04°, the last ending at 324.96°.

### Replaced: the "Chrono" board, with the bars in the bezel

Before that, the plugin drew the canvas's "Chrono" board. Its bezel was r 210, with the pedal bars
in grooves (centre r 193, 10 wide, round ends) and the bars 6.5 wide in them. ABS and TC labels
(11 px, letter spacing 0.1 em) sat past the grooves' upper ends, turned along the circle, and lit up in
the pedal colours. The maintainer replaced it with "Chrono Inner" (above).

- **ABS and TC placement:** centred on their design angles, the wider ABS came 14.5 design px from its
  groove, and looked closer than TC. At TC's 19.3 it looked further away, because the A's diagonal
  leans away from the bar. So it sat halfway, at 16.9.

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
(24 % at the top), whose lower edge fades out instead of ending in a line. It was chosen, and is
the plugin's glass.

## The performance overlay's design

- **The layers** come from the SimOptix Overlays design system. `overlays\performance\design\perf.html`
  rendered by Edge at 2× matches the design system's three PNGs pixel for pixel (largest difference 0 in
  every channel).
- **The text's places** were measured on that page rendered with sample readings, against the same
  page without them. The labels' ink starts 10.2–10.5 px into its cell, and the readings' ink 10.7–10.8,
  which is the pen at 10 plus each glyph's side bearing. So the plugin places each text by its pen at
  10, as the design's text boxes do.
- **Sizing it by the scene item's scale was a mistake.** A first build kept the overlay's own size at
  the design's 312 × 164 and gave a new scene item two thirds of the canvas's scale. The G-meter's size
  in OBS is its size on 1080p, and its item keeps the canvas's scale. The overlay now does the same at
  208 × 109 (dev skill, "Overlay size"). Its design scale then came from the box's 109 px height in OBS
  where the 164 design px belonged, and it drew 1.5× too large, cut off by its box.
- **Not measured:** sharpness at 1:1 on a recording, and the text's optical centring. Both overlays draw
  1:1 on a 4K canvas (scale 2, scale filtering off). The performance overlay looked softer than the
  G-meter there. Its text sits on fractions of a pixel (1.333 screen px per design px), which may be why.

## Tried and removed: tyre grip learning and ideal pedals

The G-meter once learned how close each tyre is to its limit, and worked out the ideal brake and
throttle from it, to draw as arcs and ticks on the meter. The Chrono look draws neither. So it was
switched off, then removed, with its spec and a tool that replayed it over recorded
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

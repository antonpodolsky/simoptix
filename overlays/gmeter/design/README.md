# G-meter: static layers

This folder is the source of the G-meter's image layers, for its two looks, Chrono and OLED. Each look
has an HTML file that draws its layers. Open it with `#base` (and `#glass` for Chrono) to see each
layer, or with `#preview` to see them over a backdrop. The OBS plugin embeds the PNGs
(`overlays\gmeter\gmeter.rc`) and draws the live parts from the tables below
(`overlays\gmeter\meter_chrono.cpp` and `overlays\gmeter\meter_oled.cpp`).

| File | What it holds |
|---|---|
| `meter.html` | Chrono's layers, drawn in HTML and SVG |
| `gmeter_base.png` | Chrono: everything that never moves: the bezel and its drop shadow, the dark step ring, the face, the empty pedal tracks, the 1G, 2G and 3G rings, the axes, the notches, and the empty live-G window. It is transparent outside the bezel. The ring labels are not in it. The plugin draws them at the meter's size, which is sharper than shrinking them with the image |
| `gmeter_glass.png` | Chrono's glass: a crescent reflection (23 % white at most) whose lower edge fades out, transparent elsewhere. The plugin draws it **last**, over the dot and the numbers |
| `meter_oled.html` | OLED's layer, drawn in HTML and SVG |
| `gmeter_oled_base.png` | OLED: everything that never moves: the black display with its edge, halo and shadow, the dotted 1G and 2G rings, the 3G ring, the centre point and the empty segmented pedal tracks. It is transparent outside the display's shadow. OLED has no glass |

Every layer is 960 × 960 px, which is the 480 × 480 design at 2×. Below, sizes are in design pixels
(1×), relative to the meter's centre at (240, 240). Angles are degrees clockwise from straight up, as
in `overlays\gmeter\meter_draw.h`.

## Chrono

### Fixed geometry (in the base layer)

| Part | Radius / size |
|---|---|
| Bezel rim | r 190 |
| Dark step ring | r 172 – 178 |
| Glass / face | r 172 |
| Pedal tracks | Centre r 103 (just inside the 3G ring), 5 wide, flat ends, white at 7 %. Brake 215° → 325°, throttle 145° → 35° |
| 3G ring (outer scale) | r 112, so 112 / 3 = 37.3 px per G |
| 2G, 1G rings | r 74.7 and 37.3 (2G has a gap for the live-G window). White at 16 %, 0.8 wide |
| Live-G window | 46 × 24, corner radius 7, centred at (240, 314.7) |

### Drawn live on top (between base and glass)

The font is Bahnschrift SemiBold (Regular for the ring labels). The plugin goes one weight heavier below
9 px, where strokes get thinner than a pixel. The label colour is white at 60 %.

| Part | Spec |
|---|---|
| Dot | r 12.5 (Dot Size 160 % on the canvas), accent `#ff5a1f`. Under it a glow, r 19.2 blurred (σ 6.4), accent at 50 % (scaled with the dot from r 12, σ 4) |
| Comet tail | The last 1 s. Width 0.9 × 12.5 × (1 − age), in 5 age bands at alpha 0.55, 0.35, 0.20, 0.09, 0.02. In the plugin it is `#ff6b1f`, a little warmer than the dot, drawn over the glow |
| Pedal bars | Centre r 103, 5 wide, flat ends, filling the tracks from their lower end. Brake `#ff5a4e` (`#c8281e` while ABS intervenes). Throttle `#3ddc84` (`#1f9a52` while TC intervenes) |
| Peak values | 17 px, label colour, tabular figures. Braking is centred at (240, 110) and acceleration at (240, 371). Right is left-aligned from x 364 and left is right-aligned to x 116, both vertically centred on y 240. The plugin places them by their advance (the text box, as CSS), so their digits stay in place, and by their ink's middle on the line |
| Ring labels | 1G, 2G, 3G: Bahnschrift Regular 10 px, label colour. The ink starts at x 245.3 and is 7.0 tall, with ink bottoms on y 218.5, 181.5, 144.0 |
| Live G | 16 px, accent, tabular figures (the font's own, `tnum`), centred in the window, two decimals, no unit. The plugin centres it by its digit cells horizontally and by its ink vertically on (240.8, 314.2). That is horizontally 0.8 right of the window's centre and vertically about 0.5 px above its box, and both look centred |

## OLED

### Fixed geometry (in the base layer)

| Part | Radius / size |
|---|---|
| Display | r 150, black, with a 1 px `#1f1f1f` edge inside it, a 5 px halo of black at 28 % round it, and a shadow 12 down, blurred 30, black at 40 % |
| Pedal tracks | Centre r 143 (outside the 3G ring), 7 wide, white at 10 %. Brake 215° → 325°, throttle 145° → 35°. Each is 40 segments of 4.9378 with gaps of 1.9751 along the arc, so it starts and ends on a whole segment. Its ends are exact to 0.0001, and its `pathLength` is the pattern's length (274.5409), so the last segment is whole |
| 3G ring (outer scale) | r 136, white at 60 %, 1 wide, so 136 / 3 = 45.3 px per G |
| 2G, 1G rings | Round dots 1.8 wide, white at 45 %: 95 on r 90.7 and 47 on r 45.3, evenly spaced all the way round |
| Centre point | r 1.6, white at 50 % |

### Drawn live on top

The fonts are Bahnschrift Medium for the highest values and Regular for the ring labels. The plugin
draws both in SemiBold below 9 px, where strokes get thinner than a pixel.

| Part | Spec |
|---|---|
| Pedal bars | Over the tracks, from each track's start, in the same 40 segments. A bar lights whole segments only: the pedal × 40, rounded. Brake `#ff5a4e` (`#c8281e` while ABS intervenes). Throttle `#3ddc84` (`#1f9a52` while TC intervenes) |
| Dot, glow and comet tail | Chrono's, drawn by the same code (`overlays\gmeter\meter_dot.cpp`), against the 3G ring at r 136. Without telemetry the dimmed dot waits at the centre over a black disc of its size, which hides the centre point |
| Peak values | 15 px, white at 90 %, tabular figures. Braking is centred at (240, 75) and acceleration at (240, 406). Right is left-aligned from x 400 and left is right-aligned to x 80, both vertically centred on y 240. The plugin places them as on Chrono |
| Ring labels | 1G, 2G, 3G: 9 px, white at 40 %, tabular figures, set from x 244 on baselines y 205.7, 160.3, 115.0. The plugin places them by their ink: from x 244.91, 244.49 and 244.40, with ink bottoms on y 205.76, 160.36 and 115.06 |

OLED has no live G, window, axes, notches or glass. ABS and TC show only as the darker bar colours.

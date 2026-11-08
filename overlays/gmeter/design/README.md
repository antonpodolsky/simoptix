# G-meter "Chrono": static layers

`meter.html` is the source of the meter's two image layers. Open it with `#base` or `#glass` to see
each, or with `#preview` to see both over a backdrop. The OBS plugin embeds `gmeter_base.png` and
`gmeter_glass.png` (`overlays\gmeter\gmeter.rc`) and draws the live parts from the tables below
(`overlays\gmeter\meter_chrono.cpp`).

| File | What it holds |
|---|---|
| `meter.html` | The layers, drawn in HTML and SVG |
| `gmeter_base.png` | Everything that never moves: the bezel and its drop shadow, the dark step ring, the face, the empty pedal grooves, the 1G, 2G and 3G rings, the axes, the notches, and the empty live-G window. It is transparent outside the bezel. The ring labels are not in it. The plugin draws them at the meter's size, which is sharper than shrinking them with the image |
| `gmeter_glass.png` | The glass: a crescent reflection (23 % white at most) whose lower edge fades out, transparent elsewhere. The plugin draws it **last**, over the dot and the numbers |

Both are 960 × 960 px, which is the 480 × 480 design at 2×. Below, sizes are in design pixels (1×),
relative to the meter's centre at (240, 240). Angles are degrees clockwise from straight up, as in
`overlays\gmeter\meter_draw.h`.

## Fixed geometry (in the base layer)

| Part | Radius / size |
|---|---|
| Bezel rim | r 210 |
| Dark step ring | r 172 – 178 |
| Glass / face | r 172 |
| Pedal grooves | Centre r 193, 10 wide, round ends. Brake 215° → 325°, throttle 145° → 35° |
| 3G ring (outer scale) | r 112, so 112 / 3 = 37.3 px per G |
| 2G, 1G rings | r 74.7 and 37.3 (2G has a gap for the live-G window). White at 16 %, 0.8 wide |
| Live-G window | 46 × 24, corner radius 7, centred at (240, 314.7) |

## Drawn live on top (between base and glass)

The font is Bahnschrift SemiBold (Regular for the ring labels). The plugin goes one weight heavier below
9 px, where strokes get thinner than a pixel. The label colour is white at 60 %.

| Part | Spec |
|---|---|
| Dot | r 12.5 (Dot Size 160 % on the canvas), accent `#ff5a1f`. Under it a glow, r 19.2 blurred (σ 6.4), accent at 50 % (scaled with the dot from r 12, σ 4) |
| Comet tail | The last 1 s. Width 0.9 × 12.5 × (1 − age), in 5 age bands at alpha 0.55, 0.35, 0.20, 0.09, 0.02. In the plugin it is `#ff6b1f`, a little warmer than the dot, drawn over the glow |
| Pedal bars | Centre r 193, 6.5 wide, round ends, filling the grooves from their lower end. Brake `#ff5a4e` (`#c8281e` while ABS intervenes). Throttle `#3ddc84` (`#1f9a52` while TC intervenes) |
| ABS / TC labels | 11 px, letter spacing 0.1 em, centred at r 193, 334° and 26°, rotated −26° / +26°. Label colour, or `#ff5a4e` / `#3ddc84` while active. In the plugin each sits with its near edge past its groove's round end along the circle, turned to it. TC is 19.3 design px away (its gap in the design) and ABS 16.9, which looks the same distance |
| Peak values | 17 px, label colour, tabular figures. Braking is centred at (240, 110) and acceleration at (240, 371). Right is left-aligned from x 364 and left is right-aligned to x 116, both vertically centred on y 240. The plugin places them by their advance (the text box, as CSS), so their digits stay in place, and by their ink's middle on the line |
| Ring labels | 1G, 2G, 3G: Bahnschrift Regular 10 px, label colour. The ink starts at x 245.3 and is 7.0 tall, with ink bottoms on y 218.5, 181.5, 144.0 |
| Live G | 16 px, accent, tabular figures (the font's own, `tnum`), centred in the window, two decimals, no unit. The plugin centres it by its digit cells horizontally and by its ink vertically on (240.8, 314.2). That is horizontally 0.8 right of the window's centre and vertically about 0.5 px above its box, and both look centred |
